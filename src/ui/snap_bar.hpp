#pragma once

#include <QWidget>

#include "snap/snap_types.hpp"

class QToolButton;

namespace nfcad::ui {

/// Snap toggles in the status bar: a master switch (F3) and one checkable button per snap type.
/// The mask is stored in QSettings.
class SnapBar : public QWidget {
  Q_OBJECT
 public:
  explicit SnapBar(QWidget* parent = nullptr);

  [[nodiscard]] snap::SnapMask mask() const { return mask_; }
  [[nodiscard]] bool snappingEnabled() const;

 signals:
  void maskChanged(nfcad::snap::SnapMask mask, bool enabled);

 private:
  QToolButton* master_ = nullptr;
  snap::SnapMask mask_;
};

}  // namespace nfcad::ui
