#pragma once

#include <cstdint>
#include <vector>

#include "tessellation/box3.hpp"
#include "tessellation/bvh.hpp"

namespace nfcad::tessellation {

/// Polyline as flat x, y, z floats.
using Polyline = std::vector<float>;

/// Floats per vertex: position (3), normal (3), surface parameters (u, v) normalised to [0, 1]
/// over the face's surface domain (texture coordinates for fields on faces, P2).
inline constexpr std::size_t kVertexStride = 8;

/// Display data of one entity (docs/rendering.md, "Display geometry"). Never used for modelling.
struct DisplayMesh {
  std::vector<float> vertices;         ///< interleaved x, y, z, nx, ny, nz, u, v (kVertexStride)
  std::vector<std::uint32_t> indices;  ///< triangles
  std::vector<Polyline> edges;         ///< curves, face boundaries, shell edges
  std::vector<Polyline> isocurves;     ///< wireframe only
  std::vector<float> points;           ///< point entities: x, y, z
  Box3 bounds;

  // ---- sub-elements (picking, highlighting, snapping)
  /// First index (into `indices`) of each face; size = faces + 1. Edges: for shells
  /// edges[i] is B-rep edge i; for single faces the boundary loops; for curves the curve.
  std::vector<std::uint32_t> face_starts;
  std::vector<std::int32_t> iso_faces;  ///< face of each iso-curve
  std::vector<float> corners;           ///< B-rep vertices of shells: x, y, z

  // ---- pick index (build_pick_index)
  struct Segment {
    std::uint32_t polyline;  ///< index into edges or isocurves
    std::uint32_t point;     ///< first point of the segment in that polyline
    bool iso;
  };
  std::vector<Segment> segments;
  Bvh triangle_bvh;  ///< primitive = triangle index
  Bvh segment_bvh;   ///< primitive = index into segments

  [[nodiscard]] std::size_t vertexCount() const { return vertices.size() / kVertexStride; }
  [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
  [[nodiscard]] std::size_t faceCount() const {
    return face_starts.empty() ? 0 : face_starts.size() - 1;
  }
  [[nodiscard]] QVector3D vertex(std::uint32_t i) const {
    return {vertices[kVertexStride * i], vertices[kVertexStride * i + 1],
            vertices[kVertexStride * i + 2]};
  }
  /// Normalised surface parameters (u, v) of vertex i.
  [[nodiscard]] std::pair<float, float> uv(std::uint32_t i) const {
    return {vertices[kVertexStride * i + 6], vertices[kVertexStride * i + 7]};
  }
  /// Face that contains triangle `t`.
  [[nodiscard]] int faceOfTriangle(std::size_t t) const;
  /// End points of a segment.
  [[nodiscard]] std::pair<QVector3D, QVector3D> segmentPoints(const Segment& s) const;
};

/// Builds triangle and segment BVHs (called at the end of tessellate()).
void build_pick_index(DisplayMesh& mesh);

}  // namespace nfcad::tessellation
