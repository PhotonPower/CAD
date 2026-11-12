#include "ui/node_editor.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QRegularExpression>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include <functional>
#include <optional>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsSceneHoverEvent>
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

QJsonObject type_of(const QJsonObject& workflow, const QString& type) {
  for (const QJsonValue& t : workflow.value(QStringLiteral("types")).toArray()) {
    if (t.toObject()["type"].toString() == type) return t.toObject();
  }
  return {};
}

QJsonObject node_of(const QJsonObject& graph, const QString& id) {
  for (const QJsonValue& v : graph["nodes"].toArray()) {
    if (v.toObject()["id"].toString() == id) return v.toObject();
  }
  return {};
}

QString tr_(const char* text) { return QObject::tr(text); }

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
  /// A light port of a model (R24): both channels in one point.
  [[nodiscard]] bool isPort() const { return spec_["port"].toBool(); }
  [[nodiscard]] QString inSocket() const { return spec_["in"].toString(); }
  [[nodiscard]] QString outSocket() const { return spec_["out"].toString(); }

 private:
  QString node_;
  QJsonObject spec_;
};

class NodeItem : public QGraphicsRectItem {
 public:
  NodeItem(NodeEditor* editor, const QJsonObject& node, const QJsonArray& sockets, const QString& category,
           const QString& problem, const QStringList& conversions = {}, bool running = false, int state = -1)
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
    setPen(running ? QPen(QColor(120, 220, 255), 3.0)
                   : problem.isEmpty() ? QPen(QColor(20, 20, 20), 1.2) : QPen(QColor(230, 70, 70), 2.4));
    auto* header = new QGraphicsRectItem(0, 0, kWidth, kHeader, this);
    header->setBrush(muted ? QColor(80, 80, 85) : category_color(category));
    header->setPen(Qt::NoPen);
    const QString title = node["name"].toString(id_);
    auto* text = new QGraphicsSimpleTextItem(this);
    QFontMetrics fm(text->font());
    const QString shown = running ? title + QObject::tr(" – running …") : muted ? title + QStringLiteral(" (muted)") : title;
    text->setText(fm.elidedText(shown, Qt::ElideRight, static_cast<int>(kWidth - 12)));
    text->setBrush(QColor(240, 240, 240));
    text->setPos(6, 4);
    if (state >= 0) {  // results of the last run: still valid (tick) or out of date (cross)
      auto* badge = new QGraphicsEllipseItem(-7, -7, 14, 14, this);
      badge->setBrush(state == 1 ? QColor(60, 170, 90) : QColor(200, 60, 60));
      badge->setPen(QPen(QColor(20, 20, 20), 1.0));
      auto* glyph = new QGraphicsSimpleTextItem(state == 1 ? QStringLiteral("✓") : QStringLiteral("✗"), badge);
      QFont f = glyph->font();
      f.setBold(true);
      f.setPointSizeF(f.pointSizeF() * 0.8);
      glyph->setFont(f);
      glyph->setBrush(QColor(255, 255, 255));
      glyph->setPos(-glyph->boundingRect().width() / 2, -glyph->boundingRect().height() / 2);
      badge->setToolTip(state == 1 ? QObject::tr("Up to date: kept from the last run")
                                   : QObject::tr("Out of date: computed again at the next run"));
      badge->setZValue(2);
    }
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
    base_pen_ = pen;
    setZValue(-1);
    setAcceptHoverEvents(true);
    setFlag(ItemIsSelectable);
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
      label_back_ = back;
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
    for (Arrow& a : arrows_) {  // in the middle of the band: forward above it, back below it
      const double at = 0.5;
      const QPointF m = path.pointAtPercent(at);
      double angle = path.angleAtPercent(at);  // degrees, counter-clockwise, y up
      if (a.reverse) angle += 180.0;
      const double rad = angle * M_PI / 180.0;
      const bool right = std::cos(rad) >= 0.0;
      // ▶ / ◀ as seen on the screen: the direction's horizontal sense
      a.text->setText((right ? QStringLiteral("▶ ") : QStringLiteral("◀ ")) + a.text->text().section(QLatin1Char(' '), -1));
      a.box->setRect(a.text->boundingRect().adjusted(-3, -1, 3, 1));
      const double h = a.box->rect().height();
      const double shift = arrows_.size() == 1 ? 0.0 : (a.reverse ? 0.5 : -0.5) * (h + 2.0);
      a.box->setPos(m - a.box->rect().center() + QPointF(0.0, shift));
    }
  }
  [[nodiscard]] int index() const { return index_; }
  [[nodiscard]] QString representation() const { return representation_; }
  /// The plan was made before the last edit: its label grey, the tooltip says so.
  void markStale() {
    if (label_back_ != nullptr) label_back_->setBrush(QColor(120, 120, 120));
    setToolTip(toolTip() + QObject::tr("\n(planned before the last edit – Check workflow renews it)"));
  }
  /// A connection of two ports (R24) rather than a directed link.
  [[nodiscard]] bool isConnection() const { return connection_; }
  void setConnection(bool on) { connection_ = on; }
  /// `direction`: the arrow of a highlighted branch (0 forward, 1 back, −1 none) gets its colour.
  void setHighlight(const QColor& color, int direction = -1) {
    base_pen_.setColor(color);
    base_pen_.setWidthF(base_pen_.widthF() + 2.5);
    setPen(base_pen_);
    setZValue(-0.5);
    for (Arrow& a : arrows_) {
      if (direction >= 0 && a.reverse == (direction == 1)) a.box->setBrush(color);
    }
  }
  /// Labels of a connection, one per direction it carries: an arrow along the band (forward:
  /// from the first end to the second) and the representation, grey when the plan is out of date.
  void setDirections(const QString& forward, const QString& back, bool forward_used, bool back_used, bool stale) {
    for (Arrow& a : arrows_) delete a.box;
    arrows_.clear();
    if (label_ != nullptr) label_->hide();  // the single label of a link is replaced
    auto make = [this, stale](const QString& text, bool reverse, bool used) {
      if (text.isEmpty()) return;
      auto* box = new QGraphicsRectItem(this);
      box->setPen(Qt::NoPen);
      box->setBrush(stale ? QColor(120, 120, 120) : base_pen_.color());
      auto* t = new QGraphicsSimpleTextItem(text, box);
      QFont f = t->font();
      f.setBold(true);
      f.setPointSizeF(f.pointSizeF() * 0.85);
      t->setFont(f);
      t->setBrush(QColor(20, 20, 20));
      box->setRect(t->boundingRect().adjusted(-3, -1, 3, 1));
      arrows_.append(Arrow{box, t, reverse, used});
    };
    make(forward, false, forward_used);
    make(back, true, back_used);
    update();
  }

  /// A wide band around the curve, so that a link can be hit with the mouse.
  [[nodiscard]] QPainterPath shape() const override {
    QPainterPathStroker stroker;
    stroker.setWidth(12.0);
    stroker.setCapStyle(Qt::RoundCap);
    return stroker.createStroke(path());
  }
  [[nodiscard]] QRectF boundingRect() const override { return shape().boundingRect(); }

 protected:
  void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override {
    QPen p = base_pen_;
    p.setWidthF(base_pen_.widthF() + 2.0);
    p.setColor(base_pen_.color().lighter(130));
    setPen(p);
    QGraphicsPathItem::hoverEnterEvent(event);
  }
  void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override {
    setPen(base_pen_);
    QGraphicsPathItem::hoverLeaveEvent(event);
  }
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override {
    QStyleOptionGraphicsItem o(*option);
    o.state &= ~QStyle::State_Selected;  // no dashed selection rectangle
    if (isSelected()) {
      QPen p = pen();
      p.setWidthF(std::max(p.widthF(), base_pen_.widthF() + 2.0));
      p.setColor(QColor(255, 255, 255, 230));
      painter->setPen(p);
      painter->drawPath(path());
      return;
    }
    QGraphicsPathItem::paint(painter, &o, widget);
  }

 private:
  int index_;
  SocketItem* a_;
  SocketItem* b_;
  QString transport_;
  QString representation_;
  QGraphicsSimpleTextItem* label_ = nullptr;
  QGraphicsRectItem* label_back_ = nullptr;
  QPen base_pen_;
  bool connection_ = false;
  struct Arrow {
    QGraphicsRectItem* box;
    QGraphicsSimpleTextItem* text;
    bool reverse;
    bool used;
  };
  QList<Arrow> arrows_;
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
  if (!modelMode()) scope_.clear();
  while (!scope_.isEmpty() && graph().isEmpty()) scope_.removeLast();  // the group is gone
  showScope();
  QJsonObject g = graph();
  if (!g.isEmpty() && !hasPositions(g)) {
    // imported graphs have no layout yet: arrange them (not an edit of the document)
    const QHash<QString, QPointF> pos = modelMode() ? layoutModel(g) : layoutColumns(g);
    QJsonArray nodes = g["nodes"].toArray();
    for (int k = 0; k < nodes.size(); ++k) {
      QJsonObject n = nodes[k].toObject();
      const QPointF p = pos.value(n["id"].toString());
      n["position"] = QJsonArray{p.x(), p.y()};
      nodes[k] = n;
    }
    g["nodes"] = nodes;
    writeGraph(g);
  }
  if (!modelMode()) highlighted_.clear();
  rebuild();
  emit branchesChanged();
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
  const QHash<QString, QPointF> pos = modelMode() ? layoutModel(g) : layoutColumns(g);
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

int NodeEditor::linkAt(const QPoint& view_pos) const {
  for (QGraphicsItem* item : items(view_pos)) {
    for (QGraphicsItem* p = item; p != nullptr; p = p->parentItem()) {
      if (dynamic_cast<NodeItem*>(p) != nullptr) return -1;  // a block lies on top
      if (auto* l = dynamic_cast<LinkItem*>(p)) return l->index();
    }
  }
  return -1;
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
  connections_.clear();
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
    auto* item = new NodeItem(this, n, shownSockets(n), categories.value(type, type_category(type)),
                              problems.value(n["id"].toString()).trimmed(), conversions.value(n["id"].toString()),
                              n["id"].toString() == running_,
                              states_.contains(n["id"].toString()) ? (states_.value(n["id"].toString()) ? 1 : 0) : -1);
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
    if (modelMode()) {  // a light channel is drawn at its port (an observer: port -> display)
      if (sa == nullptr && a != nullptr) {
        sa = a->socket(portOfSocket(node_of(g, a->id()), l["from"].toArray().at(1).toString()));
      }
      if (sb == nullptr && b != nullptr) {
        sb = b->socket(portOfSocket(node_of(g, b->id()), l["to"].toArray().at(1).toString()));
      }
    }
    if (sa == nullptr || sb == nullptr) continue;  // reported by the check of the plugin
    const QString key = link_key(l);
    auto* link = new LinkItem(k, sa, sb, l["transport"].toString(QStringLiteral("auto")),
                              sa->kind() == QLatin1String("light"), planned_links[key].toObject(),
                              link_problems.value(key).trimmed());
    if (plan["stale"].toBool()) link->markStale();
    scene()->addItem(link);
    links_.append(link);
  }
  if (!modelMode()) return;
  // connections of two ports (R24): no direction; the planned representation per direction
  const QJsonObject planned_connections = plan["connections"].toObject();
  const QJsonArray connections = g["connections"].toArray();
  for (int k = 0; k < connections.size(); ++k) {
    const QJsonObject c = connections[k].toObject();
    const QJsonArray ends = c["ports"].toArray();
    NodeItem* a = nodes_.value(ends.at(0).toArray().at(0).toString());
    NodeItem* b = nodes_.value(ends.at(1).toArray().at(0).toString());
    SocketItem* sa = a != nullptr ? a->socket(ends.at(0).toArray().at(1).toString()) : nullptr;
    SocketItem* sb = b != nullptr ? b->socket(ends.at(1).toArray().at(1).toString()) : nullptr;
    if (sa == nullptr || sb == nullptr) continue;
    const bool per_direction = c["transport"].isObject();
    const QString fixed_forward = per_direction ? c["transport"].toObject()["forward"].toString() : c["transport"].toString(QStringLiteral("auto"));
    const QString fixed_back = per_direction ? c["transport"].toObject()["back"].toString() : fixed_forward;
    const QString transport = fixed_forward == fixed_back ? fixed_forward : fixed_forward + QLatin1Char('/') + fixed_back;
    const QJsonObject reps = planned_connections[QString::number(k)].toObject();
    const bool stale = plan["stale"].toBool();
    const QString name_a = node_of(g, ends.at(0).toArray().at(0).toString())["name"].toString(ends.at(0).toArray().at(0).toString());
    const QString name_b = node_of(g, ends.at(1).toArray().at(0).toString())["name"].toString(ends.at(1).toArray().at(0).toString());
    // which directions the branches use
    bool forward_used = false;
    bool back_used = false;
    for (const QJsonValue& br : g["branches"].toArray()) {
      const QJsonArray steps = br.toObject()["steps"].toArray();
      for (int s = 0; s + 1 < steps.size(); ++s) {
        const QJsonArray from{steps[s].toArray().at(0), steps[s].toArray().at(2)};
        const QJsonArray to{steps[s + 1].toArray().at(0), steps[s + 1].toArray().at(1)};
        forward_used = forward_used || (from == ends.at(0).toArray() && to == ends.at(1).toArray());
        back_used = back_used || (from == ends.at(1).toArray() && to == ends.at(0).toArray());
      }
    }
    // per direction: the planned representation, else a fixed transport (marked *)
    auto text_of = [&reps](const char* side, const QString& fixed, bool used) {
      if (!used) return QString();
      if (fixed != QLatin1String("auto")) return fixed + QStringLiteral("*");  // the planner keeps it
      return reps[QLatin1String(side)].toString();
    };
    const QString forward_text = text_of("forward", fixed_forward, forward_used);
    const QString back_text = text_of("back", fixed_back, back_used);
    QJsonObject planned;  // the tooltip
    auto line = [&reps, stale](const QString& from, const QString& to, const char* side, const QString& fixed, bool used) {
      QString r = reps[QLatin1String(side)].toString();
      QString t = QStringLiteral("%1 → %2: ").arg(from, to);
      if (!used) return t + tr_("no branch");
      t += r.isEmpty() ? QStringLiteral("–") : r + (stale ? tr_(" (out of date)") : QString());
      t += fixed == QLatin1String("auto") ? tr_(", transport auto") : tr_(", transport fixed %1").arg(fixed);
      return t;
    };
    planned["reason"] = line(name_a, name_b, "forward", fixed_forward, forward_used) + QLatin1Char('\n') +
                        line(name_b, name_a, "back", fixed_back, back_used);
    planned["representation"] = QString();
    // problems of the connection's checks are reported at "a.p — b.q"
    const QString where = QStringLiteral("%1.%2 — %3.%4").arg(ends.at(0).toArray().at(0).toString(), ends.at(0).toArray().at(1).toString(),
                                                            ends.at(1).toArray().at(0).toString(), ends.at(1).toArray().at(1).toString());
    QString problem;
    for (const QJsonValue& d : workflow_.value(QStringLiteral("diagnostics")).toArray()) {
      if (d.toObject()["location"].toString() == where) problem += d.toObject()["code"].toString() + QStringLiteral(": ") + d.toObject()["message"].toString() + QLatin1Char('\n');
    }
    auto* item = new LinkItem(k, sa, sb, transport, true, planned, problem.trimmed());
    item->setConnection(true);
    item->setToolTip(planned["reason"].toString() + (problem.isEmpty() ? QString() : QStringLiteral("\n\n") + problem.trimmed()));
    item->setDirections(forward_text, back_text, forward_used, back_used, stale);
    scene()->addItem(item);
    connections_.append(item);
  }
  // the highlighted branch and the branch being recorded
  auto mark = [this](const QJsonArray& steps, const QColor& color) {
    for (int k = 0; k < steps.size(); ++k) {
      const QJsonArray s = steps[k].toArray();
      if (NodeItem* n = nodes_.value(s.at(0).toString())) n->setPen(QPen(color, 3.0));
      if (k + 1 >= steps.size() || s.at(2).isNull()) continue;
      const QJsonArray next = steps[k + 1].toArray();
      for (LinkItem* c : connections_) {
        const QJsonArray ends = graph()["connections"].toArray().at(c->index()).toObject()["ports"].toArray();
        const QJsonArray from{s.at(0), s.at(2)};
        const QJsonArray to{next.at(0), next.at(1)};
        if (ends.at(0).toArray() == from && ends.at(1).toArray() == to) c->setHighlight(color, 0);
        if (ends.at(1).toArray() == from && ends.at(0).toArray() == to) c->setHighlight(color, 1);
      }
    }
  };
  if (!highlighted_.isEmpty()) {
    for (const QJsonValue& v : g["branches"].toArray()) {
      if (v.toObject()["name"].toString() == highlighted_) mark(v.toObject()["steps"].toArray(), QColor(80, 220, 255));
    }
  }
  if (recording_) mark(recorded_, QColor(255, 210, 80));
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
  for (LinkItem* c : connections_) c->update();  // connections of ports follow the blocks too
}

void NodeEditor::nodeMoved() {
  moved_ = true;
  updateLinks();
}

void NodeEditor::setGraph(const QJsonObject& graph) {
  // what changed (not the position): those blocks and everything behind them are out of date
  const QJsonObject before = this->graph();
  QSet<QString> seeds;
  QHash<QString, QJsonObject> old_nodes;
  for (const QJsonValue& v : before["nodes"].toArray()) {
    QJsonObject n = v.toObject();
    n.remove(QStringLiteral("position"));
    old_nodes.insert(n["id"].toString(), n);
  }
  for (const QJsonValue& v : graph["nodes"].toArray()) {
    QJsonObject n = v.toObject();
    n.remove(QStringLiteral("position"));
    if (old_nodes.value(n["id"].toString()) != n) seeds.insert(n["id"].toString());
  }
  const QJsonArray old_links = before["links"].toArray();
  const QJsonArray new_links = graph["links"].toArray();
  for (const QJsonValue& v : new_links) {
    if (!old_links.contains(v)) seeds.insert(v.toObject()["to"].toArray().at(0).toString());
  }
  for (const QJsonValue& v : old_links) {
    if (!new_links.contains(v)) seeds.insert(v.toObject()["to"].toArray().at(0).toString());
  }
  if (modelMode()) {  // connections (both ends) and branches (their blocks) that changed
    const QJsonArray old_c = before["connections"].toArray();
    const QJsonArray new_c = graph["connections"].toArray();
    for (const QJsonValue& v : new_c) {
      if (old_c.contains(v)) continue;
      for (const QJsonValue& e : v.toObject()["ports"].toArray()) seeds.insert(e.toArray().at(0).toString());
    }
    for (const QJsonValue& v : old_c) {
      if (new_c.contains(v)) continue;
      for (const QJsonValue& e : v.toObject()["ports"].toArray()) seeds.insert(e.toArray().at(0).toString());
    }
    const QJsonArray old_b = before["branches"].toArray();
    const QJsonArray new_b = graph["branches"].toArray();
    for (const QJsonArray* list : {&old_b, &new_b}) {
      for (const QJsonValue& v : *list) {
        if (old_b.contains(v) && new_b.contains(v)) continue;
        const QJsonArray steps = v.toObject()["steps"].toArray();
        if (!steps.isEmpty()) seeds.insert(steps.last().toArray().at(0).toString());  // its detector
      }
    }
  }
  writeGraph(graph);
  if (modelMode()) {
    if (!states_.isEmpty() && !seeds.isEmpty()) invalidateModel(seeds);
  } else {
    invalidate(seeds);
  }
  workflow_.remove(QStringLiteral("diagnostics"));  // stale after an edit; the next check renews it
  if (workflow_.contains(QStringLiteral("plan"))) {  // the last plan stays visible, marked out of date
    QJsonObject plan = workflow_["plan"].toObject();
    plan["stale"] = true;
    workflow_["plan"] = plan;
  }
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
  writeGraph(g);
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
  if (modelMode()) {  // its connections go; branches through it stay (the check reports them)
    QJsonArray connections;
    for (const QJsonValue& v : g["connections"].toArray()) {
      bool touches = false;
      for (const QJsonValue& e : v.toObject()["ports"].toArray()) touches = touches || e.toArray().at(0).toString() == id;
      if (!touches) connections.append(v);
    }
    g["connections"] = connections;
  }
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

namespace {
/// Socket map old -> new for the linked sockets of `id`; empty optional if not unique.
std::optional<QHash<QString, QString>> socket_map(const QJsonArray& old_sockets, const QJsonArray& new_sockets,
                                                  const QSet<QString>& linked) {
  QHash<QString, QString> map;
  QSet<QString> names;
  for (const QJsonValue& s : new_sockets) names.insert(s.toObject()["name"].toString());
  for (const QJsonValue& v : old_sockets) {
    const QJsonObject o = v.toObject();
    const QString name = o["name"].toString();
    if (!linked.contains(name)) continue;
    if (names.contains(name)) {
      map[name] = name;
      continue;
    }
    QStringList fit;
    for (const QJsonValue& s : new_sockets) {
      const QJsonObject n = s.toObject();
      if (n["direction"] == o["direction"] && n["kind"] == o["kind"]) fit << n["name"].toString();
    }
    if (fit.size() != 1) return std::nullopt;
    map[name] = fit.front();
  }
  QSet<QString> targets(map.begin(), map.end());
  if (targets.size() != map.size()) return std::nullopt;
  return map;
}
}  // namespace

namespace {
/// Ports with a connection of block `id` (R24).
QSet<QString> connected_ports(const QJsonObject& model, const QString& id) {
  QSet<QString> out;
  for (const QJsonValue& v : model["connections"].toArray()) {
    for (const QJsonValue& e : v.toObject()["ports"].toArray()) {
      if (e.toArray().at(0).toString() == id) out.insert(e.toArray().at(1).toString());
    }
  }
  return out;
}

/// Port map old -> new for the connected ports: the same name, else the only port of the new
/// type; empty optional if not unique (or two would share a port).
std::optional<QHash<QString, QString>> port_map(const QJsonArray& new_ports, const QSet<QString>& connected) {
  QStringList names;
  for (const QJsonValue& p : new_ports) names << p.toObject()["name"].toString();
  QHash<QString, QString> map;
  for (const QString& old : connected) {
    if (names.contains(old)) {
      map[old] = old;
    } else if (names.size() == 1) {
      map[old] = names.first();
    } else {
      return std::nullopt;
    }
  }
  QSet<QString> used;
  for (const QString& v : map) {
    if (used.contains(v)) return std::nullopt;
    used.insert(v);
  }
  return map;
}
}  // namespace

QStringList NodeEditor::replacementsFor(const QString& id) const {
  const QJsonObject g = graph();
  const QJsonObject old = node_of(g, id);
  if (modelMode()) {
    QStringList out;
    const QSet<QString> connected = connected_ports(g, id);
    for (const QJsonValue& v : workflow_.value(QStringLiteral("types")).toArray()) {
      const QJsonObject t = v.toObject();
      if (t["type"].toString() == old["type"].toString() || t["ports"].toArray().isEmpty()) continue;
      if (t["category"].toString() == QLatin1String("meta") || t["category"].toString() == QLatin1String("analysis")) continue;
      if (port_map(t["ports"].toArray(), connected)) out << t["type"].toString();
    }
    return out;
  }
  QSet<QString> linked;
  for (const QJsonValue& v : g["links"].toArray()) {
    const QJsonObject l = v.toObject();
    if (l["from"].toArray().at(0).toString() == id) linked.insert(l["from"].toArray().at(1).toString());
    if (l["to"].toArray().at(0).toString() == id) linked.insert(l["to"].toArray().at(1).toString());
  }
  QStringList out;
  for (const QJsonValue& v : workflow_.value(QStringLiteral("types")).toArray()) {
    const QJsonObject t = v.toObject();
    if (t["type"].toString() == old["type"].toString()) continue;
    if (t["sockets"].toArray().isEmpty()) continue;  // types with their own sockets (imported)
    if (socket_map(socketsOf(old), t["sockets"].toArray(), linked)) out << t["type"].toString();
  }
  return out;
}

bool NodeEditor::replaceNode(const QString& id, const QString& type) {
  QJsonObject g = graph();
  const QJsonObject old = node_of(g, id);
  const QJsonObject t = type_of(workflow_, type);
  if (old.isEmpty() || t.isEmpty()) return false;
  if (modelMode()) {  // R24: the connections and the branch steps move to the new ports
    const auto map = port_map(t["ports"].toArray(), connected_ports(g, id));
    if (!map) return false;
    QJsonObject node{{"id", id}, {"type", type}, {"position", old["position"]}, {"ports", t["ports"]}, {"sockets", t["sockets"]}};
    if (old.contains("name")) node["name"] = old["name"];
    QJsonObject params;
    for (const QJsonValue& p : t["params"].toArray()) params[p.toObject()["name"].toString()] = p.toObject()["default"];
    if (!params.isEmpty()) node["params"] = params;
    QJsonArray nodes = g["nodes"].toArray();
    for (int k = 0; k < nodes.size(); ++k) {
      if (nodes[k].toObject()["id"].toString() == id) nodes[k] = node;
    }
    QJsonArray connections = g["connections"].toArray();
    for (int k = 0; k < connections.size(); ++k) {
      QJsonObject c = connections[k].toObject();
      QJsonArray ends = c["ports"].toArray();
      for (int e = 0; e < 2; ++e) {
        QJsonArray end = ends[e].toArray();
        if (end.at(0).toString() == id) end[1] = map->value(end.at(1).toString(), end.at(1).toString());
        ends[e] = end;
      }
      c["ports"] = ends;
      connections[k] = c;
    }
    QJsonArray branches = g["branches"].toArray();
    for (int k = 0; k < branches.size(); ++k) {
      QJsonObject b = branches[k].toObject();
      QJsonArray steps = b["steps"].toArray();
      for (int s = 0; s < steps.size(); ++s) {
        QJsonArray step = steps[s].toArray();
        if (step.at(0).toString() != id) continue;
        for (int i : {1, 2}) {
          if (!step.at(i).isNull()) step[i] = map->value(step.at(i).toString(), step.at(i).toString());
        }
        while (step.size() > 3) step.removeLast();  // a channel of the old block means nothing here
        steps[s] = step;
      }
      b["steps"] = steps;
      branches[k] = b;
    }
    QJsonArray links;  // data links of the old block that the new one has no socket for go
    QSet<QString> sockets;
    for (const QJsonValue& s : t["sockets"].toArray()) sockets.insert(s.toObject()["name"].toString());
    for (const QJsonValue& v : g["links"].toArray()) {
      const QJsonObject l = v.toObject();
      const bool from = l["from"].toArray().at(0).toString() == id;
      const bool to = l["to"].toArray().at(0).toString() == id;
      if ((from && !sockets.contains(l["from"].toArray().at(1).toString())) ||
          (to && !sockets.contains(l["to"].toArray().at(1).toString()))) {
        continue;
      }
      links.append(v);
    }
    g["nodes"] = nodes;
    g["connections"] = connections;
    g["branches"] = branches;
    g["links"] = links;
    setGraph(g);
    return true;
  }
  QSet<QString> linked;
  for (const QJsonValue& v : g["links"].toArray()) {
    const QJsonObject l = v.toObject();
    if (l["from"].toArray().at(0).toString() == id) linked.insert(l["from"].toArray().at(1).toString());
    if (l["to"].toArray().at(0).toString() == id) linked.insert(l["to"].toArray().at(1).toString());
  }
  const auto map = socket_map(socketsOf(old), t["sockets"].toArray(), linked);
  if (!map) return false;
  QJsonObject node{{"id", id}, {"type", type}, {"position", old["position"]}};
  if (old.contains("name")) node["name"] = old["name"];
  QJsonObject params;
  for (const QJsonValue& p : t["params"].toArray()) params[p.toObject()["name"].toString()] = p.toObject()["default"];
  if (!params.isEmpty()) node["params"] = params;
  QJsonArray nodes = g["nodes"].toArray();
  for (int k = 0; k < nodes.size(); ++k) {
    if (nodes[k].toObject()["id"].toString() == id) nodes[k] = node;
  }
  QJsonArray links = g["links"].toArray();
  for (int k = 0; k < links.size(); ++k) {
    QJsonObject l = links[k].toObject();
    for (const char* end : {"from", "to"}) {
      QJsonArray e = l[QLatin1String(end)].toArray();
      if (e.at(0).toString() == id) e[1] = map->value(e.at(1).toString(), e.at(1).toString());
      l[QLatin1String(end)] = e;
    }
    links[k] = l;
  }
  g["nodes"] = nodes;
  g["links"] = links;
  setGraph(g);
  return true;
}

void NodeEditor::setStates(const QJsonObject& states) {
  states_.clear();
  for (auto it = states.begin(); it != states.end(); ++it) states_.insert(it.key(), it.value().toBool());
  rebuild();
}

int NodeEditor::state(const QString& id) const {
  return states_.contains(id) ? (states_.value(id) ? 1 : 0) : -1;
}

void NodeEditor::invalidate(const QSet<QString>& seeds) {
  if (states_.isEmpty() || seeds.isEmpty()) return;
  const QJsonObject g = graph();
  // outputs of a block and, per output, the inputs it depends on ("through" of the block or of
  // its type; without it every output depends on every input)
  auto outputs = [this, &g](const QString& id) {
    QStringList out;
    for (const QJsonValue& s : socketsOf(node_of(g, id))) {
      if (s.toObject()["direction"].toString() == QLatin1String("out")) out << s.toObject()["name"].toString();
    }
    return out;
  };
  auto depends = [this, &g](const QString& id, const QString& out, const QString& in) {
    const QJsonObject n = node_of(g, id);
    QJsonObject through = n["through"].toObject();
    if (through.isEmpty()) through = type_of(workflow_, n["type"].toString())["through"].toObject();
    if (through.isEmpty() || !through.contains(out)) return through.isEmpty();
    return through[out].toArray().contains(in);
  };
  auto is_drive = [this, &g](const QString& id, const QString& socket) {
    for (const QJsonValue& s : socketsOf(node_of(g, id))) {
      const QJsonObject o = s.toObject();
      if (o["name"].toString() == socket) return o["accepts"].toArray().contains(QStringLiteral("drive"));
    }
    return false;
  };
  QSet<QString> stale(seeds.begin(), seeds.end());
  QSet<QString> done;
  QStringList todo;  // "node\nsocket" of out-of-date outputs
  for (const QString& id : seeds) {
    for (const QString& o : outputs(id)) todo << id + QLatin1Char('\n') + o;
  }
  while (!todo.isEmpty()) {
    const QString item = todo.takeLast();
    if (done.contains(item)) continue;
    done.insert(item);
    const QString id = item.section(QLatin1Char('\n'), 0, 0);
    const QString socket = item.section(QLatin1Char('\n'), 1);
    if (is_drive(id, socket)) continue;  // a drive carries no value between runs
    for (const QJsonValue& v : g["links"].toArray()) {
      const QJsonObject l = v.toObject();
      if (l["from"].toArray().at(0).toString() != id || l["from"].toArray().at(1).toString() != socket) continue;
      const QString to = l["to"].toArray().at(0).toString();
      const QString in = l["to"].toArray().at(1).toString();
      const QStringList outs = outputs(to);
      bool any = outs.isEmpty();
      for (const QString& o : outs) {
        if (depends(to, o, in)) {
          todo << to + QLatin1Char('\n') + o;
          any = true;
        }
      }
      if (any) stale.insert(to);
    }
  }
  for (const QString& id : stale) states_.insert(id, false);
}

void NodeEditor::setRunning(const QString& id) {
  if (id == running_) return;
  running_ = id;
  rebuild();
}

namespace {
/// The parts of a JSON pointer ("/a/b~1c" -> a, b/c).
QStringList pointer_parts(const QString& pointer) {
  QStringList parts = pointer.mid(1).split(QLatin1Char('/'));
  for (QString& p : parts) p = p.replace(QStringLiteral("~1"), QStringLiteral("/")).replace(QStringLiteral("~0"), QStringLiteral("~"));
  return parts;
}

QJsonValue value_at(const QJsonValue& root, const QStringList& parts, int k = 0) {
  if (k == parts.size()) return root;
  if (root.isObject()) return value_at(root.toObject()[parts[k]], parts, k + 1);
  if (root.isArray()) return value_at(root.toArray().at(parts[k].toInt()), parts, k + 1);
  return {};
}

QJsonValue with_value(const QJsonValue& root, const QStringList& parts, const QJsonValue& value, int k = 0) {
  if (k == parts.size()) return value;
  if (root.isArray()) {
    QJsonArray a = root.toArray();
    const int i = parts[k].toInt();
    while (a.size() <= i) a.append(QJsonValue());
    a[i] = with_value(a.at(i), parts, value, k + 1);
    return a;
  }
  QJsonObject o = root.toObject();
  o[parts[k]] = with_value(o[parts[k]], parts, value, k + 1);
  return o;
}
}  // namespace

void NodeEditor::setParams(const QString& id, const QJsonObject& params) {
  QJsonObject g = graph();
  QJsonArray nodes = g["nodes"].toArray();
  for (int k = 0; k < nodes.size(); ++k) {
    QJsonObject n = nodes[k].toObject();
    if (n["id"].toString() != id) continue;
    QJsonObject p = n["params"].toObject();
    for (auto it = params.begin(); it != params.end(); ++it) {
      if (it.key().startsWith(QLatin1Char('/'))) {  // a JSON pointer into the params (fields)
        p = with_value(p, pointer_parts(it.key()), it.value()).toObject();
      } else {
        p[it.key()] = it.value();
      }
    }
    n["params"] = p;
    nodes[k] = n;
  }
  g["nodes"] = nodes;
  setGraph(g);
}

void NodeEditor::editParams(const QString& id) {
  const QJsonObject node = node_of(graph(), id);
  const QJsonObject t = type_of(workflow_, node["type"].toString());
  // the type's parameters and the fields the plugin describes for this node (ADR Nachtrag 1:
  // e.g. the radii, apertures, poses and glasses of a Raytatouille block), grouped
  QJsonArray specs;
  for (const QJsonValue& v : t["params"].toArray()) {
    if (v.toObject()["type"].toString() != QLatin1String("data")) specs.append(v);
  }
  for (const QJsonValue& v : node["fields"].toArray()) specs.append(v);
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Parameters of %1").arg(node["name"].toString(id)));
  auto* outer = new QVBoxLayout(&dialog);
  QTabWidget* tabs = node["fields"].toArray().isEmpty() ? nullptr : new QTabWidget(&dialog);
  QHash<QString, QFormLayout*> forms;
  auto form_of = [&](const QString& group) -> QFormLayout* {
    const QString name = group.isEmpty() ? tr("Block") : group;
    if (QFormLayout* f = forms.value(name)) return f;
    auto* page = new QWidget;
    auto* f = new QFormLayout(page);
    if (tabs != nullptr) {
      auto* scroll = new QScrollArea;
      scroll->setWidgetResizable(true);
      scroll->setWidget(page);
      tabs->addTab(scroll, name);
    } else {
      outer->addWidget(page);
    }
    forms.insert(name, f);
    return f;
  };
  if (tabs != nullptr) {
    outer->addWidget(tabs);
    dialog.resize(560, 520);
  }
  const QJsonObject current = node["params"].toObject();
  std::vector<std::function<void(QJsonObject&)>> readers;
  for (const QJsonValue& v : specs) {
    const QJsonObject p = v.toObject();
    const QString name = p["name"].toString();
    const QString type = p["type"].toString();
    const bool pointer = name.startsWith(QLatin1Char('/'));
    const QJsonValue value = pointer ? value_at(current, pointer_parts(name))
                                     : current.contains(name) ? current[name] : p["default"];
    QFormLayout* form = form_of(p["group"].toString());
    QString label = p["label"].toString(name);
    if (!p["unit"].toString().isEmpty()) label += QStringLiteral(" (%1)").arg(p["unit"].toString());
    QWidget* editor = nullptr;
    if (type == QLatin1String("boolean")) {
      auto* box = new QCheckBox(&dialog);
      box->setChecked(value.toBool());
      readers.emplace_back([box, name, init = box->isChecked()](QJsonObject& o) {
        if (box->isChecked() != init) o[name] = box->isChecked();
      });
      editor = box;
    } else if (type == QLatin1String("integer")) {
      auto* spin = new QSpinBox(&dialog);
      spin->setRange(p.contains("minimum") ? p["minimum"].toInt() : -1000000000,
                     p.contains("maximum") ? p["maximum"].toInt() : 1000000000);
      spin->setValue(value.toInt());
      readers.emplace_back([spin, name, init = spin->value()](QJsonObject& o) {
        if (spin->value() != init) o[name] = spin->value();
      });
      editor = spin;
    } else if (type == QLatin1String("number")) {
      auto* line = new QLineEdit(QString::number(value.toDouble(), 'g', 12), &dialog);
      line->setValidator(new QDoubleValidator(line));
      // only what was edited is written back: a value with more digits than shown stays exact
      readers.emplace_back([line, name, init = line->text()](QJsonObject& o) {
        if (line->text() != init) o[name] = line->text().toDouble();
      });
      editor = line;
    } else if (type == QLatin1String("numbers") || type == QLatin1String("point")) {
      QStringList parts;
      for (const QJsonValue& x : value.toArray()) {
        parts << (x.isArray() ? QString::fromUtf8(QJsonDocument(x.toArray()).toJson(QJsonDocument::Compact))
                              : QString::number(x.toDouble(), 'g', 12));
      }
      auto* line = new QLineEdit(parts.join(QStringLiteral(", ")), &dialog);
      line->setPlaceholderText(tr("comma-separated numbers"));
      readers.emplace_back([line, name, init = line->text()](QJsonObject& o) {
        if (line->text() == init) return;
        const QJsonDocument doc = QJsonDocument::fromJson("[" + line->text().toUtf8() + "]");
        o[name] = doc.isArray() ? doc.array() : QJsonArray{};
      });
      editor = line;
    } else if (type == QLatin1String("file")) {
      auto* row = new QWidget(&dialog);
      auto* h = new QHBoxLayout(row);
      h->setContentsMargins(0, 0, 0, 0);
      auto* line = new QLineEdit(value.toString(), row);
      auto* browse = new QPushButton(tr("…"), row);
      h->addWidget(line, 1);
      h->addWidget(browse);
      connect(browse, &QPushButton::clicked, &dialog, [line, &dialog] {
        const QString f = QFileDialog::getOpenFileName(&dialog, tr("Choose a file"), line->text());
        if (!f.isEmpty()) line->setText(f);
      });
      readers.emplace_back([line, name, init = line->text()](QJsonObject& o) {
        if (line->text() != init) o[name] = line->text();
      });
      editor = row;
    } else {
      auto* line = new QLineEdit(value.isString() ? value.toString()
                                                  : QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1),
                                 &dialog);
      readers.emplace_back([line, name, init = line->text()](QJsonObject& o) {
        if (line->text() != init) o[name] = line->text();
      });
      editor = line;
    }
    editor->setToolTip(p["description"].toString(name));
    editor->setObjectName(name);
    form->addRow(label, editor);
  }
  if (specs.isEmpty()) form_of({})->addRow(new QLabel(tr("This block has no parameters."), &dialog));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  outer->addWidget(buttons);
  if (dialog.exec() != QDialog::Accepted || specs.isEmpty()) return;
  QJsonObject params;
  for (auto& read : readers) read(params);
  if (!params.isEmpty()) setParams(id, params);
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
  if (modelMode()) {  // the editor draws ports from the node (R24)
    const QJsonObject t = type_of(workflow_, type);
    if (!t["ports"].toArray().isEmpty()) node["ports"] = t["ports"];
    if (!t["sockets"].toArray().isEmpty()) node["sockets"] = t["sockets"];
  }
  QJsonArray nodes = g["nodes"].toArray();
  nodes.append(node);
  g["nodes"] = nodes;
  if (!g.contains("format")) {
    g["format"] = QStringLiteral("rtt-fusion-graph");
    g["version"] = modelMode() ? 2 : 1;
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
  if (event->button() == Qt::LeftButton && recording_) {  // recording a branch (R24)
    for (QGraphicsItem* item : items(event->position().toPoint())) {
      if (auto* s = dynamic_cast<SocketItem*>(item); s != nullptr && s->isPort()) {
        if (!recordPort(s->node(), s->name())) recordDetector(s->node());
        return;
      }
      for (QGraphicsItem* p = item; p != nullptr; p = p->parentItem()) {
        if (auto* node = dynamic_cast<NodeItem*>(p)) {
          recordDetector(node->id());
          return;
        }
      }
    }
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
      if (from->isPort() && to->isPort()) {  // R24: two ports; to a display: an observer link
        SocketItem* shown = categoryOf(to->node()) == QLatin1String("analysis") ? to
                            : categoryOf(from->node()) == QLatin1String("analysis") ? from : nullptr;
        if (shown == nullptr) {
          addConnection(from->node(), from->name(), to->node(), to->name());
        } else {
          SocketItem* seen = shown == to ? from : to;
          if (!seen->outSocket().isEmpty() && !shown->inSocket().isEmpty()) {
            addLink(seen->node(), seen->outSocket(), shown->node(), shown->inSocket());
          }
        }
        break;
      }
      if (from->isPort() || to->isPort()) break;  // a port with a data socket: refused
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
  if (event->button() == Qt::LeftButton) {
    for (QGraphicsItem* item : items(event->position().toPoint())) {
      for (QGraphicsItem* p = item; p != nullptr; p = p->parentItem()) {
        if (auto* node = dynamic_cast<NodeItem*>(p)) {
          const QJsonObject n = node_of(graph(), node->id());
          if (n["type"].toString().startsWith(QLatin1String("display."))) {
            emit displayRequested(node->id(), n["type"].toString(), n["name"].toString(node->id()));
            return;
          }
          if (n.contains("variants")) {  // a group: show its inside
            enterGroup(node->id());
            return;
          }
          editParams(node->id());
          return;
        }
      }
    }
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

bool NodeEditor::event(QEvent* event) {
  // Z E / Z S / Home belong to the node editor while it has the focus, not to the main
  // window's shortcuts for the CAD views
  if (event->type() == QEvent::ShortcutOverride) {
    auto* key = static_cast<QKeyEvent*>(event);
    const bool plain = (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
    // Escape too while it means something here (back out of a group, cancel a recording) – the
    // main window's Escape (no tool) would take it first
    const bool escape = key->key() == Qt::Key_Escape && (recording_ || !scope_.isEmpty());
    if (plain && (escape || key->key() == Qt::Key_Z || key->key() == Qt::Key_Home ||
                  (z_pressed_ && (key->key() == Qt::Key_E || key->key() == Qt::Key_S)))) {
      event->accept();
      return true;
    }
  }
  return QGraphicsView::event(event);
}

void NodeEditor::zoomSelected() {
  QRectF rect;
  for (QGraphicsItem* item : scene()->selectedItems()) {
    if (dynamic_cast<NodeItem*>(item) != nullptr) rect = rect.united(item->sceneBoundingRect());
  }
  if (rect.isNull()) {
    fitAll();
    return;
  }
  fitInView(rect.adjusted(-30, -30, 30, 30), Qt::KeepAspectRatio);
  if (zoom() > 8.0) {  // the zoom limit of the editor
    const QPointF c = rect.center();
    resetTransform();
    scale(8.0, 8.0);
    centerOn(c);
  }
}

void NodeEditor::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape && recording_) {
    cancelBranch();
    return;
  }
  if (event->key() == Qt::Key_Escape && !scope_.isEmpty()) {
    leaveGroup();
    return;
  }
  // zoom extents: Z E (as in the CAD views); Home as well; zoom selected: Z S
  if (event->key() == Qt::Key_Home || (z_pressed_ && event->key() == Qt::Key_E)) {
    z_pressed_ = false;
    fitAll();
    return;
  }
  if (z_pressed_ && event->key() == Qt::Key_S) {
    z_pressed_ = false;
    zoomSelected();
    return;
  }
  z_pressed_ = event->key() == Qt::Key_Z && event->modifiers() == Qt::NoModifier;
  if (z_pressed_) return;
  QGraphicsView::keyPressEvent(event);
}

bool NodeEditor::eventFilter(QObject* watched, QEvent* event) {
  if (watched == crumb_ && event->type() == QEvent::MouseButtonRelease) {  // the level's name
    leaveGroup();
    return true;
  }
  return QGraphicsView::eventFilter(watched, event);
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
  if (const int c = connectionAt(view_pos); c >= 0) {  // a connection of two ports (R24)
    const QJsonArray ends = graph()["connections"].toArray().at(c).toObject()["ports"].toArray();
    auto block_name = [this](const QJsonValue& end) {
      const QString id = end.toArray().at(0).toString();
      return node_of(graph(), id)["name"].toString(id);
    };
    QMenu* both = menu.addMenu(tr("Transport (both directions)"));
    QMenu* forward = menu.addMenu(tr("Transport %1 → %2").arg(block_name(ends.at(0)), block_name(ends.at(1))));
    QMenu* back = menu.addMenu(tr("Transport %1 → %2").arg(block_name(ends.at(1)), block_name(ends.at(0))));
    for (const char* t : {"auto", "R", "H", "G", "S", "T", "P"}) {
      const QString name = QString::fromLatin1(t);
      both->addAction(name, this, [this, c, name] { setConnectionTransport(c, QStringLiteral("both"), name); });
      forward->addAction(name, this, [this, c, name] { setConnectionTransport(c, QStringLiteral("forward"), name); });
      back->addAction(name, this, [this, c, name] { setConnectionTransport(c, QStringLiteral("back"), name); });
    }
    menu.addAction(tr("Delete connection"), this, [this, c] { removeConnection(c); });
    menu.exec(global_pos);
    return;
  }
  for (QGraphicsItem* item : items(view_pos)) {
    for (QGraphicsItem* p = item; p != nullptr && node == nullptr; p = p->parentItem()) node = dynamic_cast<NodeItem*>(p);
    for (QGraphicsItem* p = item; p != nullptr && link == nullptr; p = p->parentItem()) {
      link = dynamic_cast<LinkItem*>(p);  // also its label
    }
    if (node != nullptr) break;
  }
  if (node != nullptr) {
    const QString id = node->id();
    bool muted = false;
    for (const QJsonValue& v : graph()["nodes"].toArray()) {
      if (v.toObject()["id"].toString() == id) muted = v.toObject()["muted"].toBool();
    }
    menu.addAction(muted ? tr("Unmute") : tr("Mute (bypass)"), this, [this, id, muted] { setMuted(id, !muted); });
    const QJsonObject self = node_of(graph(), id);
    if (self["type"].toString().startsWith(QLatin1String("display."))) {
      menu.addAction(tr("Show window"), this, [this, id, self] {
        emit displayRequested(id, self["type"].toString(), self["name"].toString(id));
      });
    }
    if (self.contains("variants")) {  // a group (R28)
      menu.addAction(tr("Open group"), this, [this, id] { enterGroup(id); });
      QMenu* variant = menu.addMenu(tr("Variant"));
      for (const QString& v : variants(id)) {
        QAction* a = variant->addAction(v, this, [this, id, v] { setVariant(id, v); });
        a->setCheckable(true);
        a->setChecked(v == self["variant"].toString());
      }
      QAction* condensed = menu.addAction(tr("Condensed (plane-wave tables)"), this,
                                          [this, id, self] { setCondensed(id, !self.contains("condense")); });
      condensed->setCheckable(true);
      condensed->setChecked(self.contains("condense"));
      menu.addAction(tr("Ungroup"), this, [this, id] {
        QString why;
        if (!ungroup(id, &why)) QMessageBox::information(this, tr("Ungroup"), why);
      });
      menu.addSeparator();
    }
    menu.addAction(tr("Parameters…"), this, [this, id] { editParams(id); });
    if (modelMode()) {
      menu.addAction(tr("Group selected blocks…"), this, [this, id] {
        QStringList ids;
        for (QGraphicsItem* item : scene()->selectedItems()) {
          if (auto* n = dynamic_cast<NodeItem*>(item)) ids << n->id();
        }
        if (!ids.contains(id)) ids << id;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Group"), tr("Name of the group:"), QLineEdit::Normal, tr("Group"), &ok);
        if (!ok) return;
        QString why;
        if (groupBlocks(ids, name, &why).isEmpty()) QMessageBox::information(this, tr("Group"), why);
      });
    }
    QMenu* replace = menu.addMenu(tr("Replace with"));
    for (const QString& type : replacementsFor(id)) {
      QAction* a = replace->addAction(type, this, [this, id, type] { replaceNode(id, type); });
      a->setToolTip(type_of(workflow_, type)["description"].toString());
    }
    replace->setEnabled(!replace->isEmpty());
    replace->setToolTipsVisible(true);
    menu.addAction(tr("Delete block"), this, [this, id] { removeNode(id); });
    menu.addSeparator();
    menu.addAction(tr("Zoom selected"), this, [this, node] {
      if (!node->isSelected()) {
        scene()->clearSelection();
        node->setSelected(true);
      }
      zoomSelected();
    });
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
    if (modelMode()) menu.addAction(tr("New branch…"), this, &NodeEditor::beginBranch);
    if (!scope_.isEmpty()) menu.addAction(tr("Back out of the group"), this, &NodeEditor::leaveGroup);
    menu.addAction(tr("Arrange"), this, &NodeEditor::arrange);
    menu.addAction(tr("Zoom selected"), this, &NodeEditor::zoomSelected)->setEnabled(!scene()->selectedItems().isEmpty());
    menu.addAction(tr("Show all"), this, &NodeEditor::fitAll);
  }
  menu.exec(global_pos);
}

// ------------------------------------------------------------------------------ model (R24)

QJsonArray NodeEditor::portsOf(const QJsonObject& node) const {
  if (node.contains("variants")) {  // a group: the exported ports of its active variant
    QJsonArray out;
    const QJsonObject body = node["variants"].toObject()[node["variant"].toString()].toObject();
    for (const QString& p : body["ports"].toObject().keys()) out.append(QJsonObject{{"name", p}, {"in", p}, {"out", p}});
    return out;
  }
  if (node.contains("ports") && !node["ports"].toArray().isEmpty()) return node["ports"].toArray();
  return type_of(workflow_, node["type"].toString())["ports"].toArray();
}

QString NodeEditor::portOfSocket(const QJsonObject& node, const QString& socket) const {
  for (const QJsonValue& v : portsOf(node)) {
    const QJsonObject p = v.toObject();
    if (p["in"].toString() == socket || p["out"].toString() == socket) return p["name"].toString();
  }
  return {};
}

QString NodeEditor::categoryOf(const QString& node) const {
  if (node_of(graph(), node).contains("variants")) return QStringLiteral("group");
  const QString type = node_of(graph(), node)["type"].toString();
  const QString c = type_of(workflow_, type)["category"].toString();
  return c.isEmpty() ? type_category(type) : c;
}

bool NodeEditor::portOnLeft(const QString& node, const QString& port) const {
  // left: a port light comes in by at the block's first visit in some branch; every other port
  // of a block on a branch right (light leaves there, or comes back)
  bool on_branch = false;
  for (const QJsonValue& b : graph()["branches"].toArray()) {
    for (const QJsonValue& v : b.toObject()["steps"].toArray()) {
      const QJsonArray s = v.toArray();
      if (s.at(0).toString() != node) continue;
      on_branch = true;
      if (s.at(1).toString() == port) return true;
      break;  // only the first visit decides
    }
  }
  if (on_branch) return false;
  for (const QJsonValue& v : portsOf(node_of(graph(), node))) {
    const QJsonObject p = v.toObject();
    if (p["name"].toString() != port) continue;
    if (p["out"].toString().isEmpty()) return true;
    if (p["in"].toString().isEmpty()) return false;
    return p["side"].toString() != QLatin1String("back");
  }
  return true;
}

QJsonArray NodeEditor::shownSockets(const QJsonObject& node) const {
  const QJsonArray sockets = socketsOf(node);
  if (!modelMode()) return sockets;
  QJsonArray out;
  for (const QJsonValue& s : sockets) {
    if (s.toObject()["kind"].toString() != QLatin1String("light")) out.append(s);
  }
  const QString id = node["id"].toString();
  for (const QJsonValue& v : portsOf(node)) {
    const QJsonObject p = v.toObject();
    const QString name = p["name"].toString();
    out.append(QJsonObject{{"name", name},
                           {"kind", "light"},
                           {"port", true},
                           {"direction", portOnLeft(id, name) ? "in" : "out"},
                           {"accepts", QJsonArray{}},
                           {"label", p["label"].toString(name)},
                           {"in", p["in"]},
                           {"out", p["out"]}});
  }
  return out;
}

int NodeEditor::connectionCount() const { return graph()["connections"].toArray().size(); }

QGraphicsPathItem* NodeEditor::connectionItem(int index) const {
  for (LinkItem* c : connections_) {
    if (c->index() == index) return c;
  }
  return nullptr;
}

int NodeEditor::connectionAt(const QPoint& view_pos) const {
  for (QGraphicsItem* item : items(view_pos)) {
    for (QGraphicsItem* p = item; p != nullptr; p = p->parentItem()) {
      if (dynamic_cast<NodeItem*>(p) != nullptr) return -1;
      if (auto* l = dynamic_cast<LinkItem*>(p)) return l->isConnection() ? l->index() : -1;
    }
  }
  return -1;
}

bool NodeEditor::addConnection(const QString& a, const QString& port_a, const QString& b, const QString& port_b) {
  if (a == b) return false;
  QJsonObject g = graph();
  QJsonArray connections = g["connections"].toArray();
  for (const QJsonValue& v : connections) {  // a port has at most one connection
    for (const QJsonValue& e : v.toObject()["ports"].toArray()) {
      if (e.toArray() == QJsonArray{a, port_a} || e.toArray() == QJsonArray{b, port_b}) return false;
    }
  }
  connections.append(QJsonObject{{"ports", QJsonArray{QJsonArray{a, port_a}, QJsonArray{b, port_b}}}});
  g["connections"] = connections;
  setGraph(g);
  return true;
}

void NodeEditor::removeConnection(int index) {
  QJsonObject g = graph();
  QJsonArray connections = g["connections"].toArray();
  if (index < 0 || index >= connections.size()) return;
  connections.removeAt(index);
  g["connections"] = connections;
  setGraph(g);
}

void NodeEditor::setConnectionTransport(int index, const QString& direction, const QString& transport) {
  QJsonObject g = graph();
  QJsonArray connections = g["connections"].toArray();
  if (index < 0 || index >= connections.size()) return;
  QJsonObject c = connections[index].toObject();
  QString forward = c["transport"].isObject() ? c["transport"].toObject()["forward"].toString() : c["transport"].toString(QStringLiteral("auto"));
  QString back = c["transport"].isObject() ? c["transport"].toObject()["back"].toString() : forward;
  if (direction != QLatin1String("back")) forward = transport;
  if (direction != QLatin1String("forward")) back = transport;
  if (forward == back) {
    if (forward == QLatin1String("auto")) {
      c.remove(QStringLiteral("transport"));
    } else {
      c["transport"] = forward;
    }
  } else {
    c["transport"] = QJsonObject{{"forward", forward}, {"back", back}};
  }
  connections[index] = c;
  g["connections"] = connections;
  setGraph(g);
}

QJsonArray NodeEditor::branchSteps(const QString& name) const {
  for (const QJsonValue& v : graph()["branches"].toArray()) {
    if (v.toObject()["name"].toString() == name) return v.toObject()["steps"].toArray();
  }
  return {};
}

QStringList NodeEditor::branchNames() const {
  QStringList out;
  for (const QJsonValue& v : graph()["branches"].toArray()) out << v.toObject()["name"].toString();
  return out;
}

void NodeEditor::highlightBranch(const QString& name) {
  if (name == highlighted_) return;
  highlighted_ = name;
  rebuild();
}

void NodeEditor::removeBranch(const QString& name) {
  QJsonObject g = graph();
  QJsonArray kept;
  for (const QJsonValue& v : g["branches"].toArray()) {
    if (v.toObject()["name"].toString() != name) kept.append(v);
  }
  g["branches"] = kept;
  if (highlighted_ == name) highlighted_.clear();
  setGraph(g);
  emit branchesChanged();
}

void NodeEditor::recordingHint() {
  QString hint;
  if (!recording_) {
    hint.clear();
  } else if (recorded_.isEmpty()) {
    hint = tr("New branch: click the port of a source the light leaves by (Esc cancels)");
  } else if (branchComplete()) {
    hint = tr("The branch reaches a detector");
  } else {
    hint = tr("Click the port the light leaves the next block by, or the detector (Esc cancels)");
  }
  emit recordingChanged(recording_, hint);
}

void NodeEditor::beginBranch() {
  if (!modelMode()) return;
  recording_ = true;
  recorded_ = QJsonArray{};
  rebuild();
  recordingHint();
}

void NodeEditor::cancelBranch() {
  if (!recording_) return;
  recording_ = false;
  recorded_ = QJsonArray{};
  rebuild();
  recordingHint();
}

bool NodeEditor::branchComplete() const {
  return !recorded_.isEmpty() && recorded_.last().toArray().at(2).isNull() && recorded_.size() > 1;
}

namespace {
/// The port at the other end of the connection of (node, port), or an empty array.
QJsonArray other_end(const QJsonObject& model, const QString& node, const QString& port) {
  for (const QJsonValue& v : model["connections"].toArray()) {
    const QJsonArray ends = v.toObject()["ports"].toArray();
    if (ends.at(0).toArray() == QJsonArray{node, port}) return ends.at(1).toArray();
    if (ends.at(1).toArray() == QJsonArray{node, port}) return ends.at(0).toArray();
  }
  return {};
}
}  // namespace

bool NodeEditor::recordPort(const QString& node, const QString& port) {
  if (!recording_ || branchComplete()) return false;
  if (recorded_.isEmpty()) {
    if (categoryOf(node) != QLatin1String("source")) return false;
    recorded_.append(QJsonArray{node, QJsonValue::Null, port});
  } else {
    const QJsonArray last = recorded_.last().toArray();
    const QJsonArray next = other_end(graph(), last.at(0).toString(), last.at(2).toString());
    if (next.isEmpty() || next.at(0).toString() != node) return false;  // not where the light goes
    QJsonArray step{node, next.at(1), port};
    if (node_of(graph(), node).contains("variants")) {  // through a group: its inner branch
      const QJsonObject n = node_of(graph(), node);
      const QJsonObject body = n["variants"].toObject()[n["variant"].toString()].toObject();
      const QJsonObject exported = body["ports"].toObject();
      for (const QJsonValue& b : body["branches"].toArray()) {
        const QJsonArray steps = b.toObject()["steps"].toArray();
        if (steps.isEmpty()) continue;
        const QJsonArray inner_first = steps.first().toArray();
        const QJsonArray inner_last = steps.last().toArray();
        if (QJsonArray{inner_first.at(0), inner_first.at(1)} == exported[next.at(1).toString()].toArray() &&
            QJsonArray{inner_last.at(0), inner_last.at(2)} == exported[port].toArray()) {
          step.append(b.toObject()["name"]);
          break;
        }
      }
    }
    recorded_.append(step);
  }
  rebuild();
  recordingHint();
  return true;
}

bool NodeEditor::recordDetector(const QString& node) {
  if (!recording_ || recorded_.isEmpty() || branchComplete()) return false;
  if (categoryOf(node) != QLatin1String("detector")) return false;
  const QJsonArray last = recorded_.last().toArray();
  const QJsonArray next = other_end(graph(), last.at(0).toString(), last.at(2).toString());
  if (next.isEmpty() || next.at(0).toString() != node) return false;
  recorded_.append(QJsonArray{node, next.at(1), QJsonValue::Null});
  rebuild();
  recordingHint();
  emit branchRecorded();
  return true;
}

bool NodeEditor::finishBranch(const QString& name) {
  if (!branchComplete() || name.trimmed().isEmpty() || branchNames().contains(name.trimmed())) return false;
  QJsonObject g = graph();
  QJsonArray branches = g["branches"].toArray();
  branches.append(QJsonObject{{"name", name.trimmed()}, {"steps", recorded_}});
  g["branches"] = branches;
  recording_ = false;
  recorded_ = QJsonArray{};
  highlighted_ = name.trimmed();
  recordingHint();
  setGraph(g);
  emit branchesChanged();
  return true;
}

void NodeEditor::invalidateModel(const QSet<QString>& seeds) {
  // along the branches (everything after a changed block) and the data links (not a drive)
  QSet<QString> stale(seeds.begin(), seeds.end());
  const QJsonObject g = graph();
  bool grew = true;
  while (grew) {
    grew = false;
    for (const QJsonValue& b : g["branches"].toArray()) {
      bool after = false;
      for (const QJsonValue& v : b.toObject()["steps"].toArray()) {
        const QString id = v.toArray().at(0).toString();
        if (after && !stale.contains(id)) {
          stale.insert(id);
          grew = true;
        }
        after = after || stale.contains(id);
      }
    }
    for (const QJsonValue& v : g["links"].toArray()) {
      const QJsonObject l = v.toObject();
      const QString from = l["from"].toArray().at(0).toString();
      const QString to = l["to"].toArray().at(0).toString();
      if (!stale.contains(from) || stale.contains(to)) continue;
      bool drive = false;
      for (const QJsonValue& s : socketsOf(node_of(g, from))) {
        const QJsonObject o = s.toObject();
        if (o["name"].toString() == l["from"].toArray().at(1).toString()) drive = o["accepts"].toArray().contains(QStringLiteral("drive"));
      }
      if (drive) continue;
      stale.insert(to);
      grew = true;
    }
  }
  for (const QString& id : stale) states_.insert(id, false);
}

QHash<QString, QPointF> NodeEditor::layoutModel(const QJsonObject& model) const {
  // column: the first step at which a branch reaches the block; blocks off the branches one
  // column behind what links into them
  QHash<QString, int> column;
  for (const QJsonValue& b : model["branches"].toArray()) {
    const QJsonArray steps = b.toObject()["steps"].toArray();
    for (int k = 0; k < steps.size(); ++k) {
      const QString id = steps[k].toArray().at(0).toString();
      if (!column.contains(id) || k < column[id]) column[id] = k;
    }
  }
  QStringList ids;
  for (const QJsonValue& v : model["nodes"].toArray()) ids << v.toObject()["id"].toString();
  for (int pass = 0; pass < 4; ++pass) {
    for (const QJsonValue& v : model["links"].toArray()) {
      const QString from = v.toObject()["from"].toArray().at(0).toString();
      const QString to = v.toObject()["to"].toArray().at(0).toString();
      if (column.contains(from) && !column.contains(to)) column[to] = column[from] + 1;
    }
  }
  QHash<int, int> rows;
  QHash<QString, QPointF> out;
  for (const QString& id : ids) {
    const int c = column.value(id, 0);
    const int r = rows[c]++;
    out[id] = QPointF(c * (kWidth + 70.0), r * 130.0);
  }
  return out;
}

// ------------------------------------------------------------------------------ groups (R28)

namespace {
/// The active variant's body of group `id` in `model` (nodes, connections, links, branches,
/// ports, params).
QJsonObject body_of(const QJsonObject& model, const QString& id) {
  const QJsonObject n = node_of(model, id);
  return n["variants"].toObject()[n["variant"].toString()].toObject();
}

QJsonObject with_body(QJsonObject model, const QString& id, const QJsonObject& body) {
  QJsonArray nodes = model["nodes"].toArray();
  for (int k = 0; k < nodes.size(); ++k) {
    QJsonObject n = nodes[k].toObject();
    if (n["id"].toString() != id) continue;
    QJsonObject variants = n["variants"].toObject();
    variants[n["variant"].toString()] = body;
    n["variants"] = variants;
    nodes[k] = n;
  }
  model["nodes"] = nodes;
  return model;
}

QJsonObject written(const QJsonObject& model, const QStringList& scope, const QJsonObject& g, int k = 0) {
  if (k == scope.size()) return g;
  return with_body(model, scope[k], written(body_of(model, scope[k]), scope, g, k + 1));
}
}  // namespace

QJsonObject NodeEditor::graph() const {
  QJsonObject g = workflow_[key()].toObject();
  for (const QString& id : scope_) {
    if (!node_of(g, id).contains("variants")) return {};
    g = body_of(g, id);
  }
  return g;
}

void NodeEditor::writeGraph(const QJsonObject& g) {
  workflow_[key()] = written(workflow_[key()].toObject(), scope_, g);
}

void NodeEditor::showScope() {
  if (crumb_ == nullptr) {
    crumb_ = new QLabel(viewport());
    crumb_->setStyleSheet(QStringLiteral("QLabel { background: rgba(60, 60, 70, 210); color: #e6e6e6; padding: 3px 7px; border-radius: 3px; }"));
    crumb_->move(8, 8);
    crumb_->setCursor(Qt::PointingHandCursor);
    crumb_->setToolTip(tr("Click (or Escape): back out of the group"));
    crumb_->installEventFilter(this);
  }
  QStringList parts{tr("Workflow")};
  QJsonObject level = workflow_[key()].toObject();
  for (const QString& id : scope_) {
    const QJsonObject n = node_of(level, id);
    parts << QStringLiteral("%1 (%2)").arg(n["name"].toString(id), n["variant"].toString());
    level = body_of(level, id);
  }
  crumb_->setText(parts.join(QStringLiteral("  ▸  ")) + (scope_.isEmpty() ? QString() : tr("   – Esc: back")));
  crumb_->adjustSize();
  crumb_->setVisible(!scope_.isEmpty());
  emit scopeChanged(scope_.isEmpty() ? QString() : parts.mid(1).join(QStringLiteral(" ▸ ")));
}

bool NodeEditor::enterGroup(const QString& id) {
  if (!modelMode() || !node_of(graph(), id).contains("variants")) return false;
  scope_ << id;
  highlighted_.clear();
  cancelBranch();
  showScope();
  QJsonObject g = graph();
  if (!g.isEmpty() && !hasPositions(g)) {  // a group from a file without layout
    const QHash<QString, QPointF> pos = layoutModel(g);
    QJsonArray nodes = g["nodes"].toArray();
    for (int k = 0; k < nodes.size(); ++k) {
      QJsonObject n = nodes[k].toObject();
      const QPointF p = pos.value(n["id"].toString());
      n["position"] = QJsonArray{p.x(), p.y()};
      nodes[k] = n;
    }
    g["nodes"] = nodes;
    writeGraph(g);
  }
  rebuild();
  emit branchesChanged();
  fitAll();
  return true;
}

void NodeEditor::leaveGroup() {
  if (scope_.isEmpty()) return;
  scope_.removeLast();
  highlighted_.clear();
  cancelBranch();
  showScope();
  rebuild();
  emit branchesChanged();
  fitAll();
}

QStringList NodeEditor::variants(const QString& id) const {
  return node_of(graph(), id)["variants"].toObject().keys();
}

void NodeEditor::setVariant(const QString& id, const QString& variant) {
  QJsonObject g = graph();
  QJsonArray nodes = g["nodes"].toArray();
  for (int k = 0; k < nodes.size(); ++k) {
    QJsonObject n = nodes[k].toObject();
    if (n["id"].toString() != id || !n["variants"].toObject().contains(variant)) continue;
    n["variant"] = variant;
    nodes[k] = n;
  }
  g["nodes"] = nodes;
  setGraph(g);
}

void NodeEditor::setCondensed(const QString& id, bool condensed) {
  QJsonObject g = graph();
  QJsonArray nodes = g["nodes"].toArray();
  for (int k = 0; k < nodes.size(); ++k) {
    QJsonObject n = nodes[k].toObject();
    if (n["id"].toString() != id) continue;
    if (condensed) {
      n["condense"] = QJsonObject{{"sigma_max", 0.2}, {"count", 5}};
    } else {
      n.remove(QStringLiteral("condense"));
    }
    nodes[k] = n;
  }
  g["nodes"] = nodes;
  setGraph(g);
}

QString NodeEditor::groupBlocks(const QStringList& ids, const QString& name, QString* why) {
  auto refuse = [why](const QString& text) {
    if (why != nullptr) *why = text;
    return QString();
  };
  if (!modelMode() || ids.isEmpty()) return refuse(tr("Select blocks of a model first."));
  const QJsonObject g = graph();
  const QSet<QString> inside(ids.begin(), ids.end());
  for (const QString& id : ids) {
    const QString c = categoryOf(id);
    if (c == QLatin1String("source") || c == QLatin1String("detector")) {
      return refuse(tr("%1 is a source or a detector: branches begin and end outside a group.").arg(id));
    }
  }
  for (const QJsonValue& v : g["links"].toArray()) {
    const QJsonObject l = v.toObject();
    const bool a = inside.contains(l["from"].toArray().at(0).toString());
    const bool b = inside.contains(l["to"].toArray().at(0).toString());
    if (a != b) {
      return refuse(tr("The data link %1.%2 → %3.%4 crosses the group's border; group the blocks it joins too, or remove it.")
                        .arg(l["from"].toArray().at(0).toString(), l["from"].toArray().at(1).toString(),
                             l["to"].toArray().at(0).toString(), l["to"].toArray().at(1).toString()));
    }
  }
  QSet<QString> taken;
  for (const QJsonValue& v : g["nodes"].toArray()) taken.insert(v.toObject()["id"].toString());
  QString stem = name.trimmed();
  stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]+")), QStringLiteral("_"));
  if (stem.isEmpty()) stem = QStringLiteral("group");
  QString gid = stem;
  for (int k = 2; taken.contains(gid); ++k) gid = QStringLiteral("%1_%2").arg(stem).arg(k);
  // inner blocks (positions relative to their corner), connections and links
  QJsonObject inner{{"format", "rtt-fusion-graph"}, {"version", 2}};
  QJsonArray inner_nodes, outer_nodes, inner_conn, outer_conn, inner_links, outer_links;
  double x0 = 1e300, y0 = 1e300, sx = 0, sy = 0;
  int count = 0;
  for (const QJsonValue& v : g["nodes"].toArray()) {
    const QJsonObject n = v.toObject();
    if (!inside.contains(n["id"].toString())) continue;
    const QJsonArray p = n["position"].toArray();
    x0 = std::min(x0, p.at(0).toDouble());
    y0 = std::min(y0, p.at(1).toDouble());
    sx += p.at(0).toDouble();
    sy += p.at(1).toDouble();
    ++count;
  }
  for (const QJsonValue& v : g["nodes"].toArray()) {
    QJsonObject n = v.toObject();
    if (!inside.contains(n["id"].toString())) {
      outer_nodes.append(n);
      continue;
    }
    const QJsonArray p = n["position"].toArray();
    n["position"] = QJsonArray{p.at(0).toDouble() - x0, p.at(1).toDouble() - y0};
    inner_nodes.append(n);
  }
  QJsonObject exported;  // "<block>.<port>" -> [block, port]
  for (const QJsonValue& v : g["connections"].toArray()) {
    QJsonObject c = v.toObject();
    QJsonArray ends = c["ports"].toArray();
    const bool a = inside.contains(ends.at(0).toArray().at(0).toString());
    const bool b = inside.contains(ends.at(1).toArray().at(0).toString());
    if (a && b) {
      inner_conn.append(c);
    } else if (!a && !b) {
      outer_conn.append(c);
    } else {
      const int k = a ? 0 : 1;
      const QJsonArray end = ends.at(k).toArray();
      const QString port = end.at(0).toString() + QLatin1Char('.') + end.at(1).toString();
      exported[port] = end;
      ends[k] = QJsonArray{gid, port};
      c["ports"] = ends;
      outer_conn.append(c);
    }
  }
  for (const QJsonValue& v : g["links"].toArray()) {
    (inside.contains(v.toObject()["from"].toArray().at(0).toString()) ? inner_links : outer_links).append(v);
  }
  auto exported_name = [&exported](const QJsonArray& end) {
    for (auto it = exported.begin(); it != exported.end(); ++it) {
      if (it.value().toArray() == end) return it.key();
    }
    return QString();
  };
  // branches: each stretch inside becomes an inner branch, the step through the group names it
  QJsonArray inner_branches, outer_branches;
  for (const QJsonValue& v : g["branches"].toArray()) {
    const QJsonObject b = v.toObject();
    const QJsonArray steps = b["steps"].toArray();
    QJsonArray out;
    int stretch = 0;
    for (int k = 0; k < steps.size();) {
      if (!inside.contains(steps[k].toArray().at(0).toString())) {
        out.append(steps[k++]);
        continue;
      }
      QJsonArray run;
      while (k < steps.size() && inside.contains(steps[k].toArray().at(0).toString())) run.append(steps[k++]);
      const QJsonArray first = run.first().toArray();
      const QJsonArray last = run.last().toArray();
      const QString entry = exported_name(QJsonArray{first.at(0), first.at(1)});
      const QString exit = exported_name(QJsonArray{last.at(0), last.at(2)});
      if (entry.isEmpty() || exit.isEmpty()) {
        return refuse(tr("Branch %1 does not enter and leave the group through a connection.").arg(b["name"].toString()));
      }
      QString inner_name;
      for (const QJsonValue& ib : inner_branches) {
        if (ib.toObject()["steps"].toArray() == run) inner_name = ib.toObject()["name"].toString();
      }
      if (inner_name.isEmpty()) {
        inner_name = b["name"].toString() + (stretch > 0 ? QStringLiteral(" %1").arg(stretch + 1) : QString());
        inner_branches.append(QJsonObject{{"name", inner_name}, {"steps", run}});
      }
      ++stretch;
      out.append(QJsonArray{gid, entry, exit, inner_name});
    }
    outer_branches.append(QJsonObject{{"name", b["name"]}, {"steps", out}});
  }
  inner["nodes"] = inner_nodes;
  inner["connections"] = inner_conn;
  inner["links"] = inner_links;
  inner["branches"] = inner_branches;
  inner["ports"] = exported;
  inner.remove(QStringLiteral("format"));
  inner.remove(QStringLiteral("version"));
  outer_nodes.append(QJsonObject{{"id", gid},
                                 {"type", "group"},
                                 {"name", name.trimmed().isEmpty() ? gid : name.trimmed()},
                                 {"position", QJsonArray{std::round(sx / std::max(count, 1)), std::round(sy / std::max(count, 1))}},
                                 {"variant", "main"},
                                 {"variants", QJsonObject{{"main", inner}}}});
  QJsonObject result = g;
  result["nodes"] = outer_nodes;
  result["connections"] = outer_conn;
  result["links"] = outer_links;
  result["branches"] = outer_branches;
  setGraph(result);
  emit branchesChanged();
  return gid;
}

bool NodeEditor::ungroup(const QString& id, QString* why) {
  auto refuse = [why](const QString& text) {
    if (why != nullptr) *why = text;
    return false;
  };
  const QJsonObject g = graph();
  const QJsonObject group = node_of(g, id);
  if (!group.contains("variants")) return refuse(tr("%1 is no group.").arg(id));
  for (const QJsonValue& v : g["links"].toArray()) {
    if (v.toObject()["to"].toArray().at(0).toString() == id || v.toObject()["from"].toArray().at(0).toString() == id) {
      return refuse(tr("A data link goes to the group itself (e.g. its pose input); remove it first."));
    }
  }
  const QJsonObject body = body_of(g, id);
  const QJsonObject exported = body["ports"].toObject();
  QSet<QString> taken;
  for (const QJsonValue& v : g["nodes"].toArray()) {
    if (v.toObject()["id"].toString() != id) taken.insert(v.toObject()["id"].toString());
  }
  QHash<QString, QString> rename;
  for (const QJsonValue& v : body["nodes"].toArray()) {
    const QString old = v.toObject()["id"].toString();
    QString fresh = old;
    for (int k = 2; taken.contains(fresh); ++k) fresh = QStringLiteral("%1_%2").arg(old).arg(k);
    taken.insert(fresh);
    rename[old] = fresh;
  }
  auto mapped = [&rename](QJsonArray end) {
    end[0] = rename.value(end.at(0).toString(), end.at(0).toString());
    return end;
  };
  const QJsonArray at = group["position"].toArray();
  QJsonArray nodes;
  for (const QJsonValue& v : g["nodes"].toArray()) {
    if (v.toObject()["id"].toString() != id) nodes.append(v);
  }
  for (const QJsonValue& v : body["nodes"].toArray()) {
    QJsonObject n = v.toObject();
    const QJsonArray p = n["position"].toArray();
    n["id"] = rename.value(n["id"].toString());
    n["position"] = QJsonArray{at.at(0).toDouble() + p.at(0).toDouble(), at.at(1).toDouble() + p.at(1).toDouble()};
    nodes.append(n);
  }
  QJsonArray connections;
  for (const QJsonValue& v : body["connections"].toArray()) {
    QJsonObject c = v.toObject();
    c["ports"] = QJsonArray{mapped(c["ports"].toArray().at(0).toArray()), mapped(c["ports"].toArray().at(1).toArray())};
    connections.append(c);
  }
  for (const QJsonValue& v : g["connections"].toArray()) {
    QJsonObject c = v.toObject();
    QJsonArray ends = c["ports"].toArray();
    for (int k = 0; k < 2; ++k) {
      if (ends.at(k).toArray().at(0).toString() == id) ends[k] = mapped(exported[ends.at(k).toArray().at(1).toString()].toArray());
    }
    c["ports"] = ends;
    connections.append(c);
  }
  QJsonArray links = g["links"].toArray();
  for (const QJsonValue& v : body["links"].toArray()) {
    QJsonObject l = v.toObject();
    l["from"] = mapped(l["from"].toArray());
    l["to"] = mapped(l["to"].toArray());
    links.append(l);
  }
  QHash<QString, QJsonArray> inner;
  for (const QJsonValue& v : body["branches"].toArray()) inner[v.toObject()["name"].toString()] = v.toObject()["steps"].toArray();
  QJsonArray branches;
  for (const QJsonValue& v : g["branches"].toArray()) {
    QJsonObject b = v.toObject();
    QJsonArray steps;
    for (const QJsonValue& s : b["steps"].toArray()) {
      const QJsonArray step = s.toArray();
      if (step.at(0).toString() != id) {
        steps.append(step);
        continue;
      }
      const QString channel = step.size() > 3 ? step.at(3).toString() : QString();
      if (!inner.contains(channel)) {
        return refuse(tr("Branch %1 names no inner branch of the group (%2).").arg(b["name"].toString(), channel));
      }
      for (const QJsonValue& is : inner[channel]) steps.append(mapped(is.toArray()));
    }
    b["steps"] = steps;
    branches.append(b);
  }
  QJsonObject result = g;
  result["nodes"] = nodes;
  result["connections"] = connections;
  result["links"] = links;
  result["branches"] = branches;
  setGraph(result);
  emit branchesChanged();
  return true;
}

void NodeEditor::setStepChannel(const QString& branch, int step, const QString& channel) {
  QJsonObject g = graph();
  QJsonArray branches = g["branches"].toArray();
  for (int k = 0; k < branches.size(); ++k) {
    QJsonObject b = branches[k].toObject();
    if (b["name"].toString() != branch) continue;
    QJsonArray steps = b["steps"].toArray();
    if (step < 0 || step >= steps.size()) return;
    QJsonArray s = steps[step].toArray();
    while (s.size() > 3) s.removeLast();
    if (!channel.trimmed().isEmpty()) s.append(channel.trimmed());
    steps[step] = s;
    b["steps"] = steps;
    branches[k] = b;
  }
  g["branches"] = branches;
  setGraph(g);
  emit branchesChanged();
}

QStringList NodeEditor::channelChoices(const QString& branch, int step) const {
  QStringList out{QString()};
  for (const QJsonValue& v : graph()["branches"].toArray()) {
    if (v.toObject()["name"].toString() != branch) continue;
    const QJsonArray steps = v.toObject()["steps"].toArray();
    if (step < 0 || step >= steps.size()) return out;
    const QJsonArray s = steps[step].toArray();
    const QJsonObject n = node_of(graph(), s.at(0).toString());
    if (n.contains("variants")) {  // the inner branches between the step's ports
      const QJsonObject body = body_of(graph(), s.at(0).toString());
      const QJsonObject exported = body["ports"].toObject();
      for (const QJsonValue& b : body["branches"].toArray()) {
        const QJsonArray inner = b.toObject()["steps"].toArray();
        if (inner.isEmpty()) continue;
        if (QJsonArray{inner.first().toArray().at(0), inner.first().toArray().at(1)} == exported[s.at(1).toString()].toArray() &&
            QJsonArray{inner.last().toArray().at(0), inner.last().toArray().at(2)} == exported[s.at(2).toString()].toArray()) {
          out << b.toObject()["name"].toString();
        }
      }
    } else if (n["type"].toString().startsWith(QLatin1String("rtt."))) {  // ghosts at its surfaces
      for (const QJsonValue& sid : n["params"].toObject()["surfaces"].toArray()) out << QStringLiteral("ghost:") + sid.toString();
    }
  }
  return out;
}

}  // namespace nfcad::ui
