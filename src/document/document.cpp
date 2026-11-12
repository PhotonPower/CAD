#include "document/document.hpp"

#include <QJsonArray>
#include <QUndoCommand>
#include <algorithm>
#include <set>
#include <tuple>

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

class EditCommand : public QUndoCommand {
 public:
  EditCommand(Document* doc, Edit edit, const QString& text)
      : QUndoCommand(text), doc_(doc), edit_(std::move(edit)) {}
  void redo() override {
    removed_.clear();
    for (EntityId id : edit_.remove) {
      if (doc_->entity(id) != nullptr) removed_.push_back(doc_->erase(id));
    }
    old_.clear();
    for (auto& [id, g] : edit_.replace) old_.emplace_back(id, doc_->setGeometry(id, g));
    for (Entity& e : edit_.add) e.id = doc_->insert(e);
  }
  void undo() override {
    for (auto it = edit_.add.rbegin(); it != edit_.add.rend(); ++it) doc_->erase(it->id);
    for (auto it = old_.rbegin(); it != old_.rend(); ++it) doc_->setGeometry(it->first, it->second);
    for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) doc_->insert(*it);
  }
  [[nodiscard]] std::vector<EntityId> added() const {
    std::vector<EntityId> out;
    for (const Entity& e : edit_.add) out.push_back(e.id);
    return out;
  }

 private:
  Document* doc_;
  Edit edit_;
  std::vector<Entity> removed_;
  std::vector<std::pair<EntityId, GeometryPtr>> old_;
};

class GroupCommand : public QUndoCommand {
 public:
  GroupCommand(Document* doc, Group group, std::vector<EntityId> ids)
      : QUndoCommand(QObject::tr("Group")), doc_(doc), group_(std::move(group)), ids_(std::move(ids)) {
    for (EntityId id : ids_) {
      const Entity* e = doc_->entity(id);
      old_.push_back(e != nullptr ? e->group : 0);
    }
  }
  void redo() override {
    group_.id = doc_->insertGroup(group_);
    for (EntityId id : ids_) doc_->setEntityGroup(id, group_.id);
  }
  void undo() override {
    for (std::size_t i = 0; i < ids_.size(); ++i) doc_->setEntityGroup(ids_[i], old_[i]);
    doc_->eraseGroup(group_.id);
  }
  [[nodiscard]] GroupId groupId() const { return group_.id; }

 private:
  Document* doc_;
  Group group_;
  std::vector<EntityId> ids_;
  std::vector<GroupId> old_;
};

class UngroupCommand : public QUndoCommand {
 public:
  UngroupCommand(Document* doc, Group group)
      : QUndoCommand(QObject::tr("Ungroup")), doc_(doc), group_(std::move(group)) {}
  void redo() override {
    members_.clear();
    subgroups_.clear();
    for (const auto& [id, e] : doc_->entities()) {
      if (e.group == group_.id) members_.push_back(id);
    }
    for (const Group& g : doc_->groups()) {
      if (g.parent == group_.id) subgroups_.push_back(g);
    }
    for (EntityId id : members_) doc_->setEntityGroup(id, group_.parent);
    for (Group g : subgroups_) {
      doc_->eraseGroup(g.id);
      g.parent = group_.parent;
      doc_->insertGroup(g);
    }
    doc_->eraseGroup(group_.id);
  }
  void undo() override {
    doc_->insertGroup(group_);
    for (const Group& g : subgroups_) {
      doc_->eraseGroup(g.id);
      doc_->insertGroup(g);
    }
    for (EntityId id : members_) doc_->setEntityGroup(id, group_.id);
  }

 private:
  Document* doc_;
  Group group_;
  std::vector<EntityId> members_;
  std::vector<Group> subgroups_;
};

class AttributeCommand : public QUndoCommand {
 public:
  AttributeCommand(Document* doc, EntityId id, QVariantMap before, QVariantMap after)
      : QUndoCommand(QObject::tr("Change attribute")), doc_(doc), id_(id),
        before_(std::move(before)), after_(std::move(after)) {}
  void redo() override { doc_->setEntityAttributes(id_, after_); }
  void undo() override { doc_->setEntityAttributes(id_, before_); }

 private:
  Document* doc_;
  EntityId id_;
  QVariantMap before_, after_;
};

class AppliedCommand : public QUndoCommand {
 public:
  AppliedCommand(Document* doc, const QString& text, std::vector<Entity> added, std::vector<Entity> removed,
                 std::vector<Group> added_groups, std::vector<Group> removed_groups,
                 std::vector<std::tuple<EntityId, GeometryPtr, GeometryPtr>> geometry,
                 std::vector<std::tuple<EntityId, QVariantMap, QVariantMap>> attributes,
                 std::optional<std::pair<QJsonObject, QJsonObject>> workflow = std::nullopt)
      : QUndoCommand(text), doc_(doc), added_(std::move(added)), removed_(std::move(removed)),
        added_groups_(std::move(added_groups)), removed_groups_(std::move(removed_groups)),
        geometry_(std::move(geometry)), attributes_(std::move(attributes)), workflow_(std::move(workflow)) {}
  void redo() override {
    if (first_) {  // already applied live
      first_ = false;
      return;
    }
    for (const Entity& e : removed_) doc_->erase(e.id);
    for (const Group& g : removed_groups_) doc_->eraseGroup(g.id);
    for (const Group& g : added_groups_) doc_->insertGroup(g);
    for (const Entity& e : added_) doc_->insert(e);
    for (const auto& [id, before, after] : geometry_) doc_->setGeometry(id, after);
    for (const auto& [id, before, after] : attributes_) doc_->setEntityAttributes(id, after);
    if (workflow_) doc_->setWorkflow(workflow_->second);
  }
  void undo() override {
    if (workflow_) doc_->setWorkflow(workflow_->first);
    for (const auto& [id, before, after] : attributes_) doc_->setEntityAttributes(id, before);
    for (const auto& [id, before, after] : geometry_) doc_->setGeometry(id, before);
    for (auto it = added_.rbegin(); it != added_.rend(); ++it) doc_->erase(it->id);
    for (auto it = added_groups_.rbegin(); it != added_groups_.rend(); ++it) doc_->eraseGroup(it->id);
    for (auto it = removed_groups_.rbegin(); it != removed_groups_.rend(); ++it) doc_->insertGroup(*it);
    for (const Entity& e : removed_) doc_->insert(e);
  }

 private:
  Document* doc_;
  bool first_ = true;
  std::vector<Entity> added_;
  std::vector<Entity> removed_;
  std::vector<Group> added_groups_;
  std::vector<Group> removed_groups_;
  std::vector<std::tuple<EntityId, GeometryPtr, GeometryPtr>> geometry_;
  std::vector<std::tuple<EntityId, QVariantMap, QVariantMap>> attributes_;
  std::optional<std::pair<QJsonObject, QJsonObject>> workflow_;  ///< before, after
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
  return e.visible && (l == nullptr || l->visible) && !groupHidden(e.group);
}

const Group* Document::group(GroupId id) const {
  const auto it = std::find_if(groups_.begin(), groups_.end(), [id](const Group& g) { return g.id == id; });
  return it == groups_.end() ? nullptr : &*it;
}

bool Document::groupHidden(GroupId id) const {
  for (int depth = 0; id != 0 && depth < 1000; ++depth) {  // depth guard against cycles
    const Group* g = group(id);
    if (g == nullptr) return false;
    if (!g->visible) return true;
    id = g->parent;
  }
  return false;
}

std::vector<EntityId> Document::entitiesInGroup(GroupId id) const {
  auto inside = [&](GroupId g) {
    for (int depth = 0; g != 0 && depth < 1000; ++depth) {
      if (g == id) return true;
      const Group* gr = group(g);
      g = gr != nullptr ? gr->parent : 0;
    }
    return false;
  };
  std::vector<EntityId> out;
  for (const auto& [eid, e] : entities_) {
    if (inside(e.group)) out.push_back(eid);
  }
  return out;
}

GroupId Document::groupEntities(const std::vector<EntityId>& ids, const QString& name, GroupId parent) {
  Group g;
  g.name = name;
  g.parent = group(parent) != nullptr ? parent : 0;
  auto* cmd = new GroupCommand(this, g, ids);
  undo_.push(cmd);
  return cmd->groupId();
}

void Document::ungroup(GroupId id) {
  if (const Group* g = group(id)) undo_.push(new UngroupCommand(this, *g));
}

void Document::renameGroup(GroupId id, const QString& name) {
  for (Group& g : groups_) {
    if (g.id == id && g.name != name) {
      g.name = name;
      emit groupsChanged();
    }
  }
}

void Document::setEntityVisible(EntityId id, bool visible) {
  const auto it = entities_.find(id);
  if (it == entities_.end() || it->second.visible == visible) return;
  it->second.visible = visible;
  ++it->second.revision;
  emit entityChanged(id);
}

void Document::setGroupVisible(GroupId id, bool visible) {
  for (Group& g : groups_) {
    if (g.id == id && g.visible != visible) {
      g.visible = visible;
      touchEntities(entitiesInGroup(id));
      emit groupsChanged();
    }
  }
}

void Document::isolate(const std::vector<EntityId>& ids) {
  const std::set<EntityId> keep(ids.begin(), ids.end());
  for (Group& g : groups_) g.visible = true;  // keep the path to the isolated objects open
  for (auto& [id, e] : entities_) {
    const bool v = keep.contains(id);
    if (e.visible != v) {
      e.visible = v;
      ++e.revision;
    }
  }
  touchEntities([this] {
    std::vector<EntityId> all;
    for (const auto& [id, e] : entities_) all.push_back(id);
    return all;
  }());
  emit groupsChanged();
}

void Document::showAll() {
  for (Group& g : groups_) g.visible = true;
  for (Layer& l : layers_) l.visible = true;
  std::vector<EntityId> all;
  for (auto& [id, e] : entities_) {
    e.visible = true;
    ++e.revision;
    all.push_back(id);
  }
  touchEntities(all);
  emit groupsChanged();
  emit layersChanged();
}

void Document::setAttribute(EntityId id, const QString& key, const QVariant& value) {
  const Entity* e = entity(id);
  if (e == nullptr) return;
  QVariantMap after = e->attributes;
  if (value.isValid() && !value.isNull()) {
    if (after.value(key) == value) return;
    after.insert(key, value);
  } else {
    if (!after.contains(key)) return;
    after.remove(key);
  }
  undo_.push(new AttributeCommand(this, id, e->attributes, after));
}

GroupId Document::insertGroup(Group g) {
  GroupId next = 1;
  for (const Group& o : groups_) next = std::max(next, o.id + 1);
  if (g.id == 0 || group(g.id) != nullptr) g.id = next;
  groups_.push_back(g);
  emit groupsChanged();
  return g.id;
}

void Document::eraseGroup(GroupId id) {
  std::erase_if(groups_, [id](const Group& g) { return g.id == id; });
  emit groupsChanged();
}

void Document::setEntityGroup(EntityId id, GroupId group) {
  const auto it = entities_.find(id);
  if (it == entities_.end() || it->second.group == group) return;
  it->second.group = group;
  ++it->second.revision;
  emit entityChanged(id);
  emit groupsChanged();
}

void Document::setEntityAttributes(EntityId id, QVariantMap attributes) {
  const auto it = entities_.find(id);
  if (it == entities_.end()) return;
  it->second.attributes = std::move(attributes);
  ++it->second.revision;
  emit entityChanged(id);
}

void Document::touchEntities(const std::vector<EntityId>& ids) {
  for (EntityId id : ids) {
    if (entities_.contains(id)) emit entityChanged(id);
  }
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

std::vector<EntityId> Document::apply(Edit edit, const QString& text) {
  if (edit.empty()) return {};
  auto* cmd = new EditCommand(this, std::move(edit), text);
  undo_.push(cmd);
  return cmd->added();
}

void Document::markSaved() {
  undo_.setClean();
  if (settings_dirty_) {
    settings_dirty_ = false;
    emit modificationChanged(isModified());
  }
}

void Document::setPluginParameters(const QString& plugin, const QJsonObject& values) {
  if (plugin_parameters_.value(plugin).toObject() == values) return;
  plugin_parameters_.insert(plugin, values);
  if (!settings_dirty_) {
    settings_dirty_ = true;
    emit modificationChanged(true);
  }
}

void Document::setAllPluginParameters(const QJsonObject& all) { plugin_parameters_ = all; }

void Document::setWorkflow(const QJsonObject& workflow) {
  if (workflow_ == workflow) return;
  workflow_ = workflow;
  if (!settings_dirty_) {
    settings_dirty_ = true;
    emit modificationChanged(true);
  }
  emit workflowChanged();
}

void Document::recordApplied(const QString& text, const LiveChanges& changes) {
  std::vector<Entity> add;
  for (EntityId id : changes.added) {
    if (const Entity* e = entity(id)) add.push_back(*e);
  }
  std::vector<Group> groups;
  for (GroupId id : changes.added_groups) {
    if (const Group* g = group(id)) groups.push_back(*g);
  }
  std::vector<std::tuple<EntityId, GeometryPtr, GeometryPtr>> geometry;
  for (const auto& [id, before] : changes.old_geometry) {
    if (const Entity* e = entity(id)) geometry.emplace_back(id, before, e->geometry);
  }
  std::vector<std::tuple<EntityId, QVariantMap, QVariantMap>> attributes;
  for (const auto& [id, before] : changes.old_attributes) {
    if (const Entity* e = entity(id)) attributes.emplace_back(id, before, e->attributes);
  }
  std::optional<std::pair<QJsonObject, QJsonObject>> workflow;
  if (changes.old_workflow) {  // only a change of the model / graph itself (not of the checks)
    // what a plugin derives for the editor (fields, the sockets and ports it fills in) is no edit
    auto content = [](const QJsonObject& w) {
      QJsonObject model = w["model"].toObject();
      QJsonArray nodes;
      for (const QJsonValue& v : model["nodes"].toArray()) {
        QJsonObject n = v.toObject();
        for (const char* key : {"fields", "sockets", "ports"}) n.remove(QLatin1String(key));
        nodes.append(n);
      }
      model["nodes"] = nodes;
      return QJsonObject{{"model", model}, {"graph", w["graph"]}};
    };
    if (content(*changes.old_workflow) != content(workflow_)) workflow.emplace(*changes.old_workflow, workflow_);
  }
  if (add.empty() && groups.empty() && changes.removed.empty() && changes.removed_groups.empty() &&
      geometry.empty() && attributes.empty() && !workflow) {
    return;
  }
  undo_.push(new AppliedCommand(this, text, std::move(add), changes.removed, std::move(groups),
                                changes.removed_groups, std::move(geometry), std::move(attributes),
                                std::move(workflow)));
}

GeometryPtr Document::setGeometry(EntityId id, GeometryPtr geometry) {
  const auto it = entities_.find(id);
  if (it == entities_.end()) return {};
  GeometryPtr old = std::exchange(it->second.geometry, std::move(geometry));
  ++it->second.revision;
  emit entityChanged(id);
  return old;
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

void Document::reset(std::vector<Layer> layers, std::vector<Entity> entities,
                     std::vector<Group> groups) {
  undo_.clear();
  entities_.clear();
  plugin_parameters_ = {};
  workflow_ = {};
  settings_dirty_ = false;
  groups_ = std::move(groups);
  layers_ = std::move(layers);
  if (layer(0) == nullptr) layers_.insert(layers_.begin(), default_layer());
  next_id_ = 1;
  for (Entity& e : entities) {
    if (e.id == 0) e.id = next_id_;
    next_id_ = std::max(next_id_, e.id + 1);
    if (layer(e.layer) == nullptr) e.layer = 0;
    if (group(e.group) == nullptr) e.group = 0;
    entities_.insert_or_assign(e.id, std::move(e));
  }
  file_path_.clear();
  emit layersChanged();
  emit groupsChanged();
  emit documentReset();
}

}  // namespace nfcad::document
