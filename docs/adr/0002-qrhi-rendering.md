# ADR 0002 – Viewport rendering with Qt RHI (`QRhiWidget`)

**Context.** macOS has deprecated OpenGL (frozen at 4.1); Windows drivers are most reliable with
Direct3D; Linux needs OpenGL or Vulkan. A CAD viewport needs custom shaders (wide lines, edges on
faces, id-buffer picking). Options: `QOpenGLWidget`, `QRhiWidget`, VTK, OpenCASCADE visualisation,
a third-party engine.

**Decision.** Each viewport is a `QRhiWidget` with our own small renderer in `src/render/`.
Shaders are GLSL 450 compiled by `qt_add_shaders()` to all backends.

**Consequences.**
- Native backend everywhere (Metal, D3D11/12, OpenGL/Vulkan) from one renderer.
- The QRhi API is only partly covered by Qt's compatibility promise; all RHI code stays in
  `src/render/` and `src/viewport/` so that adaptations stay local. Minimum Qt 6.8.
- No dependency on VTK or OCCT; the renderer is small because it only needs lines, triangles,
  points and an id buffer.
