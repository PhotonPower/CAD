#include "ui/results_panel.hpp"

#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

#include "document/document.hpp"
#include "results/result_store.hpp"

namespace nfcad::ui {

namespace {
constexpr int kSetRole = Qt::UserRole + 1;   // result set id (set rows)
constexpr int kItemRole = Qt::UserRole + 2;  // result item id (item rows)

QString kind_text(results::ResultItem::Kind k) {
  switch (k) {
    case results::ResultItem::Kind::Lines: return QObject::tr("Rays / lines");
    case results::ResultItem::Kind::Field: return QObject::tr("Field on face");
    case results::ResultItem::Kind::Curve: return QObject::tr("Curve");
    case results::ResultItem::Kind::Image: return QObject::tr("Image");
  }
  return {};
}

const std::array<QColor, 6> kCurveColors = {QColor(90, 160, 255), QColor(255, 170, 60),
                                            QColor(110, 220, 120), QColor(240, 90, 90),
                                            QColor(190, 130, 255), QColor(230, 230, 90)};
}  // namespace

ResultsPanel::ResultsPanel(results::ResultStore* store, document::Document* doc, QWidget* parent)
    : QWidget(parent), store_(store), doc_(doc) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("Result"), tr("Type")});
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(tree_);
  connect(tree_, &QTreeWidget::itemChanged, this, &ResultsPanel::onItemChanged);
  connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ResultsPanel::showContextMenu);
  connect(store_, &results::ResultStore::changed, this, &ResultsPanel::rebuild);
  rebuild();
}

void ResultsPanel::rebuild() {
  rebuilding_ = true;
  const int scroll = tree_->verticalScrollBar()->value();  // the list does not jump on every click
  tree_->clear();
  for (const results::ResultSet& s : store_->sets()) {
    auto* set = new QTreeWidgetItem(tree_, {s.name, s.source});
    set->setData(0, kSetRole, QVariant::fromValue<quint64>(s.id));
    set->setFlags(set->flags() | Qt::ItemIsUserCheckable);
    set->setCheckState(0, s.visible ? Qt::Checked : Qt::Unchecked);
    for (const results::ResultItem& i : s.items) {
      auto* row = new QTreeWidgetItem(set, {i.name, kind_text(i.kind)});
      row->setData(0, kItemRole, QVariant::fromValue<quint64>(i.id));
      row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
      row->setCheckState(0, i.visible ? Qt::Checked : Qt::Unchecked);
      if (i.kind == results::ResultItem::Kind::Lines) {
        row->setToolTip(0, tr("%1 polylines").arg(i.lines->polylineCount()));
      }
    }
    set->setExpanded(true);
  }
  tree_->verticalScrollBar()->setValue(scroll);
  rebuilding_ = false;
}

void ResultsPanel::onItemChanged(QTreeWidgetItem* item, int column) {
  if (rebuilding_ || column != 0) return;
  const bool on = item->checkState(0) == Qt::Checked;
  const bool is_set = item->data(0, kSetRole).isValid();
  const auto id = (is_set ? item->data(0, kSetRole) : item->data(0, kItemRole)).toULongLong();
  // Applied after Qt has finished with the click: the store change rebuilds this tree, and
  // rebuilding inside QTreeWidgetItem::setData deleted the item Qt was still using (crash).
  QTimer::singleShot(0, this, [this, is_set, id, on] {
    if (is_set) {
      store_->setSetVisible(id, on);
    } else {
      store_->setItemVisible(id, on);
    }
  });
}

void ResultsPanel::showContextMenu(const QPoint& pos) {
  QTreeWidgetItem* item = tree_->itemAt(pos);
  if (item == nullptr) return;
  QMenu menu(this);
  if (item->data(0, kSetRole).isValid()) {
    const auto id = item->data(0, kSetRole).toULongLong();
    menu.addAction(tr("Delete run"), this, [this, id] { store_->removeSet(id); });
  } else if (const results::ResultItem* r = store_->item(item->data(0, kItemRole).toULongLong());
             r != nullptr && r->kind == results::ResultItem::Kind::Lines) {
    const auto lines = r->lines;
    menu.addAction(tr("Convert to CAD curves…"), this, [this, lines] {
      bool ok = false;
      const int every = QInputDialog::getInt(this, tr("Convert to curves"),
                                             tr("Every n-th ray (of %1):").arg(lines->polylineCount()),
                                             std::max<int>(1, static_cast<int>(lines->polylineCount() / 50)),
                                             1, 1000000, 1, &ok);
      if (!ok) return;
      auto entities = results::ResultStore::promote(*lines, static_cast<std::size_t>(every), 2000);
      if (!entities.empty()) doc_->addEntities(std::move(entities), tr("Rays to curves"));
    });
  }
  if (!menu.isEmpty()) menu.exec(tree_->viewport()->mapToGlobal(pos));
}

// ------------------------------------------------------------------------------ plot

PlotWidget::PlotWidget(const results::ResultStore* store, QWidget* parent)
    : QWidget(parent), store_(store) {
  setMinimumHeight(140);
  connect(store_, &results::ResultStore::changed, this, qOverload<>(&QWidget::update));
  connect(store_, &results::ResultStore::curveUpdated, this, qOverload<>(&QWidget::update));
}

void PlotWidget::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this);
  p.fillRect(rect(), QColor(28, 31, 36));
  p.setRenderHint(QPainter::Antialiasing);
  const auto curves = store_->visibleItems(results::ResultItem::Kind::Curve);
  p.setPen(QColor(150, 155, 165));
  if (curves.empty()) {
    p.drawText(rect(), Qt::AlignCenter, tr("No curves (e.g. the merit function of an optimisation)"));
    return;
  }
  double x0 = std::numeric_limits<double>::max(), x1 = std::numeric_limits<double>::lowest();
  double y0 = x0, y1 = x1;
  for (const results::ResultItem* c : curves) {
    for (std::size_t k = 0; k < std::min(c->curve->x.size(), c->curve->y.size()); ++k) {
      if (!std::isfinite(c->curve->x[k]) || !std::isfinite(c->curve->y[k])) continue;
      x0 = std::min(x0, c->curve->x[k]);
      x1 = std::max(x1, c->curve->x[k]);
      y0 = std::min(y0, c->curve->y[k]);
      y1 = std::max(y1, c->curve->y[k]);
    }
  }
  if (x0 > x1) return;
  if (x1 == x0) x1 = x0 + 1;
  if (y1 == y0) y1 = y0 + 1;
  const QRectF area(52, 10, width() - 64, height() - 40);
  auto map = [&](double x, double y) {
    return QPointF(area.left() + (x - x0) / (x1 - x0) * area.width(),
                   area.bottom() - (y - y0) / (y1 - y0) * area.height());
  };
  // axes and ticks
  p.setPen(QColor(80, 86, 96));
  p.drawRect(area);
  QFont f = font();
  f.setPointSizeF(f.pointSizeF() * 0.85);
  p.setFont(f);
  p.setPen(QColor(170, 176, 186));
  for (int i = 0; i <= 4; ++i) {
    const double ty = y0 + (y1 - y0) * i / 4.0;
    const double tx = x0 + (x1 - x0) * i / 4.0;
    const QPointF py = map(x0, ty);
    const QPointF px = map(tx, y0);
    p.drawText(QRectF(0, py.y() - 8, 48, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(ty, 'g', 4));
    p.drawText(QRectF(px.x() - 30, area.bottom() + 2, 60, 16), Qt::AlignHCenter | Qt::AlignTop,
               QString::number(tx, 'g', 4));
  }
  // curves and legend
  int index = 0;
  for (const results::ResultItem* c : curves) {
    const QColor color = kCurveColors[static_cast<std::size_t>(index) % kCurveColors.size()];
    QPainterPath path;
    bool started = false;
    for (std::size_t k = 0; k < std::min(c->curve->x.size(), c->curve->y.size()); ++k) {
      if (!std::isfinite(c->curve->x[k]) || !std::isfinite(c->curve->y[k])) {
        started = false;
        continue;
      }
      const QPointF q = map(c->curve->x[k], c->curve->y[k]);
      if (started) {
        path.lineTo(q);
      } else {
        path.moveTo(q);
        started = true;
      }
    }
    p.setPen(QPen(color, 1.6));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.drawText(QPointF(area.left() + 8, area.top() + 16 + 14 * index), c->name);
    ++index;
  }
  const auto* first = curves.front()->curve.get();
  p.setPen(QColor(170, 176, 186));
  p.drawText(QRectF(area.left(), height() - 16, area.width(), 16), Qt::AlignCenter, first->x_label);
}

}  // namespace nfcad::ui
