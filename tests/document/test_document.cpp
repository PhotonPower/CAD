#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <nurbforge/io/step.hpp>
#include <nurbforge/nurbforge.hpp>

#include "document/document.hpp"
#include "document/nfcad_io.hpp"
#include "kernel/step_import.hpp"

using nfcad::document::Document;
using nfcad::document::Entity;
using nfcad::document::EntityId;
using nfcad::document::Geometry;
using nfcad::document::make_entity;

namespace {

std::vector<Entity> sample_entities() {
  std::vector<Entity> v;
  v.push_back(make_entity(nf::Vec3{1, 2, 3}, QStringLiteral("p")));
  v.push_back(make_entity(nf::make_circle({0, 0, 0}, {0, 0, 1}, 2.0), QStringLiteral("circle")));
  v.push_back(make_entity(nf::make_rectangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0}), QStringLiteral("rect")));
  const nf::NurbsSurface s = nf::make_rectangle({0, 0, 1}, {4, 0, 0}, {0, 4, 0});
  v.push_back(make_entity(nf::TrimmedSurface(s, nf::rectangle_loop(0, 1, 0, 1),
                                             {nf::circle_loop(0.5, 0.5, 0.2)}),
                          QStringLiteral("face")));
  v.push_back(make_entity(nf::make_box({0, 0, 0}, {1, 2, 3}), QStringLiteral("box")));
  return v;
}

nf::Vec3 probe(const Geometry& g) {
  return std::visit(
      [](const auto& v) -> nf::Vec3 {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, nf::Vec3>) {
          return v;
        } else if constexpr (std::is_same_v<T, nf::NurbsCurve>) {
          return v.evaluate(0.3 * v.domain_end());
        } else if constexpr (std::is_same_v<T, nf::NurbsSurface>) {
          return v.evaluate(0.3, 0.6);
        } else if constexpr (std::is_same_v<T, nf::TrimmedSurface>) {
          return v.surface().evaluate(0.3, 0.6) + nf::Vec3(v.area(), 0, 0);
        } else {
          return nf::Vec3(v.volume(), static_cast<double>(v.faces().size()),
                          static_cast<double>(v.edges().size()));
        }
      },
      g);
}

}  // namespace

class TestDocument : public QObject {
  Q_OBJECT
 private slots:
  void addUndoRedo() {
    Document doc;
    QSignalSpy added(&doc, &Document::entityAdded);
    QSignalSpy removed(&doc, &Document::entityRemoved);
    const auto ids = doc.addEntities(sample_entities(), QStringLiteral("add"));
    QCOMPARE(ids.size(), std::size_t{5});
    QCOMPARE(doc.entities().size(), std::size_t{5});
    QCOMPARE(added.count(), 5);
    QVERIFY(doc.isModified());

    doc.undoStack()->undo();
    QCOMPARE(doc.entities().size(), std::size_t{0});
    QCOMPARE(removed.count(), 5);
    doc.undoStack()->redo();
    QCOMPARE(doc.entities().size(), std::size_t{5});
    QVERIFY(doc.entity(ids[1]) != nullptr);  // same ids after redo
    QCOMPARE(doc.entity(ids[1])->name, QStringLiteral("circle"));

    doc.removeEntities({ids[0], ids[2]}, QStringLiteral("delete"));
    QCOMPARE(doc.entities().size(), std::size_t{3});
    doc.undoStack()->undo();
    QCOMPARE(doc.entities().size(), std::size_t{5});
  }

  void layerVisibility() {
    Document doc;
    const auto layer = doc.addLayer({0, QStringLiteral("L1"), QColor(Qt::red), true});
    Entity e = make_entity(nf::Vec3{0, 0, 0});
    e.layer = layer;
    const EntityId id = doc.insert(e);
    QCOMPARE(doc.displayColor(*doc.entity(id)), QColor(Qt::red));
    QSignalSpy changed(&doc, &Document::entityChanged);
    doc.setLayerVisible(layer, false);
    QCOMPARE(changed.count(), 1);
    QVERIFY(!doc.isDisplayed(*doc.entity(id)));
  }

  void saveLoadRoundTrip() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.nfcad"));
    Document doc;
    doc.addEntities(sample_entities(), QStringLiteral("add"));
    QVERIFY(nfcad::document::save_nfcad(doc, path).isEmpty());

    Document loaded;
    QSignalSpy reset(&loaded, &Document::documentReset);
    QCOMPARE(nfcad::document::load_nfcad(loaded, path), QString());
    QCOMPARE(reset.count(), 1);
    QCOMPARE(loaded.filePath(), path);
    QVERIFY(!loaded.isModified());
    QCOMPARE(loaded.entities().size(), doc.entities().size());
    for (const auto& [id, e] : doc.entities()) {
      const Entity* l = loaded.entity(id);
      QVERIFY(l != nullptr);
      QCOMPARE(l->name, e.name);
      QCOMPARE(l->geometry->index(), e.geometry->index());
      const nf::Vec3 a = probe(*e.geometry);
      const nf::Vec3 b = probe(*l->geometry);
      QVERIFY2(nf::distance(a, b) < 1e-9, qPrintable(e.name));
    }
  }

  void loadRejectsGarbage() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.nfcad"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"({"format":"nfcad","version":1,"entities":[{"geometry":{"type":"curve","degree":2,"knots":[0,1],"points":[0,0,0]}}]})");
    f.close();
    Document doc;
    QVERIFY(!nfcad::document::load_nfcad(doc, path).isEmpty());
  }

  void stepImport() {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("box.step"));
    nf::StepOptions options;
    options.shell_names = {"my box"};
    nf::write_step(path.toStdString(), {nf::make_box({0, 0, 0}, {1, 2, 3})}, options);
    const auto result = nfcad::kernel::import_step(path);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.shells.size(), std::size_t{1});
    QCOMPARE(result.faceCount(), std::size_t{6});
    QCOMPARE(result.shells.front().name, QStringLiteral("my box"));
    QVERIFY(std::abs(result.shells.front().shell.volume() - 6.0) < 1e-6);

    const auto missing = nfcad::kernel::import_step(dir.filePath(QStringLiteral("none.step")));
    QVERIFY(!missing.error.isEmpty());
  }
};

QTEST_GUILESS_MAIN(TestDocument)
#include "test_document.moc"
