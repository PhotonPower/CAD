#pragma once

#include <QByteArray>
#include <QRhiWidget>

namespace nfcad::platform {

/// Graphics API of the viewports (docs/rendering.md, ADR 0002): the native one per platform,
/// overridable with the environment variable NFCAD_RHI_BACKEND = opengl | vulkan | metal |
/// d3d11 | d3d12 (e.g. Vulkan on Linux, or OpenGL in virtual machines).
[[nodiscard]] inline QRhiWidget::Api preferred_rhi_api() {
  const QByteArray forced = qgetenv("NFCAD_RHI_BACKEND").toLower();
  if (forced == "opengl") return QRhiWidget::Api::OpenGL;
  if (forced == "vulkan") return QRhiWidget::Api::Vulkan;
  if (forced == "metal") return QRhiWidget::Api::Metal;
  if (forced == "d3d11") return QRhiWidget::Api::Direct3D11;
  if (forced == "d3d12") return QRhiWidget::Api::Direct3D12;
#if defined(Q_OS_MACOS)
  return QRhiWidget::Api::Metal;
#elif defined(Q_OS_WIN)
  return QRhiWidget::Api::Direct3D11;
#else
  return QRhiWidget::Api::OpenGL;
#endif
}

}  // namespace nfcad::platform
