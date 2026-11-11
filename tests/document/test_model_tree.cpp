#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <nurbforge/nurbforge.hpp>

#include "document/document.hpp"
#include "document/nfcad_io.hpp"
#include "document/selection.hpp"
#include "ui/model_tree_panel.hpp"

using nfcad::document::Document;
using nfcad::document::EntityId;
using nfcad::document::GroupId;
using nfcad::document::make_entity;

namespace {
std::vector<EntityId> three(Document& doc) {
  return doc.addEntities({make_entity(nf::Vec3{0, 0, 0}, QStringLiteral("a")),
                          make_entity(nf::Vec3{1, 0, 0}, QStringLiteral("b")),
                          make_entity(nf::Vec3{2, 0, 0}, QStringLiteral("c"))},
                         QStringLiteral("add"));
}
}  // namespace

class TestModelTree : public QObject {
  Q_OBJECT
 private slots:
  void groupsNestAndUndo() {
    Document doc;
    const auto ids = three(doc);
    const GroupId outer = doc.groupEntities({ids[0], ids[1]}, QStringLiteral("outer"));
    const GroupId inner = doc.groupEntities({ids[1]}, QStringLiteral("inner"), outer);
    QCOMPARE(doc.group(inner)->parent, outer);
    QCOMPARE(doc.entity(ids[1])->group, inner);
    QCOMPARE(doc.entitiesInGroup(outer).size(), std::size_t{2});  // indirect members count

    doc.ungroup(outer);  // inner moves to the top level, a to the top level
    QVERIFY(doc.group(outer) == nullptr);
    QCOMPARE(doc.group(inner)->parent, GroupId{0});
    QCOMPARE(doc.entity(ids[0])->group, GroupId{0});
    doc.undoStack()->undo();
    QCOMPARE(doc.group(inner)->parent, outer);
    QCOMPARE(doc.entity(ids[0])->group, outer);
    doc.undoStack()->undo();  // the inner group
    QVERIFY(doc.group(inner) == nullptr);
    QCOMPARE(doc.entity(ids[1])->group, outer);
  }

  void visibilityIsInherited() {
    Document doc;
    const auto ids = three(doc);
    const GroupId outer = doc.groupEntities({ids[0], ids[1]}, QStringLiteral("outer"));
    const GroupId inner = doc.groupEntities({ids[1]}, QStringLiteral("inner"), outer);
    QSignalSpy changed(&doc, &Document::entityChanged);
    doc.setGroupVisible(outer, false);
    QCOMPARE(changed.count(), 2);  // both members re-render
    QVERIFY(!doc.isDisplayed(*doc.entity(ids[0])));
    QVERIFY(!doc.isDisplayed(*doc.entity(ids[1])));  // via the outer group
    QVERIFY(doc.isDisplayed(*doc.entity(ids[2])));
    QVERIFY(doc.groupHidden(inner));

    doc.isolate({ids[1]});
    QVERIFY(doc.isDisplayed(*doc.entity(ids[1])));  // groups on the path are opened
    QVERIFY(!doc.isDisplayed(*doc.entity(ids[0])));
    QVERIFY(!doc.isDisplayed(*doc.entity(ids[2])));
    doc.showAll();
    for (EntityId id : ids) QVERIFY(doc.isDisplayed(*doc.entity(id)));
  }

  void attributesWithUndo() {
    Document doc;
    const auto ids = three(doc);
    doc.setAttribute(ids[0], QStringLiteral("rtt.role"), QStringLiteral("mirror"));
    doc.setAttribute(ids[0], QStringLiteral("sim.inactive"), true);
    QCOMPARE(doc.entity(ids[0])->attributes.size(), 2);
    doc.setAttribute(ids[0], QStringLiteral("sim.inactive"), QVariant());  // remove
    QVERIFY(!doc.entity(ids[0])->attributes.contains(QStringLiteral("sim.inactive")));
    doc.undoStack()->undo();
    QVERIFY(doc.entity(ids[0])->attributes.value(QStringLiteral("sim.inactive")).toBool());
  }

  void fileFormatVersion2AndVersion1() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.nfcad"));
    Document doc;
    const auto ids = three(doc);
    const GroupId g = doc.groupEntities({ids[0], ids[2]}, QStringLiteral("optics"));
    doc.setGroupVisible(g, false);
    doc.setAttribute(ids[2], QStringLiteral("rtt.surface"), QStringLiteral("/root/children/0"));
    doc.setAttribute(ids[2], QStringLiteral("rtt.thickness"), 2.5);
    QVERIFY(nfcad::document::save_nfcad(doc, path).isEmpty());

    Document back;
    QCOMPARE(nfcad::document::load_nfcad(back, path), QString());
    QCOMPARE(back.groups().size(), std::size_t{1});
    QCOMPARE(back.groups()[0].name, QStringLiteral("optics"));
    QVERIFY(!back.groups()[0].visible);
    QCOMPARE(back.entity(ids[2])->group, g);
    QCOMPARE(back.entity(ids[2])->attributes.value(QStringLiteral("rtt.surface")).toString(),
             QStringLiteral("/root/children/0"));
    QCOMPARE(back.entity(ids[2])->attributes.value(QStringLiteral("rtt.thickness")).toDouble(), 2.5);

    // a version 1 file (no groups, no attributes) still loads
    const QString v1 = dir.filePath(QStringLiteral("v1.nfcad"));
    QFile f(v1);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"format":"nfcad","version":1,"units":"mm","layers":[],"entities":[{"id":"7","name":"p","layer":0,"visible":true,"geometry":{"type":"point","point":[1,2,3]}}]})");
    f.close();
    Document old;
    QCOMPARE(nfcad::document::load_nfcad(old, v1), QString());
    QCOMPARE(old.entity(7)->group, GroupId{0});
    QVERIFY(old.entity(7)->attributes.isEmpty());
  }

  void panelVisibilityAndSelection() {
    Document doc;
    nfcad::document::Selection selection;
    selection.attach(&doc);
    const auto ids = three(doc);
    doc.groupEntities({ids[0], ids[1]}, QStringLiteral("parts"));
    nfcad::ui::ModelTreePanel panel(&doc, &selection);
    QApplication::processEvents();  // the rebuild is deferred
    auto* tree = panel.findChild<QTreeWidget*>();
    QVERIFY(tree != nullptr);
    QCOMPARE(tree->topLevelItemCount(), 2);  // group "parts" and entity c
    QTreeWidgetItem* group = tree->topLevelItem(0);
    QCOMPARE(group->text(0), QStringLiteral("parts"));
    QCOMPARE(group->childCount(), 2);

    group->setCheckState(0, Qt::Unchecked);  // hide the group
    QVERIFY(!doc.isDisplayed(*doc.entity(ids[0])));

    group->setSelected(true);  // selecting a group selects its objects
    QCOMPARE(selection.entities().size(), std::size_t{2});
    selection.apply({{ids[2], {}}}, nfcad::document::Selection::Mode::Replace);
    QApplication::processEvents();
    QCOMPARE(tree->selectedItems().size(), 1);
    QCOMPARE(tree->selectedItems().front()->text(0), QStringLiteral("c"));
  }
};

QTEST_MAIN(TestModelTree)
#include "test_model_tree.moc"
