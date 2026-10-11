#include "../tools/rm2026_observer_view.hpp"
#include <QApplication>
#include <cassert>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    Rm2026ObserverView view(39005);
    auto* tree = view.findChild<QTreeWidget*>("stateTree");
    assert(tree && tree->topLevelItemCount() == 13);
    assert(tree->editTriggers() == QAbstractItemView::NoEditTriggers);
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        assert(tree->topLevelItem(i)->text(1) == "--");
        assert(tree->topLevelItem(i)->childCount() == 0);
    }
    const QString untrusted = QStringLiteral("<b>裁判事件</b> <a href='file:///x'>操作</a>");
    view.updateReport({{"link", "subscribed"}, {"state", QJsonObject{
        {"self_dynamic", QJsonObject{{"health", 0}}},
        {"respawn", QJsonObject{{"pending", false}}},
        {"latest_event", QJsonObject{{"param", untrusted}}},
        {"position", QJsonObject{{"x", -2.5}}},
        {"latest_buff", QJsonObject{{"level", -1}}},
        {"performance", QJsonObject{{"shooter", 0}}},
        {"deployment", QJsonObject{{"state", "unknown"}}}}}});
    assert(tree->topLevelItem(1)->child(0)->text(1) == "0");
    assert(tree->topLevelItem(7)->child(0)->text(1) == "false");
    assert(tree->topLevelItem(8)->child(0)->text(1) == untrusted);
    assert(tree->topLevelItem(9)->child(0)->text(1) == "-2.5");
    assert(tree->topLevelItem(9)->childCount() == 1); // No fabricated missing axes/ID.
    assert(tree->topLevelItem(10)->child(0)->text(1) == "-1");
    assert(tree->topLevelItem(10)->childCount() == 1); // No fabricated robot ID/type.
    assert(tree->topLevelItem(11)->child(0)->text(1) == "0");
    assert(tree->topLevelItem(12)->child(0)->text(1) == "unknown");
    view.updateReport({{"link", "subscribed"}, {"stale", QJsonArray{"RobotDynamicStatus", "Event",
        "Buff", "RobotPerformanceSelectionSync", "DeployModeStatusSync"}},
        {"state", QJsonObject{{"position", QJsonObject{{"y", 0}}}}}});
    assert(tree->topLevelItem(1)->childCount() == 0);
    assert(tree->topLevelItem(1)->text(2).startsWith(QStringLiteral("过期")));
    assert(tree->topLevelItem(8)->childCount() == 0);
    assert(tree->topLevelItem(9)->child(0)->text(0) == "y");
    assert(tree->topLevelItem(9)->child(0)->text(1) == "0");
    for (int i = 10; i < 13; ++i) {
        assert(tree->topLevelItem(i)->childCount() == 0);
        assert(tree->topLevelItem(i)->text(2).startsWith(QStringLiteral("过期")));
    }
    view.updateReport({{"link", "offline"}, {"state", QJsonObject{}}});
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        assert(tree->topLevelItem(i)->childCount() == 0);
        assert(tree->topLevelItem(i)->text(1) == "--");
        assert(!tree->topLevelItem(i)->text(2).startsWith(QStringLiteral("过期")));
    }
    view.show();
    app.processEvents();
    assert(!view.grab().isNull()); // Offscreen window actually renders.
    Rm2026ObserverView demo(39005, true);
    assert(demo.windowTitle().contains(QStringLiteral("项目合成报文演示")));
    demo.updateReport({{"link", "offline"}});
    assert(demo.windowTitle().contains(QStringLiteral("非赛事数据")));
    bool demoBanner = false;
    for (auto* label : demo.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("非真实裁判系统"))) {
            demoBanner = true;
            assert(label->textFormat() == Qt::PlainText);
        }
    }
    assert(demoBanner);
}
