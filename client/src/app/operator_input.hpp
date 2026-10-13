#pragma once
#include "core/rm2026_commands.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QAbstractNativeEventFilter>
#include <QCursor>

namespace rm27::app {
// Window events -> domain input. No sockets, Protobuf, or authority to arm.
class OperatorInput final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit OperatorInput(QObject *parent = nullptr);
    ~OperatorInput() override;
    void observe(QWindow *window);
    void setEnabled(bool enabled);
    void setMouseSensitivity(int percent);
    bool enabled() const { return enabled_; }
    bool relativeMouseCaptured() const { return relativeMouseCaptured_; }
    historical::rm2026::KeyboardMouseCommand frame() const { return frame_; }
signals:
    void frameReady();
    void captureLost();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
private:
    void clear();
    void flush();
    bool captureMouse();
    void releaseMouse();
    void addMouseDelta(qreal x, qreal y);
    QPointer<QWindow> window_;
    QTimer timer_;
    bool enabled_ = false, focused_ = false, havePosition_ = false;
    QPointF position_;
    bool relativeMouseCaptured_ = false;
    QCursor previousCursor_;
    int sensitivity_ = 100;
    qreal remainderX_ = 0, remainderY_ = 0;
    quint32 rawKeys_ = 0, quarantinedKeys_ = 0;
    Qt::MouseButtons rawButtons_{}, quarantinedButtons_{};
    historical::rm2026::KeyboardMouseCommand frame_;
};
}
