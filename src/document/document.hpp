#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUndoStack>
#include <map>
#include <vector>

#include "document/entity.hpp"

namespace nfcad::document {

/// A combined change applied as one undo step (commands: transforms, Booleans, ...).
struct Edit {
  std::vector<Entity> add;
  std::vector<EntityId> remove;
  std::vector<std::pair<EntityId, GeometryPtr>> replace;  ///< new geometry for an entity
  [[nodiscard]] bool empty() const { return add.empty() && remove.empty() && replace.empty(); }
};

/// Node of the model tree; groups nest (parent 0 = top level).
struct Group {
  GroupId id = 0;
  QString name;
  GroupId parent = 0;
  bool visible = true;
};

/// Changes already applied live (a plugin run) that become one undo step together.
struct LiveChanges {
  std::vector<EntityId> added;                               ///< still in the document
  std::vector<Entity> removed;                               ///< as they were before the run
  std::vector<GroupId> added_groups;                         ///< in creation order
  std::vector<Group> removed_groups;                         ///< empty groups, in removal order
  std::vector<std::pair<EntityId, GeometryPtr>> old_geometry;
  std::vector<std::pair<EntityId, QVariantMap>> old_attributes;
};

struct Layer {
  LayerId id = 0;
  QString name;
  QColor color;
  bool visible = true;
};

/// The model: entities and layers, undo stack, file path (docs/architecture.md).
/// All changes that the user can undo go through commands on undoStack(); the low-level
/// insert/erase functions below are used by those commands and by loading.
class Document : public QObject {
  Q_OBJECT
 public:
  explicit Document(QObject* parent = nullptr);

  // ---- queries
  [[nodiscard]] const std::map<EntityId, Entity>& entities() const { return entities_; }
  [[nodiscard]] const Entity* entity(EntityId id) const;
  [[nodiscard]] const std::vector<Layer>& layers() const { return layers_; }
  [[nodiscard]] const Layer* layer(LayerId id) const;
  /// Effective display colour: entity colour or its layer's colour.
  [[nodiscard]] QColor displayColor(const Entity& e) const;
  /// Visible if the entity, its layer and all its groups are visible.
  [[nodiscard]] bool isDisplayed(const Entity& e) const;

  // ---- model tree (groups)
  [[nodiscard]] const std::vector<Group>& groups() const { return groups_; }
  [[nodiscard]] const Group* group(GroupId id) const;
  /// True if `id` or one of its ancestors is hidden.
  [[nodiscard]] bool groupHidden(GroupId id) const;
  /// Entities directly or indirectly in a group.
  [[nodiscard]] std::vector<EntityId> entitiesInGroup(GroupId id) const;
  /// Creates a group (one undo step) and moves the entities into it; returns its id.
  GroupId groupEntities(const std::vector<EntityId>& ids, const QString& name, GroupId parent = 0);
  /// Removes a group (one undo step); its entities and subgroups move to its parent.
  void ungroup(GroupId id);
  void renameGroup(GroupId id, const QString& name);
  /// Visibility of entities and groups (view state, not undoable – like layer visibility).
  void setEntityVisible(EntityId id, bool visible);
  void setGroupVisible(GroupId id, bool visible);
  /// Hides everything except the given entities (and shows them and their groups).
  void isolate(const std::vector<EntityId>& ids);
  void showAll();

  // ---- attributes (undoable)
  void setAttribute(EntityId id, const QString& key, const QVariant& value);  ///< null: remove

  /// Records changes that were already applied without undo (live updates of a plugin run) as
  /// one undo step: the added entities, the previous geometry and attributes of changed ones.
  void recordApplied(const QString& text, const LiveChanges& changes);

  // ---- low level (undo commands, loading)
  GroupId insertGroup(Group g);  ///< keeps g.id if free, else assigns one
  void eraseGroup(GroupId id);
  void setEntityGroup(EntityId id, GroupId group);
  void setEntityAttributes(EntityId id, QVariantMap attributes);

  [[nodiscard]] QUndoStack* undoStack() { return &undo_; }
  [[nodiscard]] QString filePath() const { return file_path_; }
  void setFilePath(const QString& path) { file_path_ = path; }
  [[nodiscard]] bool isModified() const { return !undo_.isClean() || settings_dirty_; }
  /// After saving: the document counts as unmodified.
  void markSaved();

  // ---- plugin parameters (P4): values of a plugin's parameter panel, saved with the document
  [[nodiscard]] QJsonObject pluginParameters(const QString& plugin) const {
    return plugin_parameters_.value(plugin).toObject();
  }
  [[nodiscard]] const QJsonObject& allPluginParameters() const { return plugin_parameters_; }
  void setPluginParameters(const QString& plugin, const QJsonObject& values);
  void setAllPluginParameters(const QJsonObject& all);  ///< loading

  // ---- node workflow (RTT-Fusion R16): {"graph": rtt-fusion-graph, "types": [...],
  // "diagnostics": [...]}, saved with the document; edited in the node editor and by plugins
  [[nodiscard]] const QJsonObject& workflow() const { return workflow_; }
  void setWorkflow(const QJsonObject& workflow);
  void setWorkflowLoaded(const QJsonObject& workflow) { workflow_ = workflow; }  ///< loading
  /// Asks the GUI to show the node workflow (a plugin set it, even if it did not change).
  void requestWorkflowShown() { emit workflowShowRequested(); }
  /// The block of the node workflow that a plugin computes now (empty: none); not stored.
  void reportWorkflowRunning(const QString& node) { emit workflowRunning(node); }
  /// Up to date (true) / out of date (false) per block, from the plugin's kept results.
  void reportWorkflowState(const QJsonObject& states) { emit workflowState(states); }

  // ---- user operations (undoable)
  /// Adds entities in one undo step; returns their ids.
  std::vector<EntityId> addEntities(std::vector<Entity> entities, const QString& text);
  void removeEntities(const std::vector<EntityId>& ids, const QString& text);
  /// Applies an Edit as one undo step; returns the ids of the added entities.
  std::vector<EntityId> apply(Edit edit, const QString& text);
  /// Moves entities to a layer (one undo step).
  void assignLayer(const std::vector<EntityId>& ids, LayerId layer);

  // ---- low level (used by undo commands and loading; not undoable)
  /// Inserts an entity. Assigns a new id if e.id == 0.
  EntityId insert(Entity e);
  /// Removes an entity and returns it (for undo).
  Entity erase(EntityId id);
  LayerId addLayer(Layer layer);
  void setLayerVisible(LayerId id, bool visible);
  void setLayerColor(LayerId id, const QColor& color);
  void renameLayer(LayerId id, const QString& name);
  /// Replaces an entity's geometry (low level, used by undo commands); returns the old one.
  GeometryPtr setGeometry(EntityId id, GeometryPtr geometry);
  /// Sets an entity's layer (low level, used by the undo command).
  void setEntityLayer(EntityId id, LayerId layer);
  /// Removes everything, keeps the default layer, clears the undo stack.
  void clear();
  /// Replaces the content (after loading a file); clears the undo stack.
  void reset(std::vector<Layer> layers, std::vector<Entity> entities,
             std::vector<Group> groups = {});

 signals:
  void entityAdded(nfcad::document::EntityId id);
  void entityRemoved(nfcad::document::EntityId id);
  void entityChanged(nfcad::document::EntityId id);
  void layersChanged();
  /// Groups were added, removed, renamed or changed visibility.
  void groupsChanged();
  /// The modified state changed without an undo step (plugin parameters, saving).
  void modificationChanged(bool modified);
  /// The node workflow was replaced or edited.
  void workflowChanged();
  void workflowShowRequested();
  void workflowRunning(const QString& node);
  void workflowState(const QJsonObject& states);
  /// Everything changed (clear / load): views rebuild from scratch.
  void documentReset();

 private:
  void touchLayer(LayerId id);
  void touchEntities(const std::vector<EntityId>& ids);

  std::map<EntityId, Entity> entities_;
  std::vector<Layer> layers_;
  std::vector<Group> groups_;
  QJsonObject plugin_parameters_;
  QJsonObject workflow_;
  bool settings_dirty_ = false;
  EntityId next_id_ = 1;
  QUndoStack undo_;
  QString file_path_;
};

}  // namespace nfcad::document
