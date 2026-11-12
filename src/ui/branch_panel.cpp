#include "ui/branch_panel.hpp"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/node_editor.hpp"

namespace nfcad::ui {

BranchPanel::BranchPanel(NodeEditor* editor, QWidget* parent) : QWidget(parent), editor_(editor) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  auto* title = new QLabel(tr("Branches"), this);
  QFont f = title->font();
  f.setBold(true);
  title->setFont(f);
  layout->addWidget(title);
  list_ = new QListWidget(this);
  list_->setToolTip(tr("Exactly these branches are computed. Select one to highlight it."));
  layout->addWidget(list_, 1);
  hint_ = new QLabel(this);
  hint_->setWordWrap(true);
  hint_->hide();
  layout->addWidget(hint_);
  auto* buttons = new QHBoxLayout;
  add_ = new QPushButton(tr("New branch"), this);
  add_->setToolTip(tr("Click the port of a source the light leaves by, then the port it leaves each "
                      "following block by, then the detector"));
  remove_ = new QPushButton(tr("Delete"), this);
  buttons->addWidget(add_);
  buttons->addWidget(remove_);
  layout->addLayout(buttons);

  ask_name = [this](const QString& suggestion) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New branch"), tr("Name of the branch:"), QLineEdit::Normal, suggestion, &ok);
    return ok ? name : QString();
  };
  connect(list_, &QListWidget::currentTextChanged, editor_, [this](const QString& name) { editor_->highlightBranch(name); });
  connect(add_, &QPushButton::clicked, editor_, [this] {
    if (editor_->recordingBranch()) {
      editor_->cancelBranch();
    } else {
      editor_->beginBranch();
      editor_->setFocus();
    }
  });
  connect(remove_, &QPushButton::clicked, editor_, [this] {
    if (list_->currentItem() != nullptr) editor_->removeBranch(list_->currentItem()->text());
  });
  connect(editor_, &NodeEditor::recordingChanged, this, [this](bool on, const QString& hint) {
    hint_->setText(hint);
    hint_->setVisible(on);
    add_->setText(on ? tr("Cancel") : tr("New branch"));
  });
  connect(editor_, &NodeEditor::branchRecorded, this, [this] {
    QString suggestion;
    for (int k = 1; suggestion.isEmpty() || editor_->branchNames().contains(suggestion); ++k) {
      suggestion = tr("branch %1").arg(k);
    }
    const QString name = ask_name ? ask_name(suggestion) : suggestion;
    if (name.isEmpty() || !editor_->finishBranch(name)) editor_->cancelBranch();
  });
  connect(editor_, &NodeEditor::branchesChanged, this, &BranchPanel::refresh);
  connect(editor_, &NodeEditor::edited, this, &BranchPanel::refresh);
  refresh();
}

void BranchPanel::refresh() {
  const QStringList names = editor_->branchNames();
  QStringList shown;
  for (int k = 0; k < list_->count(); ++k) shown << list_->item(k)->text();
  if (shown != names) {
    const QSignalBlocker block(list_);
    list_->clear();
    list_->addItems(names);
  }
  const QString current = editor_->highlightedBranch();
  for (int k = 0; k < list_->count(); ++k) {
    if (list_->item(k)->text() == current && list_->currentRow() != k) {
      const QSignalBlocker block(list_);
      list_->setCurrentRow(k);
    }
  }
  remove_->setEnabled(!names.isEmpty());
  setVisible(editor_->modelMode());
}

}  // namespace nfcad::ui
