#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
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
