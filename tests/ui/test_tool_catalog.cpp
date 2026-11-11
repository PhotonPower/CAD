#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include "ui/tool_box.hpp"
#include "ui/tool_catalog.hpp"

using nfcad::ui::ToolGroup;

class TestToolCatalog : public QObject {
  Q_OBJECT
 private slots:
  void idsAreUnique() {
    QSet<QString> ids;
    for (const auto& group : nfcad::ui::tool_catalog()) {
      QVERIFY(!group.tools.empty());
      for (const auto& tool : group.tools) {
        QVERIFY2(!ids.contains(tool.id), qPrintable(tool.id));
        ids.insert(tool.id);
      }
    }
  }

  void groupCollapsesAndEmits() {
    ToolGroup group(nfcad::ui::tool_catalog().front());
    group.resize(150, 400);
    group.show();
    QVERIFY(group.isExpanded());
    QSignalSpy expanded(&group, &ToolGroup::expandedChanged);
    group.setExpanded(false);
    QCOMPARE(expanded.count(), 1);
    QVERIFY(!group.isExpanded());

    QSignalSpy triggered(&group, &ToolGroup::toolTriggered);
    group.setExpanded(true);
    auto* button = group.findChild<QToolButton*>(QStringLiteral("toolButton"));
    QVERIFY(button != nullptr);
    button->click();
    QCOMPARE(triggered.count(), 1);
  }
};

QTEST_MAIN(TestToolCatalog)
#include "test_tool_catalog.moc"
