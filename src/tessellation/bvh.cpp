#include "tessellation/bvh.hpp"

#include <algorithm>
#include <numeric>

namespace nfcad::tessellation {

namespace {
constexpr std::uint32_t kLeafSize = 4;
}

void Bvh::build(const std::vector<Box3>& primitive_boxes) {
  nodes_.clear();
  order_.resize(primitive_boxes.size());
  std::iota(order_.begin(), order_.end(), std::uint32_t{0});
  if (primitive_boxes.empty()) return;
  std::vector<QVector3D> centers;
  centers.reserve(primitive_boxes.size());
  for (const Box3& b : primitive_boxes) centers.push_back(b.center());
  nodes_.reserve(primitive_boxes.size() / 2 + 1);
  nodes_.emplace_back();
  buildInto(0, 0, static_cast<std::uint32_t>(primitive_boxes.size()), primitive_boxes, centers);
}

void Bvh::buildInto(std::uint32_t node, std::uint32_t begin, std::uint32_t end,
                    const std::vector<Box3>& boxes, const std::vector<QVector3D>& centers) {
  Box3 box;
  Box3 cbox;
  for (std::uint32_t i = begin; i < end; ++i) {
    box.add(boxes[order_[i]]);
    cbox.add(centers[order_[i]]);
  }
  nodes_[node].box = box;
  if (end - begin <= kLeafSize) {
    nodes_[node].first = begin;
    nodes_[node].count = end - begin;
    return;
  }
  const QVector3D ext = cbox.max - cbox.min;
  const int axis = ext.x() >= ext.y() && ext.x() >= ext.z() ? 0 : (ext.y() >= ext.z() ? 1 : 2);
  const std::uint32_t mid = begin + (end - begin) / 2;
  std::nth_element(order_.begin() + begin, order_.begin() + mid, order_.begin() + end,
                   [&](std::uint32_t a, std::uint32_t b) { return centers[a][axis] < centers[b][axis]; });
  const auto left = static_cast<std::uint32_t>(nodes_.size());
  nodes_.emplace_back();
  nodes_.emplace_back();
  nodes_[node].first = left;
  nodes_[node].count = 0;
  buildInto(left, begin, mid, boxes, centers);
  buildInto(left + 1, mid, end, boxes, centers);
}

float ray_box(const QVector3D& origin, const QVector3D& inv_dir, const Box3& box, float t_max) {
  float t0 = 0.0F;
  float t1 = t_max;
  for (int a = 0; a < 3; ++a) {
    float ta = (box.min[a] - origin[a]) * inv_dir[a];
    float tb = (box.max[a] - origin[a]) * inv_dir[a];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta);
    t1 = std::min(t1, tb);
    if (t0 > t1) return -1.0F;
  }
  return t0;
}

}  // namespace nfcad::tessellation
