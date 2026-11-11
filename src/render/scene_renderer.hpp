#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <memory>
#include <optional>
#include <rhi/qrhi.h>
#include <unordered_map>
#include <vector>

#include "document/selection.hpp"
#include "render/display_mode.hpp"
#include "render/line_renderer.hpp"
#include "render/scene.hpp"

namespace nfcad::render {

/// Highlight colours (docs/ui-concept.md, "Visual style").
inline const QColor kSelectedColor(255, 214, 51);
inline const QColor kHoverColor(80, 225, 255);

/// Per-viewport GPU copy of the shared Scene and the face pipelines (docs/rendering.md).
/// Faces: mesh.vert/frag with depth bias; highlighted single faces are drawn again with an
/// equal-depth overlay pipeline. Edges, iso-curves and points: line pipeline with depth test.
class SceneRenderer {
 public:
  struct FrameParams {
    QMatrix4x4 clip_view_projection;  ///< clip correction * projection * view
    QMatrix4x4 view;
    bool perspective = false;
    float world_per_pixel = 1.0F;  ///< for screen-sized point markers
    DisplayMode mode = DisplayMode::Shaded;
    const document::Selection* selection = nullptr;
  };

  void initialize(QRhi* rhi, QRhiRenderTarget* target, int sample_count);
  void release();

  /// Uploads new / changed items, drops removed ones, writes uniforms. Call before beginPass.
  void prepare(const Scene& scene, const FrameParams& frame, QRhiResourceUpdateBatch* updates);
  void drawFaces(QRhiCommandBuffer* cb, const QRhiViewport& viewport);
  void drawLines(QRhiCommandBuffer* cb, const QRhiViewport& viewport);

 private:
  enum class Whole { None, Hovered, Selected };
  struct Style {
    QColor color;
    DisplayMode mode = DisplayMode::Shaded;
    Whole whole = Whole::None;
    std::vector<document::SubElement> selected_parts;
    std::optional<document::SubElement> hovered_part;
    bool operator==(const Style&) const = default;
  };
  struct Overlay {  // tinted redraw of single faces
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    std::vector<std::pair<quint32, quint32>> ranges;  // first index, index count
  };
  struct GpuItem {
    tessellation::DisplayMeshPtr mesh;  // identity of the uploaded mesh
    Style style;
    bool styled = false;
    bool visible = true;
    std::unique_ptr<QRhiBuffer> vbuf;
    std::unique_ptr<QRhiBuffer> ibuf;
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    quint32 index_count = 0;
    Overlay selected_faces;
    Overlay hovered_face;
    LineLayer edges;
    LineLayer isocurves;
    LineLayer points;  // point entities and highlighted B-rep vertices
  };

  [[nodiscard]] Style styleFor(document::EntityId id, const QColor& color,
                               const FrameParams& frame) const;
  void upload(GpuItem& item, QRhiResourceUpdateBatch* updates);
  void applyStyle(GpuItem& item);
  void ensureOverlay(Overlay& o);
  void writeUniforms(QRhiBuffer* ubuf, const QColor& color, const FrameParams& frame,
                     QRhiResourceUpdateBatch* updates) const;

  QRhi* rhi_ = nullptr;
  QRhiRenderPassDescriptor* rp_ = nullptr;
  std::unique_ptr<QRhiGraphicsPipeline> mesh_pipeline_;
  std::unique_ptr<QRhiGraphicsPipeline> overlay_pipeline_;
  std::unique_ptr<QRhiBuffer> layout_ubuf_;
  std::unique_ptr<QRhiShaderResourceBindings> layout_srb_;
  LineRenderer lines_{true};
  std::unordered_map<document::EntityId, GpuItem> items_;
  DisplayMode mode_ = DisplayMode::Shaded;
};

}  // namespace nfcad::render
