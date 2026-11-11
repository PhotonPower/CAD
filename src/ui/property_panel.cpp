#include "ui/property_panel.hpp"

#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
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
  add_attribute_ = new QPushButton(tr("Add attribute…"), this);
  add_attribute_->setToolTip(tr("Free key / value data saved with the document, e.g. "
                                "rtt.role = mirror or sim.inactive = true"));
  layout->addWidget(add_attribute_);
  connect(add_attribute_, &QPushButton::clicked, this, &PropertyPanel::addAttribute);
  connect(tree_, &QTreeWidget::itemChanged, this, &PropertyPanel::onItemChanged);

  connect(selection_, &document::Selection::changed, this, &PropertyPanel::refresh);
  connect(doc_, &document::Document::entityChanged, this, &PropertyPanel::refresh);
  connect(doc_, &document::Document::layersChanged, this, &PropertyPanel::refresh);
  refresh();
}

namespace {
constexpr int kAttributeKeyRole = Qt::UserRole + 1;

/// Typed value from text: true / false, numbers, else text.
QVariant parse_value(const QString& text) {
  const QString t = text.trimmed();
  if (t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) return true;
  if (t.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) return false;
  bool ok = false;
  const double d = t.toDouble(&ok);
  if (ok) return d;
  return t;
}

const document::Entity* single_entity(const document::Document* doc, const document::Selection* s) {
  if (s->items().size() != 1) return nullptr;
  const document::SelectionItem& i = *s->items().begin();
  return i.sub.kind == document::SubElement::Kind::None ? doc->entity(i.entity) : nullptr;
}
}  // namespace

void PropertyPanel::addAttribute() {
  const document::Entity* e = single_entity(doc_, selection_);
  if (e == nullptr) return;
  bool ok = false;
  const QString text = QInputDialog::getText(this, tr("Add attribute"), tr("key = value"),
                                             QLineEdit::Normal, QStringLiteral("sim.inactive = true"), &ok);
  if (!ok) return;
  const qsizetype eq = text.indexOf(QLatin1Char('='));
  const QString key = (eq < 0 ? text : text.left(eq)).trimmed();
  if (key.isEmpty()) return;
  doc_->setAttribute(e->id, key, eq < 0 ? QVariant(true) : parse_value(text.mid(eq + 1)));
}

void PropertyPanel::onItemChanged(QTreeWidgetItem* item, int column) {
  if (refreshing_ || column != 1) return;
  const QString key = item->data(0, kAttributeKeyRole).toString();
  const document::Entity* e = single_entity(doc_, selection_);
  if (key.isEmpty() || e == nullptr) return;
  // an empty value removes the attribute
  doc_->setAttribute(e->id, key, item->text(1).trimmed().isEmpty() ? QVariant() : parse_value(item->text(1)));
}

void PropertyPanel::refresh() {
  refreshing_ = true;
  struct Done {
    bool& flag;
    ~Done() { flag = false; }
  } done{refreshing_};
  tree_->clear();
  add_attribute_->setEnabled(single_entity(doc_, selection_) != nullptr);
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
    if (item.sub.kind == document::SubElement::Kind::None && !e->attributes.isEmpty()) {
      auto* section = new QTreeWidgetItem(tree_, {tr("Attributes")});
      QFont bold = section->font(0);
      bold.setBold(true);
      section->setFont(0, bold);
      for (auto it = e->attributes.cbegin(); it != e->attributes.cend(); ++it) {
        auto* row = new QTreeWidgetItem(tree_, {it.key(), it.value().toString()});
        row->setData(0, kAttributeKeyRole, it.key());
        row->setFlags(row->flags() | Qt::ItemIsEditable);
        row->setToolTip(1, tr("Double-click to edit; empty removes the attribute"));
      }
    }
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
