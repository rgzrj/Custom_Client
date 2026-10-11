#pragma once

#include <QString>
#include <memory>

namespace rm27::app {

// Development builds use their client directory; portable packages use the exe directory.
QString artifactRoot();
QString previewPath(const QString& name);

class RuntimeLog final {
public:
    explicit RuntimeLog(const QString& root = artifactRoot());
    ~RuntimeLog();
    RuntimeLog(const RuntimeLog&) = delete;
    RuntimeLog& operator=(const RuntimeLog&) = delete;

    void append(const QString& message);
    QString path() const;
    static int cleanExpired(const QString& root);

private:
    struct Writer;
    std::unique_ptr<Writer> writer_;
};

} // namespace rm27::app
