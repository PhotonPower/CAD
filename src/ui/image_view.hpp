#pragma once

#include <QImage>
#include <QWidget>
#include <optional>

#include "results/result_types.hpp"

class QCheckBox;
class QComboBox;
class QLabel;

namespace nfcad::results {
class ResultStore;
}

namespace nfcad::ui {

class ImageCanvas;

/// Image dock: the visible image results (camera images and other 2D grids) with their
/// physical axes, a colour bar, linear or logarithmic scale and a read-out of the value under
/// the cursor. Several images: chosen in the list (the newest is shown first).
class ImageView : public QWidget {
  Q_OBJECT
 public:
  explicit ImageView(const results::ResultStore* store, QWidget* parent = nullptr);
  [[nodiscard]] QSize sizeHint() const override { return {460, 400}; }
  /// The image shown (null if none).
  [[nodiscard]] const results::ResultItem* current() const;
  void setLogScale(bool on);
  /// Shows only the items with this key (a display window of the node workflow); empty: all
  /// visible images.
  void setKey(const QString& key);

 private:
  void rebuild();

  const results::ResultStore* store_;
  QComboBox* chooser_ = nullptr;
  QCheckBox* log_ = nullptr;
  QLabel* readout_ = nullptr;
  ImageCanvas* canvas_ = nullptr;
  results::ResultId shown_ = 0;
  results::ResultId newest_ = 0;
  QString key_;
};

/// The drawing area of the image dock.
class ImageCanvas : public QWidget {
  Q_OBJECT
 public:
  explicit ImageCanvas(QWidget* parent = nullptr);
  void setImage(std::shared_ptr<const results::Image> image, const QString& title, bool log);

 signals:
  void hovered(const QString& text);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  [[nodiscard]] QRectF imageRect() const;  ///< where the image is drawn (aspect from the view)
  [[nodiscard]] QPointF toData(const QPointF& px) const;  ///< widget position -> x, y of the image
  void render();
  void resetView();

  std::shared_ptr<const results::Image> image_;
  QString title_;
  bool log_ = false;
  QImage rendered_;
  double lo_ = 0.0, hi_ = 1.0;  ///< colour range (log10 values in log scale)
  QRectF view_;                 ///< shown part of the image in data coordinates (zoom / pan)
  std::optional<QPointF> drag_;  ///< last mouse position while panning
};

}  // namespace nfcad::ui
