#pragma once

#include <QWidget>
#include <functional>

#include "ui/plugin_form.hpp"

class QComboBox;
class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace nfcad::document {
class Document;
}
namespace nfcad::plugins {
class PluginHost;
class PluginManager;
}  // namespace nfcad::plugins

namespace nfcad::ui {

/// Right dock "Plugin" (docs/plugins.md, D4): choose a plugin, edit its parameters (form from
/// its schema, values saved with the document), start its actions.
class PluginPanel : public QWidget {
  Q_OBJECT
 public:
  using Run = std::function<void(plugins::PluginHost* host, const QString& action)>;
  PluginPanel(plugins::PluginManager* manager, document::Document* doc, PluginForm::Callbacks callbacks,
              Run run, QWidget* parent = nullptr);

  /// Disables the action buttons while a plugin job runs.
  void setBusy(bool busy);
  void rebuild();
  [[nodiscard]] PluginForm* form() const { return form_; }

 private:
  void showPlugin();

  plugins::PluginManager* manager_;
  document::Document* doc_;
  PluginForm::Callbacks callbacks_;
  Run run_;
  QComboBox* chooser_ = nullptr;
  QLabel* description_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  PluginForm* form_ = nullptr;
  QWidget* actions_ = nullptr;
  QVBoxLayout* action_layout_ = nullptr;
  bool busy_ = false;
};

}  // namespace nfcad::ui
