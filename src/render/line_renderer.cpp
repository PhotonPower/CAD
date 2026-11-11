#include "render/line_renderer.hpp"

#include <QFile>

namespace nfcad::render {

namespace {
constexpr quint32 kMvpSize = 64;

QShader load_shader(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) qFatal("cannot load shader %s", qPrintable(path));
  return QShader::fromSerialized(f.readAll());
}
}  // namespace

void LineLayer::setVertices(std::vector<LineVertex> vertices) {
  vertices_ = std::move(vertices);
  vertices_dirty_ = true;
}

void LineLayer::releaseGpu() {
  srb_.reset();
  vbuf_.reset();
  ubuf_.reset();
  capacity_ = 0;
  vertices_dirty_ = true;
  mvp_dirty_ = true;
}

void LineRenderer::initialize(QRhi* rhi, QRhiRenderTarget* target, int sample_count) {
  if (rhi == rhi_ && pipeline_ && rp_ == target->renderPassDescriptor()) return;
  release();
  rhi_ = rhi;
  rp_ = target->renderPassDescriptor();

  layout_ubuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMvpSize));
  layout_ubuf_->create();
  layout_srb_.reset(rhi_->newShaderResourceBindings());
  layout_srb_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
      0, QRhiShaderResourceBinding::VertexStage, layout_ubuf_.get())});
  layout_srb_->create();

  pipeline_.reset(rhi_->newGraphicsPipeline());
  pipeline_->setShaderStages({
      {QRhiShaderStage::Vertex, load_shader(QStringLiteral(":/shaders/line.vert.qsb"))},
      {QRhiShaderStage::Fragment, load_shader(QStringLiteral(":/shaders/line.frag.qsb"))},
  });
  QRhiVertexInputLayout layout;
  layout.setBindings({{sizeof(LineVertex)}});
  layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                        {0, 1, QRhiVertexInputAttribute::Float4, 3 * sizeof(float)}});
  pipeline_->setVertexInputLayout(layout);
  pipeline_->setTopology(QRhiGraphicsPipeline::Lines);
  QRhiGraphicsPipeline::TargetBlend blend;
  blend.enable = true;
  blend.srcColor = QRhiGraphicsPipeline::One;
  blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  blend.srcAlpha = QRhiGraphicsPipeline::One;
  blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
  pipeline_->setTargetBlends({blend});
  pipeline_->setDepthTest(false);
  pipeline_->setDepthWrite(false);
  pipeline_->setSampleCount(sample_count);
  pipeline_->setShaderResourceBindings(layout_srb_.get());
  pipeline_->setRenderPassDescriptor(rp_);
  pipeline_->create();
}

void LineRenderer::release() {
  pipeline_.reset();
  layout_srb_.reset();
  layout_ubuf_.reset();
  rhi_ = nullptr;
  rp_ = nullptr;
}

void LineRenderer::ensureBindings(LineLayer& layer) {
  if (!layer.ubuf_) {
    layer.releaseGpu();
    layer.ubuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMvpSize));
    layer.ubuf_->create();
    layer.srb_.reset(rhi_->newShaderResourceBindings());
    layer.srb_->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage, layer.ubuf_.get())});
    layer.srb_->create();
  }
}

void LineRenderer::prepare(LineLayer& layer, QRhiResourceUpdateBatch* updates) {
  ensureBindings(layer);
  if (layer.vertices_dirty_ && !layer.vertices_.empty()) {
    const auto count = static_cast<quint32>(layer.vertices_.size());
    if (count > layer.capacity_) {
      layer.capacity_ = count + count / 2;
      layer.vbuf_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                        layer.capacity_ * sizeof(LineVertex)));
      layer.vbuf_->create();
    }
    updates->updateDynamicBuffer(layer.vbuf_.get(), 0, count * sizeof(LineVertex),
                                 layer.vertices_.data());
    layer.vertices_dirty_ = false;
  }
  if (layer.mvp_dirty_) {
    updates->updateDynamicBuffer(layer.ubuf_.get(), 0, kMvpSize, layer.mvp_.constData());
    layer.mvp_dirty_ = false;
  }
}

void LineRenderer::draw(QRhiCommandBuffer* cb, LineLayer& layer, const QRhiViewport& viewport) {
  if (layer.vertices_.empty() || !layer.vbuf_) return;
  cb->setGraphicsPipeline(pipeline_.get());
  cb->setViewport(viewport);
  cb->setShaderResources(layer.srb_.get());
  const QRhiCommandBuffer::VertexInput input(layer.vbuf_.get(), 0);
  cb->setVertexInput(0, 1, &input);
  cb->draw(static_cast<quint32>(layer.vertices_.size()));
}

}  // namespace nfcad::render
