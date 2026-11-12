#include "ui/branch_panel.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

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
  where_ = new QLabel(this);
  where_->setWordWrap(true);
  where_->hide();
  layout->addWidget(where_);
  list_ = new QListWidget(this);
  list_->setToolTip(tr("Exactly these branches are computed. Select one to highlight it."));
  layout->addWidget(list_, 2);
  auto* steps_title = new QLabel(tr("Steps"), this);
  layout->addWidget(steps_title);
  steps_ = new QListWidget(this);
  steps_->setToolTip(tr("The steps of the selected branch. Double-click a step to set its channel: the inner "
                        "branch through a group, a ghost of a Raytatouille section (reflected at a surface), "
                        "an order …"));
  layout->addWidget(steps_, 2);
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
  connect(list_, &QListWidget::currentTextChanged, editor_, [this](const QString& name) {
    editor_->highlightBranch(name);
    refresh();
  });
  ask_channel = [this](const QStringList& choices, const QString& current) {
    QStringList shown = choices;
    if (!shown.isEmpty() && shown.first().isEmpty()) shown[0] = tr("(none)");
    bool ok = false;
    const int at = std::max(0, static_cast<int>(choices.indexOf(current)));
    QString picked = QInputDialog::getItem(this, tr("Channel of the step"), tr("Channel:"), shown, at, true, &ok);
    if (!ok) return QString();
    return picked == tr("(none)") ? QStringLiteral("") : picked;
  };
  connect(steps_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
    const QString branch = editor_->highlightedBranch();
    const int k = steps_->row(item);
    const QStringList choices = editor_->channelChoices(branch, k);
    const QString chosen = ask_channel ? ask_channel(choices, item->data(Qt::UserRole).toString()) : QString();
    if (!chosen.isNull()) editor_->setStepChannel(branch, k, chosen);
  });
  connect(editor_, &NodeEditor::scopeChanged, this, [this](const QString& where) {
    where_->setText(tr("Inside: %1").arg(where));
    where_->setVisible(!where.isEmpty());
    refresh();
  });
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
  // the steps of the selected branch
  const QSignalBlocker block_steps(steps_);
  steps_->clear();
  for (const QJsonValue& v : editor_->branchSteps(current)) {
    const QJsonArray s = v.toArray();
    const QString in = s.at(1).isNull() ? QStringLiteral("·") : s.at(1).toString();
    const QString out = s.at(2).isNull() ? QStringLiteral("·") : s.at(2).toString();
    const QString channel = s.size() > 3 ? s.at(3).toString() : QString();
    auto* item = new QListWidgetItem(QStringLiteral("%1:  %2 → %3%4").arg(s.at(0).toString(), in, out,
                                                                       channel.isEmpty() ? QString() : QStringLiteral("  [%1]").arg(channel)),
                                     steps_);
    item->setData(Qt::UserRole, channel);
  }
  setVisible(editor_->modelMode());
}

}  // namespace nfcad::ui
