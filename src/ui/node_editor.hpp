#pragma once

#include <QGraphicsView>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

class QGraphicsPathItem;
class QGraphicsRectItem;

namespace nfcad::ui {

class NodeItem;
class SocketItem;
class LinkItem;

/// Node editor of the RTT-Fusion workflow (RTT-Fusion docs/konzept-node-workflow.md, R16).
///
/// Shows the working graph of the document – {"graph": rtt-fusion-graph, "types": [...],
/// "diagnostics": [...]} – as blocks with sockets and links, and lets the user edit it: move blocks
/// (links follow), drag a link from an output to an input socket, delete links and blocks, mute a
/// block, fix the transport of a link, add blocks from the type catalogue (context menu). Every
/// edit emits `edited` with the whole workflow; the checks and the computation live in RTT-Fusion
/// (the plugin), the editor only refuses obvious nonsense (input to input, light to data).
class NodeEditor : public QGraphicsView {
  Q_OBJECT
 public:
  explicit NodeEditor(QWidget* parent = nullptr);

  void setWorkflow(const QJsonObject& workflow);
  [[nodiscard]] const QJsonObject& workflow() const { return workflow_; }
  /// Shows the whole graph.
  void fitAll();
  /// Arranges the blocks in columns along the light (where light first arrives) and emits `edited`.
  void arrange();

  // ---- editing operations (also used by the context menus; each emits `edited`)
  bool addLink(const QString& from_node, const QString& from_socket, const QString& to_node,
               const QString& to_socket);
  void removeLink(int index);
  void setTransport(int index, const QString& transport);
  void removeNode(const QString& id);
  void setMuted(const QString& id, bool muted);
  QString addNode(const QString& type, const QPointF& position);
  /// The item of a block (its pen marks problems; tooltip: type, parameters, problems).
  [[nodiscard]] QGraphicsRectItem* nodeItem(const QString& id) const;
  [[nodiscard]] int linkCount() const;

 signals:
  void edited(const QJsonObject& workflow);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  friend class NodeItem;
  void rebuild();
  void updateLinks();
  void commit();  ///< writes the graph back into workflow_ and emits `edited`
  [[nodiscard]] QJsonArray socketsOf(const QJsonObject& node) const;
  [[nodiscard]] QJsonObject graph() const { return workflow_["graph"].toObject(); }
  void setGraph(const QJsonObject& graph);
  void nodeMoved();
  /// Column layout of a graph (positions per node id); used for graphs without positions.
  [[nodiscard]] QHash<QString, QPointF> layoutColumns(const QJsonObject& graph) const;
  [[nodiscard]] static bool hasPositions(const QJsonObject& graph);

  QJsonObject workflow_;
  QHash<QString, NodeItem*> nodes_;
  QList<LinkItem*> links_;
  SocketItem* drag_from_ = nullptr;
  QGraphicsPathItem* drag_line_ = nullptr;
  bool moved_ = false;
  QPoint pan_from_;
  bool panning_ = false;
};

}  // namespace nfcad::ui
