#pragma once

#include <QHostAddress>
#include <QImage>
#include <QVariantMap>
#include <memory>

namespace rm27::video {

struct UpstreamVideoView {
    QImage image;
    QVariantMap stats;
    quint64 serial{0};
    qint64 decodedAtMs{-1};
};

// One worker receives/reassembles/decodes; the GUI takes only the latest image.
class UpstreamVideo final {
public:
    UpstreamVideo();
    ~UpstreamVideo();
    bool start(const QHostAddress& bind, quint16 port,
               const QHostAddress& source, unsigned fragmentBase);
    void stop();
    UpstreamVideoView view() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rm27::video
