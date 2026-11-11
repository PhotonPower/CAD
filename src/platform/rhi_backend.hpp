#pragma once

#include <QRhiWidget>

namespace nfcad::platform {

/// Native graphics API per platform (docs/rendering.md, ADR 0002).
[[nodiscard]] inline QRhiWidget::Api preferred_rhi_api() {
#if defined(Q_OS_MACOS)
  return QRhiWidget::Api::Metal;
#elif defined(Q_OS_WIN)
  return QRhiWidget::Api::Direct3D11;
#else
  return QRhiWidget::Api::OpenGL;  // Vulkan selectable later (M7)
#endif
}

}  // namespace nfcad::platform
