#include "render/scene_renderer.hpp"

#include <QFile>
#include <algorithm>

namespace nfcad::render {

namespace {

using document::SubElement;

constexpr quint32 kUniformSize = 64 + 64 + 16 + 16;  // mvp, view, color, params
constexpr float kPointMarkerPx = 5.0F;
constexpr float kVertexMarkerPx = 7.0F;
// Edges and curves are pulled towards the camera in clip space (z' = z - eps * w) so that they
// win the depth test against the faces they bound, on every backend (a polygon offset on the
// faces alone is too weak for faces seen head-on with Metal's float depth buffer).
constexpr float kLineDepthOffset = 5e-5F;

QMatrix4x4 line_offset(const QMatrix4x4& mvp) {
  QMatrix4x4 offset;  // identity
  offset(2, 3) = -kLineDepthOffset;
  return offset * mvp;
}

QShader load_shader(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) qFatal("cannot load shader %s", qPrintable(path));
  return QShader::fromSerialized(f.readAll());
}

using Rgba = std::array<float, 4>;

Rgba rgba(const QColor& c, float scale = 1.0F, float alpha = 1.0F) {
  return {static_cast<float>(c.redF()) * scale, static_cast<float>(c.greenF()) * scale,
          static_cast<float>(c.blueF()) * scale, alpha};
}

QColor mix(const QColor& a, const QColor& b, double t) {
  return QColor::fromRgbF(static_cast<float>(a.redF() * (1 - t) + b.redF() * t),
                          static_cast<float>(a.greenF() * (1 - t) + b.greenF() * t),
                          static_cast<float>(a.blueF() * (1 - t) + b.blueF() * t));
}

void add_polyline(std::vector<LineVertex>& out, const tessellation::Polyline& pl, const Rgba& c) {
  for (std::size_t i = 0; i + 5 < pl.size(); i += 3) {
    out.push_back({pl[i], pl[i + 1], pl[i + 2], c[0], c[1], c[2], c[3]});
    out.push_back({pl[i + 3], pl[i + 4], pl[i + 5], c[0], c[1], c[2], c[3]});
  }
}

void add_cross(std::vector<LineVertex>& out, float x, float y, float z, float h, const Rgba& c) {
  for (int axis = 0; axis < 3; ++axis) {
    float a[3] = {x, y, z};
    float b[3] = {x, y, z};
    a[axis] -= h;
    b[axis] += h;
    out.push_back({a[0], a[1], a[2], c[0], c[1], c[2], c[3]});
    out.push_back({b[0], b[1], b[2], c[0], c[1], c[2], c[3]});
  }
}

bool contains(const std::vector<SubElement>& v, SubElement::Kind kind, int index) {
  return std::find(v.begin(), v.end(), SubElement{kind, index}) != v.end();
}

std::unique_ptr<QRhiGraphicsPipeline> make_mesh_pipeline(QRhi* rhi, QRhiRenderPassDescriptor* rp,
                                                         QRhiShaderResourceBindings* layout,
                                                         int samples, bool overlay) {
  std::unique_ptr<QRhiGraphicsPipeline> p(rhi->newGraphicsPipeline());
  p->setShaderStages({
      {QRhiShaderStage::Vertex, load_shader(QStringLiteral(":/shaders/mesh.vert.qsb"))},
      {QRhiShaderStage::Fragment, load_shader(QStringLiteral(":/shaders/mesh.frag.qsb"))},
  });
  QRhiVertexInputLayout layout_in;
  layout_in.setBindings({{tessellation::kVertexStride * sizeof(float)}});
  layout_in.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                           {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)}});
  p->setVertexInputLayout(layout_in);
  p->setTopology(QRhiGraphicsPipeline::Triangles);
  p->setCullMode(QRhiGraphicsPipeline::None);
  p->setDepthTest(true);
  p->setDepthWrite(!overlay);
  // Overlay: the same triangles drawn again pass with an equal depth.
  p->setDepthOp(overlay ? QRhiGraphicsPipeline::LessOrEqual : QRhiGraphicsPipeline::Less);
  p->setDepthBias(2);
  p->setSlopeScaledDepthBias(1.5F);
  p->setSampleCount(samples);
  p->setShaderResourceBindings(layout);
  p->setRenderPassDescriptor(rp);
  p->create();
  return p;
}

}  // namespace

void SceneRenderer::initialize(QRhi* rhi, QRhiRenderTarget* target, int sample_count) {
  if (rhi == rhi_ && mesh_pipeline_ && rp_ == target->renderPassDescriptor()) return;
  release();
  lines_.initialize(rhi, target, sample_count);
  rhi_ = rhi;
  rp_ = target->renderPassDescriptor();

  layout_ubuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformSize));
  layout_ubuf_->create();
  layout_srb_.reset(rhi_->newShaderResourceBindings());
  layout_srb_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
      0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
      layout_ubuf_.get())});
  layout_srb_->create();
  mesh_pipeline_ = make_mesh_pipeline(rhi_, rp_, layout_srb_.get(), sample_count, false);
  overlay_pipeline_ = make_mesh_pipeline(rhi_, rp_, layout_srb_.get(), sample_count, true);
}

void SceneRenderer::release() {
  items_.clear();
  mesh_pipeline_.reset();
  overlay_pipeline_.reset();
  layout_srb_.reset();
  layout_ubuf_.reset();
  lines_.release();
  rhi_ = nullptr;
  rp_ = nullptr;
}

SceneRenderer::Style SceneRenderer::styleFor(document::EntityId id, const QColor& color,
                                             const FrameParams& frame) const {
  Style s;
  s.color = color;
  s.mode = frame.mode;
  if (frame.selection == nullptr) return s;
  if (frame.selection->isSelected(id)) {
    s.whole = Whole::Selected;
  } else {
    s.selected_parts = frame.selection->selectedParts(id);
  }
  if (const auto h = frame.selection->hovered(); h && h->entity == id) {
    if (h->sub.kind == SubElement::Kind::None) {
      if (s.whole == Whole::None) s.whole = Whole::Hovered;
    } else {
      s.hovered_part = h->sub;
    }
  }
  return s;
}

void SceneRenderer::ensureOverlay(Overlay& o) {
  if (o.ubuf) return;
  o.ubuf.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformSize));
  o.ubuf->create();
  o.srb.reset(rhi_->newShaderResourceBindings());
  o.srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
      0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
      o.ubuf.get())});
  o.srb->create();
}

void SceneRenderer::applyStyle(GpuItem& item) {
  const tessellation::DisplayMesh& m = *item.mesh;
  const Style& s = item.style;
  const bool wire = s.mode == DisplayMode::Wireframe;
  const bool has_faces = !m.indices.empty();
  const Rgba base = wire || !has_faces ? rgba(s.color) : Rgba{0.10F, 0.11F, 0.13F, 1.0F};
  const Rgba selected = rgba(kSelectedColor);
  const Rgba hovered = rgba(kHoverColor);
  auto edge_color = [&](int i) {
    if (s.whole == Whole::Selected) return selected;
    if (s.hovered_part && s.hovered_part->kind == SubElement::Kind::Edge && s.hovered_part->index == i) {
      return hovered;
    }
    if (contains(s.selected_parts, SubElement::Kind::Edge, i)) return selected;
    if (s.whole == Whole::Hovered) return hovered;
    return base;
  };
  std::vector<LineVertex> edges;
  for (std::size_t i = 0; i < m.edges.size(); ++i) {
    add_polyline(edges, m.edges[i], edge_color(static_cast<int>(i)));
  }
  item.edges.setVertices(std::move(edges));

  std::vector<LineVertex> iso;
  if (wire) {
    for (std::size_t i = 0; i < m.isocurves.size(); ++i) {
      const int face = i < m.iso_faces.size() ? m.iso_faces[i] : 0;
      Rgba c = rgba(s.color, 0.85F, 0.55F);
      if (s.whole == Whole::Selected || contains(s.selected_parts, SubElement::Kind::Face, face)) {
        c = rgba(kSelectedColor, 1.0F, 0.8F);
      } else if (s.whole == Whole::Hovered ||
                 (s.hovered_part && s.hovered_part->kind == SubElement::Kind::Face &&
                  s.hovered_part->index == face)) {
        c = rgba(kHoverColor, 1.0F, 0.8F);
      }
      add_polyline(iso, m.isocurves[i], c);
    }
  }
  item.isocurves.setVertices(std::move(iso));

  // face overlays
  auto range = [&](int face) -> std::optional<std::pair<quint32, quint32>> {
    if (face < 0 || static_cast<std::size_t>(face) + 1 >= m.face_starts.size()) return std::nullopt;
    const quint32 first = m.face_starts[static_cast<std::size_t>(face)];
    return std::make_pair(first, m.face_starts[static_cast<std::size_t>(face) + 1] - first);
  };
  item.selected_faces.ranges.clear();
  item.hovered_face.ranges.clear();
  if (s.whole == Whole::None) {
    for (const SubElement& p : s.selected_parts) {
      if (p.kind == SubElement::Kind::Face) {
        if (auto r = range(p.index)) item.selected_faces.ranges.push_back(*r);
      }
    }
  }
  if (s.hovered_part && s.hovered_part->kind == SubElement::Kind::Face) {
    if (auto r = range(s.hovered_part->index)) item.hovered_face.ranges.push_back(*r);
  }
  if (!item.selected_faces.ranges.empty()) ensureOverlay(item.selected_faces);
  if (!item.hovered_face.ranges.empty()) ensureOverlay(item.hovered_face);
}

void SceneRenderer::upload(GpuItem& item, QRhiResourceUpdateBatch* updates) {
  const tessellation::DisplayMesh& m = *item.mesh;
  item.vbuf.reset();
  item.ibuf.reset();
  item.index_count = 0;
  if (!m.indices.empty()) {
    const auto vbytes = static_cast<quint32>(m.vertices.size() * sizeof(float));
    const auto ibytes = static_cast<quint32>(m.indices.size() * sizeof(std::uint32_t));
    item.vbuf.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vbytes));
    item.vbuf->create();
    item.ibuf.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, ibytes));
    item.ibuf->create();
    updates->uploadStaticBuffer(item.vbuf.get(), m.vertices.data());
    updates->uploadStaticBuffer(item.ibuf.get(), m.indices.data());
    item.index_count = static_cast<quint32>(m.indices.size());
  }
  if (!item.ubuf) {
    item.ubuf.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformSize));
    item.ubuf->create();
    item.srb.reset(rhi_->newShaderResourceBindings());
    item.srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
        item.ubuf.get())});
    item.srb->create();
  }
  item.styled = false;
}

void SceneRenderer::writeUniforms(QRhiBuffer* ubuf, const QColor& color, const FrameParams& frame,
                                  QRhiResourceUpdateBatch* updates) const {
  const Rgba c = rgba(color);
  const float params[4] = {frame.mode == DisplayMode::Shaded ? 1.0F : 0.0F,
                           frame.perspective ? 1.0F : 0.0F, 0.0F, 0.0F};
  updates->updateDynamicBuffer(ubuf, 0, 64, frame.clip_view_projection.constData());
  updates->updateDynamicBuffer(ubuf, 64, 64, frame.view.constData());
  updates->updateDynamicBuffer(ubuf, 128, 16, c.data());
  updates->updateDynamicBuffer(ubuf, 144, 16, params);
}

void SceneRenderer::prepare(const Scene& scene, const FrameParams& frame,
                            QRhiResourceUpdateBatch* updates) {
  mode_ = frame.mode;
  for (auto it = items_.begin(); it != items_.end();) {
    it = scene.items().contains(it->first) ? std::next(it) : items_.erase(it);
  }
  const QMatrix4x4 line_mvp = line_offset(frame.clip_view_projection);
  for (const auto& [id, src] : scene.items()) {
    if (!src.mesh) continue;
    GpuItem& item = items_[id];
    item.visible = src.visible;
    if (item.mesh != src.mesh) {
      item.mesh = src.mesh;
      upload(item, updates);
    }
    Style style = styleFor(id, src.color, frame);
    if (!item.styled || !(style == item.style)) {
      item.style = std::move(style);
      item.styled = true;
      applyStyle(item);
    }
    if (!item.visible) continue;

    const Style& s = item.style;
    const QColor face_color = s.whole == Whole::Selected ? mix(s.color, kSelectedColor, 0.45)
                              : s.whole == Whole::Hovered ? mix(s.color, kHoverColor, 0.3)
                                                          : s.color;
    writeUniforms(item.ubuf.get(), face_color, frame, updates);
    if (!item.selected_faces.ranges.empty()) {
      writeUniforms(item.selected_faces.ubuf.get(), mix(s.color, kSelectedColor, 0.55), frame,
                    updates);
    }
    if (!item.hovered_face.ranges.empty()) {
      writeUniforms(item.hovered_face.ubuf.get(), mix(s.color, kHoverColor, 0.4), frame, updates);
    }

    item.edges.setMvp(line_mvp);
    item.isocurves.setMvp(line_mvp);
    lines_.prepare(item.edges, updates);
    lines_.prepare(item.isocurves, updates);

    // point entities and highlighted B-rep vertices: screen-sized crosses
    const auto& m = *item.mesh;
    std::vector<LineVertex> cross;
    const Rgba pc = s.whole == Whole::Selected  ? rgba(kSelectedColor)
                    : s.whole == Whole::Hovered ? rgba(kHoverColor)
                                                : rgba(s.color);
    for (std::size_t i = 0; i + 2 < m.points.size(); i += 3) {
      add_cross(cross, m.points[i], m.points[i + 1], m.points[i + 2],
                kPointMarkerPx * frame.world_per_pixel, pc);
    }
    auto vertex_marker = [&](int index, const QColor& c) {
      const auto k = static_cast<std::size_t>(index) * 3;
      if (index < 0 || k + 2 >= m.corners.size()) return;
      add_cross(cross, m.corners[k], m.corners[k + 1], m.corners[k + 2],
                kVertexMarkerPx * frame.world_per_pixel, rgba(c));
    };
    for (const SubElement& p : s.selected_parts) {
      if (p.kind == SubElement::Kind::Vertex) vertex_marker(p.index, kSelectedColor);
    }
    if (s.hovered_part && s.hovered_part->kind == SubElement::Kind::Vertex) {
      vertex_marker(s.hovered_part->index, kHoverColor);
    }
    if (!cross.empty() || !item.points.empty()) {
      item.points.setVertices(std::move(cross));
      item.points.setMvp(line_mvp);
      lines_.prepare(item.points, updates);
    }
  }
}

void SceneRenderer::drawFaces(QRhiCommandBuffer* cb, const QRhiViewport& viewport) {
  if (mode_ == DisplayMode::Wireframe) return;
  bool bound = false;
  for (auto& [id, item] : items_) {
    if (!item.visible || item.index_count == 0) continue;
    if (!bound) {
      cb->setGraphicsPipeline(mesh_pipeline_.get());
      cb->setViewport(viewport);
      bound = true;
    }
    cb->setShaderResources(item.srb.get());
    const QRhiCommandBuffer::VertexInput input(item.vbuf.get(), 0);
    cb->setVertexInput(0, 1, &input, item.ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
    cb->drawIndexed(item.index_count);
  }
  // overlays of highlighted single faces
  bool overlay_bound = false;
  for (auto& [id, item] : items_) {
    if (!item.visible || item.index_count == 0) continue;
    for (Overlay* o : {&item.selected_faces, &item.hovered_face}) {
      if (o->ranges.empty()) continue;
      if (!overlay_bound) {
        cb->setGraphicsPipeline(overlay_pipeline_.get());
        cb->setViewport(viewport);
        overlay_bound = true;
      }
      cb->setShaderResources(o->srb.get());
      const QRhiCommandBuffer::VertexInput input(item.vbuf.get(), 0);
      cb->setVertexInput(0, 1, &input, item.ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
      for (const auto& [first, count] : o->ranges) cb->drawIndexed(count, 1, first);
    }
  }
}

void SceneRenderer::drawLines(QRhiCommandBuffer* cb, const QRhiViewport& viewport) {
  for (auto& [id, item] : items_) {
    if (!item.visible || !item.mesh) continue;
    lines_.draw(cb, item.edges, viewport);
    if (mode_ == DisplayMode::Wireframe) lines_.draw(cb, item.isocurves, viewport);
    lines_.draw(cb, item.points, viewport);
  }
}

}  // namespace nfcad::render
