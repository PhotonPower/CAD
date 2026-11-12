#include "results/result_store.hpp"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>
#include <nurbforge/geometry/primitives.hpp>

namespace nfcad::results {

Range data_range(const std::vector<float>& values) {
  double lo = std::numeric_limits<double>::max();
  double hi = std::numeric_limits<double>::lowest();
  for (float v : values) {
    if (std::isnan(v)) continue;
    lo = std::min(lo, static_cast<double>(v));
    hi = std::max(hi, static_cast<double>(v));
  }
  if (lo > hi) return {0.0, 1.0};
  if (lo == hi) return {lo - 0.5, hi + 0.5};
  return {lo, hi};
}

Range FaceField::effectiveRange() const { return range.value_or(data_range(values)); }

Range Image::effectiveRange() const { return range.value_or(data_range(values)); }

std::vector<unsigned char> FaceField::texture() const {
  const auto n = static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
  if (values.empty()) {
    std::vector<unsigned char> out = rgba;
    out.resize(4 * n, 0);
    return out;
  }
  const Range r = effectiveRange();
  std::vector<unsigned char> out(4 * n);
  for (std::size_t k = 0; k < n && k < values.size(); ++k) {
    const auto c = colormap_rgba(colormap, (values[k] - r.min) / (r.max - r.min));
    std::copy(c.begin(), c.end(), out.begin() + static_cast<std::ptrdiff_t>(4 * k));
  }
  return out;
}

ResultStore::ResultStore(QObject* parent) : QObject(parent) {}

const ResultSet* ResultStore::set(ResultId id) const {
  const auto it = std::find_if(sets_.begin(), sets_.end(), [id](const ResultSet& s) { return s.id == id; });
  return it == sets_.end() ? nullptr : &*it;
}

ResultId ResultStore::createSet(const QString& name, const QString& source) {
  ResultSet s;
  s.id = next_++;
  s.name = name;
  s.source = source;
  sets_.push_back(std::move(s));
  emit changed();
  return sets_.back().id;
}

ResultId ResultStore::add(ResultId set_id, ResultItem item) {
  for (ResultSet& s : sets_) {
    if (s.id != set_id) continue;
    item.id = next_++;
    s.items.push_back(std::move(item));
    emit changed();
    return s.items.back().id;
  }
  return 0;
}

ResultId ResultStore::addLines(ResultId set, const QString& name, LineSet lines) {
  ResultItem i;
  i.name = name;
  i.kind = ResultItem::Kind::Lines;
  if (lines.starts.empty() && lines.pointCount() > 0) {
    lines.starts = {0, static_cast<std::uint32_t>(lines.pointCount())};  // one polyline
  }
  i.lines = std::make_shared<const LineSet>(std::move(lines));
  return add(set, std::move(i));
}

ResultId ResultStore::addField(ResultId set, const QString& name, FaceField field) {
  ResultItem i;
  i.name = name;
  i.kind = ResultItem::Kind::Field;
  i.field = std::make_shared<const FaceField>(std::move(field));
  return add(set, std::move(i));
}

ResultId ResultStore::addImage(ResultId set, const QString& name, Image image, const QString& key) {
  ResultItem i;
  i.name = name;
  i.key = key;
  i.kind = ResultItem::Kind::Image;
  i.image = std::make_shared<const Image>(std::move(image));
  return add(set, std::move(i));
}

ResultId ResultStore::addCurve(ResultId set, const QString& name, Curve curve, const QString& key) {
  ResultItem i;
  i.name = name;
  i.key = key;
  i.kind = ResultItem::Kind::Curve;
  i.curve = std::make_shared<const Curve>(std::move(curve));
  return add(set, std::move(i));
}

void ResultStore::setKey(ResultId item, const QString& key) {
  if (ResultItem* i = mutableItem(item)) {
    i->key = key;
    emit changed();
  }
}

std::vector<const ResultItem*> ResultStore::itemsWithKey(const QString& key, ResultItem::Kind kind) const {
  for (auto s = sets_.rbegin(); s != sets_.rend(); ++s) {
    std::vector<const ResultItem*> out;
    for (const ResultItem& i : s->items) {
      if (i.key == key && i.kind == kind) out.push_back(&i);
    }
    if (!out.empty()) return out;
  }
  return {};
}

ResultItem* ResultStore::mutableItem(ResultId id) {
  for (ResultSet& s : sets_) {
    for (ResultItem& i : s.items) {
      if (i.id == id) return &i;
    }
  }
  return nullptr;
}

const ResultItem* ResultStore::item(ResultId id) const {
  return const_cast<ResultStore*>(this)->mutableItem(id);
}

void ResultStore::appendToCurve(ResultId id, double x, double y) {
  ResultItem* i = mutableItem(id);
  if (i == nullptr || !i->curve) return;
  auto c = std::make_shared<Curve>(*i->curve);  // copy-on-write: renderers may hold the old one
  c->x.push_back(x);
  c->y.push_back(y);
  i->curve = std::move(c);
  emit curveUpdated(id);
}

void ResultStore::setSetVisible(ResultId id, bool visible) {
  for (ResultSet& s : sets_) {
    if (s.id == id && s.visible != visible) {
      s.visible = visible;
      emit changed();
    }
  }
}

void ResultStore::setItemVisible(ResultId id, bool visible) {
  if (ResultItem* i = mutableItem(id); i != nullptr && i->visible != visible) {
    i->visible = visible;
    emit changed();
  }
}

void ResultStore::removeSet(ResultId id) {
  std::erase_if(sets_, [id](const ResultSet& s) { return s.id == id; });
  emit changed();
}

void ResultStore::clear() {
  sets_.clear();
  emit changed();
}

std::vector<const ResultItem*> ResultStore::visibleItems(ResultItem::Kind kind) const {
  std::vector<const ResultItem*> out;
  for (const ResultSet& s : sets_) {
    if (!s.visible) continue;
    for (const ResultItem& i : s.items) {
      if (i.visible && i.kind == kind) out.push_back(&i);
    }
  }
  return out;
}

std::vector<document::Entity> ResultStore::promote(const LineSet& lines, std::size_t every,
                                                   std::size_t max_count) {
  std::vector<document::Entity> out;
  every = std::max<std::size_t>(every, 1);
  for (std::size_t k = 0; k < lines.polylineCount() && out.size() < max_count; k += every) {
    std::vector<nf::Vec3> pts;
    for (std::uint32_t p = lines.starts[k]; p < lines.starts[k + 1]; ++p) {
      const nf::Vec3 q{lines.points[3 * p], lines.points[3 * p + 1], lines.points[3 * p + 2]};
      if (std::isnan(q.x) || std::isnan(q.y) || std::isnan(q.z)) break;  // lost ray: stop there
      if (pts.empty() || nf::distance(pts.back(), q) > 1e-12) pts.push_back(q);
    }
    if (pts.size() < 2) continue;
    out.push_back(document::make_entity(nf::make_polyline(pts), QObject::tr("Ray %1").arg(k)));
  }
  return out;
}

}  // namespace nfcad::results
