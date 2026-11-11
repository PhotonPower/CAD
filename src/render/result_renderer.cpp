#include "render/result_renderer.hpp"

#include <QFile>
#include <cmath>

namespace nfcad::render {

namespace {

constexpr quint32 kUniformSize = 64 + 64 + 16 + 16;

QShader load_shader(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) qFatal("cannot load shader %s", qPrintable(path));
  return QShader::fromSerialized(f.readAll());
}

std::vector<LineVertex> line_vertices(const results::LineSet& s) {
  std::vector<LineVertex> out;
  const bool mapped = !s.values.empty() && s.values.size() == s.pointCount();
  const results::Range r = mapped ? s.range.value_or(results::data_range(s.values)) : results::Range{};
  auto color = [&](std::uint32_t p) -> std::array<float, 4> {
    if (!mapped) return s.color;
    const auto c = results::colormap_rgba(s.colormap, (s.values[p] - r.min) / (r.max - r.min));
    return {c[0] / 255.0F, c[1] / 255.0F, c[2] / 255.0F, s.color[3]};
  };
  for (std::size_t k = 0; k < s.polylineCount(); ++k) {
    for (std::uint32_t p = s.starts[k]; p + 1 < s.starts[k + 1]; ++p) {
      const float* a = &s.points[3 * p];
      const float* b = &s.points[3 * (p + 1)];
      if (std::isnan(a[0]) || std::isnan(b[0])) break;  // a lost ray ends here
      const auto ca = color(p);
      const auto cb = color(p + 1);
      out.push_back({a[0], a[1], a[2], ca[0], ca[1], ca[2], ca[3]});
      out.push_back({b[0], b[1], b[2], cb[0], cb[1], cb[2], cb[3]});
    }
  }
  return out;
}

}  // namespace

void ResultRenderer::initialize(QRhi* rhi, QRhiRenderTarget* target, int sample_count) {
  if (rhi == rhi_ && field_pipeline_ && rp_ == target->renderPassDescriptor()) return;
  release();
  rhi_ = rhi;
  rp_ = target->renderPassDescriptor();
  samples_ = sample_count;
  top_lines_.initialize(rhi, target, sample_count);
  scene_lines_.initialize(rhi, target, sample_count);

  sampler_.reset(rhi_->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                  QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
  sampler_->create();
  layout_ubuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformSize));
  layout_ubuf_->create();
  layout_texture_.reset(rhi_->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
  layout_texture_->create();
  layout_srb_.reset(rhi_->newShaderResourceBindings());
  layout_srb_->setBindings(
      {QRhiShaderResourceBinding::uniformBuffer(
           0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
           layout_ubuf_.get()),
       QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                 layout_texture_.get(), sampler_.get())});
  layout_srb_->create();

  field_pipeline_.reset(rhi_->newGraphicsPipeline());
  field_pipeline_->setShaderStages({
      {QRhiShaderStage::Vertex, load_shader(QStringLiteral(":/shaders/meshtex.vert.qsb"))},
      {QRhiShaderStage::Fragment, load_shader(QStringLiteral(":/shaders/meshtex.frag.qsb"))},
  });
  QRhiVertexInputLayout layout;
  layout.setBindings({{tessellation::kVertexStride * sizeof(float)}});
  layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                        {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
                        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)}});
  field_pipeline_->setVertexInputLayout(layout);
  field_pipeline_->setTopology(QRhiGraphicsPipeline::Triangles);
  field_pipeline_->setCullMode(QRhiGraphicsPipeline::None);
  field_pipeline_->setDepthTest(true);
  field_pipeline_->setDepthWrite(true);
  field_pipeline_->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
  field_pipeline_->setDepthBias(2);  // the same as the model faces: equal depth
  field_pipeline_->setSlopeScaledDepthBias(1.5F);
  field_pipeline_->setSampleCount(sample_count);
  field_pipeline_->setShaderResourceBindings(layout_srb_.get());
  field_pipeline_->setRenderPassDescriptor(rp_);
  field_pipeline_->create();
}

void ResultRenderer::release() {
  lines_.clear();
  fields_.clear();
  field_pipeline_.reset();
  layout_srb_.reset();
  layout_texture_.reset();
  layout_ubuf_.reset();
  sampler_.reset();
  top_lines_.release();
  scene_lines_.release();
  rhi_ = nullptr;
  rp_ = nullptr;
}

void ResultRenderer::uploadField(GpuField& g, const tessellation::DisplayMesh& mesh,
                                 QRhiResourceUpdateBatch* updates) {
  const results::FaceField& f = *g.data;
  // the triangles of the face, expanded (fields are few, faces small)
  std::vector<float> verts;
  if (mesh.face_starts.size() >= 2) {
    const auto face = static_cast<std::size_t>(std::clamp(f.face, 0, static_cast<int>(mesh.faceCount()) - 1));
    for (std::uint32_t k = mesh.face_starts[face]; k < mesh.face_starts[face + 1]; ++k) {
      const std::size_t v = tessellation::kVertexStride * mesh.indices[k];
      verts.insert(verts.end(), mesh.vertices.begin() + static_cast<std::ptrdiff_t>(v),
                   mesh.vertices.begin() + static_cast<std::ptrdiff_t>(v + tessellation::kVertexStride));
    }
  }
  g.vertex_count = static_cast<quint32>(verts.size() / tessellation::kVertexStride);
  g.vbuf.reset();
  if (g.vertex_count > 0) {
    g.vbuf.reset(rhi_->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                 static_cast<quint32>(verts.size() * sizeof(float))));
    g.vbuf->create();
    updates->uploadStaticBuffer(g.vbuf.get(), verts.data());
  }
  const QSize size(std::max(f.width, 1), std::max(f.height, 1));
  g.texture.reset(rhi_->newTexture(QRhiTexture::RGBA8, size));
  g.texture->create();
  const std::vector<unsigned char> pixels = f.texture();
  if (pixels.size() >= static_cast<std::size_t>(4 * size.width() * size.height())) {
    QRhiTextureSubresourceUploadDescription sub(pixels.data(), static_cast<quint32>(pixels.size()));
    sub.setSourceSize(size);
    updates->uploadTexture(g.texture.get(), QRhiTextureUploadDescription({0, 0, sub}));
  }
  if (!g.ubuf) {
    g.ubuf.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformSize));
    g.ubuf->create();
  }
  g.srb.reset(rhi_->newShaderResourceBindings());
  g.srb->setBindings(
      {QRhiShaderResourceBinding::uniformBuffer(
           0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
           g.ubuf.get()),
       QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                 g.texture.get(), sampler_.get())});
  g.srb->create();
}

void ResultRenderer::prepare(const results::ResultStore& store, const Scene& scene,
                             const FrameParams& frame, QRhiResourceUpdateBatch* updates) {
  // ---- line sets
  std::unordered_map<results::ResultId, bool> alive;
  for (const results::ResultItem* item : store.visibleItems(results::ResultItem::Kind::Lines)) {
    alive[item->id] = true;
    GpuLines& g = lines_[item->id];
    if (g.data != item->lines) {
      g.data = item->lines;
      g.on_top = item->lines->on_top;
      g.layer.setVertices(line_vertices(*item->lines));
    }
    g.layer.setMvp(frame.clip_view_projection);
    (g.on_top ? top_lines_ : scene_lines_).prepare(g.layer, updates);
  }
  std::erase_if(lines_, [&](const auto& kv) { return !alive.contains(kv.first); });

  // ---- face fields
  alive.clear();
  for (const results::ResultItem* item : store.visibleItems(results::ResultItem::Kind::Field)) {
    const auto s = scene.items().find(item->field->entity);
    if (s == scene.items().end() || !s->second.mesh || !s->second.visible) continue;
    alive[item->id] = true;
    GpuField& g = fields_[item->id];
    if (g.data != item->field || g.mesh != s->second.mesh) {
      g.data = item->field;
      g.mesh = s->second.mesh;
      uploadField(g, *s->second.mesh, updates);
    }
    const float params[4] = {frame.mode == DisplayMode::Shaded ? 1.0F : 0.0F,
                             frame.perspective ? 1.0F : 0.0F, 0.0F, 0.0F};
    const float color[4] = {1, 1, 1, 1};
    updates->updateDynamicBuffer(g.ubuf.get(), 0, 64, frame.clip_view_projection.constData());
    updates->updateDynamicBuffer(g.ubuf.get(), 64, 64, frame.view.constData());
    updates->updateDynamicBuffer(g.ubuf.get(), 128, 16, color);
    updates->updateDynamicBuffer(g.ubuf.get(), 144, 16, params);
  }
  std::erase_if(fields_, [&](const auto& kv) { return !alive.contains(kv.first); });
}

void ResultRenderer::drawInScene(QRhiCommandBuffer* cb, const QRhiViewport& viewport) {
  bool bound = false;
  for (auto& [id, g] : fields_) {
    if (g.vertex_count == 0) continue;
    if (!bound) {
      cb->setGraphicsPipeline(field_pipeline_.get());
      cb->setViewport(viewport);
      bound = true;
    }
    cb->setShaderResources(g.srb.get());
    const QRhiCommandBuffer::VertexInput input(g.vbuf.get(), 0);
    cb->setVertexInput(0, 1, &input);
    cb->draw(g.vertex_count);
  }
  for (auto& [id, g] : lines_) {
    if (!g.on_top) scene_lines_.draw(cb, g.layer, viewport);
  }
}

void ResultRenderer::drawOnTop(QRhiCommandBuffer* cb, const QRhiViewport& viewport) {
  for (auto& [id, g] : lines_) {
    if (g.on_top) top_lines_.draw(cb, g.layer, viewport);
  }
}

}  // namespace nfcad::render
