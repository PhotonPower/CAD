#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <memory>
#include <rhi/qrhi.h>
#include <unordered_map>

#include "render/display_mode.hpp"
#include "render/line_renderer.hpp"
#include "render/scene.hpp"

namespace nfcad::render {

/// Per-viewport GPU copy of the shared Scene and the face pipeline (docs/rendering.md).
/// Faces: mesh.vert/frag with depth bias so that edges on faces stay visible;
/// edges, iso-curves and points: line pipeline with depth test.
class SceneRenderer {
 public:
  struct FrameParams {
    QMatrix4x4 clip_view_projection;  ///< clip correction * projection * view
    QMatrix4x4 view;
    bool perspective = false;
    float world_per_pixel = 1.0F;  ///< for screen-sized point markers
    DisplayMode mode = DisplayMode::Shaded;
  };

  void initialize(QRhi* rhi, QRhiRenderTarget* target, int sample_count);
  void release();

  /// Uploads new / changed items, drops removed ones, writes uniforms. Call before beginPass.
  void prepare(const Scene& scene, const FrameParams& frame, QRhiResourceUpdateBatch* updates);
  void drawFaces(QRhiCommandBuffer* cb, const QRhiViewport& viewport);
  void drawLines(QRhiCommandBuffer* cb, const QRhiViewport& viewport);

 private:
  struct GpuItem {
    tessellation::DisplayMeshPtr mesh;  // identity of the uploaded mesh
    QColor color;
    DisplayMode mode = DisplayMode::Shaded;
    bool visible = true;
    std::unique_ptr<QRhiBuffer> vbuf;
    std::unique_ptr<QRhiBuffer> ibuf;
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    quint32 index_count = 0;
    LineLayer edges;
    LineLayer isocurves;
    LineLayer points;
  };

  void upload(GpuItem& item, QRhiResourceUpdateBatch* updates);
  void rebuildLines(GpuItem& item);

  QRhi* rhi_ = nullptr;
  QRhiRenderPassDescriptor* rp_ = nullptr;
  std::unique_ptr<QRhiGraphicsPipeline> mesh_pipeline_;
  std::unique_ptr<QRhiBuffer> layout_ubuf_;
  std::unique_ptr<QRhiShaderResourceBindings> layout_srb_;
  LineRenderer lines_{true};
  std::unordered_map<document::EntityId, GpuItem> items_;
  DisplayMode mode_ = DisplayMode::Shaded;
};

}  // namespace nfcad::render
