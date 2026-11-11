#include "ui/layer_panel.hpp"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "document/document.hpp"
#include "document/selection.hpp"

namespace nfcad::ui {

namespace {
enum Column { kName = 0, kColor = 1 };
constexpr int kLayerIdRole = Qt::UserRole + 1;

QIcon swatch(const QColor& c) {
  QPixmap pm(14, 14);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setPen(QColor(20, 20, 20));
  p.setBrush(c);
  p.drawRect(0, 0, 13, 13);
  return QIcon(pm);
}
}  // namespace

LayerPanel::LayerPanel(document::Document* doc, document::Selection* selection, QWidget* parent)
    : QWidget(parent), doc_(doc), selection_(selection) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("Layer"), tr("Colour")});
  tree_->setRootIsDecorated(false);
  tree_->header()->setSectionResizeMode(kName, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(kColor, QHeaderView::ResizeToContents);
  layout->addWidget(tree_);

  auto* buttons = new QHBoxLayout;
  auto* add = new QPushButton(tr("New layer"), this);
  auto* assign = new QPushButton(tr("Move selection here"), this);
  assign->setToolTip(tr("Moves the selected objects to the highlighted layer"));
  buttons->addWidget(add);
  buttons->addWidget(assign);
  layout->addLayout(buttons);

  connect(add, &QPushButton::clicked, this, [this] {
    const auto n = doc_->layers().size();
    doc_->addLayer({0, tr("Layer %1").arg(n), QColor::fromHsv(static_cast<int>(n * 67 % 360), 120, 220), true});
  });
  connect(assign, &QPushButton::clicked, this, [this] {
    QTreeWidgetItem* current = tree_->currentItem();
    if (current == nullptr) return;
    doc_->assignLayer(selection_->entities(),
                      static_cast<document::LayerId>(current->data(kName, kLayerIdRole).toUInt()));
  });
  connect(tree_, &QTreeWidget::itemChanged, this, &LayerPanel::onItemChanged);
  connect(tree_, &QTreeWidget::itemDoubleClicked, this, &LayerPanel::onItemDoubleClicked);
  connect(doc_, &document::Document::layersChanged, this, &LayerPanel::rebuild);
  rebuild();
}

void LayerPanel::rebuild() {
  rebuilding_ = true;
  const QVariant current = tree_->currentItem() != nullptr
                               ? tree_->currentItem()->data(kName, kLayerIdRole)
                               : QVariant();
  tree_->clear();
  for (const document::Layer& l : doc_->layers()) {
    auto* item = new QTreeWidgetItem(tree_);
    item->setText(kName, l.name);
    item->setData(kName, kLayerIdRole, l.id);
    item->setCheckState(kName, l.visible ? Qt::Checked : Qt::Unchecked);
    item->setIcon(kColor, swatch(l.color));
    item->setToolTip(kColor, tr("Double-click to change the colour"));
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    if (current.isValid() && current.toUInt() == l.id) tree_->setCurrentItem(item);
  }
  rebuilding_ = false;
}

void LayerPanel::onItemChanged(QTreeWidgetItem* item, int column) {
  if (rebuilding_ || column != kName) return;
  const auto id = static_cast<document::LayerId>(item->data(kName, kLayerIdRole).toUInt());
  const bool visible = item->checkState(kName) == Qt::Checked;
  const QString name = item->text(kName).trimmed();
  // Applied after Qt has finished with the click / edit: the document change rebuilds this
  // tree, which must not happen inside QTreeWidgetItem::setData (the item would be deleted).
  QTimer::singleShot(0, this, [this, id, visible, name] {
    doc_->setLayerVisible(id, visible);
    if (!name.isEmpty()) doc_->renameLayer(id, name);
  });
}

void LayerPanel::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
  if (column != kColor) return;
  const auto id = static_cast<document::LayerId>(item->data(kName, kLayerIdRole).toUInt());
  const document::Layer* l = doc_->layer(id);
  if (l == nullptr) return;
  const QColor c = QColorDialog::getColor(l->color, this, tr("Layer colour"));
  if (c.isValid()) doc_->setLayerColor(id, c);
}

}  // namespace nfcad::ui
