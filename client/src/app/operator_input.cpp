// SPDX-License-Identifier: MIT
// Adapted from RM26_Client e291f2f MainWindow keyboard/mouse mapping and reset.
// Copyright (c) 2025-2026 Fudan University EGA Team. See third_party/rm26_client/LICENSE.
#include "app/operator_input.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <limits>

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
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(14); // <=75Hz; sender still applies its monotonic limit.
    connect(&timer_, &QTimer::timeout, this, &OperatorInput::flush);
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
    quarantinedKeys_ |= rawKeys_;
    quarantinedButtons_ |= rawButtons_;
}
void OperatorInput::setEnabled(bool enabled) {
    enabled = enabled && window_ && focused_;
    if (enabled_ == enabled) return;
    enabled_ = enabled;
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
        const auto pos = static_cast<QMouseEvent *>(event)->position();
        if (havePosition_) {
            frame_.x = add(frame_.x, qRound64(pos.x() - position_.x()));
            frame_.y = add(frame_.y, -qRound64(pos.y() - position_.y()));
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
