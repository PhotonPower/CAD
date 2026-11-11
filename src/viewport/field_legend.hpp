#pragma once

#include <QWidget>

#include "results/colormap.hpp"

namespace nfcad::viewport {

/// Colour bar with name, unit and range of the shown result, bottom right in a viewport.
class FieldLegend : public QWidget {
  Q_OBJECT
 public:
  explicit FieldLegend(QWidget* parent);
  void setLegend(const QString& title, results::Colormap map, double min, double max,
                 const QString& unit);
  void clear();
  void place();  ///< after a resize of the viewport

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QString title_;
  QString unit_;
  results::Colormap map_ = results::Colormap::Viridis;
  double min_ = 0.0;
  double max_ = 1.0;
};

}  // namespace nfcad::viewport
