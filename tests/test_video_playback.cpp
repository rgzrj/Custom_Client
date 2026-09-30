#include <QGuiApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCamera>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTest>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QVideoSink>
#include <cmath>
#include "app/terminal_controller.hpp"
#include "video/udp_video_ingress.hpp"

constexpr quint16 kTestRefereePort = 39011;

class PlaybackTest : public QObject {
    Q_OBJECT

    // 启动面板现在是每次启动的默认入口；本文件的历史用例验证的是 HUD 与回放，
    // 因此先把它关掉（面板自身的进入路径由 startPanelOfflineSentinel 用例覆盖）。
    static void dismissStartPanel(QQuickWindow* window)
    {
        QVERIFY(window->setProperty("startPanelVisible", false));
        QTest::qWait(60);
    }

    // 删掉隔离配置文件，回到默认档（非哨兵、非正式地址），
    // 避免上一个用例保存的哨兵段把后续用例带进模拟模式。
    static void resetIsolatedSettings()
    {
        const QString path = QString::fromUtf8(qgetenv("RM27_SETTINGS_FILE"));
        QVERIFY(!path.isEmpty());
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
        QFile::remove(path + QStringLiteral(".tmp"));
    }

    // Repeater 生成的委托只能沿可视树找到（QObject::findChild 不覆盖它们），
    // 因此队伍栏里的格子按 childItems() 递归查找。
    static QQuickItem* findVisual(QQuickItem* root, const QString& name)
    {
        if (root == nullptr) {
            return nullptr;
        }
        if (root->objectName() == name) {
            return root;
        }
        const QList<QQuickItem*> children = root->childItems();
        for (QQuickItem* child : children) {
            if (QQuickItem* found = findVisual(child, name)) {
                return found;
            }
        }
        return nullptr;
    }

private slots:
    void simulationUdpVideo()
    {
        QFile fixture(qEnvironmentVariable("RM27_TEST_TS"));
        if (!fixture.open(QIODevice::ReadOnly)) QSKIP("Set RM27_TEST_TS to an MPEG-TS video sample");
        const QByteArray transportStream = fixture.readAll();
        QVERIFY(transportStream.size() >= 1316);
        TerminalController terminal(false, nullptr, kTestRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", true);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());

        QUdpSocket sender;
        qsizetype offset = 0;
        QTimer transmit;
        transmit.setInterval(21); // About 500 kbps for the generated 10-second sample.
        connect(&transmit, &QTimer::timeout, &transmit, [&] {
            if (offset + 1316 > transportStream.size()) offset = 0;
            sender.writeDatagram(transportStream.constData() + offset, 1316,
                                 QHostAddress::LocalHost, 39003);
            offset += 1316;
        });
        transmit.start();
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 15000);
        QVERIFY(terminal.videoConnected());
        transmit.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.videoConnected(), 2500);
        QVERIFY(!terminal.controlsArmed());
    }

    void playbackAndRecovery()
    {
        resetIsolatedSettings();
        const auto fixture = QUrl::fromLocalFile(qEnvironmentVariable("RM27_TEST_VIDEO"));
        QVERIFY2(!fixture.isEmpty(), "Set RM27_TEST_VIDEO to an existing video fixture");
        TerminalController terminal(false, nullptr, kTestRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        dismissStartPanel(window);
        auto* player = window->findChild<QMediaPlayer*>("localPlayer");
        auto* pause = window->findChild<QQuickItem*>("playbackButton");
        auto* timeline = window->findChild<QQuickItem*>("videoTimeline");
        auto* loop = window->findChild<QQuickItem*>("loopPlayback");
        auto* playbackDock = window->findChild<QQuickItem*>("playbackDock");
        auto* playbackDockToggle = window->findChild<QQuickItem*>("playbackDockToggle");
        auto* viewport = window->findChild<QQuickItem*>("videoViewport");
        auto* videoSurface = window->findChild<QQuickItem*>("localVideoOutput");
        auto* operatorCamera = window->findChild<QCamera*>("operatorCamera");
        auto* cameraPanel = window->findChild<QQuickItem*>("operatorCameraPanel");
        auto* cameraOpen = window->findChild<QQuickItem*>("operatorCameraOpen");
        auto* cameraStop = window->findChild<QQuickItem*>("operatorCameraStop");
        auto* cameraSelector = window->findChild<QQuickItem*>("operatorCameraSelector");
        QVERIFY(player && pause && timeline && loop && viewport && videoSurface
                && playbackDock && playbackDockToggle && operatorCamera && cameraPanel
                && cameraOpen && cameraStop && cameraSelector);
        QVERIFY(!operatorCamera->isActive());
        QVERIFY(cameraOpen->isVisible());
        QVERIFY(!cameraSelector->isVisible());
        auto click = [window](QQuickItem* item, qreal fraction = 0.5) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() * fraction, item->height() / 2)).toPoint());
        };
        auto open = [window](const QUrl& url) {
            return QMetaObject::invokeMethod(window, "openLocalVideo", Q_ARG(QUrl, url));
        };
        QVERIFY(open(fixture));
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
        QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());
        terminal.toggleFallbackMode();
        QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));

        click(pause);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        const auto pausedAt = player->position();
        QTest::qWait(300);
        QCOMPARE(player->position(), pausedAt);
        click(timeline, 0.5);
        QTRY_VERIFY(qAbs(player->position() - player->duration() / 2) < 700);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QVERIFY(window->grabWindow().save("playback-1600x900.png"));
        window->resize(1180, 680);
        QTest::qWait(200);
        QVERIFY(viewport->height() > 400);
        QVERIFY(timeline->width() > 100);
        click(playbackDockToggle);
        QTRY_COMPARE(playbackDock->height(), 0.0);
        QVERIFY(playbackDockToggle->isVisible());
        QVERIFY(window->grabWindow().save("playback-collapsed-1180x680.png"));
        click(playbackDockToggle);
        QTRY_COMPARE(playbackDock->height(), 48.0);
        click(cameraOpen);
        QTRY_VERIFY(cameraPanel->height() > cameraPanel->width() * 0.5625 + 30);
        QVERIFY(cameraSelector->isVisible());
        click(cameraStop);
        QTRY_COMPARE(cameraPanel->height(), 28.0);
        QVERIFY(!cameraSelector->isVisible());
        auto* hud = window->findChild<QQuickItem*>("hudOverlay");
        auto* weapon = window->findChild<QQuickItem*>("weaponPanel");
        auto* map = window->findChild<QQuickItem*>("minimap");
        auto* friendly = window->findChild<QQuickItem*>("friendlyTeam");
        auto* enemy = window->findChild<QQuickItem*>("enemyTeam");
        auto* teamBadge = window->findChild<QQuickItem*>("ownTeamBadge");
        auto* teamBadgeLabel = window->findChild<QQuickItem*>("ownTeamBadgeLabel");
        auto* messageFeed = window->findChild<QQuickItem*>("messageFeed");
        auto* eventList = window->findChild<QQuickItem*>("eventList");
        auto* levelIndicator = window->findChild<QQuickItem*>("levelIndicator");
        auto* shieldLabel = window->findChild<QQuickItem*>("shieldLabel");
        auto* shieldBarFill = window->findChild<QQuickItem*>("shieldBarFill");
        auto* fireIndicator = window->findChild<QQuickItem*>("fireIndicator");
        auto* yawIndicator = window->findChild<QQuickItem*>("yawIndicator");
        auto* yawLabel = window->findChild<QQuickItem*>("yawIndicatorLabel");
        auto* yawMarker = window->findChild<QQuickItem*>("yawIndicatorMarker");
        auto* selfRobotLabel = window->findChild<QQuickItem*>("selfRobotLabel");
        auto* selfPhoto = window->findChild<QQuickItem*>("selfRobotVisual");
        auto* friendlyTeamRow = window->findChild<QQuickItem*>("friendlyTeam");
        auto* enemyTeamRow = window->findChild<QQuickItem*>("enemyTeam");
        auto* ownTeamBadge = window->findChild<QQuickItem*>("ownTeamBadge");
        auto* weaponPanel = window->findChild<QQuickItem*>("weaponPanel");
        QVERIFY(friendlyTeamRow && enemyTeamRow && ownTeamBadge && weaponPanel);
        const int enemySlotsBefore = enemyTeamRow->childItems().size();
        // 数据未给出本车时不再排除任何一台：我方也显示全部六台。
        QCOMPARE(friendlyTeamRow->childItems().size(), enemySlotsBefore);
        // 身份牌在最上一行；两条队伍栏同一行、分别贴左右外边（结构对称），互不相撞，
        // 也不越过右侧仪表（仪表已下移到队伍栏之下）。
        QVERIFY(ownTeamBadge->y() + ownTeamBadge->height() <= enemyTeamRow->y());
        QCOMPARE(enemyTeamRow->y(), friendlyTeamRow->y());
        QVERIFY2(enemyTeamRow->x() + enemyTeamRow->width() <= friendlyTeamRow->x(),
                 qPrintable(QStringLiteral("敌方右沿=%1 我方左沿=%2")
                                .arg(enemyTeamRow->x() + enemyTeamRow->width())
                                .arg(friendlyTeamRow->x())));
        QVERIFY2(friendlyTeamRow->x() + friendlyTeamRow->width() <= window->width() - 20,
                 qPrintable(QStringLiteral("我方右沿=%1 窗口宽=%2")
                                .arg(friendlyTeamRow->x() + friendlyTeamRow->width())
                                .arg(window->width())));
        QVERIFY2(enemyTeamRow->y() + enemyTeamRow->height() <= weaponPanel->y(),
                 qPrintable(QStringLiteral("队伍栏底=%1 仪表顶=%2")
                                .arg(enemyTeamRow->y() + enemyTeamRow->height())
                                .arg(weaponPanel->y())));
        auto* friendlySlotHp0 = findVisual(friendlyTeamRow, QStringLiteral("friendlySlotHp0"));
        auto* friendlySlotPhoto0 = findVisual(friendlyTeamRow, QStringLiteral("friendlySlotPhoto0"));
        // 六张机型照片都按原文件名内置进了资源（缺一张就会退化成线稿，这里直接查资源）。
        for (const QString& alias : {QStringLiteral("sentry.png"), QStringLiteral("aerial.jpg"),
                                     QStringLiteral("infantry3.png"), QStringLiteral("infantry4.png"),
                                     QStringLiteral("engineer.jpg"), QStringLiteral("hero.png")}) {
            QVERIFY2(QFile(QStringLiteral(":/rm27/robots/") + alias).exists(), qPrintable(alias));
        }
        QVERIFY2(selfRobotLabel, "selfRobotLabel 未找到");
        QVERIFY2(selfPhoto, "selfRobotVisual 未找到");
        QVERIFY(friendlySlotHp0 && friendlySlotPhoto0);
        // 数据未给出本车时：左下角明确显示待确认。
        QCOMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 机型待确认"));
        QVERIFY(hud && weapon && map && friendly && enemy && teamBadge && teamBadgeLabel);
        QCOMPARE(teamBadgeLabel->property("text").toString(), QStringLiteral("我方 · 阵营待确认"));
        QVERIFY(messageFeed);
        QVERIFY(eventList);
        QVERIFY(levelIndicator && shieldLabel && shieldBarFill && fireIndicator);
        // LV 读数写在本车线稿下方，不与图形叠在一起（左下角也不再套圆形边框）。
        QVERIFY2(levelIndicator->y() >= selfPhoto->y() + selfPhoto->height(),
                 qPrintable(QStringLiteral("LV y=%1 线稿底=%2")
                                .arg(levelIndicator->y())
                                .arg(selfPhoto->y() + selfPhoto->height())));
        QVERIFY(yawIndicator && yawLabel && yawMarker);
        // 无数据时偏航条整条变暗、不显示标记，也不保留旧角度。
        QCOMPARE(yawLabel->property("text").toString(), QStringLiteral("YAW --°"));
        QVERIFY(!yawMarker->isVisible());
        QCOMPARE(viewport->size(), window->size());
        QVERIFY2(weapon->y() + weapon->height() < map->y(),
                 qPrintable(QStringLiteral("仪表底=%1 地图顶=%2")
                                .arg(weapon->y() + weapon->height())
                                .arg(map->y())));
        QVERIFY(enemy->x() + enemy->width() < friendly->x());
        // 偏航条：轨道压在俯仰弧/对照弧下端所在的水平线上（半径 = 宽度×0.205，
        // 两段弧各画 ±0.17π），水平居中于准星下方，且停在底部浮层之上。
        constexpr double kPi = 3.14159265358979323846;
        const double unit = qMin(hud->width() / 1600.0, hud->height() / 900.0);
        const double arcBottomLine = hud->height() * 0.5
            + hud->width() * 0.205 * std::sin(kPi * 0.17);
        QVERIFY2(qAbs(yawIndicator->y() + 8 * unit - arcBottomLine) < 2.0,
                 qPrintable(QStringLiteral("y=%1 arcLine=%2")
                                .arg(yawIndicator->y()).arg(arcBottomLine)));
        QCOMPARE(qRound(yawIndicator->x() + yawIndicator->width() / 2 - hud->width() / 2), 0);
        QVERIFY(yawIndicator->y() > hud->height() * 0.5);
        QVERIFY(playbackDockToggle->isVisible());
        QVERIFY(yawIndicator->y() + yawIndicator->height() <= playbackDockToggle->y());
        QVERIFY(yawIndicator->y() + yawIndicator->height() <= playbackDock->y());
        const QByteArray hudPacket = R"json({
            "channel":"rm27.sim.v1.referee","sequence":1,"sent_at_unix_ms":1789999999000,
            "phase":"match",
            "telemetry":{"hp":850,"max_hp":1000,"chassis_power_w":80,
                "buffer_energy_j":45,"barrel_heat":30,"ammo":90,
                "latency_ms":12,"packet_loss_pct":0.1},
            "hud":{"own_team":"blue","friendly_hp":[900,800,700,600,500,400],
                "enemy_hp":[880,780,680,580,480,380],
                "friendly_positions":[[0.1,0.2],[0.2,0.3],[0.3,0.4],[0.4,0.5],[0.5,0.6],[0.6,0.7]],
                "enemy_positions":[[0.9,0.8],[0.8,0.7],[0.7,0.6],[0.6,0.5],[0.5,0.4],[0.4,0.3]],
                "friendly_coins":100,"enemy_coins":80,"restart_count":2,
                "pitch_deg":12.5,"yaw_deg":-37.5,"self_index":3,"speed_mps":24.0,"allowed_shots":80,
                "level":3,"shield":60,"max_shield":100,
                "actions":{"climb":false,"fire":true,"speed_up":false}}
        })json";
        QUdpSocket sender;
        QCOMPARE(sender.writeDatagram(hudPacket, QHostAddress::LocalHost, kTestRefereePort), hudPacket.size());
        QTRY_COMPARE(terminal.hudSnapshot().value("ownTeam").toString(), QStringLiteral("blue"));
        QCOMPARE(terminal.hudSnapshot().value("friendlyHp").toList().at(0).toInt(), 900);
        QCOMPARE(hud->property("ownTeam").toString(), QStringLiteral("blue"));
        QCOMPARE(teamBadgeLabel->property("text").toString(), QStringLiteral("我方 · 蓝方"));
        QCOMPARE(teamBadgeLabel->property("color").value<QColor>(), QColor("#49D7EF"));
        QCOMPARE(terminal.hudSnapshot().value("level").toInt(), 3);
        QCOMPARE(levelIndicator->property("text").toString(), QStringLiteral("LV 3"));
        // 偏航角反馈：读数与标记同步，标记落在刻度条内。
        QTRY_VERIFY(qAbs(terminal.hudSnapshot().value("yawDeg").toDouble() + 37.5) < 1e-6);
        QTRY_COMPARE(yawLabel->property("text").toString(), QStringLiteral("YAW -37.5°"));
        QVERIFY(yawMarker->isVisible());
        QVERIFY(yawMarker->x() >= 0);
        QVERIFY(yawMarker->x() + yawMarker->width() <= yawIndicator->width());
        // 标记在轨道上方、读数在刻度下方：两者不占同一条纵带。
        QVERIFY(yawMarker->y() + yawMarker->height() <= yawLabel->y());
        // 本车身份：裁判数据给出 self_index=3（步兵4）→ 本车移出我方列表（六台 → 五台），
        // 其余队友血量按机型下标取，不会因为少了一格而错位；本车完全交给左下角自车模块。
        QTRY_COMPARE(terminal.hudSnapshot().value("selfIndex").toInt(), 3);
        QTRY_COMPARE(hud->property("selfIndex").toInt(), 3);
        QTRY_COMPARE(friendlyTeamRow->childItems().size(), enemySlotsBefore - 1);
        QVERIFY(!findVisual(friendlyTeamRow, QStringLiteral("friendlySlot3")));
        auto* friendlySlotHp0After = findVisual(friendlyTeamRow, QStringLiteral("friendlySlotHp0"));
        QVERIFY(friendlySlotHp0After);
        QTRY_COMPARE(friendlySlotHp0After->property("text").toString(), QStringLiteral("900"));
        // 敌方列表不受影响，始终六台。
        QCOMPARE(enemyTeamRow->childItems().size(), enemySlotsBefore);
        QCOMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 步兵4"));
        QVERIFY(selfPhoto->property("photoSource").toString().contains(QStringLiteral("infantry4")));
        // 队伍栏里的机型线稿确实从资源加载成功（不是回退线稿）。
        QVERIFY(findVisual(friendlyTeamRow, QStringLiteral("friendlySlotPhoto0"))
                    ->property("photoReady").toBool());
        QVERIFY(shieldLabel->property("text").toString().contains(QStringLiteral("60/100")));
        QVERIFY(shieldBarFill->width() > 0);
        QCOMPARE(fireIndicator->property("color").value<QColor>(), QColor("#FF626C"));
        QByteArray robotEvent = hudPacket;
        robotEvent.replace("\"sequence\":1", "\"sequence\":2");
        robotEvent.replace("\"hud\":{", "\"event\":{\"id\":\"upgrade\",\"category\":\"robot\",\"text\":\"模拟：英雄升级\"},\"hud\":{");
        QCOMPARE(sender.writeDatagram(robotEvent, QHostAddress::LocalHost, kTestRefereePort), robotEvent.size());
        QTRY_COMPARE(terminal.eventMessages().size(), 1);
        QTRY_COMPARE(eventList->property("count").toInt(), 1);
        robotEvent.replace("\"sequence\":2", "\"sequence\":3");
        QCOMPARE(sender.writeDatagram(robotEvent, QHostAddress::LocalHost, kTestRefereePort), robotEvent.size());
        QCOMPARE(terminal.eventMessages().size(), 1);
        QByteArray systemEvent = robotEvent;
        systemEvent.replace("\"sequence\":3", "\"sequence\":4");
        systemEvent.replace("\"id\":\"upgrade\",\"category\":\"robot\",\"text\":\"模拟：英雄升级\"",
                            "\"id\":\"dart\",\"category\":\"system\",\"text\":\"模拟：飞镖命中\"");
        QCOMPARE(sender.writeDatagram(systemEvent, QHostAddress::LocalHost, kTestRefereePort), systemEvent.size());
        QTRY_COMPARE(terminal.eventMessages().size(), 2);
        QTRY_COMPARE(eventList->property("count").toInt(), 2);
        QVERIFY(messageFeed->property("color").value<QColor>().alpha() < 128);
        videoSurface->setVisible(false);
        QVERIFY(window->grabWindow().save("hud-design-preview-1180x680.png"));
        videoSurface->setVisible(true);
        QVERIFY(window->grabWindow().save("hud-simulation-1180x680.png"));
        QByteArray redPacket = systemEvent;
        redPacket.replace("\"sequence\":4", "\"sequence\":5");
        redPacket.replace("\"own_team\":\"blue\"", "\"own_team\":\"red\"");
        QCOMPARE(sender.writeDatagram(redPacket, QHostAddress::LocalHost, kTestRefereePort), redPacket.size());
        QTRY_COMPARE(hud->property("ownTeam").toString(), QStringLiteral("red"));
        QCOMPARE(teamBadgeLabel->property("text").toString(), QStringLiteral("我方 · 红方"));
        QCOMPARE(teamBadgeLabel->property("color").value<QColor>(), QColor("#FF626C"));
        QVERIFY(window->grabWindow().save("hud-red-team-1180x680.png"));
        QTRY_VERIFY_WITH_TIMEOUT(terminal.hudSnapshot().isEmpty(), 2500);
        QCOMPARE(terminal.eventMessages().size(), 0);
        QCOMPARE(levelIndicator->property("text").toString(), QStringLiteral("LV --"));
        QCOMPARE(shieldBarFill->width(), 0.0);
        QCOMPARE(fireIndicator->property("color").value<QColor>(), QColor("#82939F"));
        // 数据过期后偏航条同样回到未知，不保留旧角度。
        QCOMPARE(yawLabel->property("text").toString(), QStringLiteral("YAW --°"));
        QVERIFY(!yawMarker->isVisible());
        // 本车身份同样回到未知：列表恢复六台、左下角回到待确认，不保留上一次机型。
        QTRY_COMPARE(friendlyTeamRow->childItems().size(), enemySlotsBefore);
        QCOMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 机型待确认"));
        QVERIFY(!selfPhoto->property("photoReady").toBool());
        QCOMPARE(hud->property("ownTeam").toString(), QStringLiteral("unknown"));
        QCOMPARE(teamBadgeLabel->property("text").toString(), QStringLiteral("我方 · 阵营待确认"));
        QVERIFY(hud->setProperty("ownTeam", "red"));
        QCOMPARE(friendly->property("accent").value<QColor>(), QColor("#FF626C"));
        QCOMPARE(enemy->property("accent").value<QColor>(), QColor("#49D7EF"));
        QVERIFY(hud->setProperty("ownTeam", "blue"));
        QCOMPARE(friendly->property("accent").value<QColor>(), QColor("#49D7EF"));
        QVERIFY(hud->setProperty("ownTeam", "unknown"));
        QVERIFY(window->grabWindow().save("playback-1180x680.png"));
        window->resize(1600, 900);

        click(pause);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        click(loop);
        QTRY_COMPARE(player->loops(), QMediaPlayer::Infinite);
        player->setPosition(player->duration() - 250);
        QTRY_VERIFY_WITH_TIMEOUT(player->position() < 2000, 4000);
        QVERIFY(player->isPlaying());
        click(loop);
        QTRY_COMPARE(player->loops(), 1);
        player->setPosition(player->duration() - 250);
        QTRY_COMPARE_WITH_TIMEOUT(player->mediaStatus(), QMediaPlayer::EndOfMedia, 4000);
        click(pause);
        QTRY_VERIFY(player->isPlaying());

        QVERIFY(open(QUrl::fromLocalFile("Z:/rm27-missing-test-file.mp4")));
        QTRY_VERIFY_WITH_TIMEOUT(player->error() != QMediaPlayer::NoError, 5000);
        QVERIFY(!terminal.controlsArmed());
        QVERIFY(open(fixture));
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
        QCOMPARE(player->error(), QMediaPlayer::NoError);
        QCOMPARE(terminal.modeLabel(), QStringLiteral("回放"));
        QVERIFY(!terminal.controlsArmed());
        player->stop();
    }

    // 启动面板 + 离线哨兵段入口：这是「无网也能直接进去」的那条路径。
    void startPanelOfflineSentinel()
    {
        TerminalController terminal(false, nullptr, kTestRefereePort);
        auto* settings = terminal.connection();
        QVERIFY(settings);

        // 预设下拉里必须带 203.0.113.0/24 这一项，现场想不起地址时可直接点选。
        bool sentinelPresetFound = false;
        const QVariantList presets = settings->presets();
        for (const QVariant& preset : presets) {
            if (preset.toMap().value(QStringLiteral("label")).toString()
                    .contains(QStringLiteral("203.0.113.0/24"))) {
                sentinelPresetFound = true;
            }
        }
        QVERIFY(sentinelPresetFound);
        QVERIFY(presets.size() >= 3);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto* startPanel = window->findChild<QQuickItem*>("startPanel");
        auto* hostField = window->findChild<QQuickItem*>("startPanelRefereeHost");
        auto* presetCombo = window->findChild<QQuickItem*>("startPanelPreset");
        auto* sentinelNote = window->findChild<QQuickItem*>("startPanelSentinel");
        auto* enterButton = window->findChild<QQuickItem*>("startPanelEnter");
        auto* offlineButton = window->findChild<QQuickItem*>("startPanelOfflineButton");
        auto* banner = window->findChild<QQuickItem*>("offlineDrillBanner");
        QVERIFY(startPanel && hostField && presetCombo && sentinelNote);
        QVERIFY(enterButton && offlineButton && banner);

        // 每次启动都弹面板，且默认不是哨兵段。
        QVERIFY(startPanel->isVisible());
        QVERIFY(!sentinelNote->isVisible());
        QVERIFY(!banner->isVisible());
        QCOMPARE(hostField->property("text").toString(), QStringLiteral("127.0.0.1"));
        QCOMPARE(presetCombo->property("count").toInt(), presets.size());

        // 非哨兵地址绝不能被当成离线入口。
        QVERIFY(settings->setProperty("refereeHost", QStringLiteral("192.168.1.10")));
        QVERIFY(!settings->offlineSentinel());
        QVERIFY(!sentinelNote->isVisible());

        // 点选哨兵预设 → 主机被填成哨兵地址、提示块出现。
        QVERIFY(QMetaObject::invokeMethod(settings, "selectPreset", Q_ARG(int, 2)));
        QCOMPARE(settings->refereeHost(), QStringLiteral("203.0.113.7"));
        QVERIFY(settings->offlineSentinel());
        QTRY_VERIFY(sentinelNote->isVisible());
        QCOMPARE(hostField->property("text").toString(), QStringLiteral("203.0.113.7"));

        // 端口换成测试专用，避免与本机可能正在运行的模拟器抢占 39001/39002。
        QVERIFY(settings->setProperty("refereePort", 39021));
        QVERIFY(settings->setProperty("videoProbePort", 39022));
        QVERIFY(settings->setProperty("simulationVideoPort", 39023));

        window->resize(1180, 680);
        QTest::qWait(220);
        QVERIFY(enterButton->isVisible());
        QVERIFY(enterButton->mapToScene(QPointF(0, 0)).y() + enterButton->height()
                <= window->height());
        QVERIFY(window->grabWindow().save("start-panel-1180x680.png"));

        // 离线入口进入：面板收起、常驻横幅出现、进入模拟模式、控制保持锁定。
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          enterButton->mapToScene(enterButton->boundingRect().center()).toPoint());
        QTRY_VERIFY(!startPanel->isVisible());
        QVERIFY(terminal.offlineDrill());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        QTRY_VERIFY(banner->isVisible());
        QVERIFY(!terminal.controlsArmed());
        terminal.toggleControlsArmed();
        QVERIFY(!terminal.controlsArmed());
        terminal.toggleFallbackMode();
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        QVERIFY(window->grabWindow().save("start-panel-offline-1180x680.png"));

        // F1 重新打开面板；离线演练入口同样可以再次进入。
        // 右列卡片在 1180×680 下需要滚动才可见，因此这里调用面板函数（与点击按钮同一条代码路径），
        // 左列顶部的哨兵快捷按钮则必须仍在视口内。
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(startPanel->isVisible());
        auto* offlineShortcut = window->findChild<QQuickItem*>("startPanelOfflineShortcut");
        QVERIFY(offlineShortcut && offlineShortcut->isVisible());
        QVERIFY(QMetaObject::invokeMethod(startPanel, "enterOffline"));
        QTRY_VERIFY(!startPanel->isVisible());
        QVERIFY(terminal.offlineDrill());

        // 改回普通地址并套用 → 离线演练结束，横幅消失，模式回到重装操作。
        QVERIFY(settings->setProperty("refereeHost", QStringLiteral("192.168.1.10")));
        QVERIFY(terminal.applyConnectionProfile());
        QVERIFY(!terminal.offlineDrill());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("重装操作"));
        QTRY_VERIFY(!banner->isVisible());

        // 隐形快捷键 Ctrl+Shift+O：面板关闭时也能直接进离线演练。
        QVERIFY(window->setProperty("startPanelVisible", false));
        QTest::qWait(60);
        QTest::keyClick(window, Qt::Key_O, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(terminal.offlineDrill());
        QTRY_VERIFY(banner->isVisible());
        QCOMPARE(terminal.modeLabel(), QStringLiteral("模拟"));
        QVERIFY(!startPanel->isVisible());

        // 参数校验：非法地址必须被挡住，不能静默进入。
        QVERIFY(settings->setProperty("refereeHost", QStringLiteral("referee.local")));
        QVERIFY(!settings->validationMessage().isEmpty());
        QVERIFY(!settings->entryAllowed());
        QVERIFY(!terminal.applyConnectionProfile());
    }

    // 视频链路分阶段诊断：到达缺口、断流、重同步、来源拒绝都必须真的计数。
    void videoLinkDiagnostics()
    {
        rm27::video::UdpVideoIngress ingress;
        QVERIFY(ingress.start(39102));
        QUdpSocket sender;
        const QByteArray payload(64, 'x');
        auto send = [&] { return sender.writeDatagram(payload, QHostAddress::LocalHost, 39102); };

        QCOMPARE(ingress.packetCount(), quint64(0));
        QCOMPARE(ingress.silentMs(), qint64(-1));
        QVERIFY(send() == payload.size());
        QTRY_COMPARE(ingress.packetCount(), quint64(1));
        QVERIFY(ingress.receivedAny());
        QCOMPARE(ingress.arrivalGapCount(), quint32(0));
        QVERIFY(ingress.silentMs() >= 0);

        // 静默超过到达缺口阈值后再收包 → 记一次到达缺口（不是官方序号缺口）。
        QTest::qWait(400);
        QVERIFY(send() == payload.size());
        QTRY_COMPARE(ingress.packetCount(), quint64(2));
        QCOMPARE(ingress.arrivalGapCount(), quint32(1));

        // 静默超过 700ms → 断流一次；再收包 → 重同步一次。
        QTRY_COMPARE_WITH_TIMEOUT(ingress.stallCount(), quint32(1), 3000);
        QVERIFY(send() == payload.size());
        QTRY_COMPARE(ingress.resyncCount(), quint32(1));
        QCOMPARE(ingress.rejectedSourceCount(), quint32(0));

        // 控制器把读数聚合成分阶段诊断，并明确标出尚未接入的两段。
        TerminalController terminal(false, nullptr, kTestRefereePort);
        QVERIFY(terminal.connection()->setProperty("videoProbePort", 39103));
        QVERIFY(terminal.connection()->setProperty("refereePort", 39104));
        QVERIFY(terminal.applyConnectionProfile());
        const QVariantMap idle = terminal.videoDiagnostics();
        QCOMPARE(idle.value(QStringLiteral("stage")).toString(), QStringLiteral("等待首包"));
        QVERIFY(idle.value(QStringLiteral("stagePending")).toString()
                    .contains(QStringLiteral("待官方")));
        QVERIFY(terminal.videoDiagnosticsLabel().contains(QStringLiteral("来源拒绝")));

        QUdpSocket probe;
        QCOMPARE(probe.writeDatagram(payload, QHostAddress::LocalHost, 39103), payload.size());
        QTRY_COMPARE(terminal.videoDiagnostics().value(QStringLiteral("stage")).toString(),
                     QStringLiteral("已收包·等待解码"));
        QVERIFY(terminal.videoDiagnostics().value(QStringLiteral("packets")).toULongLong() >= 1);
        QVERIFY(terminal.videoDiagnosticsLabel().contains(QStringLiteral("已收包")));
    }

    // 配置原子写 + 坏文件自愈：损坏文件被隔离，优先从 .bak 恢复，否则回落默认值。
    void connectionConfigRecovery()
    {
        const QByteArray previousEnv = qgetenv("RM27_SETTINGS_FILE");
        const QString path = QDir::temp().filePath(QStringLiteral("rm27-recovery-test.ini"));
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
        QFile::remove(path + QStringLiteral(".tmp"));
        for (const QString& stale : QDir(QDir::temp())
                                        .entryList({QStringLiteral("rm27-recovery-test.ini.corrupt-*")},
                                                   QDir::Files)) {
            QFile::remove(QDir::temp().filePath(stale));
        }

        // 1) 造一个被截断、不属于本应用的文件。
        {
            QFile garbage(path);
            QVERIFY(garbage.open(QIODevice::WriteOnly | QIODevice::Text));
            garbage.write("\x00garbage ][ truncated-not-our-ini");
            garbage.close();
        }

        qputenv("RM27_SETTINGS_FILE", path.toUtf8());
        {
            rm27::app::ConnectionSettings settings;
            QVERIFY(!settings.storageNotice().isEmpty());
            QCOMPARE(settings.refereeHost(), QStringLiteral("127.0.0.1"));
            QVERIFY(settings.validationMessage().isEmpty());
            QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak")));
            const QStringList quarantined = QDir(QDir::temp())
                .entryList({QStringLiteral("rm27-recovery-test.ini.corrupt-*")}, QDir::Files);
            QCOMPARE(quarantined.size(), 1);

            // 2) 保存：原子写 + 生成 .bak + 清空自愈提示，且不留 .tmp。
            QVERIFY(settings.save());
            QVERIFY(settings.storageNotice().isEmpty());
            QVERIFY(QFileInfo::exists(path + QStringLiteral(".bak")));
            QVERIFY(!QFileInfo::exists(path + QStringLiteral(".tmp")));
            QFile written(path);
            QVERIFY(written.open(QIODevice::ReadOnly));
            const QByteArray body = written.readAll();
            written.close();
            // QSettings 的 INI 写法是分组：键 meta/schema 落地为 [meta] 段下的 schema。
            QVERIFY(body.contains("[meta]"));
            QVERIFY(body.contains("schema"));
            QVERIFY(body.contains("referee_host"));
        }
        // 3) 主文件再损坏 → 这次应从 .bak 恢复，而不是回落默认值。
        {
            QFile broken(path);
            QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Text));
            broken.write("###");
            broken.close();
        }
        {
            rm27::app::ConnectionSettings settings;
            QVERIFY(settings.storageNotice().contains(QStringLiteral("备份")));
            QVERIFY(settings.validationMessage().isEmpty());
        }

        qputenv("RM27_SETTINGS_FILE", previousEnv);
    }

    // 界面占用闸门 + 单一输入闸门：占屏时命令类动作被拦，界面类动作仍可派发。
    void inputGateAndUiOccupancy()
    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, kTestRefereePort);
        QCOMPARE(terminal.modeLabel(), QStringLiteral("重装操作"));

        // 动作表即快捷键表：提示由映射生成。
        const QVariantList actions = terminal.inputActions();
        QVERIFY(actions.size() >= 7);
        bool sawOfflineHint = false;
        for (const QVariant& entry : actions) {
            const QVariantMap action = entry.toMap();
            QVERIFY(!action.value(QStringLiteral("id")).toString().isEmpty());
            QVERIFY(!action.value(QStringLiteral("keys")).toString().isEmpty());
            if (action.value(QStringLiteral("hint")).toString()
                == QStringLiteral("离线演练进入 [Ctrl+Shift+O]")) {
                sawOfflineHint = true;
            }
        }
        QVERIFY(sawOfflineHint);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        // 启动面板可见 → 界面占用；命令类动作被拦，界面类动作仍可派发。
        QTRY_VERIFY(terminal.uiOccupied());
        QVERIFY(!terminal.submitInputAction(QStringLiteral("controls.toggleArmed")));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("界面正在占用")));
        QVERIFY(terminal.submitInputAction(QStringLiteral("panel.start")));
        QVERIFY(!terminal.controlsArmed());
        // 未知动作直接拒绝。
        QVERIFY(!terminal.submitInputAction(QStringLiteral("no.such.action")));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("未知输入动作")));
        // 「检查指令门」在占屏时也必须报告占用而不是别的原因。
        QVERIFY(!terminal.submitInputAction(QStringLiteral("gate.inspectMotion")));
        QVERIFY(terminal.lastInputDecision().contains(QStringLiteral("界面正在占用")));

        // 收起面板 → 占用解除，命令类动作改由链路原因拦截（仍不放行）。
        QVERIFY(window->setProperty("startPanelVisible", false));
        QTRY_VERIFY(!terminal.uiOccupied());
        QVERIFY(!terminal.submitInputAction(QStringLiteral("controls.toggleArmed")));
        QVERIFY(!terminal.lastInputDecision().contains(QStringLiteral("界面正在占用")));
        QVERIFY(!terminal.lastInputDecision().isEmpty());
    }

    // 性能读数：出帧率与 GUI 线程抖动（抖动大 = 解码/IO 回到 GUI 线程）。
    void performanceAndFreshness()
    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, kTestRefereePort);
        QCOMPARE(terminal.refereeFreshnessLabel(), QStringLiteral("无数据"));
        QCOMPARE(terminal.refereeFreshnessScale(), 0.0);
        QVERIFY(!terminal.guiThreadStalled());
        QVERIFY(terminal.performanceLabel().contains(QStringLiteral("出帧")));

        // 收到模拟裁判数据 → 分级进入「新鲜」。
        QUdpSocket sender;
        const QByteArray packet = R"json({
            "channel":"rm27.sim.v1.referee","sequence":1,"sent_at_unix_ms":1789999999000,
            "phase":"match","telemetry":{"hp":800,"max_hp":1000,"chassis_power_w":70,
                "buffer_energy_j":40,"barrel_heat":20,"ammo":80,"latency_ms":10,
                "packet_loss_pct":0.0}
        })json";
        QCOMPARE(sender.writeDatagram(packet, QHostAddress::LocalHost, kTestRefereePort), packet.size());
        QTRY_COMPARE(terminal.refereeFreshnessLabel(), QStringLiteral("新鲜"));
        QCOMPARE(terminal.refereeFreshnessScale(), 1.0);

        // 数据停发后分级逐级下降，最终过期（分级只影响显示，不影响放行判断）。
        QTRY_VERIFY_WITH_TIMEOUT(
            terminal.refereeFreshnessLabel() == QStringLiteral("已过期"), 4000);
        QCOMPARE(terminal.refereeFreshnessScale(), 0.0);

        // GUI 线程被人为阻塞 → 心跳抖动检出（用累计计数，避免被下一次心跳复位）；
        // 阻塞结束后瞬时标志自动清除。
        const int stallsBefore = terminal.guiStallCount();
        QThread::msleep(600);
        QTRY_VERIFY2_WITH_TIMEOUT(terminal.guiStallCount() > stallsBefore,
                                  qPrintable(terminal.performanceLabel()), 3000);
        QVERIFY2(terminal.guiPeakJitterMs() > 150, qPrintable(terminal.performanceLabel()));
        QTRY_VERIFY_WITH_TIMEOUT(!terminal.guiThreadStalled(), 3000);
        QVERIFY(terminal.performanceLabel().contains(QStringLiteral("阻塞")));
    }

    // 切源代价：同源不重启，异源要满足最短驻留。
    void sourceSwitchCost()    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, kTestRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QCOMPARE(terminal.videoSourceLabel(), QStringLiteral("不加载画面"));
        // 首次切到本机 UDP 联调：允许，并由表现层真正切源。
        QVERIFY(terminal.requestVideoSource(2));
        QTRY_COMPARE(terminal.videoSourceLabel(), QStringLiteral("本机 UDP 联调"));
        // 同源再切：短路，不重启解码。
        QVERIFY(!terminal.requestVideoSource(2));
        QVERIFY(terminal.sourceSwitchHint().contains(QStringLiteral("已是当前画面来源")));
        // 冷却期内换源：拒绝。
        QVERIFY(!terminal.requestVideoSource(1));
        QVERIFY(terminal.sourceSwitchHint().contains(QStringLiteral("最短驻留")));
        // 知道来源编号非法时明确拒绝。
        QVERIFY(!terminal.requestVideoSource(9));
        QVERIFY(terminal.sourceSwitchHint().contains(QStringLiteral("未知画面来源")));
    }

    // 开局挑选本车机型：只作裁判数据到达前的身份；写入配置、面板下拉跟随，
    // 且本车会被移出我方列表（六台 → 五台），本车自身交给左下角自车模块。
    void ownRobotSelection()
    {
        resetIsolatedSettings();
        TerminalController terminal(false, nullptr, kTestRefereePort);

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto* combo = window->findChild<QQuickItem*>("startPanelOwnRobot");
        auto* preview = window->findChild<QQuickItem*>("startPanelOwnRobotPreview");
        auto* selfRobotLabel = window->findChild<QQuickItem*>("selfRobotLabel");
        auto* selfPhoto = window->findChild<QQuickItem*>("selfRobotVisual");
        auto* friendlyTeamRow = window->findChild<QQuickItem*>("friendlyTeam");
        auto* enemyTeamRow = window->findChild<QQuickItem*>("enemyTeam");
        QVERIFY(combo && preview && selfRobotLabel && selfPhoto && friendlyTeamRow && enemyTeamRow);
        const int allSlots = enemyTeamRow->childItems().size();

        // 默认未指定：下拉停在第一项、没有预览、两边都显示六台、左下角待确认。
        QCOMPARE(combo->property("currentIndex").toInt(), 0);
        QVERIFY(!preview->isVisible());
        QCOMPARE(friendlyTeamRow->childItems().size(), allSlots);
        QCOMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 机型待确认"));

        // 机型表同时提供仪表量程：英雄 16 m/s，其余 24 m/s（本车未知时按 24）。
        auto* photoTable = window->findChild<QObject*>("robotPhotos");
        QVERIFY(photoTable);
        const auto speedMaxOf = [photoTable](int roleIndex) {
            QVariant result;
            const bool ok = QMetaObject::invokeMethod(photoTable, "speedMaxAt",
                Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, roleIndex));
            return ok ? result.toInt() : -1;
        };
        QCOMPARE(speedMaxOf(5), 16);   // 英雄
        QCOMPARE(speedMaxOf(3), 24);   // 步兵4
        QCOMPARE(speedMaxOf(-1), 24);  // 本车未知

        // 在开局配置里选「工程」：写入配置、面板下拉与缩略图跟随。
        terminal.connection()->setOwnRobot(QStringLiteral("engineer"));
        QTRY_COMPARE(terminal.ownRobotPreference(), QStringLiteral("engineer"));
        QTRY_COMPARE(combo->property("currentIndex").toInt(), 5); // 未指定 + 六台机型，工程排第 5 项
        QTRY_VERIFY(preview->isVisible());
        QTRY_COMPARE(selfRobotLabel->property("text").toString(), QStringLiteral("本车 · 工程"));
        QVERIFY(selfPhoto->property("photoSource").toString().contains(QStringLiteral("engineer")));
        // 本车移出我方列表：六台 → 五台，且被移走的正是工程那一格。
        QTRY_COMPARE(friendlyTeamRow->childItems().size(), allSlots - 1);
        QVERIFY(!findVisual(friendlyTeamRow, QStringLiteral("friendlySlot4")));
        QVERIFY(findVisual(friendlyTeamRow, QStringLiteral("friendlySlot0")));
        QCOMPARE(enemyTeamRow->childItems().size(), allSlots);

        // 选择会落盘（进入终端时走的就是这条保存路径），下次启动仍生效。
        QVERIFY(terminal.connection()->save());
        const QString iniPath = QString::fromUtf8(qgetenv("RM27_SETTINGS_FILE"));
        QVERIFY(!iniPath.isEmpty());
        QSettings persisted(iniPath, QSettings::IniFormat);
        QCOMPARE(persisted.value(QStringLiteral("identity/own_robot")).toString(),
                 QStringLiteral("engineer"));

        // 非法 id（非 ASCII）被拒绝，等价于未指定，不会写出脏值。
        terminal.connection()->setOwnRobot(QStringLiteral("步兵"));
        QCOMPARE(terminal.connection()->ownRobot(), QString());
        terminal.connection()->setOwnRobot(QString());
        QTRY_COMPARE(friendlyTeamRow->childItems().size(), allSlots);
    }

    void optionalCameraSmoke()
    {
        if (qEnvironmentVariableIsEmpty("RM27_TEST_CAMERA"))
            QSKIP("Set RM27_TEST_CAMERA=1 to test a local camera");
        if (QMediaDevices::videoInputs().isEmpty())
            QSKIP("No camera is available on this host");

        TerminalController terminal(false, nullptr, kTestRefereePort);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("terminal", &terminal);
        engine.rootContext()->setContextProperty("initialVideo", QUrl());
        engine.rootContext()->setContextProperty("initialSimUdpVideo", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(RM27_QML_SOURCE)));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        dismissStartPanel(window);
        auto* panel = window->findChild<QQuickItem*>("operatorCameraPanel");
        auto* camera = window->findChild<QCamera*>("operatorCamera");
        auto* preview = window->findChild<QQuickItem*>("operatorCameraPreview");
        auto* selector = window->findChild<QQuickItem*>("operatorCameraSelector");
        auto* cameraOpen = window->findChild<QQuickItem*>("operatorCameraOpen");
        QVERIFY(panel && camera && preview && selector && cameraOpen);
        QVERIFY(!camera->isActive());
        QVERIFY(!selector->isVisible());
        QVERIFY(selector->property("count").toInt() > 0);
        const auto fixture = QUrl::fromLocalFile(qEnvironmentVariable("RM27_TEST_VIDEO"));
        QVERIFY(QMetaObject::invokeMethod(window, "openLocalVideo", Q_ARG(QUrl, fixture)));
        QTRY_VERIFY_WITH_TIMEOUT(terminal.decodedFrames() > 5, 10000);
        auto* sink = preview->property("videoSink").value<QVideoSink*>();
        QVERIFY(sink);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          cameraOpen->mapToScene(cameraOpen->boundingRect().center()).toPoint());
        QTRY_VERIFY(selector->isVisible());
        QVERIFY(QMetaObject::invokeMethod(panel, "selectCamera", Q_ARG(int, 0)));
        QTRY_VERIFY_WITH_TIMEOUT(camera->isActive(), 5000);
        QVERIFY(!selector->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(sink->videoFrame().isValid(), 7000);
        const int framesBeforeClose = terminal.decodedFrames();
        QVERIFY(QMetaObject::invokeMethod(panel, "stopCamera"));
        QTRY_VERIFY(!camera->isActive());
        QVERIFY(!preview->isVisible());
        QTRY_COMPARE(panel->height(), 28.0);
        QTRY_VERIFY(terminal.decodedFrames() > framesBeforeClose);
        QVERIFY(!terminal.controlsArmed());
    }
};

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    // 测试使用独立的配置文件：绝不读写用户真实的启动面板设置。
    const QString settingsFile = QDir::temp().filePath(QStringLiteral("rm27-test-connection.ini"));
    QFile::remove(settingsFile);
    qputenv("RM27_SETTINGS_FILE", settingsFile.toUtf8());
    PlaybackTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_video_playback.moc"
