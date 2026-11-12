#include "render/scene.hpp"

namespace nfcad::render {

Scene::Scene(document::Document* doc, tessellation::TessellationService* service, QObject* parent)
    : QObject(parent), doc_(doc), service_(service) {
  connect(doc_, &document::Document::entityAdded, this, &Scene::add);
  connect(doc_, &document::Document::entityChanged, this, &Scene::update);
  connect(doc_, &document::Document::entityRemoved, this, &Scene::remove);
  connect(doc_, &document::Document::documentReset, this, &Scene::rebuild);
  connect(service_, &tessellation::TessellationService::meshReady, this, &Scene::onMeshReady);
  rebuild();
}

void Scene::add(document::EntityId id) {
  const document::Entity* e = doc_->entity(id);
  if (e == nullptr) return;
  Item& item = items_[id];
  item.geometry = e->geometry;
  item.color = doc_->displayColor(*e);
  item.visible = doc_->isDisplayed(*e);
  item.mesh.reset();
  service_->request(id, e->revision, e->geometry);
  emit changed();
}

void Scene::update(document::EntityId id) {
  const document::Entity* e = doc_->entity(id);
  const auto it = items_.find(id);
  if (e == nullptr || it == items_.end()) {
    add(id);
    return;
  }
  it->second.color = doc_->displayColor(*e);
  it->second.visible = doc_->isDisplayed(*e);
  if (it->second.geometry != e->geometry) {
    it->second.geometry = e->geometry;
    service_->request(id, e->revision, e->geometry);  // keep the old mesh until the new arrives
  }
  emit changed();
}

void Scene::remove(document::EntityId id) {
  service_->cancel(id);
  items_.erase(id);
  emit changed();
}

void Scene::rebuild() {
  service_->cancelAll();
  items_.clear();
  for (const auto& [id, e] : doc_->entities()) {
    Item& item = items_[id];
    item.geometry = e.geometry;
    item.color = doc_->displayColor(e);
    item.visible = doc_->isDisplayed(e);
    service_->request(id, e.revision, e.geometry);
  }
  emit changed();
}

void Scene::onMeshReady(document::EntityId id, std::uint64_t /*revision*/,
                        tessellation::DisplayMeshPtr mesh) {
  const auto it = items_.find(id);
  if (it == items_.end()) return;
  it->second.mesh = std::move(mesh);
  emit changed();
}

tessellation::Box3 Scene::bounds() const {
  tessellation::Box3 box;
  for (const auto& [id, item] : items_) {
    if (item.visible && item.mesh) box.add(item.mesh->bounds);
  }
  return box;
}

tessellation::Box3 Scene::bounds(const std::vector<document::SelectionItem>& selected) const {
  using Kind = document::SubElement::Kind;
  tessellation::Box3 box;
  for (const document::SelectionItem& s : selected) {
    const auto it = items_.find(s.entity);
    if (it == items_.end() || !it->second.visible || !it->second.mesh) continue;
    const tessellation::DisplayMesh& m = *it->second.mesh;
    const int i = s.sub.index;
    auto vertex = [&m](std::uint32_t v) {
      return QVector3D(m.vertices[v * tessellation::kVertexStride], m.vertices[v * tessellation::kVertexStride + 1],
                       m.vertices[v * tessellation::kVertexStride + 2]);
    };
    switch (s.sub.kind) {
      case Kind::None: box.add(m.bounds); break;
      case Kind::Face:
        if (i >= 0 && static_cast<std::size_t>(i) + 1 < m.face_starts.size()) {
          for (std::uint32_t k = m.face_starts[i]; k < m.face_starts[i + 1]; ++k) box.add(vertex(m.indices[k]));
        }
        break;
      case Kind::Edge:
        if (i >= 0 && static_cast<std::size_t>(i) < m.edges.size()) {
          const tessellation::Polyline& p = m.edges[i];
          for (std::size_t k = 0; k + 2 < p.size(); k += 3) box.add(QVector3D(p[k], p[k + 1], p[k + 2]));
        }
        break;
      case Kind::Vertex:
        if (i >= 0 && static_cast<std::size_t>(3 * i + 2) < m.corners.size()) {
          box.add(QVector3D(m.corners[3 * i], m.corners[3 * i + 1], m.corners[3 * i + 2]));
        }
        break;
    }
  }
  return box;
}

std::vector<pick::Target> Scene::pickTargets() const {
  std::vector<pick::Target> out;
  out.reserve(items_.size());
  for (const auto& [id, item] : items_) {
    if (item.visible && item.mesh) out.push_back({id, item.mesh.get(), item.geometry});
  }
  return out;
}

std::size_t Scene::triangleCount() const {
  std::size_t n = 0;
  for (const auto& [id, item] : items_) {
    if (item.mesh) n += item.mesh->triangleCount();
  }
  return n;
}

}  // namespace nfcad::render
