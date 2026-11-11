// Drawing and measuring commands (docs/commands.md, M5).
#include <QObject>
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <nurbforge/geometry/primitives.hpp>

#include "commands/command.hpp"

namespace nfcad::commands {

namespace {

constexpr double kEps = 1e-12;

nf::Vec3 V(const Point3& p) { return {p.x, p.y, p.z}; }
Point3 P(const nf::Vec3& v) { return {v.x, v.y, v.z}; }

double dist(const Point3& a, const Point3& b) { return nf::distance(V(a), V(b)); }

/// Component of p - origin in the plane (normal part removed).
nf::Vec3 in_plane(const Point3& p, const Point3& origin, const PlaneFrame& f) {
  const nf::Vec3 d = V(p) - V(origin);
  return d - V(f.n) * nf::dot(d, V(f.n));
}

Polyline3 sample(const nf::NurbsCurve& c, std::size_t n = 96) {
  Polyline3 out;
  for (const nf::Vec3& p : c.sample(n)) out.push_back(P(p));
  return out;
}

/// Preview helper: builds a curve, returns its polyline or nothing if the input is degenerate.
std::vector<Polyline3> try_curve(const std::function<nf::NurbsCurve()>& make, std::size_t n = 96) {
  try {
    return {sample(make(), n)};
  } catch (const std::exception&) {
    return {};
  }
}

/// Adds a curve entity; false if the construction failed (degenerate input).
bool add_curve(std::vector<document::Entity>& out, const std::function<nf::NurbsCurve()>& make) {
  try {
    out.push_back(document::make_entity(make()));
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// ------------------------------------------------------------------------------ Point
class PointCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("point"); }
  QString title() const override { return QObject::tr("Point"); }
  QString prompt() const override {
    return QObject::tr("Point: location (Enter / right-click: done)");
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    entities_.push_back(document::make_entity(V(p)));
    ++count_;
    last_ = p;
    return Result::Continue;
  }
  Result finish() override { return count_ > 0 ? Result::Done : Result::Cancelled; }
  std::optional<Point3> basePoint() const override { return last_; }

 private:
  int count_ = 0;
  std::optional<Point3> last_;
};

// ------------------------------------------------------------------------------ Line
class LineCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("line"); }
  QString title() const override { return QObject::tr("Line"); }
  QString prompt() const override {
    return start_ ? QObject::tr("Line: end point (or length)") : QObject::tr("Line: start point");
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!start_) {
      start_ = p;
      return Result::Continue;
    }
    if (dist(*start_, p) < kEps) return Result::Rejected;
    add_curve(entities_, [&] { return nf::make_line(V(*start_), V(p)); });
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!start_) return {};
    return {{*start_, c}};
  }
  std::optional<Point3> basePoint() const override { return start_; }

 private:
  std::optional<Point3> start_;
};

// ------------------------------------------------------------------------------ Polyline
class PolylineCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("polyline"); }
  QString title() const override { return QObject::tr("Polyline"); }
  QString prompt() const override {
    if (pts_.empty()) return QObject::tr("Polyline: start point");
    return QObject::tr("Polyline: next point (Enter: done, first point: close)");
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!pts_.empty() && dist(pts_.back(), p) < kEps) return Result::Rejected;
    if (pts_.size() >= 3 && dist(pts_.front(), p) < kEps) {  // clicked the start: close
      pts_.push_back(pts_.front());
      return create();
    }
    pts_.push_back(p);
    return Result::Continue;
  }
  Result finish() override { return pts_.size() >= 2 ? create() : Result::Cancelled; }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (pts_.empty()) return {};
    Polyline3 pl = pts_;
    pl.push_back(c);
    return {pl};
  }
  std::optional<Point3> basePoint() const override {
    return pts_.empty() ? std::nullopt : std::optional(pts_.back());
  }

 private:
  Result create() {
    std::vector<nf::Vec3> v;
    for (const Point3& p : pts_) v.push_back(V(p));
    return add_curve(entities_, [&] { return nf::make_polyline(v); }) ? Result::Done
                                                                       : Result::Cancelled;
  }
  Polyline3 pts_;
};

// ------------------------------------------------------------------------------ Rectangle
class RectangleCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("rectangle"); }
  QString title() const override { return QObject::tr("Rectangle"); }
  QString prompt() const override {
    return corner_ ? QObject::tr("Rectangle: opposite corner") : QObject::tr("Rectangle: first corner");
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!corner_) {
      corner_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    const auto [du, dv] = edges(p);
    if (nf::norm(du) < kEps || nf::norm(dv) < kEps) return Result::Rejected;
    add_curve(entities_, [&] { return nf::make_rectangle_curve(V(*corner_), du, dv); });
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!corner_) return {};
    const auto [du, dv] = edges(c);
    const nf::Vec3 o = V(*corner_);
    return {{P(o), P(o + du), P(o + du + dv), P(o + dv), P(o)}};
  }
  std::optional<Point3> basePoint() const override { return corner_; }

 private:
  std::pair<nf::Vec3, nf::Vec3> edges(const Point3& p) const {
    const nf::Vec3 d = V(p) - V(*corner_);
    return {V(plane_.u) * nf::dot(d, V(plane_.u)), V(plane_.v) * nf::dot(d, V(plane_.v))};
  }
  std::optional<Point3> corner_;
  PlaneFrame plane_;
};

// ------------------------------------------------------------------------------ Polygon
class PolygonCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("polygon"); }
  QString title() const override { return QObject::tr("Polygon"); }
  QString prompt() const override {
    return centre_ ? QObject::tr("Polygon (%1 sides): corner point or radius").arg(sides_)
                   : QObject::tr("Polygon (%1 sides): centre (type a number to change the sides)")
                         .arg(sides_);
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!centre_) {
      centre_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    const nf::Vec3 r = in_plane(p, *centre_, plane_);
    if (nf::norm(r) < kEps) return Result::Rejected;
    add_curve(entities_, [&] {
      return nf::make_polygon(V(*centre_), V(plane_.n), nf::norm(r), sides_, r);
    });
    return Result::Done;
  }
  Result number(double v) override {
    if (!centre_) {
      if (v < 3 || v > 1000 || std::floor(v) != v) return Result::Rejected;
      sides_ = static_cast<std::size_t>(v);
      return Result::Continue;
    }
    if (v <= 0) return Result::Rejected;
    add_curve(entities_, [&] {
      return nf::make_polygon(V(*centre_), V(plane_.n), v, sides_, V(plane_.u));
    });
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!centre_) return {};
    const nf::Vec3 r = in_plane(c, *centre_, plane_);
    return try_curve([&] {
      return nf::make_polygon(V(*centre_), V(plane_.n), nf::norm(r), sides_, r);
    }, 4 * sides_);
  }
  std::optional<Point3> basePoint() const override { return centre_; }

 private:
  std::optional<Point3> centre_;
  PlaneFrame plane_;
  std::size_t sides_ = 6;
};

// ------------------------------------------------------------------------------ Circle
class CircleCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("circle"); }
  QString title() const override { return QObject::tr("Circle"); }
  QString prompt() const override {
    return centre_ ? QObject::tr("Circle: radius (point or number)") : QObject::tr("Circle: centre");
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!centre_) {
      centre_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    return create(nf::norm(in_plane(p, *centre_, plane_)));
  }
  Result number(double v) override { return centre_ ? create(v) : Result::Rejected; }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!centre_) return {};
    const double r = nf::norm(in_plane(c, *centre_, plane_));
    auto out = try_curve([&] { return nf::make_circle(V(*centre_), V(plane_.n), r); });
    out.push_back({*centre_, c});
    return out;
  }
  std::optional<Point3> basePoint() const override { return centre_; }

 private:
  Result create(double r) {
    if (r < kEps) return Result::Rejected;
    add_curve(entities_, [&] { return nf::make_circle(V(*centre_), V(plane_.n), r); });
    return Result::Done;
  }
  std::optional<Point3> centre_;
  PlaneFrame plane_;
};

// ------------------------------------------------------------------------------ Circle 3 points
class Circle3pCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("circle3p"); }
  QString title() const override { return QObject::tr("Circle (3 points)"); }
  QString prompt() const override {
    return QObject::tr("Circle: point %1 of 3").arg(pts_.size() + 1);
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    for (const Point3& q : pts_) {
      if (dist(q, p) < kEps) return Result::Rejected;
    }
    pts_.push_back(p);
    if (pts_.size() < 3) return Result::Continue;
    if (!add_curve(entities_, [&] {
          return nf::make_circle_3pt(V(pts_[0]), V(pts_[1]), V(pts_[2]));
        })) {
      pts_.pop_back();  // collinear
      return Result::Rejected;
    }
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (pts_.size() == 1) return {{pts_[0], c}};
    if (pts_.size() == 2) {
      return try_curve([&] { return nf::make_circle_3pt(V(pts_[0]), V(pts_[1]), V(c)); });
    }
    return {};
  }
  std::optional<Point3> basePoint() const override {
    return pts_.empty() ? std::nullopt : std::optional(pts_.back());
  }

 private:
  Polyline3 pts_;
};

// ------------------------------------------------------------------------------ Arc
class ArcCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("arc"); }
  QString title() const override { return QObject::tr("Arc"); }
  QString prompt() const override {
    switch (pts_.size()) {
      case 0: return QObject::tr("Arc: start point");
      case 1: return QObject::tr("Arc: end point");
      default: return QObject::tr("Arc: point on the arc");
    }
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    for (const Point3& q : pts_) {
      if (dist(q, p) < kEps) return Result::Rejected;
    }
    pts_.push_back(p);
    if (pts_.size() < 3) return Result::Continue;
    // start, end, through -> arc from start through the third point to the end
    if (!add_curve(entities_, [&] { return nf::make_arc_3pt(V(pts_[0]), V(pts_[2]), V(pts_[1])); })) {
      pts_.pop_back();
      return Result::Rejected;
    }
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (pts_.size() == 1) return {{pts_[0], c}};
    if (pts_.size() == 2) {
      return try_curve([&] { return nf::make_arc_3pt(V(pts_[0]), V(c), V(pts_[1])); });
    }
    return {};
  }
  std::optional<Point3> basePoint() const override {
    return pts_.empty() ? std::nullopt : std::optional(pts_.back());
  }

 private:
  Polyline3 pts_;
};

// ------------------------------------------------------------------------------ Helix
class HelixCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("helix"); }
  QString title() const override { return QObject::tr("Helix"); }
  QString prompt() const override {
    switch (pts_.size()) {
      case 0:
        return QObject::tr("Helix (%1 turns): axis start (type a number to change the turns)")
            .arg(turns_);
      case 1: return QObject::tr("Helix (%1 turns): axis end").arg(turns_);
      default: return QObject::tr("Helix (%1 turns): radius (point or number)").arg(turns_);
    }
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (pts_.size() < 2) {
      if (!pts_.empty() && dist(pts_[0], p) < kEps) return Result::Rejected;
      pts_.push_back(p);
      plane_ = plane;
      return Result::Continue;
    }
    return create(start_for(p), 1e-7);
  }
  Result number(double v) override {
    if (pts_.size() < 2) {
      if (v <= 0) return Result::Rejected;
      turns_ = v;
      return Result::Continue;
    }
    if (v <= 0) return Result::Rejected;
    return create(V(pts_[0]) + perpendicular() * v, 1e-7);
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (pts_.size() == 1) return {{pts_[0], c}};
    if (pts_.size() == 2) {
      const nf::Vec3 start = start_for(c);
      const double h = nf::norm(V(pts_[1]) - V(pts_[0]));
      std::vector<Polyline3> out = try_curve([&] {
        return nf::make_helix(V(pts_[0]), axis(), start, h / turns_, turns_, -1.0, false, 1e-3 * h);
      }, static_cast<std::size_t>(std::clamp(turns_ * 48.0, 48.0, 2000.0)));
      out.push_back({pts_[0], pts_[1]});
      return out;
    }
    return {};
  }
  std::optional<Point3> basePoint() const override {
    return pts_.empty() ? std::nullopt : std::optional(pts_.back());
  }

 private:
  nf::Vec3 axis() const { return nf::normalized(V(pts_[1]) - V(pts_[0])); }
  nf::Vec3 perpendicular() const {
    // the plane's u axis projected perpendicular to the axis, else any perpendicular
    const nf::Vec3 a = axis();
    const nf::Vec3 u = V(plane_.u) - a * nf::dot(V(plane_.u), a);
    return nf::norm(u) > 1e-9 ? u / nf::norm(u) : nf::any_perpendicular(a);
  }
  nf::Vec3 start_for(const Point3& p) const {
    const nf::Vec3 a = axis();
    nf::Vec3 r = V(p) - V(pts_[0]);
    r = r - a * nf::dot(r, a);
    return V(pts_[0]) + r;
  }
  Result create(const nf::Vec3& start, double tol) {
    if (nf::norm(start - V(pts_[0])) < kEps) return Result::Rejected;
    const double h = nf::norm(V(pts_[1]) - V(pts_[0]));
    return add_curve(entities_, [&] {
             return nf::make_helix(V(pts_[0]), axis(), start, h / turns_, turns_, -1.0, false, tol);
           })
               ? Result::Done
               : Result::Rejected;
  }
  Polyline3 pts_;
  PlaneFrame plane_;
  double turns_ = 5.0;
};

// ------------------------------------------------------------------------------ Distance
class DistanceCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("distance"); }
  QString title() const override { return QObject::tr("Distance"); }
  QString prompt() const override {
    return first_ ? QObject::tr("Distance: second point") : QObject::tr("Distance: first point");
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!first_) {
      first_ = p;
      return Result::Continue;
    }
    const double dx = p.x - first_->x;
    const double dy = p.y - first_->y;
    const double dz = p.z - first_->z;
    message_ = QObject::tr("Distance %1   (dx %2, dy %3, dz %4)")
                   .arg(std::sqrt(dx * dx + dy * dy + dz * dz), 0, 'g', 10)
                   .arg(dx, 0, 'g', 8)
                   .arg(dy, 0, 'g', 8)
                   .arg(dz, 0, 'g', 8);
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!first_) return {};
    return {{*first_, c}};
  }
  std::optional<Point3> basePoint() const override { return first_; }

 private:
  std::optional<Point3> first_;
};

struct Entry {
  const char* id;
  std::vector<const char*> aliases;  // (an initializer_list member would dangle)
  std::function<std::unique_ptr<Command>()> make;
};

const std::vector<Entry>& registry() {
  static const std::vector<Entry> r = {
      {"point", {"pt", "po"}, [] { return std::make_unique<PointCommand>(); }},
      {"line", {"l", "li"}, [] { return std::make_unique<LineCommand>(); }},
      {"polyline", {"pl"}, [] { return std::make_unique<PolylineCommand>(); }},
      {"rectangle", {"rec", "rect"}, [] { return std::make_unique<RectangleCommand>(); }},
      {"polygon", {"pol"}, [] { return std::make_unique<PolygonCommand>(); }},
      {"circle", {"c", "ci"}, [] { return std::make_unique<CircleCommand>(); }},
      {"circle3p", {"c3"}, [] { return std::make_unique<Circle3pCommand>(); }},
      {"arc", {"a"}, [] { return std::make_unique<ArcCommand>(); }},
      {"helix", {"h", "he"}, [] { return std::make_unique<HelixCommand>(); }},
      {"distance", {"dist", "di"}, [] { return std::make_unique<DistanceCommand>(); }},
  };
  return r;
}

}  // namespace

std::unique_ptr<Command> create_command(const QString& name) {
  const QString n = name.trimmed().toLower();
  for (const Entry& e : registry()) {
    if (n == QLatin1String(e.id)) return e.make();
    for (const char* a : e.aliases) {
      if (n == QLatin1String(a)) return e.make();
    }
  }
  return nullptr;
}

QStringList command_ids() {
  QStringList out;
  for (const Entry& e : registry()) out << QString::fromLatin1(e.id);
  return out;
}

}  // namespace nfcad::commands
