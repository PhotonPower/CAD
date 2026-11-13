#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

#include "document/document.hpp"
#include "document/nfcad_io.hpp"
#include "ui/plugin_form.hpp"

using nfcad::ui::PluginForm;

namespace {
QJsonArray schema() {
  return {
      QJsonObject{{"name", "n"}, {"type", "integer"}, {"default", 3}, {"min", 1}, {"max", 10}},
      QJsonObject{{"name", "len"}, {"type", "number"}, {"default", 2.5}, {"unit", "mm"}, {"group", "Geometry"}},
      QJsonObject{{"name", "on"}, {"type", "boolean"}, {"default", true}},
      QJsonObject{{"name", "map"}, {"type", "choice"}, {"default", "b"}, {"options", QJsonArray{"a", "b", "c"}}},
      QJsonObject{{"name", "wl"}, {"type", "numbers"}, {"default", QJsonArray{1.0, 2.0}}},
      QJsonObject{{"name", "p"}, {"type", "point"}, {"default", QJsonArray{0.0, 0.0, 0.0}}, {"group", "Geometry"}},
      QJsonObject{{"name", "e"}, {"type", "entity"}, {"default", QJsonArray{}}, {"kinds", QJsonArray{"curve"}}},
  };
}
}  // namespace

class TestPluginForm : public QObject {
  Q_OBJECT
 private slots:
  void defaultsStoredValuesAndGroups() {
    PluginForm f(schema(), QJsonObject{{"n", 5}}, {});
    QCOMPARE(f.values()["n"].toInt(), 5);         // stored value wins
    QCOMPARE(f.values()["len"].toDouble(), 2.5);  // default
    QCOMPARE(f.values()["map"].toString(), QStringLiteral("b"));
    QCOMPARE(f.findChildren<QGroupBox*>().size(), 1);  // "Geometry"
    auto* spin = f.findChild<QSpinBox*>();
    QCOMPARE(spin->maximum(), 10);
    QCOMPARE(f.findChild<QDoubleSpinBox*>()->suffix(), QStringLiteral(" mm"));
  }

  void editingEmitsValues() {
    PluginForm f(schema(), {}, {});
    QSignalSpy changed(&f, &PluginForm::valuesChanged);
    f.findChild<QSpinBox*>()->setValue(8);
    f.findChild<QCheckBox*>()->setChecked(false);
    f.findChild<QComboBox*>()->setCurrentText(QStringLiteral("c"));
    QCOMPARE(changed.count(), 3);
    QCOMPARE(f.values()["n"].toInt(), 8);
    QCOMPARE(f.values()["on"].toBool(), false);
    QCOMPARE(f.values()["map"].toString(), QStringLiteral("c"));
  }

  void numberListValidation() {
    PluginForm f(schema(), {}, {});
    auto* edit = f.findChild<QLineEdit*>();
    QCOMPARE(edit->text(), QStringLiteral("1, 2"));
    edit->setText(QStringLiteral("486.1, 587.6, 656.3"));
    QCOMPARE(f.values()["wl"].toArray().size(), 3);
    QVERIFY(f.invalid().isEmpty());
    edit->setText(QStringLiteral("1, x"));
    QCOMPARE(f.invalid(), QStringList{QStringLiteral("wl")});
    QCOMPARE(f.values()["wl"].toArray().size(), 3);  // the last valid value stays
  }

  // ---------------------------------------------------------------- structured (ADR 0010, W4a)

  void listOfEntries() {
    // a layer stack: entries with a kind, a thickness and – nested – a table of lamellae
    const QJsonArray lamellae{QJsonObject{{"name", "x"}, {"type", "number"}, {"default", 0.0}, {"label", "x"}},
                              QJsonObject{{"name", "material"}, {"type", "text"}, {"default", "air"}, {"label", "material"}}};
    const QJsonArray layer{
        QJsonObject{{"name", "kind"}, {"type", "choice"}, {"default", "homogeneous"}, {"options", QJsonArray{"homogeneous", "lamellar"}}},
        QJsonObject{{"name", "thickness"}, {"type", "number"}, {"default", 0.1}, {"unit", "µm"}},
        QJsonObject{{"name", "lamellae"}, {"type", "table"}, {"default", QJsonArray{}}, {"columns", lamellae}}};
    const QJsonArray s{QJsonObject{{"name", "layers"}, {"type", "list"}, {"default", QJsonArray{}}, {"of", layer}, {"item_label", "kind"}}};
    PluginForm::Callbacks cb;
    int edits = 0;
    cb.edit_entry = [&edits](const QJsonArray& schema, QJsonObject& entry) {
      ++edits;
      if (schema.size() != 3) return false;  // the entry's own schema, nested table included
      entry["kind"] = edits == 1 ? QStringLiteral("lamellar") : QStringLiteral("homogeneous");
      entry["thickness"] = 0.1 * edits;
      return true;
    };
    PluginForm f(s, {}, cb);
    auto* list = f.findChild<QWidget*>(QStringLiteral("list:layers"));
    QVERIFY(list != nullptr);
    auto press = [list](const char* id) { list->findChild<QPushButton*>(QString::fromLatin1(id))->click(); };
    press("add");  // a new entry with the defaults, edited before it is added
    press("add");
    QJsonArray layers = f.values()["layers"].toArray();
    QCOMPARE(layers.size(), 2);
    QCOMPARE(layers[0].toObject()["kind"].toString(), QStringLiteral("lamellar"));
    QCOMPARE(layers[0].toObject()["lamellae"].toArray().size(), 0);  // the nested default
    auto* items = list->findChild<QListWidget*>();
    QCOMPARE(items->item(0)->text(), QStringLiteral("1. lamellar"));
    items->setCurrentRow(1);
    press("up");  // the second entry first
    layers = f.values()["layers"].toArray();
    QCOMPARE(layers[0].toObject()["kind"].toString(), QStringLiteral("homogeneous"));
    QCOMPARE(items->currentRow(), 0);
    cb.edit_entry = nullptr;
    press("remove");
    QCOMPARE(f.values()["layers"].toArray().size(), 1);
    // a cancelled edit changes nothing
    PluginForm::Callbacks no;
    no.edit_entry = [](const QJsonArray&, QJsonObject& entry) {
      entry["kind"] = QStringLiteral("lamellar");
      return false;
    };
    PluginForm g(s, QJsonObject{{"layers", f.values()["layers"]}}, no);
    g.findChild<QWidget*>(QStringLiteral("list:layers"))->findChild<QListWidget*>()->setCurrentRow(0);
    g.findChild<QWidget*>(QStringLiteral("list:layers"))->findChild<QPushButton*>(QStringLiteral("edit"))->click();
    QCOMPARE(g.values()["layers"], f.values()["layers"]);
  }

  void tableInPlace() {
    const QJsonArray columns{
        QJsonObject{{"name", "x"}, {"type", "number"}, {"default", 0.0}, {"label", "x"}, {"unit", "µm"}},
        QJsonObject{{"name", "n"}, {"type", "integer"}, {"default", 1}, {"label", "n"}},
        QJsonObject{{"name", "m"}, {"type", "choice"}, {"default", "air"}, {"options", QJsonArray{"air", "glass"}}}};
    const QJsonArray s{QJsonObject{{"name", "rows"}, {"type", "table"}, {"columns", columns},
                                   {"default", QJsonArray{QJsonObject{{"x", 0.3}, {"n", 2}, {"m", "glass"}}}}}};
    PluginForm f(s, {}, {});
    auto* table = f.findChild<QTableWidget*>();
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("x µm"));
    QCOMPARE(table->item(0, 0)->text(), QStringLiteral("0.3"));
    table->item(0, 0)->setText(QStringLiteral("0.45"));
    QCOMPARE(f.values()["rows"].toArray()[0].toObject()["x"].toDouble(), 0.45);
    table->item(0, 1)->setText(QStringLiteral("2.5"));  // no integer
    QCOMPARE(f.invalid(), QStringList{QStringLiteral("rows")});
    table->item(0, 1)->setText(QStringLiteral("3"));
    table->item(0, 2)->setText(QStringLiteral("gold"));  // not an option
    QCOMPARE(f.invalid(), QStringList{QStringLiteral("rows")});
    table->item(0, 2)->setText(QStringLiteral("air"));
    QVERIFY(f.invalid().isEmpty());
    QCOMPARE(f.values()["rows"].toArray()[0].toObject(), (QJsonObject{{"x", 0.45}, {"n", 3}, {"m", "air"}}));
    f.findChild<QWidget*>(QStringLiteral("table:rows"))->findChild<QPushButton*>(QStringLiteral("add"))->click();
    QCOMPARE(f.values()["rows"].toArray()[1].toObject(), (QJsonObject{{"x", 0.0}, {"n", 1}, {"m", "air"}}));
  }

  void jsonText() {
    const QJsonArray s{QJsonObject{{"name", "raw"}, {"type", "json"}, {"default", QJsonObject{{"a", 1}}}}};
    PluginForm f(s, {}, {});
    auto* edit = f.findChild<QPlainTextEdit*>();
    QVERIFY(edit->toPlainText().contains(QStringLiteral("\"a\"")));
    edit->setPlainText(QStringLiteral("[1, 2, {\"b\": null}]"));
    QCOMPARE(f.values()["raw"].toArray().size(), 3);
    edit->setPlainText(QStringLiteral("{oops"));
    QCOMPARE(f.invalid(), QStringList{QStringLiteral("raw")});
    QCOMPARE(f.values()["raw"].toArray().size(), 3);  // the last valid value stays
    edit->setPlainText(QStringLiteral("2.5"));  // any JSON value
    QVERIFY(f.invalid().isEmpty());
    QCOMPARE(f.values()["raw"].toDouble(), 2.5);
  }

  void entityFromSelectionAndPointPick() {
    PluginForm::Callbacks cb;
    QStringList asked;
    cb.selection = [&](const QStringList& kinds) {
      asked = kinds;
      return std::vector<std::pair<qint64, QString>>{{11, QStringLiteral("mirror")}, {12, QStringLiteral("other")}};
    };
    cb.entity_name = [](qint64 id) { return id == 11 ? QStringLiteral("mirror") : QString(); };
    cb.pick_point = [](std::function<void(double, double, double)> done) { done(1.0, 2.0, 3.0); };
    PluginForm f(schema(), {}, cb);
    for (auto* b : f.findChildren<QPushButton*>()) {
      if (b->text() == QStringLiteral("Use selection") || b->text() == QStringLiteral("Pick")) b->click();
    }
    QCOMPARE(asked, QStringList{QStringLiteral("curve")});
    QCOMPARE(f.values()["e"].toArray(), (QJsonArray{11}));  // single: only the first
    QCOMPARE(f.values()["p"].toArray(), (QJsonArray{1.0, 2.0, 3.0}));
    bool shown = false;
    for (auto* l : f.findChildren<QLabel*>()) shown = shown || l->text() == QStringLiteral("mirror");
    QVERIFY(shown);
  }

  void snapshot() {
    // with NFCAD_SNAPSHOT_DIR set the form is saved for a visual review
    PluginForm f(schema(), {}, {});
    f.resize(300, 400);
    const QImage img = f.grab().toImage();
    QVERIFY(!img.isNull());
    if (const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR"); !dir.isEmpty()) {
      QVERIFY(img.save(QString::fromLocal8Bit(dir) + QStringLiteral("/plugin_form.png")));
    }
  }

  void fileList() {
    // several files (e.g. glass catalogues); a ';'-separated text of older documents is read too
    const QJsonArray s{QJsonObject{{"name", "cat"}, {"type", "files"}, {"default", QJsonArray{}}}};
    PluginForm stored(s, QJsonObject{{"cat", QJsonArray{"/a/schott.agf", "/b/ohara.agf"}}}, {});
    auto* list = stored.findChild<QListWidget*>();
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);
    QCOMPARE(list->item(0)->text(), QStringLiteral("schott.agf"));
    list->item(0)->setSelected(true);
    for (auto* b : stored.findChildren<QPushButton*>()) {
      if (b->text() == QStringLiteral("Remove")) b->click();
    }
    QCOMPARE(stored.values()["cat"].toArray(), (QJsonArray{"/b/ohara.agf"}));
    PluginForm legacy(s, QJsonObject{{"cat", "/a/schott.agf; /b/ohara.agf"}}, {});
    QCOMPARE(legacy.findChild<QListWidget*>()->count(), 2);
  }

  void narrowPanel() {
    // a dock of ~300 px: the file editor and its button stay usable (rows wrap)
    const QJsonArray s{QJsonObject{{"name", "system"}, {"type", "file"}, {"default", ""}, {"label", "System file"},
                                   {"group", "System"}},
                       QJsonObject{{"name", "tolerance"}, {"type", "number"}, {"default", 1e-4}, {"decimals", 7},
                                   {"unit", "mm"}, {"label", "Drawing tolerance"}, {"group", "System"}}};
    PluginForm f(s, {}, {});
    f.resize(300, 200);
    f.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f) || true);
    auto* edit = f.findChild<QLineEdit*>();
    QPushButton* browse = nullptr;
    for (auto* b : f.findChildren<QPushButton*>()) browse = b->text() == QStringLiteral("…") ? b : browse;
    QVERIFY(browse != nullptr);
    const QString sizes = QStringLiteral("form %1, field %2, button %3..%4 in %5")
                              .arg(f.width()).arg(edit->width()).arg(browse->geometry().left())
                              .arg(browse->geometry().right()).arg(browse->parentWidget()->width());
    QVERIFY2(edit->width() >= f.width() / 3, qPrintable(sizes));  // a usable field
    QVERIFY2(browse->geometry().right() < browse->parentWidget()->width(), qPrintable(sizes));  // not cut off
    if (const QByteArray dir = qgetenv("NFCAD_SNAPSHOT_DIR"); !dir.isEmpty()) {
      f.grab().save(QString::fromLocal8Bit(dir) + QStringLiteral("/plugin_form_narrow.png"));
    }
  }

  void parametersAreSavedWithTheDocument() {
    nfcad::document::Document doc;
    QSignalSpy modified(&doc, &nfcad::document::Document::modificationChanged);
    doc.setPluginParameters(QStringLiteral("raytracer"), QJsonObject{{"rays", 1000}});
    QVERIFY(doc.isModified());
    QCOMPARE(modified.count(), 1);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.nfcad"));
    QVERIFY(nfcad::document::save_nfcad(doc, path).isEmpty());
    doc.markSaved();
    QVERIFY(!doc.isModified());
    nfcad::document::Document back;
    QCOMPARE(nfcad::document::load_nfcad(back, path), QString());
    QCOMPARE(back.pluginParameters(QStringLiteral("raytracer"))["rays"].toInt(), 1000);
    QVERIFY(!back.isModified());
  }
};

QTEST_MAIN(TestPluginForm)
#include "test_plugin_form.moc"
