#include "ui/node_editor.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>

namespace nfcad::ui {

namespace {
constexpr double kWidth = 190.0;
constexpr double kHeader = 24.0;
constexpr double kRow = 18.0;
constexpr double kSocket = 5.0;

QColor category_color(const QString& c) {
  static const std::map<QString, QColor> colors = {
      {QStringLiteral("source"), QColor(70, 140, 90)},      {QStringLiteral("domain"), QColor(60, 105, 165)},
      {QStringLiteral("splitter"), QColor(130, 85, 160)},   {QStringLiteral("detector"), QColor(190, 120, 50)},
      {QStringLiteral("conversion"), QColor(105, 110, 120)}, {QStringLiteral("data"), QColor(50, 140, 140)},
      {QStringLiteral("analysis"), QColor(170, 150, 50)},   {QStringLiteral("group"), QColor(95, 95, 105)},
      {QStringLiteral("meta"), QColor(95, 95, 105)}};
  const auto it = colors.find(c);
  return it == colors.end() ? QColor(95, 95, 105) : it->second;
}

QString type_category(const QString& type_id) {
  if (type_id.endsWith(QLatin1String("source"))) return QStringLiteral("source");
  if (type_id.endsWith(QLatin1String("splitter"))) return QStringLiteral("splitter");
  if (type_id.endsWith(QLatin1String("detector"))) return QStringLiteral("detector");
  return QStringLiteral("domain");
}
}  // namespace

// ------------------------------------------------------------------------------ items

class SocketItem : public QGraphicsEllipseItem {
 public:
  SocketItem(QString node, QJsonObject spec, QGraphicsItem* parent)
      : QGraphicsEllipseItem(-kSocket, -kSocket, 2 * kSocket, 2 * kSocket, parent),
        node_(std::move(node)), spec_(std::move(spec)) {
    const bool light = spec_["kind"].toString() == QLatin1String("light");
    setBrush(light ? QColor(240, 185, 60) : QColor(90, 170, 230));
    setPen(QPen(QColor(30, 30, 30), 1));
    QStringList accepts;
    for (const QJsonValue& a : spec_["accepts"].toArray()) accepts << a.toString();
    setToolTip(QStringLiteral("%1 (%2: %3)").arg(name(), spec_["kind"].toString(), accepts.join(QStringLiteral(", "))));
    setAcceptHoverEvents(true);
  }
  [[nodiscard]] QString node() const { return node_; }
  [[nodiscard]] QString name() const { return spec_["name"].toString(); }
  [[nodiscard]] bool isInput() const { return spec_["direction"].toString() == QLatin1String("in"); }
  [[nodiscard]] QString kind() const { return spec_["kind"].toString(); }

 private:
  QString node_;
  QJsonObject spec_;
};

class NodeItem : public QGraphicsRectItem {
 public:
  NodeItem(NodeEditor* editor, const QJsonObject& node, const QJsonArray& sockets, const QString& category,
           const QString& problem, const QStringList& conversions = {})
      : editor_(editor), id_(node["id"].toString()) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    QJsonArray ins;
    QJsonArray outs;
    for (const QJsonValue& s : sockets) (s.toObject()["direction"].toString() == QLatin1String("in") ? ins : outs).append(s);
    const double h = kHeader + kRow * std::max<double>({1.0, static_cast<double>(ins.size()),
                                                        static_cast<double>(outs.size())}) + 6.0;
    setRect(0, 0, kWidth, h);
    const bool muted = node["muted"].toBool();
    setBrush(muted ? QColor(55, 55, 58) : QColor(44, 48, 56));
    setPen(problem.isEmpty() ? QPen(QColor(20, 20, 20), 1.2) : QPen(QColor(230, 70, 70), 2.4));
    auto* header = new QGraphicsRectItem(0, 0, kWidth, kHeader, this);
    header->setBrush(muted ? QColor(80, 80, 85) : category_color(category));
    header->setPen(Qt::NoPen);
    const QString title = node["name"].toString(id_);
    auto* text = new QGraphicsSimpleTextItem(this);
    QFontMetrics fm(text->font());
    text->setText(fm.elidedText(muted ? title + QStringLiteral(" (muted)") : title, Qt::ElideRight,
                                static_cast<int>(kWidth - 12)));
    text->setBrush(QColor(240, 240, 240));
    text->setPos(6, 4);
    auto place = [this](const QJsonArray& list, bool input) {
      for (int k = 0; k < list.size(); ++k) {
        const QJsonObject spec = list[k].toObject();
        auto* s = new SocketItem(id_, spec, this);
        const double y = kHeader + kRow * (k + 0.5) + 3;
        s->setPos(input ? 0.0 : kWidth, y);
        sockets_.append(s);
        auto* label = new QGraphicsSimpleTextItem(spec["label"].toString(spec["name"].toString()), this);
        QFont f = label->font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        label->setFont(f);
        label->setBrush(QColor(200, 204, 210));
        QFontMetrics lm(f);
        label->setText(lm.elidedText(label->text(), Qt::ElideRight, static_cast<int>(kWidth / 2 - 14)));
        const double w = label->boundingRect().width();
        label->setPos(input ? 9.0 : kWidth - 9.0 - w, y - label->boundingRect().height() / 2);
      }
    };
    place(ins, true);
    place(outs, false);
    QStringList tip;
    tip << QStringLiteral("%1 – %2").arg(id_, node["type"].toString());
    const QJsonObject params = node["params"].toObject();
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (it.key() == QLatin1String("surfaces") || it.key() == QLatin1String("paths")) continue;
      tip << QStringLiteral("%1 = %2").arg(it.key(), it.value().toVariant().toString());
    }
    if (!conversions.isEmpty()) {
      tip << QString() << QObject::tr("planned conversions:") << conversions;
      auto* mark = new QGraphicsSimpleTextItem(QStringLiteral("⇄"), this);
      mark->setBrush(QColor(240, 240, 240));
      mark->setPos(kWidth - 6 - mark->boundingRect().width(), 4);
      text->setText(QFontMetrics(text->font()).elidedText(text->text(), Qt::ElideRight, static_cast<int>(kWidth - 30)));
    }
    if (!problem.isEmpty()) tip << QString() << problem;
    setToolTip(tip.join(QLatin1Char('\n')));
    setPos(node["position"].toArray().at(0).toDouble(), node["position"].toArray().at(1).toDouble());
  }
  [[nodiscard]] QString id() const { return id_; }
  [[nodiscard]] SocketItem* socket(const QString& name) const {
    for (SocketItem* s : sockets_) {
      if (s->name() == name) return s;
    }
    return nullptr;
  }

 protected:
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
    if (change == ItemPositionHasChanged && editor_ != nullptr) editor_->nodeMoved();
    return QGraphicsRectItem::itemChange(change, value);
  }

 private:
  NodeEditor* editor_;
  QString id_;
  QList<SocketItem*> sockets_;
};

class LinkItem : public QGraphicsPathItem {
 public:
  LinkItem(int index, SocketItem* a, SocketItem* b, const QString& transport, bool light, const QJsonObject& planned = {},
           const QString& problem = {})
      : index_(index), a_(a), b_(b), transport_(transport) {
    QPen pen(light ? QColor(240, 185, 60, 220) : QColor(90, 170, 230, 220), transport == QLatin1String("auto") ? 2.0 : 3.0);
    if (!problem.isEmpty()) {
      pen.setColor(QColor(230, 70, 70));
      pen.setStyle(Qt::DashLine);
    }
    setPen(pen);
    setZValue(-1);
    QStringList tip;
    tip << (transport == QLatin1String("auto") ? QObject::tr("transport: auto (the planner chooses)")
                                               : QObject::tr("transport: %1 (fixed)").arg(transport));
    representation_ = planned["representation"].toString();
    if (!representation_.isEmpty()) {
      tip << QObject::tr("planned: %1 – %2").arg(representation_, planned["reason"].toString());
      label_ = new QGraphicsSimpleTextItem(representation_ + (transport == QLatin1String("auto") ? QString() : QStringLiteral("*")), this);
      QFont f = label_->font();
      f.setBold(true);
      f.setPointSizeF(f.pointSizeF() * 0.85);
      label_->setFont(f);
      label_->setBrush(QColor(20, 20, 20));
      auto* back = new QGraphicsRectItem(label_->boundingRect().adjusted(-3, -1, 3, 1), label_);
      back->setBrush(pen.color());
      back->setPen(Qt::NoPen);
      back->setFlag(QGraphicsItem::ItemStacksBehindParent);
    }
    if (!problem.isEmpty()) tip << QString() << problem;
    setToolTip(tip.join(QLatin1Char('\n')));
    update();
  }
  void update() {
    const QPointF p = a_->scenePos();
    const QPointF q = b_->scenePos();
    QPainterPath path(p);
    if (q.x() >= p.x() + 20.0) {
      const double dx = std::max(40.0, (q.x() - p.x()) * 0.5);
      path.cubicTo(p + QPointF(dx, 0), q - QPointF(dx, 0), q);
    } else {
      // a return link (light running back, e.g. from a mirror): an arc below the blocks
      const double drop = 40.0 + 0.15 * std::abs(p.x() - q.x());
      path.cubicTo(p + QPointF(60.0, drop), q + QPointF(-60.0, drop), q);
    }
    setPath(path);
    if (label_ != nullptr) {
      const QPointF m = path.pointAtPercent(0.5);
      label_->setPos(m - label_->boundingRect().center());
    }
  }
  [[nodiscard]] int index() const { return index_; }
  [[nodiscard]] QString representation() const { return representation_; }

 private:
  int index_;
  SocketItem* a_;
  SocketItem* b_;
  QString transport_;
  QString representation_;
  QGraphicsSimpleTextItem* label_ = nullptr;
};

static QString link_key(const QJsonObject& l) {
  const QJsonArray a = l["from"].toArray();
  const QJsonArray b = l["to"].toArray();
  return QStringLiteral("%1.%2 -> %3.%4").arg(a.at(0).toString(), a.at(1).toString(), b.at(0).toString(), b.at(1).toString());
}

// ------------------------------------------------------------------------------ editor

NodeEditor::NodeEditor(QWidget* parent) : QGraphicsView(parent) {
  setScene(new QGraphicsScene(this));
  setRenderHint(QPainter::Antialiasing);
  setBackgroundBrush(QColor(30, 32, 37));
  setDragMode(QGraphicsView::RubberBandDrag);
  // navigation as in the CAD views (docs/ui-concept.md): no scroll bars, an open canvas,
  // zoom to the cursor, pan by middle / right / Alt + left drag and two-finger scroll
  setTransformationAnchor(QGraphicsView::NoAnchor);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scene()->setSceneRect(-1.0e6, -1.0e6, 2.0e6, 2.0e6);
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  setFocusPolicy(Qt::StrongFocus);
}

double NodeEditor::zoom() const { return transform().m11(); }

void NodeEditor::zoomAt(const QPointF& view_pos, double factor) {
  constexpr double kMin = 0.05;
  constexpr double kMax = 8.0;
  factor = std::clamp(zoom() * factor, kMin, kMax) / zoom();
  if (factor == 1.0) return;
  const QPointF anchor = mapToScene(view_pos.toPoint());
  scale(factor, factor);
  pan(view_pos - mapFromScene(anchor));  // the anchor goes back under the cursor
}

void NodeEditor::pan(const QPointF& delta) {
  horizontalScrollBar()->setValue(horizontalScrollBar()->value() - qRound(delta.x()));
  verticalScrollBar()->setValue(verticalScrollBar()->value() - qRound(delta.y()));
}

void NodeEditor::setWorkflow(const QJsonObject& workflow) {
  workflow_ = workflow;
  QJsonObject g = graph();
  if (!g.isEmpty() && !hasPositions(g)) {
    // imported graphs have no layout yet: arrange them (not an edit of the document)
    const QHash<QString, QPointF> pos = layoutColumns(g);
    QJsonArray nodes = g["nodes"].toArray();
    for (int k = 0; k < nodes.size(); ++k) {
      QJsonObject n = nodes[k].toObject();
      const QPointF p = pos.value(n["id"].toString());
      n["position"] = QJsonArray{p.x(), p.y()};
      nodes[k] = n;
    }
    g["nodes"] = nodes;
    workflow_["graph"] = g;
  }
  rebuild();
}

bool NodeEditor::hasPositions(const QJsonObject& graph) {
  // all blocks on one line (the importer's provisional placement) counts as "no layout"
  QSet<double> ys;
  int n = 0;
  for (const QJsonValue& v : graph["nodes"].toArray()) {
    ++n;
    const QJsonArray p = v.toObject()["position"].toArray();
    ys.insert(p.size() == 2 ? p[1].toDouble() : 0.0);
  }
  return n <= 1 || ys.size() > 1;
}

QHash<QString, QPointF> NodeEditor::layoutColumns(const QJsonObject& graph) const {
  // longest path from the sources on the level of sockets (links out -> in, inside a block
  // in -> out); a block's column is where light first arrives at it
  QHash<QString, QStringList> edges;
  QHash<QString, int> indegree;
  QStringList order;
  auto vertex = [&](const QString& v) {
    if (!indegree.contains(v)) {
      indegree.insert(v, 0);
      order << v;
    }
  };
  auto edge = [&](const QString& a, const QString& b) {
    vertex(a);
    vertex(b);
    edges[a] << b;
    ++indegree[b];
  };
  QStringList ids;
  for (const QJsonValue& v : graph["nodes"].toArray()) {
    const QJsonObject n = v.toObject();
    const QString id = n["id"].toString();
    ids << id;
    QStringList ins, outs;
    for (const QJsonValue& s : socketsOf(n)) {
      const QJsonObject o = s.toObject();
      (o["direction"].toString() == QLatin1String("in") ? ins : outs) << o["name"].toString();
    }
    QJsonObject through = n["through"].toObject();
    if (through.isEmpty()) {  // the type's dependencies are not in the catalogue: all to all
      for (const QString& o : outs) through[o] = QJsonArray::fromStringList(ins);
    }
    for (auto it = through.begin(); it != through.end(); ++it) {
      for (const QJsonValue& i : it.value().toArray()) edge(id + QLatin1Char('.') + i.toString(), id + QLatin1Char('.') + it.key());
    }
    for (const QString& o : outs) vertex(id + QLatin1Char('.') + o);
  }
  for (const QJsonValue& v : graph["links"].toArray()) {
    const QJsonObject l = v.toObject();
    edge(l["from"].toArray().at(0).toString() + QLatin1Char('.') + l["from"].toArray().at(1).toString(),
         l["to"].toArray().at(0).toString() + QLatin1Char('.') + l["to"].toArray().at(1).toString());
  }
  QHash<QString, int> depth;
  QStringList queue;
  for (const QString& v : order) {
    if (indegree.value(v) == 0) queue << v;
  }
  QHash<QString, int> remaining = indegree;
  while (!queue.isEmpty()) {
    const QString v = queue.takeFirst();
    for (const QString& w : edges.value(v)) {
      depth[w] = std::max(depth.value(w), depth.value(v) + 1);
      if (--remaining[w] == 0) queue << w;
    }
  }
  QHash<QString, int> column;
  for (const QString& id : ids) {
    int first = -1;
    for (const QString& v : order) {
      if (!v.startsWith(id + QLatin1Char('.'))) continue;
      const int d = depth.value(v);
      if (first < 0 || d < first) first = d;
    }
    column[id] = std::max(first, 0);
  }
  QList<int> distinct(column.begin(), column.end());
  std::sort(distinct.begin(), distinct.end());
  distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
  QHash<int, int> rows;
  QHash<QString, QPointF> out;
  for (const QString& id : ids) {
    const int c = static_cast<int>(distinct.indexOf(column[id]));
    const int r = rows[c]++;
    out[id] = QPointF(c * (kWidth + 70.0), r * 130.0);
  }
  return out;
}

void NodeEditor::arrange() {
  QJsonObject g = graph();
  const QHash<QString, QPointF> pos = layoutColumns(g);
  QJsonArray nodes = g["nodes"].toArray();
  for (int k = 0; k < nodes.size(); ++k) {
    QJsonObject n = nodes[k].toObject();
    const QPointF p = pos.value(n["id"].toString());
    n["position"] = QJsonArray{p.x(), p.y()};
    nodes[k] = n;
  }
  g["nodes"] = nodes;
  setGraph(g);
  fitAll();
}

QJsonArray NodeEditor::socketsOf(const QJsonObject& node) const {
  if (node.contains("sockets")) return node["sockets"].toArray();
  for (const QJsonValue& t : workflow_.value(QStringLiteral("types")).toArray()) {
    if (t.toObject()["type"].toString() == node["type"].toString()) return t.toObject()["sockets"].toArray();
  }
  return {};
}

QGraphicsRectItem* NodeEditor::nodeItem(const QString& id) const { return nodes_.value(id); }

QGraphicsPathItem* NodeEditor::linkItem(int index) const {
  for (LinkItem* l : links_) {
    if (l->index() == index) return l;
  }
  return nullptr;
}

QString NodeEditor::plannedRepresentation(int index) const {
  for (LinkItem* l : links_) {
    if (l->index() == index) return l->representation();
  }
  return {};
}

int NodeEditor::linkCount() const { return graph()["links"].toArray().size(); }

void NodeEditor::rebuild() {
  scene()->clear();
  nodes_.clear();
  links_.clear();
  drag_line_ = nullptr;
  const QJsonObject g = graph();
  // problems per node from the last check
  QHash<QString, QString> problems;
  QHash<QString, QString> link_problems;
  for (const QJsonValue& d : workflow_.value(QStringLiteral("diagnostics")).toArray()) {
    const QJsonObject o = d.toObject();
    if (o["severity"].toString() != QLatin1String("error")) continue;
    const QString loc = o["location"].toString();
    if (loc.contains(QLatin1String(" -> "))) {
      link_problems[loc] += o["code"].toString() + QStringLiteral(": ") + o["message"].toString() + QLatin1Char('\n');
    }
    const QString node = loc.section(QLatin1Char('.'), 0, 0).section(QLatin1Char(' '), 0, 0);
    problems[node] += o["code"].toString() + QStringLiteral(": ") + o["message"].toString() + QLatin1Char('\n');
  }
  // the planner's result (RTT-Fusion R17): representation per link, conversions per block
  const QJsonObject plan = workflow_.value(QStringLiteral("plan")).toObject();
  const QJsonObject planned_links = plan["links"].toObject();
  QHash<QString, QStringList> conversions;
  for (const QJsonValue& v : plan["conversions"].toArray()) {
    const QJsonObject c = v.toObject();
    QStringList ops;
    for (const QJsonValue& op : c["operators"].toArray()) ops << op.toString();
    conversions[c["node"].toString()] << QStringLiteral("%1 → %2: %3 (%4)").arg(c["from"].toString(), c["to"].toString(),
                                                                               c["chain"].toString(), ops.join(QStringLiteral(", ")));
  }
  QHash<QString, QString> categories;
  for (const QJsonValue& t : workflow_.value(QStringLiteral("types")).toArray()) {
    categories[t.toObject()["type"].toString()] = t.toObject()["category"].toString();
  }
  for (const QJsonValue& v : g["nodes"].toArray()) {
    const QJsonObject n = v.toObject();
    const QString type = n["type"].toString();
    auto* item = new NodeItem(this, n, socketsOf(n), categories.value(type, type_category(type)),
                              problems.value(n["id"].toString()).trimmed(), conversions.value(n["id"].toString()));
    scene()->addItem(item);
    nodes_.insert(item->id(), item);
  }
  const QJsonArray links = g["links"].toArray();
  for (int k = 0; k < links.size(); ++k) {
    const QJsonObject l = links[k].toObject();
    NodeItem* a = nodes_.value(l["from"].toArray().at(0).toString());
    NodeItem* b = nodes_.value(l["to"].toArray().at(0).toString());
    SocketItem* sa = a != nullptr ? a->socket(l["from"].toArray().at(1).toString()) : nullptr;
    SocketItem* sb = b != nullptr ? b->socket(l["to"].toArray().at(1).toString()) : nullptr;
    if (sa == nullptr || sb == nullptr) continue;  // reported by the check of the plugin
    const QString key = link_key(l);
    auto* link = new LinkItem(k, sa, sb, l["transport"].toString(QStringLiteral("auto")),
                              sa->kind() == QLatin1String("light"), planned_links[key].toObject(),
                              link_problems.value(key).trimmed());
    scene()->addItem(link);
    links_.append(link);
  }
}

void NodeEditor::fitAll() {
  if (scene()->items().isEmpty()) return;
  fitInView(scene()->itemsBoundingRect().adjusted(-30, -30, 30, 30), Qt::KeepAspectRatio);
  if (zoom() > 1.5) {  // a single block does not fill the window
    const QPointF c = scene()->itemsBoundingRect().center();
    resetTransform();
    scale(1.5, 1.5);
    centerOn(c);
  }
}

void NodeEditor::updateLinks() {
  for (LinkItem* l : links_) l->update();
}

void NodeEditor::nodeMoved() {
  moved_ = true;
  updateLinks();
}

void NodeEditor::setGraph(const QJsonObject& graph) {
  workflow_["graph"] = graph;
  workflow_.remove(QStringLiteral("diagnostics"));  // stale after an edit; the next check renews it
  workflow_.remove(QStringLiteral("plan"));
  rebuild();
  emit edited(workflow_);
}

void NodeEditor::commit() {
  // positions from the items
  QJsonObject g = graph();
  QJsonArray nodes = g["nodes"].toArray();
  for (int k = 0; k < nodes.size(); ++k) {
    QJsonObject n = nodes[k].toObject();
    if (NodeItem* item = nodes_.value(n["id"].toString())) {
      n["position"] = QJsonArray{std::round(item->pos().x()), std::round(item->pos().y())};
    }
    nodes[k] = n;
  }
  g["nodes"] = nodes;
  workflow_["graph"] = g;
  emit edited(workflow_);
}

bool NodeEditor::addLink(const QString& from_node, const QString& from_socket, const QString& to_node,
                         const QString& to_socket) {
  QJsonObject g = graph();
  QJsonArray links = g["links"].toArray();
  for (const QJsonValue& v : links) {
    const QJsonObject l = v.toObject();
    if (l["to"].toArray() == QJsonArray{to_node, to_socket}) return false;  // one link per input
  }
  links.append(QJsonObject{{"from", QJsonArray{from_node, from_socket}}, {"to", QJsonArray{to_node, to_socket}}});
  g["links"] = links;
  setGraph(g);
  return true;
}

void NodeEditor::removeLink(int index) {
  QJsonObject g = graph();
  QJsonArray links = g["links"].toArray();
  if (index < 0 || index >= links.size()) return;
  links.removeAt(index);
  g["links"] = links;
  setGraph(g);
}

void NodeEditor::setTransport(int index, const QString& transport) {
  QJsonObject g = graph();
  QJsonArray links = g["links"].toArray();
  if (index < 0 || index >= links.size()) return;
  QJsonObject l = links[index].toObject();
  if (transport == QLatin1String("auto")) {
    l.remove(QStringLiteral("transport"));
  } else {
    l["transport"] = transport;
  }
  links[index] = l;
  g["links"] = links;
  setGraph(g);
}

void NodeEditor::removeNode(const QString& id) {
  QJsonObject g = graph();
  QJsonArray nodes;
  for (const QJsonValue& v : g["nodes"].toArray()) {
    if (v.toObject()["id"].toString() != id) nodes.append(v);
  }
  QJsonArray links;
  for (const QJsonValue& v : g["links"].toArray()) {
    const QJsonObject l = v.toObject();
    if (l["from"].toArray().at(0).toString() != id && l["to"].toArray().at(0).toString() != id) links.append(v);
  }
  g["nodes"] = nodes;
  g["links"] = links;
  setGraph(g);
}

void NodeEditor::setMuted(const QString& id, bool muted) {
  QJsonObject g = graph();
  QJsonArray nodes = g["nodes"].toArray();
  for (int k = 0; k < nodes.size(); ++k) {
    QJsonObject n = nodes[k].toObject();
    if (n["id"].toString() != id) continue;
    if (muted) {
      n["muted"] = true;
    } else {
      n.remove(QStringLiteral("muted"));
    }
    nodes[k] = n;
  }
  g["nodes"] = nodes;
  setGraph(g);
}

QString NodeEditor::addNode(const QString& type, const QPointF& position) {
  QJsonObject g = graph();
  QSet<QString> ids;
  for (const QJsonValue& v : g["nodes"].toArray()) ids.insert(v.toObject()["id"].toString());
  QString stem = type.section(QLatin1Char('.'), -1);
  QString id = stem;
  for (int k = 2; ids.contains(id); ++k) id = QStringLiteral("%1_%2").arg(stem).arg(k);
  QJsonObject node{{"id", id}, {"type", type}, {"position", QJsonArray{std::round(position.x()), std::round(position.y())}}};
  QJsonObject params;
  for (const QJsonValue& t : workflow_.value(QStringLiteral("types")).toArray()) {
    if (t.toObject()["type"].toString() != type) continue;
    for (const QJsonValue& p : t.toObject()["params"].toArray()) {
      params[p.toObject()["name"].toString()] = p.toObject()["default"];
    }
  }
  if (!params.isEmpty()) node["params"] = params;
  QJsonArray nodes = g["nodes"].toArray();
  nodes.append(node);
  g["nodes"] = nodes;
  if (!g.contains("format")) {
    g["format"] = QStringLiteral("rtt-fusion-graph");
    g["version"] = 1;
  }
  setGraph(g);
  return id;
}

// ------------------------------------------------------------------------------ interaction

void NodeEditor::mousePressEvent(QMouseEvent* event) {
  const bool alt_left = event->button() == Qt::LeftButton && (event->modifiers() & Qt::AltModifier);
  if (event->button() == Qt::MiddleButton || alt_left) {
    panning_ = true;
    pan_from_ = event->position();
    viewport()->setCursor(Qt::ClosedHandCursor);
    return;
  }
  if (event->button() == Qt::RightButton) {
    right_down_ = true;
    right_dragged_ = false;
    right_from_ = event->position();
    pan_from_ = event->position();
    return;
  }
  if (event->button() == Qt::LeftButton) {
    for (QGraphicsItem* item : items(event->position().toPoint())) {
      if (auto* s = dynamic_cast<SocketItem*>(item)) {
        drag_from_ = s;
        drag_line_ = scene()->addPath(QPainterPath(), QPen(QColor(230, 230, 230), 1.5, Qt::DashLine));
        return;
      }
    }
  }
  moved_ = false;
  QGraphicsView::mousePressEvent(event);
}

void NodeEditor::mouseMoveEvent(QMouseEvent* event) {
  if (right_down_ && !right_dragged_ &&
      (event->position() - right_from_).manhattanLength() > QApplication::startDragDistance()) {
    right_dragged_ = true;
    panning_ = true;
    viewport()->setCursor(Qt::ClosedHandCursor);
  }
  if (panning_) {
    pan(event->position() - pan_from_);
    pan_from_ = event->position();
    return;
  }
  if (drag_from_ != nullptr && drag_line_ != nullptr) {
    const QPointF p = drag_from_->scenePos();
    const QPointF q = mapToScene(event->position().toPoint());
    QPainterPath path(p);
    path.lineTo(q);
    drag_line_->setPath(path);
    return;
  }
  QGraphicsView::mouseMoveEvent(event);
}

void NodeEditor::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton && right_down_) {
    right_down_ = false;
    if (!right_dragged_) showMenu(event->position().toPoint(), event->globalPosition().toPoint());
    if (panning_) {
      panning_ = false;
      viewport()->unsetCursor();
    }
    return;
  }
  if (panning_) {
    panning_ = false;
    viewport()->unsetCursor();
    return;
  }
  if (drag_from_ != nullptr) {
    SocketItem* from = drag_from_;
    drag_from_ = nullptr;
    delete drag_line_;
    drag_line_ = nullptr;
    for (QGraphicsItem* item : items(event->position().toPoint())) {
      auto* to = dynamic_cast<SocketItem*>(item);
      if (to == nullptr || to == from || to->node() == from->node()) continue;
      if (to->isInput() == from->isInput() || to->kind() != from->kind()) break;  // refused
      SocketItem* out = from->isInput() ? to : from;
      SocketItem* in = from->isInput() ? from : to;
      addLink(out->node(), out->name(), in->node(), in->name());
      break;
    }
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
  if (moved_) {
    moved_ = false;
    commit();
  }
}

void NodeEditor::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton) {
    fitAll();
    return;
  }
  QGraphicsView::mouseDoubleClickEvent(event);
}

void NodeEditor::wheelEvent(QWheelEvent* event) {
  // as in the CAD views: two-finger trackpad scroll pans, the wheel (or Cmd/Ctrl + scroll) zooms
  constexpr double kWheelZoomPerNotch = 0.85;  // same step as the viewports
  const bool zoom_modifier = (event->modifiers() & Qt::ControlModifier) != 0;
  const bool trackpad_scroll = !event->pixelDelta().isNull() && event->phase() != Qt::NoScrollPhase;
  if (trackpad_scroll && !zoom_modifier) {
    pan(QPointF(event->pixelDelta()));
  } else {
    double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) steps = event->pixelDelta().y() / 50.0;
    zoomAt(event->position(), 1.0 / std::pow(kWheelZoomPerNotch, steps));
  }
  event->accept();
}

void NodeEditor::keyPressEvent(QKeyEvent* event) {
  // zoom extents: Z E (as in the CAD views); Home as well
  if (event->key() == Qt::Key_Home || (z_pressed_ && event->key() == Qt::Key_E)) {
    z_pressed_ = false;
    fitAll();
    return;
  }
  z_pressed_ = event->key() == Qt::Key_Z && event->modifiers() == Qt::NoModifier;
  if (z_pressed_) return;
  QGraphicsView::keyPressEvent(event);
}

bool NodeEditor::viewportEvent(QEvent* event) {
  if (event->type() == QEvent::NativeGesture) {
    auto* gesture = static_cast<QNativeGestureEvent*>(event);
    if (gesture->gestureType() == Qt::ZoomNativeGesture) {  // trackpad pinch
      zoomAt(mapFromGlobal(gesture->globalPosition().toPoint()), std::exp(gesture->value()));
      return true;
    }
    if (gesture->gestureType() == Qt::SmartZoomNativeGesture) {  // two-finger double tap
      fitAll();
      return true;
    }
  }
  return QGraphicsView::viewportEvent(event);
}

void NodeEditor::contextMenuEvent(QContextMenuEvent* event) {
  // mouse: opened on the release of the right button (a right drag pans); keyboard: here
  if (event->reason() == QContextMenuEvent::Mouse) {
    event->accept();
    return;
  }
  showMenu(event->pos(), event->globalPos());
}

void NodeEditor::showMenu(const QPoint& view_pos, const QPoint& global_pos) {
  QMenu menu(this);
  const QPointF at = mapToScene(view_pos);
  NodeItem* node = nullptr;
  LinkItem* link = nullptr;
  for (QGraphicsItem* item : items(view_pos)) {
    for (QGraphicsItem* p = item; p != nullptr && node == nullptr; p = p->parentItem()) node = dynamic_cast<NodeItem*>(p);
    if (link == nullptr) link = dynamic_cast<LinkItem*>(item);
    if (node != nullptr) break;
  }
  if (node != nullptr) {
    const QString id = node->id();
    bool muted = false;
    for (const QJsonValue& v : graph()["nodes"].toArray()) {
      if (v.toObject()["id"].toString() == id) muted = v.toObject()["muted"].toBool();
    }
    menu.addAction(muted ? tr("Unmute") : tr("Mute (bypass)"), this, [this, id, muted] { setMuted(id, !muted); });
    menu.addAction(tr("Delete block"), this, [this, id] { removeNode(id); });
  } else if (link != nullptr) {
    const int index = link->index();
    QMenu* transport = menu.addMenu(tr("Transport"));
    for (const char* t : {"auto", "R", "H", "G", "S", "T", "P"}) {
      transport->addAction(QString::fromLatin1(t), this, [this, index, t] { setTransport(index, QString::fromLatin1(t)); });
    }
    menu.addAction(tr("Delete link"), this, [this, index] { removeLink(index); });
  } else {
    QMenu* add = menu.addMenu(tr("Add block"));
    std::map<QString, QMenu*> by_category;
    for (const QJsonValue& v : workflow_.value(QStringLiteral("types")).toArray()) {
      const QJsonObject t = v.toObject();
      const QString category = t["category"].toString();
      QMenu*& sub = by_category[category];
      if (sub == nullptr) sub = add->addMenu(category);
      const QString type = t["type"].toString();
      QAction* a = sub->addAction(type, this, [this, type, at] { addNode(type, at); });
      a->setToolTip(t["description"].toString());
    }
    add->setEnabled(!by_category.empty());
    add->setToolTipsVisible(true);
    menu.addAction(tr("Arrange"), this, &NodeEditor::arrange);
    menu.addAction(tr("Show all"), this, &NodeEditor::fitAll);
  }
  menu.exec(global_pos);
}

}  // namespace nfcad::ui
