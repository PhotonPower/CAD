#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QSettings>

#include "app/main_window.hpp"
#include "support/crash_handler.hpp"
#include "support/settings_migration.hpp"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setOrganizationName(QStringLiteral("PhotonPower"));
  QApplication::setOrganizationDomain(QStringLiteral("photonpower.github.io"));
  QApplication::setApplicationName(QStringLiteral("Raytatouille Fusion"));
  QApplication::setApplicationVersion(QStringLiteral(NFCAD_VERSION));

  // local crash reports (nothing is sent), settings of older versions
  nfcad::support::CrashHandler::install(nfcad::support::CrashHandler::defaultFolder(), QStringLiteral(NFCAD_VERSION));
  if (QSettings settings; true) {
    nfcad::support::migrate_settings(settings, {QApplication::organizationDomain(), QApplication::organizationName()});
  }

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Raytatouille Fusion – CAD front end for Nurbforge and its computation kernels"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument(QStringLiteral("file"),
                               QStringLiteral("Document (.nfcad) or STEP file to open, or a plugin package (.nfplugin) to install"));
  const QCommandLineOption benchmark(
      QStringLiteral("benchmark"),
      QStringLiteral("Open the file, orbit the 3D view for <frames> frames, print timings, quit."),
      QStringLiteral("frames"));
  parser.addOption(benchmark);
  const QCommandLineOption display_mode(
      QStringLiteral("display-mode"),
      QStringLiteral("Initial display mode of all views: wireframe, surfaces or shaded."),
      QStringLiteral("mode"));
  parser.addOption(display_mode);
  parser.process(app);

  if (QFile style(QStringLiteral(":/styles/dark.qss")); style.open(QIODevice::ReadOnly)) {
    app.setStyleSheet(QString::fromUtf8(style.readAll()));
  }

  nfcad::app::MainWindow window;
  if (parser.isSet(benchmark)) window.runBenchmarkWhenReady(parser.value(benchmark).toInt());
  if (parser.isSet(display_mode)) window.setDisplayModeAll(parser.value(display_mode));
  window.show();
  window.reportPreviousCrash();
  if (!parser.positionalArguments().isEmpty()) {
    if (!window.openFile(parser.positionalArguments().constFirst(), parser.isSet(benchmark))) {
      return 1;
    }
  }
  return QApplication::exec();
}
