#include "ui/tool_box.hpp"

#include <QFile>
#include <QGridLayout>
#include <QIcon>
#include <QResizeEvent>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace nfcad::ui {

namespace {
constexpr int kButtonSize = 36;  // 32 px icon area + frame
constexpr int kSpacing = 2;

QString settingsKey(const QString& group_id) {
  return QStringLiteral("toolbox/expanded/") + group_id;
}
}  // namespace

ToolGroup::ToolGroup(const ToolGroupDef& def, QWidget* parent) : QWidget(parent), id_(def.id) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  header_ = new QToolButton(this);
  header_->setObjectName(QStringLiteral("toolGroupHeader"));
  header_->setText(def.title);
  header_->setCheckable(true);
  header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  header_->setAutoRaise(true);
  layout->addWidget(header_);

  body_ = new QWidget(this);
  auto* grid = new QGridLayout(body_);
  grid->setContentsMargins(kSpacing, kSpacing, kSpacing, kSpacing);
  grid->setSpacing(kSpacing);
  grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->addWidget(body_);

  for (const ToolDef& tool : def.tools) {
    auto* button = new QToolButton(body_);
    button->setObjectName(QStringLiteral("toolButton"));
    button->setFixedSize(kButtonSize, kButtonSize);
    button->setAutoRaise(true);
    const QString icon_path = QStringLiteral(":/icons/%1/%2.svg").arg(def.id, tool.id);
    if (QFile::exists(icon_path)) {
      button->setIcon(QIcon(icon_path));
      button->setIconSize({24, 24});
    } else {
      button->setText(tool.short_label);  // placeholder until the icon set exists
    }
    QString tip = tool.label;
    if (!tool.shortcut.isEmpty()) {
      tip += QStringLiteral("  (%1)").arg(tool.shortcut.toString(QKeySequence::NativeText));
    }
    button->setToolTip(tip);
    button->setStatusTip(tool.label);
    connect(button, &QToolButton::clicked, this, [this, id = tool.id] { emit toolTriggered(id); });
    buttons_.push_back(button);
  }

  connect(header_, &QToolButton::toggled, this, [this](bool on) {
    header_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    body_->setVisible(on);
    emit expandedChanged(on);
  });
  header_->setChecked(def.expanded_by_default);
  header_->setArrowType(def.expanded_by_default ? Qt::DownArrow : Qt::RightArrow);
  body_->setVisible(def.expanded_by_default);
  relayout();
}

bool ToolGroup::isExpanded() const { return header_->isChecked(); }

void ToolGroup::setExpanded(bool expanded) { header_->setChecked(expanded); }

void ToolGroup::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  relayout();
}

void ToolGroup::relayout() {
  const int usable = std::max(width() - 2 * kSpacing, kButtonSize);
  const int columns = std::max(1, (usable + kSpacing) / (kButtonSize + kSpacing));
  if (columns == columns_) return;
  columns_ = columns;
  auto* grid = static_cast<QGridLayout*>(body_->layout());
  for (QToolButton* b : buttons_) grid->removeWidget(b);
  for (std::size_t i = 0; i < buttons_.size(); ++i) {
    const int idx = static_cast<int>(i);
    grid->addWidget(buttons_[i], idx / columns, idx % columns);
  }
}

ToolBox::ToolBox(QWidget* parent) : QScrollArea(parent) {
  setWidgetResizable(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setFrameShape(QFrame::NoFrame);

  auto* content = new QWidget(this);
  auto* layout = new QVBoxLayout(content);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(1);

  QSettings settings;
  for (const ToolGroupDef& def : tool_catalog()) {
    auto* group = new ToolGroup(def, content);
    const QString key = settingsKey(def.id);
    if (settings.contains(key)) group->setExpanded(settings.value(key).toBool());
    connect(group, &ToolGroup::expandedChanged, this,
            [key](bool on) { QSettings().setValue(key, on); });
    connect(group, &ToolGroup::toolTriggered, this, &ToolBox::toolTriggered);
    layout->addWidget(group);
  }
  layout->addStretch(1);
  setWidget(content);
}

}  // namespace nfcad::ui
