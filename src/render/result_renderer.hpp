#pragma once

#include <QMatrix4x4>
#include <memory>
#include <rhi/qrhi.h>
#include <unordered_map>

#include "render/display_mode.hpp"
#include "render/line_renderer.hpp"
#include "render/scene.hpp"
#include "results/result_store.hpp"

namespace nfcad::render {

/// Per-viewport GPU copy of the visible results (docs/plugins.md, P2): line sets (rays) as
/// coloured lines, on top of the model or depth tested; face fields as textures drawn over the
/// faces they belong to (same depth as the model faces, LessOrEqual).
class ResultRenderer {
 public:
  struct FrameParams {
    QMatrix4x4 clip_view_projection;
    QMatrix4x4 view;
    bool perspective = false;
    DisplayMode mode = DisplayMode::Shaded;
  };

  void initialize(QRhi* rhi, QRhiRenderTarget* target, int sample_count);
  void release();
  void prepare(const results::ResultStore& store, const Scene& scene, const FrameParams& frame,
               QRhiResourceUpdateBatch* updates);
  /// Fields (after the model faces) and depth-tested lines.
  void drawInScene(QRhiCommandBuffer* cb, const QRhiViewport& viewport);
  /// Lines drawn on top of everything.
  void drawOnTop(QRhiCommandBuffer* cb, const QRhiViewport& viewport);

 private:
  struct GpuLines {
    std::shared_ptr<const results::LineSet> data;  // identity
    LineLayer layer;
    bool on_top = true;
  };
  struct GpuField {
    std::shared_ptr<const results::FaceField> data;  // identity
    tessellation::DisplayMeshPtr mesh;              // identity of the face mesh used
    std::unique_ptr<QRhiBuffer> vbuf;
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    quint32 vertex_count = 0;
  };

  void uploadField(GpuField& g, const tessellation::DisplayMesh& mesh, QRhiResourceUpdateBatch* updates);

  QRhi* rhi_ = nullptr;
  QRhiRenderPassDescriptor* rp_ = nullptr;
  int samples_ = 1;
  LineRenderer top_lines_{false};
  LineRenderer scene_lines_{true};
  std::unique_ptr<QRhiGraphicsPipeline> field_pipeline_;
  std::unique_ptr<QRhiSampler> sampler_;
  std::unique_ptr<QRhiBuffer> layout_ubuf_;
  std::unique_ptr<QRhiTexture> layout_texture_;
  std::unique_ptr<QRhiShaderResourceBindings> layout_srb_;
  std::unordered_map<results::ResultId, GpuLines> lines_;
  std::unordered_map<results::ResultId, GpuField> fields_;
};

}  // namespace nfcad::render
