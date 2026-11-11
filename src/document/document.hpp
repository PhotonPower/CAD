#pragma once

#include <QObject>
#include <QUndoStack>
#include <map>
#include <vector>

#include "document/entity.hpp"

namespace nfcad::document {

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
  /// Visible if the entity and its layer are visible.
  [[nodiscard]] bool isDisplayed(const Entity& e) const;

  [[nodiscard]] QUndoStack* undoStack() { return &undo_; }
  [[nodiscard]] QString filePath() const { return file_path_; }
  void setFilePath(const QString& path) { file_path_ = path; }
  [[nodiscard]] bool isModified() const { return !undo_.isClean(); }

  // ---- user operations (undoable)
  /// Adds entities in one undo step; returns their ids.
  std::vector<EntityId> addEntities(std::vector<Entity> entities, const QString& text);
  void removeEntities(const std::vector<EntityId>& ids, const QString& text);

  // ---- low level (used by undo commands and loading; not undoable)
  /// Inserts an entity. Assigns a new id if e.id == 0.
  EntityId insert(Entity e);
  /// Removes an entity and returns it (for undo).
  Entity erase(EntityId id);
  LayerId addLayer(Layer layer);
  void setLayerVisible(LayerId id, bool visible);
  /// Removes everything, keeps the default layer, clears the undo stack.
  void clear();
  /// Replaces the content (after loading a file); clears the undo stack.
  void reset(std::vector<Layer> layers, std::vector<Entity> entities);

 signals:
  void entityAdded(nfcad::document::EntityId id);
  void entityRemoved(nfcad::document::EntityId id);
  void entityChanged(nfcad::document::EntityId id);
  void layersChanged();
  /// Everything changed (clear / load): views rebuild from scratch.
  void documentReset();

 private:
  void touchLayer(LayerId id);

  std::map<EntityId, Entity> entities_;
  std::vector<Layer> layers_;
  EntityId next_id_ = 1;
  QUndoStack undo_;
  QString file_path_;
};

}  // namespace nfcad::document
