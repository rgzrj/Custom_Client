#include "app/connection_settings.hpp"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>
#include <QStandardPaths>
#include <QStringList>
#include <QVariantMap>

using rm27::ConnectionProfile;
using rm27::ProfileIssue;

namespace rm27::app {
namespace {

// 写入配置时带上 schema 标记：载入时用它区分「本应用的旧版配置」与「被截断/陌生的文件」。
constexpr int kConfigSchema = 1;

QString fromStd(const std::string& text)
{
    return QString::fromUtf8(text.c_str());
}

// 链路本地地址（169.254.0.0/16）说明网卡没拿到有效配置，不能算「有线链路就绪」；
// 环回同理。两者都不参与有线优先判定。
bool isUsableLinkAddress(const QHostAddress& address)
{
    return address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()
        && !address.isLinkLocal();
}

// Windows 会把蓝牙 PAN 适配器报成 Ethernet 类型，若照类型直译会把蓝牙写成「有线」，
// 从而误判「有线链路已连接」——这里按名称兜一层。
bool looksLikeBluetooth(const QString& name)
{
    return name.contains(QStringLiteral("蓝牙")) || name.contains(QStringLiteral("Bluetooth"),
                                                                  Qt::CaseInsensitive);
}

int clampPort(const int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 65535) {
        return 65535;
    }
    return value;
}

QVariantMap makePreset(const QString& id, const QString& label, const QString& hint,
                       const ConnectionProfile& profile, const bool offline)
{
    return QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("hint"), hint},
        {QStringLiteral("refereeHost"), fromStd(profile.refereeHost)},
        {QStringLiteral("refereePort"), int(profile.refereePort)},
        {QStringLiteral("videoBindAddress"), fromStd(profile.videoBindAddress)},
        {QStringLiteral("videoProbePort"), int(profile.videoProbePort)},
        {QStringLiteral("simulationVideoPort"), int(profile.simulationVideoPort)},
        {QStringLiteral("useMqtt"), profile.useMqtt},
        {QStringLiteral("mqttHost"), fromStd(profile.mqttHost)},
        {QStringLiteral("mqttPort"), int(profile.mqttPort)},
        {QStringLiteral("offline"), offline},
    };
}

}  // namespace

ConnectionSettings::ConnectionSettings(QObject* parent)
    : QObject(parent)
{
    // 预设顺序固定，界面下拉直接用它的下标（第三个就是离线哨兵段，便于现场想不起来时点选）。
    const ConnectionProfile localSimulation;
    presets_.append(makePreset(QStringLiteral("local-sim"), QStringLiteral("本机模拟（默认）"),
        QStringLiteral("127.0.0.1 环回 · 裁判 39001 / 探针 39002 / 联调 39003 · MQTT 关"),
        localSimulation, false));

    ConnectionProfile localWithMqtt = localSimulation;
    localWithMqtt.useMqtt = true;
    presets_.append(makePreset(QStringLiteral("local-mqtt"), QStringLiteral("本机模拟 + MQTT 39004"),
        QStringLiteral("127.0.0.1:39004 · 主题 rm27/sim/v1/referee · 只订阅不发布"),
        localWithMqtt, false));

    ConnectionProfile sentinel = localSimulation;
    sentinel.refereeHost = rm27::kOfflineSentinelHost;
    presets_.append(makePreset(QStringLiteral("offline-sentinel"),
        QStringLiteral("离线哨兵 %1（无需网络）").arg(fromStd(rm27::kOfflineSentinelCidr)),
        QStringLiteral("RFC 5737 文档保留段，内外网均不可路由 · 无需网线直接进入离线演练"),
        sentinel, true));

    ConnectionProfile formalPlaceholder = localSimulation;
    formalPlaceholder.refereeHost.clear();
    formalPlaceholder.videoBindAddress = "0.0.0.0";
    presets_.append(makePreset(QStringLiteral("formal-pending"),
        QStringLiteral("正式链路（待 RM2027 正式协议）"),
        QStringLiteral("地址/端口/主题/QoS 尚未确认，正式适配器未接入，当前不可进入"),
        formalPlaceholder, false));

    load();
    refreshInterfaces();
    updateIssue();
}

ConnectionProfile ConnectionSettings::profile() const
{
    ConnectionProfile profile;
    profile.refereeHost = refereeHost_.toStdString();
    profile.refereePort = static_cast<std::uint16_t>(clampPort(refereePort_));
    profile.videoBindAddress = videoBindAddress_.toStdString();
    profile.videoProbePort = static_cast<std::uint16_t>(clampPort(videoProbePort_));
    profile.simulationVideoPort = static_cast<std::uint16_t>(clampPort(simulationVideoPort_));
    profile.useMqtt = useMqtt_;
    profile.mqttHost = mqttHost_.toStdString();
    profile.mqttPort = static_cast<std::uint16_t>(clampPort(mqttPort_));
    return profile;
}

QString ConnectionSettings::refereeHost() const { return refereeHost_; }
int ConnectionSettings::refereePort() const { return refereePort_; }
QString ConnectionSettings::videoBindAddress() const { return videoBindAddress_; }
int ConnectionSettings::videoProbePort() const { return videoProbePort_; }
int ConnectionSettings::simulationVideoPort() const { return simulationVideoPort_; }
bool ConnectionSettings::useMqtt() const { return useMqtt_; }
QString ConnectionSettings::mqttHost() const { return mqttHost_; }
int ConnectionSettings::mqttPort() const { return mqttPort_; }
QString ConnectionSettings::ownTeam() const { return ownTeam_; }
QString ConnectionSettings::ownRobot() const { return ownRobot_; }
bool ConnectionSettings::offlineSentinel() const { return offlineSentinel_; }
bool ConnectionSettings::localSimulation() const { return localSimulation_; }
QString ConnectionSettings::sentinelCidr() const { return fromStd(rm27::kOfflineSentinelCidr); }
QString ConnectionSettings::sentinelHost() const { return fromStd(rm27::kOfflineSentinelHost); }
QString ConnectionSettings::validationMessage() const
{
    if (issue_ != ProfileIssue::Ok) return fromStd(rm27::profileIssueText(issue_));
    if (rm2026Profile_ && !entryAllowed_)
        return QStringLiteral("RM2026 需要启用 MQTT 并填写明确端点；本机协议测试只能使用 127.0.0.1");
    return entryAllowed_ ? QString()
        : QStringLiteral("正式链路适配器未接入；当前仅支持本机模拟或离线演练的环回端点");
}
QString ConnectionSettings::entryModeLabel() const { return entryModeLabel_; }
bool ConnectionSettings::entryAllowed() const { return entryAllowed_; }
bool ConnectionSettings::formalLinkReady() const { return rm2026Profile_ && !rm2026TestSource_; }
void ConnectionSettings::enableRm2026Profile(bool testSource)
{
    rm2026Profile_ = true;
    rm2026TestSource_ = testSource;
    updateIssue();
    emit changed();
}
QVariantList ConnectionSettings::interfaces() const { return interfaces_; }
bool ConnectionSettings::hasWiredLink() const { return hasWiredLink_; }
QString ConnectionSettings::interfaceSummary() const { return interfaceSummary_; }
QVariantList ConnectionSettings::presets() const { return presets_; }
QString ConnectionSettings::auditLabel() const
{
    if (lastOfflineEntryAt_.isEmpty()) {
        return QStringLiteral("尚无离线演练记录");
    }
    return QStringLiteral("上次离线演练：%1 · %2").arg(lastOfflineEntryAt_, lastOfflineEntryHost_);
}
ProfileIssue ConnectionSettings::issue() const { return issue_; }

QString ConnectionSettings::storagePath() const
{
    const QString override = qEnvironmentVariable("RM27_SETTINGS_FILE");
    if (!override.isEmpty()) {
        return override;
    }
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(directory).filePath(QStringLiteral("connection.ini"));
}

int ConnectionSettings::presetIndex() const
{
    for (int index = 0; index < presets_.size(); ++index) {
        const QVariantMap preset = presets_.at(index).toMap();
        const bool sameHost = preset.value(QStringLiteral("refereeHost")).toString() == refereeHost_;
        const bool samePorts = preset.value(QStringLiteral("refereePort")).toInt() == refereePort_
            && preset.value(QStringLiteral("videoProbePort")).toInt() == videoProbePort_
            && preset.value(QStringLiteral("simulationVideoPort")).toInt() == simulationVideoPort_
            && preset.value(QStringLiteral("videoBindAddress")).toString() == videoBindAddress_;
        const bool sameMqtt = preset.value(QStringLiteral("useMqtt")).toBool() == useMqtt_
            && (!useMqtt_
                || (preset.value(QStringLiteral("mqttHost")).toString() == mqttHost_
                    && preset.value(QStringLiteral("mqttPort")).toInt() == mqttPort_));
        if (sameHost && samePorts && sameMqtt) {
            return index;
        }
    }
    return -1;
}

void ConnectionSettings::setStringField(QString& field, const QString& value)
{
    if (field == value) {
        return;
    }
    field = value;
    updateIssue();
    emit changed();
}

void ConnectionSettings::setPortField(int& field, const int value)
{
    const int clamped = clampPort(value);
    if (field == clamped) {
        return;
    }
    field = clamped;
    updateIssue();
    emit changed();
}

void ConnectionSettings::setRefereeHost(const QString& value) { setStringField(refereeHost_, value); }
void ConnectionSettings::setRefereePort(const int value) { setPortField(refereePort_, value); }
void ConnectionSettings::setVideoBindAddress(const QString& value) { setStringField(videoBindAddress_, value); }
void ConnectionSettings::setVideoProbePort(const int value) { setPortField(videoProbePort_, value); }
void ConnectionSettings::setSimulationVideoPort(const int value) { setPortField(simulationVideoPort_, value); }
void ConnectionSettings::setMqttHost(const QString& value) { setStringField(mqttHost_, value); }
void ConnectionSettings::setMqttPort(const int value) { setPortField(mqttPort_, value); }
void ConnectionSettings::setOwnTeam(const QString& value) { setStringField(ownTeam_, value); }

void ConnectionSettings::setOwnRobot(const QString& value)
{
    // 只接受短 ASCII 角色 id（如 infantry4）：具体有哪些机型由界面侧的机型表定义，
    // 这里不做第二份清单，未知 id 在界面上等价于未指定。
    QString sanitized;
    const QString trimmed = value.trimmed();
    if (trimmed.size() <= 16) {
        bool ascii = true;
        for (const QChar character : trimmed) {
            if (character.unicode() > 0x7F) {
                ascii = false;
                break;
            }
        }
        if (ascii) {
            sanitized = trimmed;
        }
    }
    setStringField(ownRobot_, sanitized);
}

void ConnectionSettings::setUseMqtt(const bool value)
{
    if (useMqtt_ == value) {
        return;
    }
    useMqtt_ = value;
    updateIssue();
    emit changed();
}

void ConnectionSettings::updateIssue()
{
    const ConnectionProfile current = profile();
    issue_ = rm27::validateProfile(current);
    offlineSentinel_ = rm27::profileUsesOfflineSentinel(current);
    localSimulation_ = rm27::profileIsLocalSimulation(current);

    if (rm2026Profile_) {
        const QHostAddress broker(mqttHost_);
        issue_ = broker.protocol() == QAbstractSocket::IPv4Protocol && !broker.isNull()
            && broker.toString() == mqttHost_ ? ProfileIssue::Ok : ProfileIssue::MqttHostInvalid;
        entryModeLabel_ = mqttHost_ == "127.0.0.1" ? QStringLiteral("RM2026 本机协议测试") : QStringLiteral("RM2026 裁判数据");
        entryAllowed_ = issue_ == ProfileIssue::Ok && current.useMqtt
            && !rm27::isOfflineSentinelHost(current.mqttHost);
        return;
    }

    if (offlineSentinel_) {
        entryModeLabel_ = QStringLiteral("离线演练");
    } else if (localSimulation_) {
        entryModeLabel_ = QStringLiteral("本机模拟");
    } else {
        entryModeLabel_ = QStringLiteral("正式链路");
    }

    entryAllowed_ = issue_ == ProfileIssue::Ok
        && (offlineSentinel_ || localSimulation_)
        && rm27::isLoopbackHost(current.videoBindAddress)
        && (!current.useMqtt || rm27::isLoopbackHost(current.mqttHost));
}

void ConnectionSettings::refreshInterfaces()
{
    QVariantList list;
    int wiredCount = 0;
    int wirelessCount = 0;
    bool hasWiredLink = false;

    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
        const QNetworkInterface::InterfaceFlags flags = interface.flags();
        const bool up = flags.testFlag(QNetworkInterface::IsUp)
            && flags.testFlag(QNetworkInterface::IsRunning);

        QString kind = QStringLiteral("other");
        QString kindLabel = QStringLiteral("其他");
        switch (interface.type()) {
        case QNetworkInterface::Ethernet:
            kind = QStringLiteral("wired");
            kindLabel = QStringLiteral("有线");
            break;
        case QNetworkInterface::Wifi:
            kind = QStringLiteral("wireless");
            kindLabel = QStringLiteral("无线");
            break;
        case QNetworkInterface::Loopback:
            kind = QStringLiteral("loopback");
            kindLabel = QStringLiteral("环回");
            break;
        case QNetworkInterface::Virtual:
            kind = QStringLiteral("virtual");
            kindLabel = QStringLiteral("虚拟");
            break;
        default:
            break;
        }

        const QString name = interface.humanReadableName().isEmpty()
            ? interface.name()
            : interface.humanReadableName();
        if (kind == QLatin1String("wired") && looksLikeBluetooth(name)) {
            kind = QStringLiteral("virtual");
            kindLabel = QStringLiteral("虚拟");
        }

        QStringList addresses;
        bool hasAddress = false;
        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            addresses.append(entry.ip().toString());
            if (isUsableLinkAddress(entry.ip())) {
                hasAddress = true;
            }
        }

        if (kind == QLatin1String("wired") && up && hasAddress) {
            hasWiredLink = true;
            ++wiredCount;
        } else if (kind == QLatin1String("wireless") && up && hasAddress) {
            ++wirelessCount;
        }

        list.append(QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("kindLabel"), kindLabel},
            {QStringLiteral("up"), up},
            {QStringLiteral("addresses"), addresses.isEmpty()
                 ? QStringLiteral("—") : addresses.join(QStringLiteral(", "))},
            {QStringLiteral("stateLabel"), up ? (hasAddress ? QStringLiteral("已连接")
                                                            : QStringLiteral("已启用"))
                                              : QStringLiteral("未连接")},
        });
    }

    interfaces_ = list;
    hasWiredLink_ = hasWiredLink;
    interfaceSummary_ = QStringLiteral("网卡 %1 · 有线 %2 · 无线 %3%4")
                            .arg(list.size())
                            .arg(wiredCount)
                            .arg(wirelessCount)
                            .arg(hasWiredLink ? QString()
                                              : QStringLiteral(" · 未检测到有线链路，正式模式不可进入"));

    emit interfacesChanged();
    updateIssue();
    emit changed();
}

bool ConnectionSettings::selectPreset(const int index)
{
    if (index < 0 || index >= presets_.size()) {
        return false;
    }
    const QVariantMap preset = presets_.at(index).toMap();
    refereeHost_ = preset.value(QStringLiteral("refereeHost")).toString();
    refereePort_ = preset.value(QStringLiteral("refereePort")).toInt();
    videoBindAddress_ = preset.value(QStringLiteral("videoBindAddress")).toString();
    videoProbePort_ = preset.value(QStringLiteral("videoProbePort")).toInt();
    simulationVideoPort_ = preset.value(QStringLiteral("simulationVideoPort")).toInt();
    useMqtt_ = preset.value(QStringLiteral("useMqtt")).toBool();
    mqttHost_ = preset.value(QStringLiteral("mqttHost")).toString();
    mqttPort_ = preset.value(QStringLiteral("mqttPort")).toInt();
    updateIssue();
    emit changed();
    return true;
}

bool ConnectionSettings::enterOfflineSentinel()
{
    setStringField(refereeHost_, fromStd(rm27::kOfflineSentinelHost));
    return offlineSentinel_;
}

void ConnectionSettings::recordOfflineEntry()
{
    lastOfflineEntryAt_ = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    lastOfflineEntryHost_ = refereeHost_;
    QSettings settings(storagePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("audit/offline_entry_at"), lastOfflineEntryAt_);
    settings.setValue(QStringLiteral("audit/offline_entry_host"), lastOfflineEntryHost_);
    settings.sync();
    emit auditChanged();
    emit changed();
}

void ConnectionSettings::load()
{
    const ConnectionProfile defaults;
    applyDefaults(defaults);
    storageNotice_.clear();

    const QString path = storagePath();
    if (!QFileInfo::exists(path)) {
        emit auditChanged();
        return;
    }

    QString failure;
    if (readFromFile(path, failure)) {
        emit auditChanged();
        return;
    }

    // 坏文件自愈：先隔离原件，再优先用上一份可用配置（.bak），最后才回落默认值。
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString quarantine = path + QStringLiteral(".corrupt-") + stamp;
    QFile::remove(quarantine);
    const bool isolated = QFile::rename(path, quarantine) || QFile::copy(path, quarantine);

    const QString backupPath = path + QStringLiteral(".bak");
    QString ignored;
    if (QFileInfo::exists(backupPath) && readFromFile(backupPath, ignored)) {
        // 顺序要紧：先把恢复出来的值写回主文件（save 成功会清空提示），再设置提示。
        const QString notice = QStringLiteral("配置%1，已隔离为 %2，并从备份恢复")
                                   .arg(failure, QFileInfo(quarantine).fileName());
        save();
        storageNotice_ = notice;
    } else {
        storageNotice_ = isolated
            ? QStringLiteral("配置%1，已隔离为 %2，本次使用默认值")
                  .arg(failure, QFileInfo(quarantine).fileName())
            : QStringLiteral("配置%1，且无法隔离原文件，本次使用默认值").arg(failure);
    }
    emit auditChanged();
    emit changed();
}

void ConnectionSettings::setVolume(int value)
{
    value = std::clamp(value, 0, 100);
    if (volume_ == value) return;
    volume_ = value;
    emit changed();
}

void ConnectionSettings::setMouseSensitivity(int value)
{
    value = std::clamp(value, 10, 300);
    if (mouseSensitivity_ == value) return;
    mouseSensitivity_ = value;
    emit changed();
}

void ConnectionSettings::setShowCrosshair(bool value)
{
    if (showCrosshair_ == value) return;
    showCrosshair_ = value;
    emit changed();
}

void ConnectionSettings::setShowMap(bool value)
{
    if (showMap_ == value) return;
    showMap_ = value;
    emit changed();
}

void ConnectionSettings::setShowFps(bool value)
{
    if (showFps_ == value) return;
    showFps_ = value;
    emit changed();
}

void ConnectionSettings::setFullscreen(bool value)
{
    if (fullscreen_ == value) return;
    fullscreen_ = value;
    emit changed();
}

void ConnectionSettings::setMapImage(const QString& value)
{
    // 底图只读本地文件，不允许远程 URL 或网络共享。
    const QUrl url(value);
    const QString accepted = value.size() <= 4096 && url.isLocalFile()
        && url.host().isEmpty() && !url.toLocalFile().startsWith(QStringLiteral("//"))
        && !url.toLocalFile().startsWith(QStringLiteral("\\\\")) ? value : QString();
    if (mapImage_ == accepted) return;
    mapImage_ = accepted;
    emit changed();
}

bool ConnectionSettings::save()
{
    const QString path = storagePath();
    const QString tempPath = path + QStringLiteral(".tmp");
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        storageNotice_ = QStringLiteral("配置目录不可写，本次未保存");
        emit changed();
        return false;
    }

    // 1) 先写临时 INI：写不成就原样保留旧文件，绝不留下半个配置。
    {
        QSettings temp(tempPath, QSettings::IniFormat);
        temp.setValue(QStringLiteral("meta/schema"), kConfigSchema);
        temp.setValue(QStringLiteral("endpoint/referee_host"), refereeHost_);
        temp.setValue(QStringLiteral("endpoint/referee_port"), refereePort_);
        temp.setValue(QStringLiteral("endpoint/video_bind"), videoBindAddress_);
        temp.setValue(QStringLiteral("endpoint/video_probe_port"), videoProbePort_);
        temp.setValue(QStringLiteral("endpoint/simulation_video_port"), simulationVideoPort_);
        temp.setValue(QStringLiteral("endpoint/use_mqtt"), useMqtt_);
        temp.setValue(QStringLiteral("endpoint/mqtt_host"), mqttHost_);
        temp.setValue(QStringLiteral("endpoint/mqtt_port"), mqttPort_);
        temp.setValue(QStringLiteral("identity/own_team"), ownTeam_);
        temp.setValue(QStringLiteral("identity/own_robot"), ownRobot_);
        temp.setValue(QStringLiteral("display/volume"), volume_);
        temp.setValue(QStringLiteral("display/mouse_sensitivity"), mouseSensitivity_);
        temp.setValue(QStringLiteral("display/crosshair"), showCrosshair_);
        temp.setValue(QStringLiteral("display/map"), showMap_);
        temp.setValue(QStringLiteral("display/fps"), showFps_);
        temp.setValue(QStringLiteral("display/fullscreen"), fullscreen_);
        temp.setValue(QStringLiteral("display/map_image"), mapImage_);
        temp.setValue(QStringLiteral("audit/offline_entry_at"), lastOfflineEntryAt_);
        temp.setValue(QStringLiteral("audit/offline_entry_host"), lastOfflineEntryHost_);
        temp.sync();
        if (temp.status() != QSettings::NoError) {
            QFile::remove(tempPath);
            storageNotice_ = QStringLiteral("配置写入失败，旧配置未改动");
            emit changed();
            return false;
        }
    }

    // 2) 原子替换：写到同目录临时文件再 rename，崩溃或断电不会留下残缺配置。
    QFile staged(tempPath);
    const bool stagedReadable = staged.open(QIODevice::ReadOnly);
    const QByteArray payload = stagedReadable ? staged.readAll() : QByteArray();
    staged.close();
    QFile::remove(tempPath);

    bool committed = false;
    if (stagedReadable) {
        QSaveFile target(path);
        if (target.open(QIODevice::WriteOnly)) {
            target.write(payload);
            committed = target.commit();
        }
    }

    // 3) 保留上一份可用配置为 .bak，自愈时优先用它而不是直接回落默认值。
    if (committed) {
        const QString backupPath = path + QStringLiteral(".bak");
        QFile::remove(backupPath);
        QFile::copy(path, backupPath);
        storageNotice_.clear();
    } else {
        storageNotice_ = QStringLiteral("配置原子写入失败，旧配置与备份保持不变");
    }
    emit changed();
    return committed;
}

void ConnectionSettings::applyDefaults(const ConnectionProfile& defaults)
{
    refereeHost_ = fromStd(defaults.refereeHost);
    refereePort_ = int(defaults.refereePort);
    videoBindAddress_ = fromStd(defaults.videoBindAddress);
    videoProbePort_ = int(defaults.videoProbePort);
    simulationVideoPort_ = int(defaults.simulationVideoPort);
    useMqtt_ = defaults.useMqtt;
    mqttHost_ = fromStd(defaults.mqttHost);
    mqttPort_ = int(defaults.mqttPort);
    ownTeam_ = QStringLiteral("unknown");
    ownRobot_.clear();
    volume_ = 60;
    mouseSensitivity_ = 100;
    showCrosshair_ = showMap_ = showFps_ = true;
    fullscreen_ = false;
    mapImage_.clear();
    lastOfflineEntryAt_.clear();
    lastOfflineEntryHost_.clear();
}

bool ConnectionSettings::readFromFile(const QString& path, QString& failure)
{
    QSettings settings(path, QSettings::IniFormat);
    if (settings.status() != QSettings::NoError) {
        failure = QStringLiteral("解析失败");
        return false;
    }
    // INI 解析非常宽松：文件被截断或根本不是本应用的文件时 status 依然是 NoError，
    // 因此再用「schema 标记或任一已知键」确认这确实是我们的配置。
    const bool ours = settings.contains(QStringLiteral("meta/schema"))
        || settings.contains(QStringLiteral("endpoint/referee_host"))
        || settings.contains(QStringLiteral("endpoint/video_probe_port"))
        || settings.contains(QStringLiteral("identity/own_team"))
        || settings.contains(QStringLiteral("identity/own_robot"))
        || settings.contains(QStringLiteral("audit/offline_entry_at"));
    if (!ours) {
        failure = QStringLiteral("内容不是本应用的配置");
        return false;
    }

    const ConnectionProfile defaults;
    refereeHost_ = settings.value(QStringLiteral("endpoint/referee_host"),
                                  fromStd(defaults.refereeHost)).toString();
    refereePort_ = clampPort(settings.value(QStringLiteral("endpoint/referee_port"),
                                            int(defaults.refereePort)).toInt());
    videoBindAddress_ = settings.value(QStringLiteral("endpoint/video_bind"),
                                       fromStd(defaults.videoBindAddress)).toString();
    videoProbePort_ = clampPort(settings.value(QStringLiteral("endpoint/video_probe_port"),
                                               int(defaults.videoProbePort)).toInt());
    simulationVideoPort_ = clampPort(settings.value(QStringLiteral("endpoint/simulation_video_port"),
                                                    int(defaults.simulationVideoPort)).toInt());
    useMqtt_ = settings.value(QStringLiteral("endpoint/use_mqtt"), defaults.useMqtt).toBool();
    mqttHost_ = settings.value(QStringLiteral("endpoint/mqtt_host"),
                               fromStd(defaults.mqttHost)).toString();
    mqttPort_ = clampPort(settings.value(QStringLiteral("endpoint/mqtt_port"),
                                         int(defaults.mqttPort)).toInt());
    ownTeam_ = settings.value(QStringLiteral("identity/own_team"), QStringLiteral("unknown")).toString();
    ownRobot_ = settings.value(QStringLiteral("identity/own_robot"), QString()).toString();
    setVolume(settings.value(QStringLiteral("display/volume"), 60).toInt());
    setMouseSensitivity(settings.value(QStringLiteral("display/mouse_sensitivity"), 100).toInt());
    showCrosshair_ = settings.value(QStringLiteral("display/crosshair"), true).toBool();
    showMap_ = settings.value(QStringLiteral("display/map"), true).toBool();
    showFps_ = settings.value(QStringLiteral("display/fps"), true).toBool();
    fullscreen_ = settings.value(QStringLiteral("display/fullscreen"), false).toBool();
    setMapImage(settings.value(QStringLiteral("display/map_image")).toString());
    lastOfflineEntryAt_ = settings.value(QStringLiteral("audit/offline_entry_at")).toString();
    lastOfflineEntryHost_ = settings.value(QStringLiteral("audit/offline_entry_host")).toString();
    return true;
}

QString ConnectionSettings::storageNotice() const
{
    return storageNotice_;
}

}  // namespace rm27::app
