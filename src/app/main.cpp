#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QSettings>

#include "app/main_window.hpp"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setOrganizationName(QStringLiteral("PhotonPower"));
  QApplication::setOrganizationDomain(QStringLiteral("photonpower.github.io"));
  QApplication::setApplicationName(QStringLiteral("Raytatouille Fusion"));
  QApplication::setApplicationVersion(QStringLiteral(NFCAD_VERSION));

  // The application was called "Nurbforge CAD" until 2026-11-11: take over its settings once
  // (window layout, Python interpreter, plugin folders, snaps, …).
  if (QSettings settings; !settings.contains(QStringLiteral("migrated/from-nurbforge-cad"))) {
    // macOS stores by organisation domain, Windows / Linux by organisation name: try both
    for (const QString& organisation : {QApplication::organizationDomain(), QApplication::organizationName()}) {
      const QSettings previous(organisation, QStringLiteral("Nurbforge CAD"));
      for (const QString& key : previous.allKeys()) {
        if (!settings.contains(key)) settings.setValue(key, previous.value(key));
      }
    }
    settings.setValue(QStringLiteral("migrated/from-nurbforge-cad"), true);
  }

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Raytatouille Fusion – CAD front end for Nurbforge and its computation kernels"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument(QStringLiteral("file"),
                               QStringLiteral("Document (.nfcad) or STEP file to open"));
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
  if (!parser.positionalArguments().isEmpty()) {
    if (!window.openFile(parser.positionalArguments().constFirst(), parser.isSet(benchmark))) {
      return 1;
    }
  }
  return QApplication::exec();
}
