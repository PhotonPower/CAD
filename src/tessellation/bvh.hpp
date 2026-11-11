#pragma once

#include <QVector3D>
#include <cstdint>
#include <vector>

#include "tessellation/box3.hpp"

namespace nfcad::tessellation {

/// Bounding volume hierarchy over primitive boxes (triangles or line segments of a display
/// mesh). Used for picking (M3) and the snap candidate search (M4). Immutable after build;
/// queries are thread-safe.
class Bvh {
 public:
  struct Node {
    Box3 box;
    std::uint32_t first = 0;  ///< leaf: first index into order(); inner: index of the left child
    std::uint32_t count = 0;  ///< > 0: leaf with `count` primitives; 0: inner node (right = left+1)
  };

  /// Builds the hierarchy (median split on the longest axis, up to 4 primitives per leaf).
  void build(const std::vector<Box3>& primitive_boxes);
  [[nodiscard]] bool empty() const { return nodes_.empty(); }
  [[nodiscard]] const std::vector<Node>& nodes() const { return nodes_; }
  /// Primitive indices in leaf order.
  [[nodiscard]] const std::vector<std::uint32_t>& order() const { return order_; }

  /// Visits the primitives whose box, enlarged by `inflate`, is hit by the ray
  /// origin + t * dir with t in [0, t_max]. `visit(primitive, t_max&)` may shrink t_max
  /// (closest-hit queries).
  template <class Visit>
  void raycast(const QVector3D& origin, const QVector3D& dir, float inflate, float t_max,
               Visit&& visit) const;

  /// Visits the primitives whose box intersects `box`.
  template <class Visit>
  void overlap(const Box3& box, Visit&& visit) const;

 private:
  void buildInto(std::uint32_t node, std::uint32_t begin, std::uint32_t end,
                 const std::vector<Box3>& boxes, const std::vector<QVector3D>& centers);
  std::vector<Node> nodes_;
  std::vector<std::uint32_t> order_;
};

/// Slab test: entry parameter of the ray into the box, or a negative value if it misses
/// (or the box lies beyond t_max).
[[nodiscard]] float ray_box(const QVector3D& origin, const QVector3D& inv_dir, const Box3& box,
                            float t_max);

// ------------------------------------------------------------------------------- inline

template <class Visit>
void Bvh::raycast(const QVector3D& origin, const QVector3D& dir, float inflate, float t_max,
                  Visit&& visit) const {
  if (nodes_.empty()) return;
  const QVector3D inv(dir.x() != 0.0F ? 1.0F / dir.x() : 1e30F,
                      dir.y() != 0.0F ? 1.0F / dir.y() : 1e30F,
                      dir.z() != 0.0F ? 1.0F / dir.z() : 1e30F);
  const QVector3D grow(inflate, inflate, inflate);
  std::uint32_t stack[64];
  int top = 0;
  stack[top++] = 0;
  while (top > 0) {
    const Node& n = nodes_[stack[--top]];
    Box3 b = n.box;
    b.min -= grow;
    b.max += grow;
    if (ray_box(origin, inv, b, t_max) < 0.0F) continue;
    if (n.count > 0) {
      for (std::uint32_t i = 0; i < n.count; ++i) visit(order_[n.first + i], t_max);
    } else if (top < 62) {
      stack[top++] = n.first + 1;
      stack[top++] = n.first;
    }
  }
}

template <class Visit>
void Bvh::overlap(const Box3& box, Visit&& visit) const {
  if (nodes_.empty()) return;
  std::uint32_t stack[64];
  int top = 0;
  stack[top++] = 0;
  while (top > 0) {
    const Node& n = nodes_[stack[--top]];
    if (n.box.max.x() < box.min.x() || n.box.min.x() > box.max.x() ||
        n.box.max.y() < box.min.y() || n.box.min.y() > box.max.y() ||
        n.box.max.z() < box.min.z() || n.box.min.z() > box.max.z()) {
      continue;
    }
    if (n.count > 0) {
      for (std::uint32_t i = 0; i < n.count; ++i) visit(order_[n.first + i]);
    } else if (top < 62) {
      stack[top++] = n.first + 1;
      stack[top++] = n.first;
    }
  }
}

}  // namespace nfcad::tessellation
