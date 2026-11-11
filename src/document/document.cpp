#include "document/document.hpp"

#include <QUndoCommand>
#include <algorithm>

namespace nfcad::document {

namespace {

Layer default_layer() { return {0, QObject::tr("Default"), QColor(190, 198, 210), true}; }

class AddEntitiesCommand : public QUndoCommand {
 public:
  AddEntitiesCommand(Document* doc, std::vector<Entity> entities, const QString& text)
      : QUndoCommand(text), doc_(doc), entities_(std::move(entities)) {}
  void redo() override {
    for (Entity& e : entities_) e.id = doc_->insert(e);
  }
  void undo() override {
    for (auto it = entities_.rbegin(); it != entities_.rend(); ++it) doc_->erase(it->id);
  }
  [[nodiscard]] std::vector<EntityId> ids() const {
    std::vector<EntityId> out;
    for (const Entity& e : entities_) out.push_back(e.id);
    return out;
  }

 private:
  Document* doc_;
  std::vector<Entity> entities_;
};

class RemoveEntitiesCommand : public QUndoCommand {
 public:
  RemoveEntitiesCommand(Document* doc, std::vector<EntityId> ids, const QString& text)
      : QUndoCommand(text), doc_(doc), ids_(std::move(ids)) {}
  void redo() override {
    removed_.clear();
    for (EntityId id : ids_) {
      if (doc_->entity(id) != nullptr) removed_.push_back(doc_->erase(id));
    }
  }
  void undo() override {
    for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) doc_->insert(*it);
  }

 private:
  Document* doc_;
  std::vector<EntityId> ids_;
  std::vector<Entity> removed_;
};

class AssignLayerCommand : public QUndoCommand {
 public:
  AssignLayerCommand(Document* doc, std::vector<EntityId> ids, LayerId layer)
      : QUndoCommand(QObject::tr("Change layer")), doc_(doc), ids_(std::move(ids)), layer_(layer) {
    for (EntityId id : ids_) {
      const Entity* e = doc_->entity(id);
      old_.push_back(e != nullptr ? e->layer : 0);
    }
  }
  void redo() override {
    for (EntityId id : ids_) doc_->setEntityLayer(id, layer_);
  }
  void undo() override {
    for (std::size_t i = 0; i < ids_.size(); ++i) doc_->setEntityLayer(ids_[i], old_[i]);
  }

 private:
  Document* doc_;
  std::vector<EntityId> ids_;
  std::vector<LayerId> old_;
  LayerId layer_;
};

}  // namespace

Document::Document(QObject* parent) : QObject(parent) { layers_.push_back(default_layer()); }

const Entity* Document::entity(EntityId id) const {
  const auto it = entities_.find(id);
  return it == entities_.end() ? nullptr : &it->second;
}

const Layer* Document::layer(LayerId id) const {
  const auto it = std::find_if(layers_.begin(), layers_.end(),
                               [id](const Layer& l) { return l.id == id; });
  return it == layers_.end() ? nullptr : &*it;
}

QColor Document::displayColor(const Entity& e) const {
  if (e.color) return *e.color;
  const Layer* l = layer(e.layer);
  return l != nullptr ? l->color : default_layer().color;
}

bool Document::isDisplayed(const Entity& e) const {
  const Layer* l = layer(e.layer);
  return e.visible && (l == nullptr || l->visible);
}

std::vector<EntityId> Document::addEntities(std::vector<Entity> entities, const QString& text) {
  auto* cmd = new AddEntitiesCommand(this, std::move(entities), text);
  undo_.push(cmd);  // runs redo()
  return cmd->ids();
}

void Document::removeEntities(const std::vector<EntityId>& ids, const QString& text) {
  undo_.push(new RemoveEntitiesCommand(this, ids, text));
}

EntityId Document::insert(Entity e) {
  if (e.id == 0) e.id = next_id_++;
  next_id_ = std::max(next_id_, e.id + 1);
  if (layer(e.layer) == nullptr) e.layer = 0;
  const EntityId id = e.id;
  entities_.insert_or_assign(id, std::move(e));
  emit entityAdded(id);
  return id;
}

Entity Document::erase(EntityId id) {
  auto node = entities_.extract(id);
  if (node.empty()) return {};
  emit entityRemoved(id);
  return std::move(node.mapped());
}

void Document::assignLayer(const std::vector<EntityId>& ids, LayerId layer) {
  if (ids.empty() || this->layer(layer) == nullptr) return;
  undo_.push(new AssignLayerCommand(this, ids, layer));
}

void Document::setEntityLayer(EntityId id, LayerId layer) {
  const auto it = entities_.find(id);
  if (it == entities_.end() || it->second.layer == layer) return;
  it->second.layer = layer;
  ++it->second.revision;
  emit entityChanged(id);
}

void Document::setLayerColor(LayerId id, const QColor& color) {
  for (Layer& l : layers_) {
    if (l.id == id && l.color != color) {
      l.color = color;
      touchLayer(id);
      emit layersChanged();
    }
  }
}

void Document::renameLayer(LayerId id, const QString& name) {
  for (Layer& l : layers_) {
    if (l.id == id && l.name != name) {
      l.name = name;
      emit layersChanged();
    }
  }
}

LayerId Document::addLayer(Layer layer) {
  LayerId next = 0;
  for (const Layer& l : layers_) next = std::max(next, l.id + 1);
  layer.id = next;
  layers_.push_back(std::move(layer));
  emit layersChanged();
  return next;
}

void Document::setLayerVisible(LayerId id, bool visible) {
  for (Layer& l : layers_) {
    if (l.id == id && l.visible != visible) {
      l.visible = visible;
      touchLayer(id);
      emit layersChanged();
    }
  }
}

void Document::touchLayer(LayerId id) {
  for (auto& [eid, e] : entities_) {
    if (e.layer == id) {
      ++e.revision;
      emit entityChanged(eid);
    }
  }
}

void Document::clear() { reset({default_layer()}, {}); }

void Document::reset(std::vector<Layer> layers, std::vector<Entity> entities) {
  undo_.clear();
  entities_.clear();
  layers_ = std::move(layers);
  if (layer(0) == nullptr) layers_.insert(layers_.begin(), default_layer());
  next_id_ = 1;
  for (Entity& e : entities) {
    if (e.id == 0) e.id = next_id_;
    next_id_ = std::max(next_id_, e.id + 1);
    if (layer(e.layer) == nullptr) e.layer = 0;
    entities_.insert_or_assign(e.id, std::move(e));
  }
  file_path_.clear();
  emit layersChanged();
  emit documentReset();
}

}  // namespace nfcad::document
