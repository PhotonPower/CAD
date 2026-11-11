#include "ui/plugin_settings_dialog.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "plugins/plugin_manager.hpp"

namespace nfcad::ui {

PluginSettingsDialog::PluginSettingsDialog(plugins::PluginManager* manager, QWidget* parent)
    : QDialog(parent), manager_(manager) {
  setWindowTitle(tr("Plugin settings"));
  auto* layout = new QVBoxLayout(this);
  auto* form = new QFormLayout;
  auto* python_row = new QHBoxLayout;
  python_ = new QLineEdit(manager_->python(), this);
  auto* browse = new QPushButton(tr("…"), this);
  python_row->addWidget(python_);
  python_row->addWidget(browse);
  form->addRow(tr("Python interpreter:"), python_row);
  const QString pkg = plugins::PluginManager::nfcadPythonPath();
  form->addRow(tr("nfcad package:"), new QLabel(pkg.isEmpty() ? tr("not found") : pkg, this));
  layout->addLayout(form);
  layout->addWidget(new QLabel(tr("Plugin folders (every sub-folder with a plugin.json):"), this));
  folders_ = new QListWidget(this);
  folders_->addItems(manager_->folders());
  layout->addWidget(folders_);
  auto* folder_buttons = new QHBoxLayout;
  auto* add = new QPushButton(tr("Add folder…"), this);
  auto* remove = new QPushButton(tr("Remove"), this);
  folder_buttons->addWidget(add);
  folder_buttons->addWidget(remove);
  folder_buttons->addStretch();
  layout->addLayout(folder_buttons);
  if (!manager_->problems().isEmpty()) {
    auto* problems = new QLabel(tr("Problems:\n") + manager_->problems().join(QLatin1Char('\n')), this);
    problems->setWordWrap(true);
    problems->setStyleSheet(QStringLiteral("color: #f0a060;"));
    layout->addWidget(problems);
  }
  auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(box);

  connect(browse, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, tr("Python interpreter"), python_->text());
    if (!f.isEmpty()) python_->setText(f);
  });
  connect(add, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, tr("Plugin folder"));
    if (!d.isEmpty()) folders_->addItem(d);
  });
  connect(remove, &QPushButton::clicked, this, [this] { delete folders_->currentItem(); });
  connect(box, &QDialogButtonBox::accepted, this, [this] {
    apply();
    accept();
  });
  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  resize(560, 380);
}

void PluginSettingsDialog::apply() {
  manager_->setPython(python_->text().trimmed());
  QStringList folders;
  for (int i = 0; i < folders_->count(); ++i) folders << folders_->item(i)->text();
  manager_->setFolders(folders);
}

}  // namespace nfcad::ui
