#include "app/runtime_log.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>

class RuntimeLogTest : public QObject {
    Q_OBJECT
    static bool oldFile(const QString& path, const QByteArray& contents)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size()
            && file.setFileTime(QDateTime::currentDateTime().addDays(-8), QFileDevice::FileModificationTime);
    }

private slots:
    void boundedBackgroundWriter()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString path;
        {
            rm27::app::RuntimeLog log(directory.path());
            path = log.path();
            log.append(QStringLiteral("中文日志\n第二行"));
            for (int batch = 0; batch < 100; ++batch) {
                for (int line = 0; line < 100; ++line) log.append(QString(2048, 'x'));
                QTest::qWait(3);
            }
            log.append("last message");
        }
        QFile output(path);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const auto text = output.readAll();
        QVERIFY(text.startsWith("RM27-RUNTIME-LOG v1\nSTART"));
        QVERIFY(text.contains(QStringLiteral("中文日志\\n第二行").toUtf8()));
        QVERIFY(text.contains("LOG LIMIT reached"));
        QVERIFY(text.contains("END dropped="));
        QVERIFY(text.size() <= 4 * 1024 * 1024);
        QVERIFY(!QFile::exists(path + ".lock"));
    }

    void scopedRetention()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QDir root(directory.path());
        QVERIFY(root.mkpath("log/nested"));
        QVERIFY(root.mkpath("pitcure/temp"));
        const QString stem = "20260101-010101000-1234-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        const QString expired = root.filePath("log/rm27-run-" + stem + ".log");
        const QString active = root.filePath("log/rm27-demo-" + stem + ".log");
        const QString unknown = root.filePath("log/my-important.log");
        const QString impostor = root.filePath("log/rm27-run-" + stem + "b.log");
        const QString wrongMarker = root.filePath("log/rm27-run-20260101-010101000-9999-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.log");
        const QString nested = root.filePath("log/nested/keep.log");
        const QString image = root.filePath("pitcure/temp/rm27-preview-" + stem + "-check.png");
        const QString legacy = root.filePath("pitcure/temp/playback-1600x900.png");
        const QString recent = root.filePath("log/rm27-run-20260101-010101000-8888-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.log");
        const auto marker = QByteArray("RM27-RUNTIME-LOG v1\n");
        for (const auto& path : {expired, active, unknown, impostor, nested}) QVERIFY(oldFile(path, marker));
        QVERIFY(oldFile(wrongMarker, "user content"));
        QVERIFY(oldFile(image, "generated test artifact"));
        QVERIFY(oldFile(legacy, "legacy image"));
        {
            QFile file(recent);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(marker), marker.size());
        }
        QLockFile activeLock(active + ".lock");
        activeLock.setStaleLockTime(0);
        QVERIFY(activeLock.tryLock(0));
        QCOMPARE(rm27::app::RuntimeLog::cleanExpired(directory.path()), 2);
        QVERIFY(!QFile::exists(expired));
        QVERIFY(!QFile::exists(image));
        for (const auto& path : {active, unknown, impostor, wrongMarker, nested, legacy, recent}) QVERIFY(QFile::exists(path));
        QCOMPARE(rm27::app::RuntimeLog::cleanExpired(directory.path()), 0);
    }

    void pathsAndWriteFailure()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray previous = qgetenv("RM27_ARTIFACT_ROOT");
        const auto restore = qScopeGuard([&] { qputenv("RM27_ARTIFACT_ROOT", previous); });
        qputenv("RM27_ARTIFACT_ROOT", directory.path().toUtf8());
        const auto image = rm27::app::previewPath("hud-check.png");
        QVERIFY(image.startsWith(directory.path() + "/pitcure/temp/rm27-preview-"));
        QVERIFY(rm27::app::previewPath("../escape.png").isEmpty());
        QFile blocker(directory.filePath("blocked"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        {
            rm27::app::RuntimeLog log(blocker.fileName());
            log.append("No usable directory; application remains usable");
        }
    }
};

QTEST_GUILESS_MAIN(RuntimeLogTest)
#include "test_runtime_log.moc"
