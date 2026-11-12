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
/// "diagnostics": [...], "plan": planner result} – as blocks with sockets and links, and lets the user edit it: move blocks
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
  /// Shows the whole graph (zoom extents).
  void fitAll();
  /// Fits the selected blocks into the view (all blocks without a selection).
  void zoomSelected();
  /// Zooms by `factor` (> 1 = closer) keeping the scene point under `view_pos` in place.
  void zoomAt(const QPointF& view_pos, double factor);
  /// Moves the view content by `delta` pixels.
  void pan(const QPointF& delta);
  /// Current zoom (1 = one scene unit per pixel).
  [[nodiscard]] double zoom() const;
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
  /// Exchanges a block for one of `type` and keeps its links: each linked socket moves to the new
  /// socket of the same name, else to the only one of the same direction and kind (RTT-Fusion
  /// `replace_block`). Returns false (and changes nothing) if a socket has no unique counterpart.
  bool replaceNode(const QString& id, const QString& type);
  /// Types a block can be exchanged for (every linked socket finds a unique counterpart).
  [[nodiscard]] QStringList replacementsFor(const QString& id) const;
  /// Sets parameters of a block (merged into its "params").
  void setParams(const QString& id, const QJsonObject& params);
  /// Dialog with the type's parameters (number, integer, boolean, text, file, numbers, point).
  void editParams(const QString& id);
  /// Marks the block that is computed now (empty: none) – a bright frame and "running …".
  void setRunning(const QString& id);
  [[nodiscard]] QString running() const { return running_; }
  /// The item of a block (its pen marks problems; tooltip: type, parameters, problems).
  [[nodiscard]] QGraphicsRectItem* nodeItem(const QString& id) const;
  [[nodiscard]] int linkCount() const;
  /// The item of a link (tooltip: transport, planned representation and reason, problems).
  [[nodiscard]] QGraphicsPathItem* linkItem(int index) const;
  /// The link under a point of the view (also on its label), or −1.
  [[nodiscard]] int linkAt(const QPoint& view_pos) const;
  /// The representation the planner chose for a link ("" without a plan).
  [[nodiscard]] QString plannedRepresentation(int index) const;

 signals:
  void edited(const QJsonObject& workflow);
  /// A display block (type display.*) asks for its window (double-click, context menu).
  void displayRequested(const QString& id, const QString& type, const QString& title);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  bool viewportEvent(QEvent* event) override;
  bool event(QEvent* event) override;

 private:
  friend class NodeItem;
  void rebuild();
  void updateLinks();
  void commit();  ///< writes the graph back into workflow_ and emits `edited`
  [[nodiscard]] QJsonArray socketsOf(const QJsonObject& node) const;
  [[nodiscard]] QJsonObject graph() const { return workflow_["graph"].toObject(); }
  void setGraph(const QJsonObject& graph);
  void nodeMoved();
  void showMenu(const QPoint& view_pos, const QPoint& global_pos);
  /// Column layout of a graph (positions per node id); used for graphs without positions.
  [[nodiscard]] QHash<QString, QPointF> layoutColumns(const QJsonObject& graph) const;
  [[nodiscard]] static bool hasPositions(const QJsonObject& graph);

  QJsonObject workflow_;
  QHash<QString, NodeItem*> nodes_;
  QList<LinkItem*> links_;
  SocketItem* drag_from_ = nullptr;
  QGraphicsPathItem* drag_line_ = nullptr;
  bool moved_ = false;
  QPointF pan_from_;
  bool panning_ = false;
  // right button: a drag pans (as in the 2D CAD views), a click opens the context menu
  bool right_down_ = false;
  bool right_dragged_ = false;
  QPointF right_from_;
  bool z_pressed_ = false;
  QString running_;  ///< first key of the sequences Z E (extents) and Z S (selected)
};

}  // namespace nfcad::ui
