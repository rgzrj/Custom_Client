#pragma once

#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <array>

// Presentation of the adapter's typed diagnostic state, not a wire parser.
// No publisher, normal TerminalState, HUD binding or saved connection settings.
class Rm2026ObserverView final : public QWidget {
public:
    explicit Rm2026ObserverView(unsigned port, bool syntheticDemo = false)
    {
        setWindowTitle(QStringLiteral("RM2026 V2.0.0 · 本机只读协议监视（非 RM2027）"));
        resize(1000, 700);
        auto* layout = new QVBoxLayout(this);
        if (syntheticDemo) {
            setWindowTitle(QStringLiteral("项目合成报文演示 · RM2026 历史参考 · 非赛事数据"));
            auto* demo = new QLabel(QStringLiteral("项目合成报文演示 · 非官方数据 / 非真实裁判系统"), this);
            demo->setTextFormat(Qt::PlainText);
            demo->setStyleSheet("color: #9b5200; font-weight: bold;");
            layout->addWidget(demo);
        }
        auto* boundary = new QLabel(QStringLiteral(
            "仅 127.0.0.1:%1 · 不发布指令 · 不代表登录/控制权限/实时视频\n"
            "1500 ms 为本机接收窗口；缺失不补零，过期不表示处罚解除；事件仅保留最新样本。")
            .arg(port), this);
        boundary->setTextFormat(Qt::PlainText);
        boundary->setWordWrap(true);
        layout->addWidget(boundary);
        link_ = new QLabel(this);
        link_->setTextFormat(Qt::PlainText);
        layout->addWidget(link_);
        tree_ = new QTreeWidget(this);
        tree_->setObjectName("stateTree");
        tree_->setColumnCount(3);
        tree_->setHeaderLabels({QStringLiteral("数据块 / 原始字段名"),
                               QStringLiteral("当前值"), QStringLiteral("本机接收状态")});
        tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        layout->addWidget(tree_);
        for (const auto& block : blocks_) {
            auto* item = new QTreeWidgetItem(tree_, {QString::fromUtf8(block.title)});
            item->setExpanded(true);
        }
        updateReport({});
    }

    void updateReport(const QJsonObject& report)
    {
        const auto counts = report.value("counts").toObject();
        link_->setText(QStringLiteral("订阅链路：%1 · 会话 %2 · 接受 %3 / 拒绝 %4 / retained %5 / DUP %6")
            .arg(report.value("link").toString("stopped"))
            .arg(report.value("sessions").toInt()).arg(counts.value("accepted").toInt())
            .arg(counts.value("rejected").toInt()).arg(counts.value("retained").toInt())
            .arg(counts.value("duplicates").toInt()));
        const auto state = report.value("state").toObject();
        const auto stale = report.value("stale").toArray();
        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            const auto& block = blocks_[i];
            auto* item = tree_->topLevelItem(static_cast<int>(i));
            qDeleteAll(item->takeChildren());
            const auto value = state.value(block.key);
            const bool present = value.isObject();
            item->setText(1, present ? QString{} : QStringLiteral("--"));
            item->setText(2, present ? QStringLiteral("接收窗口内（非源端实时证明）") :
                stale.contains(QString::fromLatin1(block.topic)) ? QStringLiteral("过期 · 旧值已清空") :
                QStringLiteral("无数据 / 会话已清空"));
            if (present) appendValue(item, value);
        }
    }

private:
    static void appendValue(QTreeWidgetItem* parent, const QJsonValue& value)
    {
        if (value.isObject()) {
            const auto object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                auto* child = new QTreeWidgetItem(parent, {it.key()});
                appendValue(child, it.value());
            }
        } else if (value.isArray()) {
            const auto array = value.toArray();
            for (qsizetype i = 0; i < array.size(); ++i) {
                auto* child = new QTreeWidgetItem(parent, {QString::number(i)});
                appendValue(child, array.at(i));
            }
        } else {
            // QTreeWidget renders plain text, including untrusted event parameters.
            const auto text = value.isString() ? value.toString() : value.isBool() ?
                (value.toBool() ? QStringLiteral("true") : QStringLiteral("false")) :
                value.isDouble() ? QString::number(value.toDouble(), 'g', 17) : QStringLiteral("--");
            parent->setText(1, text);
        }
    }

    struct Block { const char* key; const char* topic; const char* title; };
    inline static constexpr std::array<Block, 13> blocks_{{
        {"self_config", "RobotStaticStatus", "本车配置 · self_config"},
        {"self_dynamic", "RobotDynamicStatus", "本车动态 · self_dynamic"},
        {"units", "GlobalUnitStatus", "双方单位 · units"},
        {"economy", "GlobalLogisticsStatus", "本方经济 · economy"},
        {"game", "GameStatus", "比赛报告 · game"},
        {"penalty", "PenaltyInfo", "判罚报告 · penalty"},
        {"modules", "RobotModuleStatus", "模块报告 · modules"},
        {"respawn", "RobotRespawnStatus", "复活报告 · respawn"},
        {"latest_event", "Event", "最新事件 · latest_event"},
        {"position", "RobotPosition", "位置原值 · position（未标定地图）"},
        {"latest_buff", "Buff", "最新增益报告 · latest_buff（非完整增益列表）"},
        {"performance", "RobotPerformanceSelectionSync", "性能体系原码 · performance"},
        {"deployment", "DeployModeStatusSync", "部署报告 · deployment"}
    }};
    QLabel* link_;
    QTreeWidget* tree_;
};
