#include "app/runtime_log.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QUuid>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace rm27::app {
namespace {
constexpr qint64 maxBytes = 4 * 1024 * 1024;
const QByteArray marker("RM27-RUNTIME-LOG v1\n");

const QString& session()
{
    static const QString value = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz")
        + '-' + QString::number(QCoreApplication::applicationPid()) + '-'
        + QUuid::createUuid().toString(QUuid::Id128);
    return value;
}

// Never follow a redirected artifact directory outside the selected root.
QString managedDirectory(const QString& root, const QString& relative)
{
    const QDir base(root);
    const QString path = base.filePath(relative);
    QString checked = base.absolutePath();
    for (const auto& part : relative.split('/')) {
        checked += '/' + part;
        const QFileInfo component(checked);
        if (component.isSymbolicLink() || component.isJunction()) return {};
    }
    if (!base.mkpath(relative)) return {};
    const QFileInfo directory(path);
    if (directory.isSymbolicLink() || directory.isJunction()
        || directory.canonicalFilePath() != QDir::cleanPath(base.canonicalPath() + '/' + relative)) return {};
    return path;
}
}

QString artifactRoot()
{
    const QString override = qEnvironmentVariable("RM27_ARTIFACT_ROOT");
    if (!override.isEmpty() && QDir::isAbsolutePath(override)) return QDir::cleanPath(override);
    QDir directory(QCoreApplication::applicationDirPath());
    if (directory.dirName().startsWith("build-") || directory.dirName() == "build") {
        QDir parent(directory);
        parent.cdUp();
        if (parent.exists("CMakeLists.txt") && parent.exists("toolchain.json")) return parent.absolutePath();
    }
    return directory.absolutePath();
}

QString previewPath(const QString& name)
{
    if (!QRegularExpression("^[a-zA-Z0-9-]+\\.png$").match(name).hasMatch()) return {};
    const QString directory = managedDirectory(artifactRoot(), "pitcure/temp");
    if (directory.isEmpty()) return {};
    return QDir(directory).filePath("rm27-preview-" + session() + '-' + name);
}

int RuntimeLog::cleanExpired(const QString& root)
{
    int removed = 0;
    const auto cutoff = QDateTime::currentDateTime().addDays(-7);
    const QRegularExpression ownedLog("^rm27-(run|demo)-[0-9]{8}-[0-9]{9}-[0-9]+-[a-f0-9]{32}\\.log$");
    const QRegularExpression ownedImage("^rm27-preview-[0-9]{8}-[0-9]{9}-[0-9]+-[a-f0-9]{32}-[a-zA-Z0-9-]+\\.png$");
    for (const QString& relative : {QStringLiteral("log"), QStringLiteral("pitcure/temp")}) {
        const QString path = managedDirectory(root, relative);
        if (path.isEmpty()) continue;
        const QDir directory(path);
        for (const auto& file : directory.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
            if (file.isSymbolicLink() || file.isJunction() || file.lastModified() >= cutoff) continue;
            const bool log = relative == "log";
            if (!(log ? ownedLog : ownedImage).match(file.fileName()).hasMatch()) continue;
            QLockFile lock(file.absoluteFilePath() + ".lock");
            lock.setStaleLockTime(0);
            if (!lock.tryLock(0)) continue; // A different running terminal owns this file.
            if (log) {
                QFile input(file.absoluteFilePath());
                if (!input.open(QIODevice::ReadOnly) || input.read(marker.size()) != marker) continue;
                input.close();
            }
            if (QFile::remove(file.absoluteFilePath())) ++removed;
        }
    }
    return removed;
}

struct RuntimeLog::Writer {
    QString root;
    QString path;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<QByteArray> queue;
    bool stopping = false;
    unsigned dropped = 0;
    std::thread thread;

    explicit Writer(const QString& rootPath) : root(rootPath),
        path(QDir(root).filePath("log/rm27-run-" + session() + ".log")),
        thread([this] { run(); }) {}

    void run()
    {
        const int removed = RuntimeLog::cleanExpired(root);
        if (managedDirectory(root, "log").isEmpty()) return;
        QLockFile lock(path + ".lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0)) return;
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return;
        output.write(marker);
        output.write("START Qt=" QT_VERSION_STR " retentionDays=7 removed=" + QByteArray::number(removed) + '\n');
        bool limited = false;
        for (;;) {
            std::deque<QByteArray> batch;
            {
                std::unique_lock<std::mutex> guard(mutex);
                ready.wait(guard, [this] { return stopping || !queue.empty(); });
                batch.swap(queue);
                if (batch.empty() && stopping) break;
            }
            for (const auto& line : batch) {
                if (limited) continue;
                if (output.pos() + line.size() < maxBytes - 128) output.write(line);
                else { output.write("LOG LIMIT reached; further messages omitted\n"); limited = true; }
            }
            output.flush();
        }
        output.write("END dropped=" + QByteArray::number(dropped) + '\n');
        output.flush();
    }
};

RuntimeLog::RuntimeLog(const QString& root) : writer_(std::make_unique<Writer>(root)) {}

RuntimeLog::~RuntimeLog()
{
    {
        std::lock_guard<std::mutex> guard(writer_->mutex);
        writer_->stopping = true;
    }
    writer_->ready.notify_one();
    writer_->thread.join();
}

void RuntimeLog::append(const QString& message)
{
    // ponytail: bounded diagnostics, not lossless recording; full replay needs a separate recorder.
    // No disk I/O in the Qt message handler or GUI thread.
    auto text = message.left(2048);
    text.replace('\n', "\\n");
    text.replace('\r', "\\r");
    const auto line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8()
        + ' ' + text.toUtf8() + '\n';
    {
        std::lock_guard<std::mutex> guard(writer_->mutex);
        if (writer_->queue.size() == 256) { ++writer_->dropped; return; }
        writer_->queue.push_back(line);
    }
    writer_->ready.notify_one();
}

QString RuntimeLog::path() const { return writer_->path; }

} // namespace rm27::app
