#pragma once

#include <QMatrix4x4>
#include <memory>
#include <rhi/qrhi.h>
#include <vector>

#include "render/line_vertex.hpp"

namespace nfcad::render {

/// A list of coloured line segments with its own vertex buffer, MVP uniform and bindings.
class LineLayer {
 public:
  void setVertices(std::vector<LineVertex> vertices);
  void setMvp(const QMatrix4x4& mvp) { mvp_ = mvp; mvp_dirty_ = true; }
  [[nodiscard]] bool empty() const { return vertices_.empty(); }
  /// Drops GPU resources (before the QRhi goes away); they are recreated on the next prepare.
  void releaseGpu();

 private:
  friend class LineRenderer;
  std::vector<LineVertex> vertices_;
  QMatrix4x4 mvp_;
  bool vertices_dirty_ = true;
  bool mvp_dirty_ = true;
  std::unique_ptr<QRhiBuffer> vbuf_;
  std::unique_ptr<QRhiBuffer> ubuf_;
  std::unique_ptr<QRhiShaderResourceBindings> srb_;
  quint32 capacity_ = 0;  // vertices
};

/// Pipeline for unlit coloured lines (resources/shaders/line.*), alpha blended, no depth test.
/// Used for grid, axes and the axis triad (M1); M2 adds face and wide-line pipelines.
class LineRenderer {
 public:
  /// (Re)creates GPU resources when the QRhi or the render pass changes.
  void initialize(QRhi* rhi, QRhiRenderTarget* target, int sample_count);
  void release();

  /// Uploads changed vertices / uniforms. Call before beginPass.
  void prepare(LineLayer& layer, QRhiResourceUpdateBatch* updates);
  /// Records the draw call inside the pass, into the given viewport (pixels, origin bottom left).
  void draw(QRhiCommandBuffer* cb, LineLayer& layer, const QRhiViewport& viewport);

 private:
  void ensureBindings(LineLayer& layer);

  QRhi* rhi_ = nullptr;
  QRhiRenderPassDescriptor* rp_ = nullptr;
  std::unique_ptr<QRhiGraphicsPipeline> pipeline_;
  std::unique_ptr<QRhiBuffer> layout_ubuf_;
  std::unique_ptr<QRhiShaderResourceBindings> layout_srb_;
};

}  // namespace nfcad::render
