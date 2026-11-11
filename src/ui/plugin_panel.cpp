#include "ui/plugin_panel.hpp"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "document/document.hpp"
#include "plugins/plugin_manager.hpp"

namespace nfcad::ui {

PluginPanel::PluginPanel(plugins::PluginManager* manager, document::Document* doc,
                         PluginForm::Callbacks callbacks, Run run, QWidget* parent)
    : QWidget(parent), manager_(manager), doc_(doc), callbacks_(std::move(callbacks)), run_(std::move(run)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  chooser_ = new QComboBox(this);
  layout->addWidget(chooser_);
  description_ = new QLabel(this);
  description_->setWordWrap(true);
  description_->setStyleSheet(QStringLiteral("color: #a8aeb8;"));
  layout->addWidget(description_);
  scroll_ = new QScrollArea(this);
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  layout->addWidget(scroll_, 1);
  actions_ = new QWidget(this);
  action_layout_ = new QVBoxLayout(actions_);
  action_layout_->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(actions_);

  connect(chooser_, &QComboBox::currentIndexChanged, this, &PluginPanel::showPlugin);
  connect(manager_, &plugins::PluginManager::pluginsChanged, this, &PluginPanel::rebuild);
  connect(doc_, &document::Document::documentReset, this, &PluginPanel::showPlugin);  // stored values
  rebuild();
}

void PluginPanel::rebuild() {
  const QString current = chooser_->currentData().toString();
  chooser_->blockSignals(true);
  chooser_->clear();
  for (const auto& h : manager_->plugins()) {
    chooser_->addItem(h->manifest().displayName(), h->manifest().id);
    // the parameters are known once the plugin said hello
    connect(h.get(), &plugins::PluginHost::readyChanged, this, &PluginPanel::showPlugin, Qt::UniqueConnection);
  }
  const int index = chooser_->findData(current);
  chooser_->setCurrentIndex(index >= 0 ? index : 0);
  chooser_->blockSignals(false);
  showPlugin();
}

void PluginPanel::showPlugin() {
  plugins::PluginHost* host = manager_->plugin(chooser_->currentData().toString());
  // actions
  while (QLayoutItem* item = action_layout_->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  form_ = nullptr;
  if (host == nullptr) {
    description_->setText(tr("No plugins – see Plugins → Settings…"));
    scroll_->setWidget(new QWidget);
    return;
  }
  description_->setText(host->manifest().description);
  const QString id = host->manifest().id;
  form_ = new PluginForm(host->paramsSchema(), doc_->pluginParameters(id), callbacks_);
  connect(form_, &PluginForm::valuesChanged, this, [this, id](const QJsonObject& values) {
    doc_->setPluginParameters(id, values);
  });
  scroll_->setWidget(form_);
  if (!host->ready()) {
    action_layout_->addWidget(new QLabel(host->running() ? tr("starting …") : tr("not running – see the plugin log"), actions_));
  }
  for (const auto& a : host->actions()) {
    auto* b = new QPushButton(a.label, actions_);
    b->setToolTip(a.description);
    b->setEnabled(!busy_);
    connect(b, &QPushButton::clicked, this, [this, host, action = a.id] {
      if (form_ != nullptr && !form_->invalid().isEmpty()) return;  // fix the red fields first
      run_(host, action);
    });
    action_layout_->addWidget(b);
  }
  if (!host->paramsSchema().isEmpty()) {
    auto* reset = new QPushButton(tr("Reset parameters"), actions_);
    reset->setFlat(true);
    connect(reset, &QPushButton::clicked, this, [this, id] {
      doc_->setPluginParameters(id, {});
      showPlugin();
    });
    action_layout_->addWidget(reset);
  }
}

void PluginPanel::setBusy(bool busy) {
  busy_ = busy;
  for (auto* b : actions_->findChildren<QPushButton*>()) {
    if (b->isFlat()) continue;  // "Reset parameters" stays usable
    b->setEnabled(!busy);
  }
}

}  // namespace nfcad::ui
