// Screenshots for the user guide (docs/user-guide.md), rendered without showing a window.
// Only with NFCAD_DOC_SCREENSHOTS=<folder>; otherwise skipped (also in CI).
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QImage>
#include <QTest>

#include "app/main_window.hpp"
#include "document/document.hpp"
#include "plugins/plugin_manager.hpp"
#include "ui/image_view.hpp"
#include "render/scene.hpp"
#include "viewport/viewport.hpp"

class TestDocScreenshots : public QObject {
  Q_OBJECT
 private slots:
  void mainWindow() {
    const QString dir = qEnvironmentVariable("NFCAD_DOC_SCREENSHOTS");
    const QString model = qEnvironmentVariable("NFCAD_DOC_MODEL");
    if (dir.isEmpty() || model.isEmpty()) QSKIP("set NFCAD_DOC_SCREENSHOTS and NFCAD_DOC_MODEL");
    nfcad::app::MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen);  // laid out like a shown window, never on screen
    w.resize(1600, 1000);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w) || true);
    QVERIFY(w.openFile(model, true));
    auto* scene = w.findChild<nfcad::render::Scene*>();
    QVERIFY(scene != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(scene->triangleCount() > 0, 30000);
    QTest::qWait(1500);  // the rest of the tessellation
    for (const char* mode : {"shaded", "surfaces", "wireframe"}) {
      w.setDisplayModeAll(QString::fromLatin1(mode));
      QTest::qWait(300);
      QImage img = w.grab().toImage();
      img.setDevicePixelRatio(1.0);
      QVERIFY(!img.isNull());
      QVERIFY(img.save(QDir(dir).filePath(QStringLiteral("main-window-%1.png").arg(QString::fromLatin1(mode)))));
    }
  }

  void raytracer() {
    // the Raytracer plugin with the example system: rays, irradiance, plugin panel
    const QString dir = qEnvironmentVariable("NFCAD_DOC_SCREENSHOTS");
    if (dir.isEmpty()) QSKIP("set NFCAD_DOC_SCREENSHOTS");
    qputenv("NFCAD_DEVELOPER_MODE", "1");
    qputenv("NFCAD_PLUGIN_PATH", QByteArray(NFCAD_SOURCE_DIR "/python/plugins"));
    nfcad::app::MainWindow w;
    w.setAttribute(Qt::WA_DontShowOnScreen);
    w.resize(1600, 1000);
    w.show();
    auto* manager = w.findChild<nfcad::plugins::PluginManager*>();
    QVERIFY(manager != nullptr);
    nfcad::plugins::PluginHost* host = manager->plugin(QStringLiteral("raytracer"));
    if (host == nullptr) QSKIP("raytracer plugin not found");
    // before the plugin is ready: the panel shows these values once it is
    auto* doc = w.findChild<nfcad::document::Document*>();
    // NFCAD_DOC_RTT_SYSTEM / NFCAD_DOC_RTT_CATALOG: another system (default: the plugin's example)
    const QString system = qEnvironmentVariable("NFCAD_DOC_RTT_SYSTEM",
                                                QStringLiteral(NFCAD_SOURCE_DIR "/python/plugins/raytracer/examples/singlet_optim.rtt.json"));
    QJsonArray catalogs;
    if (const QString c = qEnvironmentVariable("NFCAD_DOC_RTT_CATALOG"); !c.isEmpty()) catalogs.append(c);
    QJsonObject params{{"system", system}, {"catalogs", catalogs}, {"rays", 217}};
    if (const QString c = qEnvironmentVariable("NFCAD_DOC_RTT_COLOUR"); !c.isEmpty()) params.insert("colour_by", c);
    doc->setPluginParameters(QStringLiteral("raytracer"), params);
    QTRY_VERIFY_WITH_TIMEOUT(host->ready() || !host->running(), 30000);
    if (!host->ready()) QSKIP("raytracer plugin does not start (raytatouille missing?)");
    params.insert(QStringLiteral("camera_rays"), qEnvironmentVariableIntValue("NFCAD_DOC_RTT_RAYS") > 0
                                                     ? qEnvironmentVariableIntValue("NFCAD_DOC_RTT_RAYS")
                                                     : 50000);
    doc->setPluginParameters(QStringLiteral("raytracer"), params);
    for (const char* action : {"load", "trace", "camera_image"}) {
      QSignalSpy finished(host, &nfcad::plugins::PluginHost::finished);
      QVERIFY(manager->run(host, QString::fromLatin1(action), params) != 0);
      QVERIFY(finished.wait(60000));
      QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(3).toString()));
    }
    QTest::qWait(1500);
    if (auto* dock = w.findChild<QDockWidget*>(QStringLiteral("pluginDock"))) dock->raise();
    w.setDisplayModeAll(QStringLiteral("shaded"));
    QTest::qWait(800);
    QImage img = w.grab().toImage();
    img.setDevicePixelRatio(1.0);
    QVERIFY(img.save(QDir(dir).filePath(QStringLiteral("raytracer.png"))));
    // the camera image in its own window
    auto* image_dock = w.findChild<QDockWidget*>(QStringLiteral("imageDock"));
    QVERIFY(image_dock != nullptr);
    QVERIFY(image_dock->isVisible());
    auto* view = w.findChild<nfcad::ui::ImageView*>();
    QVERIFY(view != nullptr && view->current() != nullptr);
    view->resize(640, 520);
    QTest::qWait(200);
    QImage cam = view->grab().toImage();
    cam.setDevicePixelRatio(1.0);
    QVERIFY(cam.save(QDir(dir).filePath(QStringLiteral("camera-image.png"))));
    view->setLogScale(true);
    QTest::qWait(100);
    cam = view->grab().toImage();
    cam.setDevicePixelRatio(1.0);
    QVERIFY(cam.save(QDir(dir).filePath(QStringLiteral("camera-image-log.png"))));
  }
};

QTEST_MAIN(TestDocScreenshots)
#include "test_doc_screenshots.moc"
