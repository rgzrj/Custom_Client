#pragma once
#include "core/rm2026_commands.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>

namespace rm27::app {
// Window events -> domain input. No sockets, Protobuf, or authority to arm.
class OperatorInput final : public QObject {
    Q_OBJECT
public:
    explicit OperatorInput(QObject *parent = nullptr);
    void observe(QWindow *window);
    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    historical::rm2026::KeyboardMouseCommand frame() const { return frame_; }
signals:
    void frameReady();
    void captureLost();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void clear();
    void flush();
    QPointer<QWindow> window_;
    QTimer timer_;
    bool enabled_ = false, focused_ = false, havePosition_ = false;
    QPointF position_;
    quint32 rawKeys_ = 0, quarantinedKeys_ = 0;
    Qt::MouseButtons rawButtons_{}, quarantinedButtons_{};
    historical::rm2026::KeyboardMouseCommand frame_;
};
}
