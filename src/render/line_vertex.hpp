#pragma once

namespace nfcad::render {

/// Vertex of the line pipeline: position + RGBA colour (resources/shaders/line.vert).
struct LineVertex {
  float x, y, z;
  float r, g, b, a;
};

}  // namespace nfcad::render
