#include <QApplication>
#include <QFile>

#include "app/main_window.hpp"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setOrganizationName(QStringLiteral("PhotonPower"));
  QApplication::setOrganizationDomain(QStringLiteral("photonpower.github.io"));
  QApplication::setApplicationName(QStringLiteral("Nurbforge CAD"));
  QApplication::setApplicationVersion(QStringLiteral(NFCAD_VERSION));

  if (QFile style(QStringLiteral(":/styles/dark.qss")); style.open(QIODevice::ReadOnly)) {
    app.setStyleSheet(QString::fromUtf8(style.readAll()));
  }

  nfcad::app::MainWindow window;
  window.show();
  return QApplication::exec();
}
