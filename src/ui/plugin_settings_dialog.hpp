#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QLabel;

namespace nfcad::plugins {
class PluginManager;
}

namespace nfcad::ui {

/// Plugins → Settings: Python interpreter (decision D5: your environment with the kernel
/// libraries) and plugin folders; shows the nfcad package location and loading problems.
class PluginSettingsDialog : public QDialog {
  Q_OBJECT
 public:
  PluginSettingsDialog(plugins::PluginManager* manager, QWidget* parent = nullptr);

 private:
  void apply();
  void checkEnvironment();
  void createEnvironment();

  plugins::PluginManager* manager_;
  QLineEdit* python_ = nullptr;
  QListWidget* folders_ = nullptr;
  QCheckBox* developer_ = nullptr;
  QPlainTextEdit* report_ = nullptr;
};

}  // namespace nfcad::ui
