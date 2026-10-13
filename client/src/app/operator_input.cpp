// SPDX-License-Identifier: MIT
// Adapted from RM26_Client e291f2f MainWindow keyboard/mouse mapping and reset.
// Copyright (c) 2025-2026 Fudan University EGA Team. See third_party/rm26_client/LICENSE.
#include "app/operator_input.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <limits>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rm27::app {
namespace {
int bitForKey(int key) {
    switch (key) {
    case Qt::Key_W: return 0; case Qt::Key_S: return 1;
    case Qt::Key_A: return 2; case Qt::Key_D: return 3;
    case Qt::Key_Shift: return 4; case Qt::Key_Control: return 5;
    case Qt::Key_Q: return 6; case Qt::Key_E: return 7;
    case Qt::Key_R: return 8; case Qt::Key_F: return 9;
    case Qt::Key_G: return 10; case Qt::Key_Z: return 11;
    case Qt::Key_X: return 12; case Qt::Key_C: return 13;
    case Qt::Key_V: return 14; case Qt::Key_B: return 15;
    default: return -1;
    }
}
std::int32_t add(std::int32_t a, qint64 b) {
    return std::int32_t(std::clamp(qint64(a) + b,
        qint64(std::numeric_limits<std::int32_t>::min()), qint64(std::numeric_limits<std::int32_t>::max())));
}
}
OperatorInput::OperatorInput(QObject *parent) : QObject(parent) {
    QCoreApplication::instance()->installNativeEventFilter(this);
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(14); // <=75Hz; sender still applies its monotonic limit.
    connect(&timer_, &QTimer::timeout, this, &OperatorInput::flush);
}
OperatorInput::~OperatorInput() {
    releaseMouse();
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
}
bool OperatorInput::captureMouse() {
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == "windows") {
        if (!window_->isVisible() || !window_->isActive()) return false;
        const auto hwnd = reinterpret_cast<HWND>(window_->winId());
        if (GetForegroundWindow() != hwnd) return false;
        // Registration is off here, so discard only the previous capture's queued input.
        MSG pending;
        while (PeekMessage(&pending, hwnd, WM_INPUT, WM_INPUT, PM_REMOVE))
            DefWindowProc(pending.hwnd, pending.message, pending.wParam, pending.lParam);
        const RAWINPUTDEVICE mouse{1, 2, 0, hwnd};
        if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) return false;
        if (!window_->setMouseGrabEnabled(true)) {
            const RAWINPUTDEVICE remove{1, 2, RIDEV_REMOVE, nullptr};
            RegisterRawInputDevices(&remove, 1, sizeof(remove));
            return false;
        }
        relativeMouseCaptured_ = true;
        previousCursor_ = window_->cursor();
        window_->setCursor(Qt::BlankCursor);
    }
#endif
    return true;
}
void OperatorInput::releaseMouse() {
    if (!relativeMouseCaptured_) return;
#ifdef Q_OS_WIN
    const RAWINPUTDEVICE remove{1, 2, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&remove, 1, sizeof(remove));
#endif
    relativeMouseCaptured_ = false;
    if (window_) {
        window_->setMouseGrabEnabled(false);
        window_->setCursor(previousCursor_);
    }
}
void OperatorInput::setMouseSensitivity(int percent) {
    percent = std::clamp(percent, 10, 300);
    if (sensitivity_ == percent) return;
    sensitivity_ = percent;
    frame_.x = frame_.y = 0;
    remainderX_ = remainderY_ = 0;
    havePosition_ = false;
}
void OperatorInput::addMouseDelta(qreal x, qreal y) {
    // Keep hundredths until consumed; 10 one-count moves at 10% still make one count.
    const qreal scaledX = x * sensitivity_ + remainderX_;
    const qreal scaledY = -y * sensitivity_ + remainderY_;
    const qint64 wholeX = qint64(std::trunc(scaledX / 100)), wholeY = qint64(std::trunc(scaledY / 100));
    remainderX_ = scaledX - wholeX * 100.0; remainderY_ = scaledY - wholeY * 100.0;
    frame_.x = add(frame_.x, wholeX); frame_.y = add(frame_.y, wholeY);
}
bool OperatorInput::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
#ifdef Q_OS_WIN
    if (!enabled_ || !relativeMouseCaptured_ || !window_) return false;
    const auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_INPUT || msg->hwnd != reinterpret_cast<HWND>(window_->winId())
        || GET_RAWINPUT_CODE_WPARAM(msg->wParam) != RIM_INPUT) return false;
    if (GetForegroundWindow() != msg->hwnd) {
        setEnabled(false); emit captureLost(); return false;
    }
    RAWINPUT input{};
    UINT size = sizeof(input);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &input, &size,
                        sizeof(RAWINPUTHEADER)) == UINT(-1)) {
        setEnabled(false); emit captureLost(); return false;
    }
    if (input.header.dwType == RIM_TYPEMOUSE && !(input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
        addMouseDelta(input.data.mouse.lLastX, input.data.mouse.lLastY);
#else
    Q_UNUSED(message);
#endif
    // Windows/Qt still performs WM_INPUT cleanup; legacy moves are ignored below.
    return false;
}
void OperatorInput::observe(QWindow *window) {
    setEnabled(false);
    clear();
    if (window_) {
        window_->removeEventFilter(this);
        disconnect(window_, nullptr, this, nullptr);
    }
    window_ = window;
    focused_ = window && window->isActive();
    if (window_) {
        window_->installEventFilter(this);
        connect(window_, &QObject::destroyed, this, [this] {
            focused_ = false; setEnabled(false); clear(); emit captureLost();
        });
    }
}
void OperatorInput::clear() {
    frame_ = {};
    havePosition_ = false;
    remainderX_ = remainderY_ = 0;
    quarantinedKeys_ |= rawKeys_;
    quarantinedButtons_ |= rawButtons_;
}
void OperatorInput::setEnabled(bool enabled) {
    enabled = enabled && window_ && focused_;
    if (enabled_ == enabled) return;
    if (enabled && !captureMouse()) { clear(); emit captureLost(); return; }
    enabled_ = enabled;
    if (!enabled_) releaseMouse();
    clear();
    if (enabled_) timer_.start(); else timer_.stop();
    emit frameReady();
}
void OperatorInput::flush() {
    if (!enabled_) return;
    emit frameReady();
    frame_.x = frame_.y = frame_.wheel = 0; // Deltas are consumed once.
}
bool OperatorInput::eventFilter(QObject *watched, QEvent *event) {
    if (watched != window_) return false;
    switch (event->type()) {
    case QEvent::WindowActivate: focused_ = true; return false;
    case QEvent::UngrabMouse:
        if (enabled_) { setEnabled(false); clear(); emit captureLost(); }
        return false;
    case QEvent::WindowDeactivate: case QEvent::FocusOut:
    case QEvent::Hide: case QEvent::Close:
        focused_ = false; setEnabled(false); clear(); emit captureLost(); return false;
    case QEvent::KeyPress: case QEvent::KeyRelease: {
        auto *key = static_cast<QKeyEvent *>(event);
        const int bit = bitForKey(key->key());
        if (bit < 0 || key->isAutoRepeat()) return false;
        const auto mask = quint32(1) << bit;
        const bool down = event->type() == QEvent::KeyPress;
        if (down) rawKeys_ |= mask; else { rawKeys_ &= ~mask; quarantinedKeys_ &= ~mask; }
        if (!enabled_ || (key->modifiers() & (Qt::AltModifier | Qt::MetaModifier))) {
            if (down) quarantinedKeys_ |= mask;
            frame_.keys &= ~mask;
            if (enabled_) flush();
            return false;
        }
        if (down && !(quarantinedKeys_ & mask)) frame_.keys |= mask;
        if (!down) frame_.keys &= ~mask;
        flush(); return true;
    }
    case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const auto button = mouse->button();
        if (!(button & (Qt::LeftButton | Qt::RightButton | Qt::MiddleButton))) return false;
        const bool down = event->type() == QEvent::MouseButtonPress;
        if (down) rawButtons_ |= button;
        else { rawButtons_ &= ~button; quarantinedButtons_ &= ~button; }
        if (!enabled_) { if (down) quarantinedButtons_ |= button; return false; }
        bool &value = button == Qt::LeftButton ? frame_.left
            : button == Qt::RightButton ? frame_.right : frame_.middle;
        value = down && !(quarantinedButtons_ & button);
        flush(); return true;
    }
    case QEvent::MouseMove: {
        if (!enabled_) return false;
        if (relativeMouseCaptured_) return true;
        const auto pos = static_cast<QMouseEvent *>(event)->position();
        if (havePosition_) {
            addMouseDelta(pos.x() - position_.x(), pos.y() - position_.y());
        }
        position_ = pos; havePosition_ = true; return true;
    }
    case QEvent::Wheel:
        if (!enabled_) return false;
        frame_.wheel = add(frame_.wheel, static_cast<QWheelEvent *>(event)->angleDelta().y());
        return true;
    default: return false;
    }
}
}
