#include "ui/property_panel.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <map>

#include "document/document.hpp"
#include "document/selection.hpp"
#include "kernel/properties.hpp"

namespace nfcad::ui {

PropertyPanel::PropertyPanel(document::Document* doc, document::Selection* selection,
                             QWidget* parent)
    : QWidget(parent), doc_(doc), selection_(selection) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  header_ = new QLabel(this);
  header_->setObjectName(QStringLiteral("propertyHeader"));
  header_->setWordWrap(true);
  layout->addWidget(header_);
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("Property"), tr("Value")});
  tree_->setRootIsDecorated(false);
  tree_->setAlternatingRowColors(true);
  tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  tree_->setTextElideMode(Qt::ElideMiddle);
  layout->addWidget(tree_);

  connect(selection_, &document::Selection::changed, this, &PropertyPanel::refresh);
  connect(doc_, &document::Document::entityChanged, this, &PropertyPanel::refresh);
  connect(doc_, &document::Document::layersChanged, this, &PropertyPanel::refresh);
  refresh();
}

void PropertyPanel::refresh() {
  tree_->clear();
  const auto& items = selection_->items();
  if (items.empty()) {
    header_->setText(tr("Nothing selected"));
    return;
  }
  auto add = [this](const QString& k, const QString& v) {
    auto* row = new QTreeWidgetItem(tree_, {k, v});
    row->setToolTip(1, v);
  };
  if (items.size() == 1) {
    const document::SelectionItem& item = *items.begin();
    const document::Entity* e = doc_->entity(item.entity);
    if (e == nullptr) return;
    QString title = e->name.isEmpty() ? tr("Entity %1").arg(e->id) : e->name;
    if (item.sub.kind != document::SubElement::Kind::None) {
      title += QStringLiteral(" · ") + document::sub_element_name(item.sub);
    }
    header_->setText(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));
    const document::Layer* layer = doc_->layer(e->layer);
    add(tr("Layer"), layer != nullptr ? layer->name : QString());
    add(tr("Id"), QString::number(e->id));
    for (const auto& [k, v] : kernel::describe(*e->geometry, item.sub)) add(k, v);
    return;
  }
  header_->setText(tr("<b>%n items selected</b>", nullptr, static_cast<int>(items.size())));
  std::map<QString, int> counts;
  for (const document::SelectionItem& i : items) {
    const document::Entity* e = doc_->entity(i.entity);
    if (e == nullptr) continue;
    const QString kind = i.sub.kind == document::SubElement::Kind::None
                             ? document::kind_name(document::kind_of(*e->geometry))
                             : document::sub_element_name({i.sub.kind, -1}).section(QLatin1Char(' '), 0, 0);
    ++counts[kind];
  }
  for (const auto& [kind, n] : counts) add(kind, QString::number(n));
}

}  // namespace nfcad::ui
