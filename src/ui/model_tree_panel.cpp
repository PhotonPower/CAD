#include "ui/model_tree_panel.hpp"

#include <QApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>

#include "document/document.hpp"
#include "document/selection.hpp"

namespace nfcad::ui {

namespace {
enum Column { kName = 0, kType = 1 };
constexpr int kKindRole = Qt::UserRole + 1;  // 0 entity, 1 group
constexpr int kIdRole = Qt::UserRole + 2;
}  // namespace

ModelTreePanel::ModelTreePanel(document::Document* doc, document::Selection* selection, QWidget* parent)
    : QWidget(parent), doc_(doc), selection_(selection) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("Model"), tr("Type")});
  tree_->header()->setSectionResizeMode(kName, QHeaderView::Stretch);
  tree_->header()->setSectionResizeMode(kType, QHeaderView::ResizeToContents);
  tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  layout->addWidget(tree_);

  connect(tree_, &QTreeWidget::itemChanged, this, &ModelTreePanel::onItemChanged);
  connect(tree_, &QTreeWidget::itemSelectionChanged, this, &ModelTreePanel::onTreeSelection);
  connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ModelTreePanel::showContextMenu);
  connect(doc_, &document::Document::documentReset, this, &ModelTreePanel::scheduleRebuild);
  connect(doc_, &document::Document::groupsChanged, this, &ModelTreePanel::scheduleRebuild);
  connect(doc_, &document::Document::entityAdded, this, &ModelTreePanel::scheduleRebuild);
  connect(doc_, &document::Document::entityRemoved, this, &ModelTreePanel::scheduleRebuild);
  connect(doc_, &document::Document::entityChanged, this, &ModelTreePanel::scheduleRebuild);
  connect(selection_, &document::Selection::changed, this, &ModelTreePanel::syncFromSelection);
  rebuild();
}

void ModelTreePanel::scheduleRebuild() {
  // many document signals arrive in a burst (import, isolate): rebuild once afterwards
  if (rebuild_pending_) return;
  rebuild_pending_ = true;
  QTimer::singleShot(0, this, [this] {
    rebuild_pending_ = false;
    rebuild();
  });
}

void ModelTreePanel::rebuild() {
  rebuilding_ = true;
  tree_->clear();
  const QIcon folder = style()->standardIcon(QStyle::SP_DirIcon);
  std::map<document::GroupId, QTreeWidgetItem*> items;
  // groups: create, then attach to their parents (parents may come later in the list)
  for (const document::Group& g : doc_->groups()) {
    auto* item = new QTreeWidgetItem;
    item->setText(kName, g.name);
    item->setText(kType, tr("Group"));
    item->setIcon(kName, folder);
    item->setData(kName, kKindRole, 1);
    item->setData(kName, kIdRole, QVariant::fromValue<quint32>(g.id));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
    item->setCheckState(kName, g.visible ? Qt::Checked : Qt::Unchecked);
    items[g.id] = item;
  }
  for (const document::Group& g : doc_->groups()) {
    const auto parent = items.find(g.parent);
    if (g.parent != 0 && parent != items.end() && parent->second != items[g.id]) {
      parent->second->addChild(items[g.id]);
    } else {
      tree_->addTopLevelItem(items[g.id]);
    }
  }
  for (const auto& [id, e] : doc_->entities()) {
    auto* item = new QTreeWidgetItem;
    item->setText(kName, e.name.isEmpty() ? tr("%1 %2").arg(document::kind_name(document::kind_of(*e.geometry))).arg(id)
                                          : e.name);
    item->setText(kType, document::kind_name(document::kind_of(*e.geometry)));
    item->setData(kName, kKindRole, 0);
    item->setData(kName, kIdRole, QVariant::fromValue<quint64>(id));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(kName, e.visible ? Qt::Checked : Qt::Unchecked);
    if (e.attributes.value(QStringLiteral("sim.inactive")).toBool()) {
      item->setForeground(kName, QApplication::palette().color(QPalette::Disabled, QPalette::Text));
      item->setToolTip(kName, tr("excluded from computations (sim.inactive)"));
    }
    const auto g = items.find(e.group);
    if (g != items.end()) {
      g->second->addChild(item);
    } else {
      tree_->addTopLevelItem(item);
    }
  }
  tree_->expandToDepth(0);
  rebuilding_ = false;
  syncFromSelection();
}

void ModelTreePanel::syncFromSelection() {
  if (rebuilding_) return;
  syncing_ = true;
  tree_->clearSelection();
  for (QTreeWidgetItemIterator it(tree_); *it != nullptr; ++it) {
    QTreeWidgetItem* item = *it;
    if (item->data(kName, kKindRole).toInt() == 0 &&
        selection_->isSelected(item->data(kName, kIdRole).toULongLong())) {
      item->setSelected(true);
    }
  }
  syncing_ = false;
}

std::vector<std::uint64_t> ModelTreePanel::selectedEntities() const {
  std::vector<std::uint64_t> out;
  for (QTreeWidgetItem* item : tree_->selectedItems()) {
    if (item->data(kName, kKindRole).toInt() == 0) {
      out.push_back(item->data(kName, kIdRole).toULongLong());
    } else {
      for (document::EntityId id : doc_->entitiesInGroup(item->data(kName, kIdRole).toUInt())) {
        out.push_back(id);
      }
    }
  }
  return out;
}

void ModelTreePanel::onTreeSelection() {
  if (syncing_ || rebuilding_) return;
  std::vector<document::SelectionItem> items;
  for (std::uint64_t id : selectedEntities()) items.push_back({id, {}});
  syncing_ = true;
  selection_->apply(items, document::Selection::Mode::Replace);
  syncing_ = false;
}

void ModelTreePanel::onItemChanged(QTreeWidgetItem* item, int column) {
  if (rebuilding_ || column != kName) return;
  const bool visible = item->checkState(kName) == Qt::Checked;
  if (item->data(kName, kKindRole).toInt() == 0) {
    doc_->setEntityVisible(item->data(kName, kIdRole).toULongLong(), visible);
  } else {
    const auto id = static_cast<document::GroupId>(item->data(kName, kIdRole).toUInt());
    doc_->setGroupVisible(id, visible);
    if (const document::Group* g = doc_->group(id); g != nullptr && g->name != item->text(kName) &&
                                                    !item->text(kName).trimmed().isEmpty()) {
      doc_->renameGroup(id, item->text(kName).trimmed());
    }
  }
}

void ModelTreePanel::groupSelection() {
  const auto ids = selection_->entities();
  if (ids.empty()) return;
  bool ok = false;
  const QString name = QInputDialog::getText(this, tr("Group"), tr("Name of the new group:"),
                                             QLineEdit::Normal, tr("Group %1").arg(doc_->groups().size() + 1), &ok);
  if (!ok) return;
  // the new group goes into the common group of the objects (if they share one)
  document::GroupId parent = 0;
  if (const document::Entity* first = doc_->entity(ids.front())) parent = first->group;
  for (document::EntityId id : ids) {
    const document::Entity* e = doc_->entity(id);
    if (e == nullptr || e->group != parent) parent = 0;
  }
  doc_->groupEntities(ids, name.trimmed().isEmpty() ? tr("Group") : name.trimmed(), parent);
}

void ModelTreePanel::ungroupSelection() {
  for (QTreeWidgetItem* item : tree_->selectedItems()) {
    if (item->data(kName, kKindRole).toInt() == 1) doc_->ungroup(item->data(kName, kIdRole).toUInt());
  }
}

void ModelTreePanel::hideSelection() {
  for (document::EntityId id : selection_->entities()) doc_->setEntityVisible(id, false);
}

void ModelTreePanel::isolateSelection() {
  const auto ids = selection_->entities();
  if (!ids.empty()) doc_->isolate(ids);
}

void ModelTreePanel::showContextMenu(const QPoint& pos) {
  QMenu menu(this);
  menu.addAction(tr("Group selection…"), this, &ModelTreePanel::groupSelection);
  menu.addAction(tr("Ungroup"), this, &ModelTreePanel::ungroupSelection);
  menu.addSeparator();
  menu.addAction(tr("Hide"), this, &ModelTreePanel::hideSelection);
  menu.addAction(tr("Isolate"), this, &ModelTreePanel::isolateSelection);
  menu.addAction(tr("Show all"), this, [this] { doc_->showAll(); });
  menu.exec(tree_->viewport()->mapToGlobal(pos));
}

}  // namespace nfcad::ui
