#pragma once

#include <QGraphicsView>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <utility>
#include <vector>

class QGraphicsPathItem;
class QLabel;
class QGraphicsRectItem;

namespace nfcad::ui {

class NodeItem;
class SocketItem;
class LinkItem;

/// Node editor of the RTT-Fusion workflow (RTT-Fusion docs/konzept-node-workflow.md, R16).
///
/// With a model (RTT-Fusion R24: {"model": rtt-fusion-graph version 2, …}) a block shows one
/// point per light port, connections join two ports without a direction, data links stay
/// directed, and the branches of the model can be listed, highlighted, recorded (click the port
/// the light leaves each block by) and deleted – the modeller gives every branch.
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

  /// Actions of the plugins on one block (its context menu): label and what to do, for a block
  /// of a type; the main window fills it from the plugins' actions with block types.
  using BlockAction = std::pair<QString, std::function<void()>>;
  std::function<std::vector<BlockAction>(const QString& id, const QString& type)> block_actions;

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
  /// Up to date (true: green tick) / out of date (false: red cross) per block; blocks not listed
  /// get no mark. An edit marks the edited blocks and all blocks behind them out of date.
  void setStates(const QJsonObject& states);
  /// The mark of a block: 1 up to date, 0 out of date, −1 none.
  [[nodiscard]] int state(const QString& id) const;
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

  // ---- model (RTT-Fusion R24): ports, connections, branches
  /// The workflow is a model (ports and branches) rather than a socket graph.
  [[nodiscard]] bool modelMode() const { return workflow_.contains(QStringLiteral("model")); }
  /// Joins two ports (each port has at most one connection).
  bool addConnection(const QString& a, const QString& port_a, const QString& b, const QString& port_b);
  void removeConnection(int index);
  /// Transport of a connection for "both" directions, "forward" (as listed, a → b) or "back".
  void setConnectionTransport(int index, const QString& direction, const QString& transport);
  [[nodiscard]] int connectionCount() const;
  [[nodiscard]] QGraphicsPathItem* connectionItem(int index) const;
  /// The connection under a point of the view, or −1.
  [[nodiscard]] int connectionAt(const QPoint& view_pos) const;
  /// Whether a port of a block is drawn on its left side (where the light comes in first).
  [[nodiscard]] bool portOnLeft(const QString& node, const QString& port) const;
  [[nodiscard]] QStringList branchNames() const;
  /// The steps of a branch at the level shown ([node, port in, port out, channel?]).
  [[nodiscard]] QJsonArray branchSteps(const QString& name) const;
  /// Highlights a branch in the editor (empty: none).
  void highlightBranch(const QString& name);
  [[nodiscard]] QString highlightedBranch() const { return highlighted_; }
  void removeBranch(const QString& name);
  /// Recording a branch: click the source's port, then the port each block is left by, then
  /// the detector; Escape cancels.
  void beginBranch();
  void cancelBranch();
  [[nodiscard]] bool recordingBranch() const { return recording_; }
  bool recordPort(const QString& node, const QString& port);
  bool recordDetector(const QString& node);
  [[nodiscard]] bool branchComplete() const;
  /// Adds the recorded branch under `name` (unique, not empty).
  bool finishBranch(const QString& name);
  /// The channel of a step of a branch ("" removes it): an inner branch of a group, a ghost
  /// "ghost:<surface>,…" of a Raytatouille section, an order …
  void setStepChannel(const QString& branch, int step, const QString& channel);
  /// Suggestions for the channel of a step: "" first, then the inner branches of a group that
  /// fit the step's ports, or the ghosts at the surfaces of a Raytatouille block.
  [[nodiscard]] QStringList channelChoices(const QString& branch, int step) const;

  // ---- groups (RTT-Fusion R28)
  /// The groups the editor shows the inside of, outermost first (empty: the whole model).
  [[nodiscard]] const QStringList& scope() const { return scope_; }
  /// Shows the active variant of a group; edits go there.
  bool enterGroup(const QString& id);
  /// Back to the level around the group.
  void leaveGroup();
  [[nodiscard]] QStringList variants(const QString& id) const;
  void setVariant(const QString& id, const QString& variant);
  /// A new variant of group `id`: a copy of the active one (same ports, inner branches), then
  /// active. Returns its name (made unique), empty if `id` is no group.
  QString addVariant(const QString& id, const QString& name);
  /// Renames a variant (false: unknown, empty or taken name).
  bool renameVariant(const QString& id, const QString& from, const QString& to);
  /// Deletes a variant; the last one stays (false). The active one gives way to another.
  bool removeVariant(const QString& id, const QString& name);
  /// Condensed (plane-wave tables, R29) or resolved into its blocks.
  void setCondensed(const QString& id, bool condensed);
  /// Makes a group of blocks: connections across become exported ports, each stretch of a branch
  /// inside an inner branch (the step through the group names it). Refused (with `why`) for
  /// sources, detectors and data links across. Returns the group's id or "".
  QString groupBlocks(const QStringList& ids, const QString& name, QString* why = nullptr);
  /// Puts the blocks of a group's active variant back in its place.
  bool ungroup(const QString& id, QString* why = nullptr);

 signals:
  void edited(const QJsonObject& workflow);
  void branchesChanged();
  /// The level shown changed ("" the whole model, else e.g. "objective (variant ideal)").
  void scopeChanged(const QString& where);
  /// A recorded branch reached a detector: it needs a name (`finishBranch`).
  void branchRecorded();
  void recordingChanged(bool on, const QString& hint);
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
  bool eventFilter(QObject* watched, QEvent* event) override;
  bool event(QEvent* event) override;

 private:
  friend class NodeItem;
  void rebuild();
  void updateLinks();
  void commit();  ///< writes the graph back into workflow_ and emits `edited`
  [[nodiscard]] QJsonArray socketsOf(const QJsonObject& node) const;
  [[nodiscard]] QString key() const { return modelMode() ? QStringLiteral("model") : QStringLiteral("graph"); }
  /// The level shown: the model, or the active variant of the group in `scope_`.
  [[nodiscard]] QJsonObject graph() const;
  /// Writes the level shown back into the workflow.
  void writeGraph(const QJsonObject& g);
  void showScope();
  /// Sockets as drawn: the sockets, or with a model the data sockets plus one entry per port.
  [[nodiscard]] QJsonArray shownSockets(const QJsonObject& node) const;
  [[nodiscard]] QJsonArray portsOf(const QJsonObject& node) const;
  /// The port whose channel is the light socket `socket` ("" if none).
  [[nodiscard]] QString portOfSocket(const QJsonObject& node, const QString& socket) const;
  [[nodiscard]] QString categoryOf(const QString& node) const;
  void invalidateModel(const QSet<QString>& seeds);
  [[nodiscard]] QHash<QString, QPointF> layoutModel(const QJsonObject& model) const;
  void recordingHint();
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
  QString running_;
  QHash<QString, bool> states_;
  QList<LinkItem*> connections_;
  QString highlighted_;
  QStringList outer_highlights_;  // the highlighted branch of each level above the scope
  bool recording_ = false;
  QJsonArray recorded_;  ///< steps [node, port in | null, port out | null]
  QStringList scope_;
  QLabel* crumb_ = nullptr;
  /// Marks `seeds` and every block behind them (along links, not along "drive") out of date.
  void invalidate(const QSet<QString>& seeds);  ///< first key of the sequences Z E (extents) and Z S (selected)
};

}  // namespace nfcad::ui
