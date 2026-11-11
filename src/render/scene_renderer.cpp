#include "render/scene_renderer.hpp"

#include <QFile>

namespace nfcad::render {

namespace {

constexpr quint32 kUniformSize = 64 + 64 + 16 + 16;  // mvp, view, color, params
constexpr float kPointMarkerPx = 5.0F;
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

std::array<float, 4> rgba(const QColor& c, float scale = 1.0F, float alpha = 1.0F) {
  return {static_cast<float>(c.redF()) * scale, static_cast<float>(c.greenF()) * scale,
          static_cast<float>(c.blueF()) * scale, alpha};
}

void add_polylines(std::vector<LineVertex>& out, const std::vector<tessellation::Polyline>& lines,
                   const std::array<float, 4>& c) {
  for (const auto& pl : lines) {
    for (std::size_t i = 0; i + 5 < pl.size(); i += 3) {
      out.push_back({pl[i], pl[i + 1], pl[i + 2], c[0], c[1], c[2], c[3]});
      out.push_back({pl[i + 3], pl[i + 4], pl[i + 5], c[0], c[1], c[2], c[3]});
    }
  }
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

  mesh_pipeline_.reset(rhi_->newGraphicsPipeline());
  mesh_pipeline_->setShaderStages({
      {QRhiShaderStage::Vertex, load_shader(QStringLiteral(":/shaders/mesh.vert.qsb"))},
      {QRhiShaderStage::Fragment, load_shader(QStringLiteral(":/shaders/mesh.frag.qsb"))},
  });
  QRhiVertexInputLayout layout;
  layout.setBindings({{6 * sizeof(float)}});
  layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                        {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)}});
  mesh_pipeline_->setVertexInputLayout(layout);
  mesh_pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
  mesh_pipeline_->setCullMode(QRhiGraphicsPipeline::None);
  mesh_pipeline_->setDepthTest(true);
  mesh_pipeline_->setDepthWrite(true);
  mesh_pipeline_->setDepthOp(QRhiGraphicsPipeline::Less);
  // Push faces back a little so that edges lying on them win the depth test.
  mesh_pipeline_->setDepthBias(2);
  mesh_pipeline_->setSlopeScaledDepthBias(1.5F);
  mesh_pipeline_->setSampleCount(sample_count);
  mesh_pipeline_->setShaderResourceBindings(layout_srb_.get());
  mesh_pipeline_->setRenderPassDescriptor(rp_);
  mesh_pipeline_->create();
}

void SceneRenderer::release() {
  items_.clear();
  mesh_pipeline_.reset();
  layout_srb_.reset();
  layout_ubuf_.reset();
  lines_.release();
  rhi_ = nullptr;
  rp_ = nullptr;
}

void SceneRenderer::rebuildLines(GpuItem& item) {
  const tessellation::DisplayMesh& m = *item.mesh;
  const bool wire = item.mode == DisplayMode::Wireframe;
  std::vector<LineVertex> edges;
  // Wireframe: edges in the entity colour; with faces: dark edges (and curves in colour).
  add_polylines(edges, m.edges,
                wire || m.indices.empty() ? rgba(item.color) : std::array<float, 4>{0.10F, 0.11F, 0.13F, 1.0F});
  item.edges.setVertices(std::move(edges));
  std::vector<LineVertex> iso;
  if (wire) add_polylines(iso, m.isocurves, rgba(item.color, 0.85F, 0.55F));
  item.isocurves.setVertices(std::move(iso));
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
  rebuildLines(item);
}

void SceneRenderer::prepare(const Scene& scene, const FrameParams& frame,
                            QRhiResourceUpdateBatch* updates) {
  mode_ = frame.mode;
  // drop removed items
  for (auto it = items_.begin(); it != items_.end();) {
    it = scene.items().contains(it->first) ? std::next(it) : items_.erase(it);
  }
  for (const auto& [id, src] : scene.items()) {
    if (!src.mesh) continue;
    GpuItem& item = items_[id];
    item.visible = src.visible;
    const bool new_mesh = item.mesh != src.mesh;
    const bool restyle = item.color != src.color || item.mode != frame.mode;
    item.color = src.color;
    item.mode = frame.mode;
    if (new_mesh) {
      item.mesh = src.mesh;
      upload(item, updates);
    } else if (restyle) {
      rebuildLines(item);
    }
    if (!item.visible) continue;

    // uniforms
    const auto c = rgba(item.color);
    const float params[4] = {frame.mode == DisplayMode::Shaded ? 1.0F : 0.0F,
                             frame.perspective ? 1.0F : 0.0F, 0.0F, 0.0F};
    updates->updateDynamicBuffer(item.ubuf.get(), 0, 64, frame.clip_view_projection.constData());
    updates->updateDynamicBuffer(item.ubuf.get(), 64, 64, frame.view.constData());
    updates->updateDynamicBuffer(item.ubuf.get(), 128, 16, c.data());
    updates->updateDynamicBuffer(item.ubuf.get(), 144, 16, params);

    const QMatrix4x4 line_mvp = line_offset(frame.clip_view_projection);
    item.edges.setMvp(line_mvp);
    item.isocurves.setMvp(line_mvp);
    lines_.prepare(item.edges, updates);
    lines_.prepare(item.isocurves, updates);

    // point markers: small screen-sized crosses
    if (!item.mesh->points.empty()) {
      const float h = kPointMarkerPx * frame.world_per_pixel;
      std::vector<LineVertex> cross;
      const auto& p = item.mesh->points;
      for (std::size_t i = 0; i + 2 < p.size(); i += 3) {
        for (int axis = 0; axis < 3; ++axis) {
          float a[3] = {p[i], p[i + 1], p[i + 2]};
          float b[3] = {p[i], p[i + 1], p[i + 2]};
          a[axis] -= h;
          b[axis] += h;
          cross.push_back({a[0], a[1], a[2], c[0], c[1], c[2], 1.0F});
          cross.push_back({b[0], b[1], b[2], c[0], c[1], c[2], 1.0F});
        }
      }
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
}

void SceneRenderer::drawLines(QRhiCommandBuffer* cb, const QRhiViewport& viewport) {
  for (auto& [id, item] : items_) {
    if (!item.visible || !item.mesh) continue;
    lines_.draw(cb, item.edges, viewport);
    if (mode_ == DisplayMode::Wireframe) lines_.draw(cb, item.isocurves, viewport);
    if (!item.mesh->points.empty()) lines_.draw(cb, item.points, viewport);
  }
}

}  // namespace nfcad::render
