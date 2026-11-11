#include "ui/snap_bar.hpp"

#include <QHBoxLayout>
#include <QSettings>
#include <QToolButton>

namespace nfcad::ui {

SnapBar::SnapBar(QWidget* parent) : QWidget(parent) {
  QSettings settings;
  mask_ = snap::SnapMask(static_cast<std::uint16_t>(
      settings.value(QStringLiteral("snap/mask"), snap::SnapMask::defaults().bits()).toUInt()));

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(1);

  master_ = new QToolButton(this);
  master_->setText(tr("Snap"));
  master_->setCheckable(true);
  master_->setChecked(settings.value(QStringLiteral("snap/enabled"), true).toBool());
  master_->setShortcut(QKeySequence(Qt::Key_F3));
  master_->setToolTip(tr("Object snaps on/off (F3)"));
  layout->addWidget(master_);

  auto notify = [this] {
    QSettings s;
    s.setValue(QStringLiteral("snap/mask"), mask_.bits());
    s.setValue(QStringLiteral("snap/enabled"), master_->isChecked());
    emit maskChanged(mask_, master_->isChecked());
  };
  connect(master_, &QToolButton::toggled, this, notify);

  for (snap::SnapType type : snap::kSnapPriority) {
    auto* button = new QToolButton(this);
    button->setText(QLatin1StringView(snap::short_name(type)));
    button->setCheckable(true);
    button->setChecked(mask_.test(type));
    button->setAutoRaise(true);
    connect(button, &QToolButton::toggled, this, [this, type, notify](bool on) {
      mask_.set(type, on);
      notify();
    });
    layout->addWidget(button);
  }
}

bool SnapBar::snappingEnabled() const { return master_->isChecked(); }

}  // namespace nfcad::ui
