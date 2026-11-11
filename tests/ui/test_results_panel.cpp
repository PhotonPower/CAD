#include <QScrollBar>
#include <QTest>
#include <QTreeWidget>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "results/result_store.hpp"
#include "ui/layer_panel.hpp"
#include "ui/results_panel.hpp"

using namespace nfcad;

class TestResultsPanel : public QObject {
  Q_OBJECT
 private slots:
  void uncheckingHidesWithoutCrash() {
    // regression: unchecking a run or an item rebuilt the tree inside QTreeWidgetItem::setData
    // and deleted the item Qt was still working on (crash when hiding a ray set)
    document::Document doc;
    results::ResultStore store;
    ui::ResultsPanel panel(&store, &doc);
    const auto run = store.createSet(QStringLiteral("Trace"));
    results::LineSet l;
    l.points = {0, 0, 0, 1, 1, 1};
    l.starts = {0, 2};
    const auto rays = store.addLines(run, QStringLiteral("Rays messarm"), l);
    store.addLines(run, QStringLiteral("Rays referenzarm"), l);
    auto* tree = panel.findChild<QTreeWidget*>();
    // the way a click goes: through the model (QTreeModel::setData uses the item afterwards)
    const QModelIndex index = tree->model()->index(0, 0, tree->model()->index(0, 0));
    constexpr int kMarker = Qt::UserRole + 99;
    tree->topLevelItem(0)->child(0)->setData(0, kMarker, 42);
    QVERIFY(tree->model()->setData(index, Qt::Unchecked, Qt::CheckStateRole));
    // still the same item right after the click: the tree is not rebuilt inside Qt's setData
    QCOMPARE(tree->topLevelItem(0)->child(0)->data(0, kMarker).toInt(), 42);
    QTRY_VERIFY(!store.item(rays)->visible);
    QCOMPARE(store.visibleItems(results::ResultItem::Kind::Lines).size(), std::size_t{1});
    QVERIFY(tree->model()->setData(tree->model()->index(0, 0), Qt::Unchecked, Qt::CheckStateRole));  // the run
    QTRY_VERIFY(store.visibleItems(results::ResultItem::Kind::Lines).empty());
    QVERIFY(tree->model()->setData(tree->model()->index(0, 0), Qt::Checked, Qt::CheckStateRole));
    QTRY_COMPARE(store.visibleItems(results::ResultItem::Kind::Lines).size(), std::size_t{1});
  }

  void layerVisibilityWithoutCrash() {
    // the same pattern in the layer panel: hide a layer by its check box
    document::Document doc;
    document::Selection selection;
    selection.attach(&doc);
    ui::LayerPanel panel(&doc, &selection);
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree->topLevelItemCount() >= 1);
    constexpr int kMarker = Qt::UserRole + 99;
    tree->topLevelItem(0)->setData(0, kMarker, 42);
    QVERIFY(tree->model()->setData(tree->model()->index(0, 0), Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(tree->topLevelItem(0)->data(0, kMarker).toInt(), 42);  // not rebuilt inside setData
    QTRY_VERIFY(!doc.layers().front().visible);
  }
};

QTEST_MAIN(TestResultsPanel)
#include "test_results_panel.moc"
