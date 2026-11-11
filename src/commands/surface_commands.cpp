// Curves and surfaces from curves (docs/commands.md, M6b): Spline, Plane, Extrude, Revolve,
// Loft, Sweep (1 and 2 rails), Coons, Gordon (network), Offset curve / surface, Fillet curves /
// surfaces.
#include <QObject>
#include <cmath>
#include <exception>
#include <functional>
#include <numbers>
#include <nurbforge/analysis/closest_point.hpp>
#include <nurbforge/fitting/fit.hpp>
#include <nurbforge/geometry/primitives.hpp>
#include <nurbforge/geometry/transform.hpp>
#include <nurbforge/modeling/construct.hpp>
#include <nurbforge/modeling/fillet.hpp>
#include <nurbforge/modeling/gordon.hpp>
#include <nurbforge/modeling/loft.hpp>
#include <nurbforge/modeling/offset.hpp>
#include <nurbforge/modeling/sweep.hpp>

#include "commands/command_util.hpp"
#include "kernel/curve_features.hpp"
#include "kernel/geometry_ops.hpp"

namespace nfcad::commands {

namespace {

using namespace util;

const nf::NurbsCurve* curve_of(const document::Entity& e) {
  return std::get_if<nf::NurbsCurve>(e.geometry.get());
}

std::optional<nf::NurbsSurface> surface_of(const document::Entity& e) {
  if (const auto* s = std::get_if<nf::NurbsSurface>(e.geometry.get())) return *s;
  if (const auto* f = std::get_if<nf::TrimmedSurface>(e.geometry.get())) {
    if (f->is_untrimmed()) return f->surface();
  }
  return std::nullopt;
}

/// Runs `make`; on failure stores the error in `message` and returns false.
template <class F>
bool attempt(QString& message, const QString& what, F&& make) {
  try {
    make();
    return true;
  } catch (const std::exception& e) {
    message = QObject::tr("%1 failed: %2").arg(what, QString::fromUtf8(e.what()));
    return false;
  }
}

/// Common base: commands whose input is the selected curves (and optionally surfaces).
class CurveInputCommand : public Command {
 public:
  bool usesSelection() const override { return true; }
  bool begin(const std::vector<document::Entity>& selection) override {
    for (const document::Entity& e : selection) {
      if (const nf::NurbsCurve* c = curve_of(e)) {
        curves_.push_back(*c);
        sources_.push_back(e);
      }
    }
    if (curves_.size() < minCurves()) {
      message_ = QObject::tr("%1: select %2").arg(title(), requirement());
      return false;
    }
    if (const std::size_t m = maxCurves(); m > 0 && curves_.size() > m) {
      message_ = QObject::tr("%1: select %2").arg(title(), requirement());
      return false;
    }
    return prepare();
  }

 protected:
  [[nodiscard]] virtual std::size_t minCurves() const { return 1; }
  [[nodiscard]] virtual std::size_t maxCurves() const { return 0; }  // 0: no limit
  [[nodiscard]] virtual QString requirement() const { return QObject::tr("one or more curves"); }
  /// Immediate commands build their result here.
  virtual bool prepare() { return true; }

  void addSurface(const nf::NurbsSurface& s) { entities_.push_back(document::make_entity(s)); }

  std::vector<nf::NurbsCurve> curves_;
  std::vector<document::Entity> sources_;
};

// ------------------------------------------------------------------------------ Spline
class SplineCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("spline"); }
  QString title() const override { return QObject::tr("Interpolated spline"); }
  QString prompt() const override {
    return pts_.empty() ? QObject::tr("Spline: first point")
                        : QObject::tr("Spline: next point (Enter: done)");
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!pts_.empty() && dist(pts_.back(), p) < kEps) return Result::Rejected;
    pts_.push_back(p);
    return Result::Continue;
  }
  Result finish() override {
    if (pts_.size() < 2) return Result::Cancelled;
    entities_.push_back(document::make_entity(make(pts_)));
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (pts_.empty()) return {};
    Polyline3 q = pts_;
    if (dist(q.back(), c) > kEps) q.push_back(c);
    if (q.size() < 2) return {};
    try {
      return {sample(make(q), 32 * q.size())};
    } catch (const std::exception&) {
      return {q};
    }
  }
  std::optional<Point3> basePoint() const override {
    return pts_.empty() ? std::nullopt : std::optional(pts_.back());
  }

 private:
  static nf::NurbsCurve make(const Polyline3& pts) {
    std::vector<nf::Vec3> v;
    for (const Point3& p : pts) v.push_back(V(p));
    return nf::interpolate_curve(v, std::min<std::size_t>(3, v.size() - 1));
  }
  Polyline3 pts_;
};

// ------------------------------------------------------------------------------ Plane
class PlaneCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("plane"); }
  QString title() const override { return QObject::tr("Plane"); }
  QString prompt() const override {
    return corner_ ? QObject::tr("Plane: opposite corner") : QObject::tr("Plane: first corner");
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!corner_) {
      corner_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    const auto [du, dv] = edges(p);
    if (nf::norm(du) < kEps || nf::norm(dv) < kEps) return Result::Rejected;
    entities_.push_back(document::make_entity(nf::make_rectangle(V(*corner_), du, dv)));
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!corner_) return {};
    const auto [du, dv] = edges(c);
    const nf::Vec3 o = V(*corner_);
    return {{P(o), P(o + du), P(o + du + dv), P(o + dv), P(o)}, {P(o), P(o + du + dv)}};
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

// ------------------------------------------------------------------------------ Extrude
class ExtrudeCommand : public CurveInputCommand {
 public:
  QString id() const override { return QStringLiteral("extrude"); }
  QString title() const override { return QObject::tr("Extrude"); }
  QString prompt() const override {
    return from_ ? QObject::tr("Extrude: end of the direction")
                 : QObject::tr("Extrude: distance along the construction-plane normal, or start of "
                               "the direction");
  }
  Result number(double h) override {
    if (from_ || std::abs(h) < kEps) return Result::Rejected;
    return create(V(plane_.n) * h);
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!from_) {
      from_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    if (dist(*from_, p) < kEps) return Result::Rejected;
    return create(V(p) - V(*from_));
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!from_) return {};
    const nf::Vec3 d = V(c) - V(*from_);
    std::vector<Polyline3> out;
    for (const nf::NurbsCurve& cv : curves_) {
      Polyline3 top;
      for (const nf::Vec3& p : cv.sample(48)) top.push_back(P(p + d));
      out.push_back(std::move(top));
      const nf::Vec3 a = cv.evaluate(cv.domain_start());
      const nf::Vec3 b = cv.evaluate(cv.domain_end());
      out.push_back({P(a), P(a + d)});
      out.push_back({P(b), P(b + d)});
    }
    out.push_back({*from_, c});
    return out;
  }
  std::optional<Point3> basePoint() const override { return from_; }
  // the construction plane of the first input decides the default direction
  bool prepare() override { return true; }

 private:
  Result create(const nf::Vec3& d) {
    for (const nf::NurbsCurve& c : curves_) addSurface(nf::extrude(c, d));
    return Result::Done;
  }
  std::optional<Point3> from_;
  PlaneFrame plane_;
};

// ------------------------------------------------------------------------------ Revolve
class RevolveCommand : public CurveInputCommand {
 public:
  QString id() const override { return QStringLiteral("revolve"); }
  QString title() const override { return QObject::tr("Revolve"); }
  QString prompt() const override {
    if (!a_) return QObject::tr("Revolve: start of the axis");
    if (!b_) return QObject::tr("Revolve: end of the axis");
    return QObject::tr("Revolve: angle in degrees (Enter: 360)");
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!a_) {
      a_ = p;
      return Result::Continue;
    }
    if (!b_ && dist(*a_, p) > kEps) {
      b_ = p;
      return Result::Continue;
    }
    return Result::Rejected;
  }
  Result number(double deg) override {
    if (!b_ || deg == 0.0 || std::abs(deg) > 360.0) return Result::Rejected;
    return create(deg * std::numbers::pi / 180.0);
  }
  Result finish() override { return b_ ? create(2.0 * std::numbers::pi) : Result::Cancelled; }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!a_) return {};
    if (!b_) return {{*a_, c}};
    // the profiles at 90° steps
    std::vector<Polyline3> out = {{*a_, *b_}};
    const nf::Vec3 axis = V(*b_) - V(*a_);
    for (int k = 1; k < 4; ++k) {
      const nf::Transform t = nf::Transform::rotation(V(*a_), axis, k * std::numbers::pi / 2);
      for (const nf::NurbsCurve& cv : curves_) {
        Polyline3 q;
        for (const nf::Vec3& p : cv.sample(48)) q.push_back(P(t.apply(p)));
        out.push_back(std::move(q));
      }
    }
    return out;
  }
  std::optional<Point3> basePoint() const override { return b_ ? b_ : a_; }

 private:
  Result create(double angle) {
    const bool ok = attempt(message_, title(), [&] {
      for (const nf::NurbsCurve& c : curves_) {
        // positive angles turn counter-clockwise about the axis direction
        addSurface(angle > 0 ? nf::revolve(c, V(*a_), V(*b_) - V(*a_), angle)
                             : nf::revolve(c, V(*a_), V(*a_) - V(*b_), -angle));
      }
    });
    return ok ? Result::Done : Result::Rejected;
  }
  std::optional<Point3> a_;
  std::optional<Point3> b_;
};

// ------------------------------------------------------------------------------ immediate ones
/// Builds its surface in begin() from the curves in selection order.
class ImmediateCurveCommand : public CurveInputCommand {
 public:
  using Make = std::function<void(ImmediateCurveCommand&)>;
  ImmediateCurveCommand(QString id, QString title, QString requirement, std::size_t min,
                        std::size_t max, Make make)
      : id_(std::move(id)), title_(std::move(title)), requirement_(std::move(requirement)),
        min_(min), max_(max), make_(std::move(make)) {}
  QString id() const override { return id_; }
  QString title() const override { return title_; }
  QString prompt() const override { return title_; }
  bool immediate() const override { return true; }
  Result point(const Point3&, const PlaneFrame&) override { return Result::Rejected; }
  Result finish() override { return Result::Done; }

  const std::vector<nf::NurbsCurve>& curves() const { return curves_; }
  void add(const nf::NurbsSurface& s) { addSurface(s); }

 protected:
  std::size_t minCurves() const override { return min_; }
  std::size_t maxCurves() const override { return max_; }
  QString requirement() const override { return requirement_; }
  bool prepare() override { return attempt(message_, title_, [&] { make_(*this); }); }

 private:
  QString id_, title_, requirement_;
  std::size_t min_, max_;
  Make make_;
};

/// Splits a curve network into two families: the curves crossing the first curve, and the rest.
std::pair<std::vector<nf::NurbsCurve>, std::vector<nf::NurbsCurve>> split_network(
    const std::vector<nf::NurbsCurve>& curves) {
  std::vector<nf::NurbsCurve> a{curves.front()};
  std::vector<nf::NurbsCurve> b;
  for (std::size_t i = 1; i < curves.size(); ++i) {
    const bool meets = !kernel::curve_intersections(curves.front(), curves[i]).empty();
    (meets ? b : a).push_back(curves[i]);
  }
  return {a, b};
}

/// Orders curves of one family by where they meet the first curve of the other family.
void order_along(std::vector<nf::NurbsCurve>& family, const nf::NurbsCurve& across) {
  std::vector<std::pair<double, std::size_t>> key;
  for (std::size_t i = 0; i < family.size(); ++i) {
    const auto x = kernel::curve_intersections(across, family[i]);
    const nf::Vec3 p = x.empty() ? family[i].evaluate(family[i].domain_start()) : x.front();
    key.emplace_back(nf::distance(across.evaluate(across.domain_start()), p), i);
  }
  std::sort(key.begin(), key.end());
  std::vector<nf::NurbsCurve> out;
  for (const auto& [d, i] : key) out.push_back(family[i]);
  family = std::move(out);
}

// ------------------------------------------------------------------------------ Offset curve
class OffsetCurveCommand : public CurveInputCommand {
 public:
  QString id() const override { return QStringLiteral("offset_curve"); }
  QString title() const override { return QObject::tr("Offset curve"); }
  QString prompt() const override {
    return QObject::tr("Offset curve (distance %1): click the side (type a number to change the distance)")
        .arg(distance_);
  }
  Result number(double d) override {
    if (d <= 0) return Result::Rejected;
    distance_ = d;
    return Result::Continue;
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    const bool ok = attempt(message_, title(), [&] {
      for (const nf::NurbsCurve& c : curves_) {
        entities_.push_back(document::make_entity(nf::offset_curve(c, side(c, p, plane) * distance_, V(plane.n))));
      }
    });
    if (!ok) {
      entities_.clear();
      return Result::Rejected;
    }
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& cursor, const PlaneFrame& plane) const override {
    std::vector<Polyline3> out;
    for (const nf::NurbsCurve& c : curves_) {
      try {
        out.push_back(sample(nf::offset_curve(c, side(c, cursor, plane) * distance_, V(plane.n), 1e-3), 96));
      } catch (const std::exception&) {
      }
    }
    return out;
  }

 private:
  static double side(const nf::NurbsCurve& c, const Point3& p, const PlaneFrame& plane) {
    // left of the direction of travel (seen from the plane normal) is positive
    const nf::CurvePoint foot = nf::closest_point(c, V(p));
    const nf::Vec3 tangent = c.tangent(foot.u);
    return nf::dot(nf::cross(V(plane.n), tangent), V(p) - foot.point) >= 0 ? 1.0 : -1.0;
  }
  double distance_ = 1.0;
};

// ------------------------------------------------------------------------------ Fillet curves
class FilletCurvesCommand : public CurveInputCommand {
 public:
  QString id() const override { return QStringLiteral("fillet_curve"); }
  QString title() const override { return QObject::tr("Fillet curves"); }
  QString prompt() const override {
    return QObject::tr("Fillet: radius (two curves: fillet between them; one curve: all its corners)");
  }
  Result number(double r) override {
    if (r <= 0) return Result::Rejected;
    const bool ok = attempt(message_, title(), [&] {
      const nf::Vec3 n = plane_normal();
      if (curves_.size() == 1) {
        replace_.emplace_back(sources_[0].id, std::make_shared<const document::Geometry>(
                                                  nf::fillet_corners(curves_[0], r)));
        return;
      }
      const nf::CurveFillet f = nf::fillet_curves(curves_[0], curves_[1], r, n);
      entities_.push_back(document::make_entity(f.arc));
      if (f.trimmed_a) {
        replace_.emplace_back(sources_[0].id, std::make_shared<const document::Geometry>(*f.trimmed_a));
      }
      if (f.trimmed_b) {
        replace_.emplace_back(sources_[1].id, std::make_shared<const document::Geometry>(*f.trimmed_b));
      }
    });
    if (!ok) {
      entities_.clear();
      replace_.clear();
      return Result::Rejected;
    }
    return Result::Done;
  }
  Result point(const Point3&, const PlaneFrame&) override { return Result::Rejected; }

 protected:
  std::size_t maxCurves() const override { return 2; }
  QString requirement() const override { return QObject::tr("one or two planar curves"); }

 private:
  nf::Vec3 plane_normal() const {
    // Newell normal of the control points of both curves
    nf::Vec3 n{0, 0, 0};
    std::vector<nf::Vec3> pts;
    for (const auto& c : curves_) pts.insert(pts.end(), c.control_points().begin(), c.control_points().end());
    for (std::size_t i = 0; i < pts.size(); ++i) n += nf::cross(pts[i], pts[(i + 1) % pts.size()]);
    if (nf::norm(n) < 1e-12) return {0, 0, 1};
    return n / nf::norm(n);
  }
};

// ------------------------------------------------------------------------------ surfaces input
class SurfaceInputCommand : public Command {
 public:
  explicit SurfaceInputCommand(bool fillet) : fillet_(fillet) {}
  QString id() const override {
    return fillet_ ? QStringLiteral("fillet_surface") : QStringLiteral("offset_surface");
  }
  QString title() const override {
    return fillet_ ? QObject::tr("Fillet surfaces") : QObject::tr("Offset surface");
  }
  QString prompt() const override {
    return fillet_ ? QObject::tr("Fillet surfaces: radius")
                   : QObject::tr("Offset surface: distance along the normal (negative: other side)");
  }
  bool usesSelection() const override { return true; }
  bool begin(const std::vector<document::Entity>& selection) override {
    for (const document::Entity& e : selection) {
      if (auto s = surface_of(e)) surfaces_.push_back(std::move(*s));
    }
    if ((fillet_ && surfaces_.size() != 2) || (!fillet_ && surfaces_.empty())) {
      message_ = fillet_ ? QObject::tr("Fillet surfaces: select two (untrimmed) surfaces")
                         : QObject::tr("Offset surface: select (untrimmed) surfaces");
      return false;
    }
    return true;
  }
  Result number(double v) override {
    if (v == 0.0) return Result::Rejected;
    const bool ok = attempt(message_, title(), [&] {
      if (!fillet_) {
        for (const auto& s : surfaces_) {
          entities_.push_back(document::make_entity(nf::offset_surface(s, v, 1e-5)));
        }
        return;
      }
      // the ball side is not known in advance: try the four combinations
      std::string last;
      for (const auto& [s1, s2] : {std::pair{1, 1}, {-1, -1}, {1, -1}, {-1, 1}}) {
        try {
          nf::SurfaceFilletOptions o;
          o.side1 = s1;
          o.side2 = s2;
          o.tolerance = 1e-5;
          entities_.push_back(document::make_entity(nf::fillet_surfaces(surfaces_[0], surfaces_[1], std::abs(v), o).surface));
          return;
        } catch (const std::exception& e) {
          last = e.what();
        }
      }
      throw std::runtime_error(last);
    });
    return ok ? Result::Done : Result::Rejected;
  }
  Result point(const Point3&, const PlaneFrame&) override { return Result::Rejected; }

 private:
  bool fillet_;
  std::vector<nf::NurbsSurface> surfaces_;
};

}  // namespace

namespace detail {

std::unique_ptr<Command> create_surface_command(const QString& n) {
  using M = ImmediateCurveCommand;
  if (n == QLatin1String("spline") || n == QLatin1String("sp")) return std::make_unique<SplineCommand>();
  if (n == QLatin1String("plane")) return std::make_unique<PlaneCommand>();
  if (n == QLatin1String("extrude") || n == QLatin1String("ext")) return std::make_unique<ExtrudeCommand>();
  if (n == QLatin1String("revolve") || n == QLatin1String("rev")) return std::make_unique<RevolveCommand>();
  if (n == QLatin1String("loft")) {
    return std::make_unique<M>(QStringLiteral("loft"), QObject::tr("Loft"),
                               QObject::tr("two or more section curves (in order)"), 2, 0,
                               [](M& m) { m.add(nf::loft(m.curves())); });
  }
  if (n == QLatin1String("sweep1")) {
    return std::make_unique<M>(QStringLiteral("sweep1"), QObject::tr("Sweep 1 rail"),
                               QObject::tr("the profile, then the rail"), 2, 2,
                               [](M& m) { m.add(nf::sweep(m.curves()[0], m.curves()[1])); });
  }
  if (n == QLatin1String("sweep2")) {
    return std::make_unique<M>(QStringLiteral("sweep2"), QObject::tr("Sweep 2 rails"),
                               QObject::tr("the profile, then rail 1 and rail 2"), 3, 3, [](M& m) {
                                 m.add(nf::sweep2(m.curves()[0], m.curves()[1], m.curves()[2]));
                               });
  }
  if (n == QLatin1String("coons")) {
    return std::make_unique<M>(QStringLiteral("coons"), QObject::tr("Coons patch"),
                               QObject::tr("four boundary curves: two opposite, then the other two"),
                               4, 4, [](M& m) {
                                 const auto& c = m.curves();
                                 m.add(nf::coons(c[0], c[1], c[2], c[3], 1e-6));
                               });
  }
  if (n == QLatin1String("gordon") || n == QLatin1String("network")) {
    return std::make_unique<M>(QStringLiteral("gordon"), QObject::tr("Gordon surface"),
                               QObject::tr("a curve network (at least 2 × 2 crossing curves)"), 4, 0,
                               [](M& m) {
                                 auto [u, v] = split_network(m.curves());
                                 if (u.size() < 2 || v.size() < 2) {
                                   throw std::runtime_error("the curves do not form a network");
                                 }
                                 order_along(u, v.front());
                                 order_along(v, u.front());
                                 m.add(nf::gordon(u, v, 1e-6));
                               });
  }
  if (n == QLatin1String("offset_curve") || n == QLatin1String("offset")) {
    return std::make_unique<OffsetCurveCommand>();
  }
  if (n == QLatin1String("fillet_curve") || n == QLatin1String("fillet")) {
    return std::make_unique<FilletCurvesCommand>();
  }
  if (n == QLatin1String("offset_surface") || n == QLatin1String("offsetsrf")) {
    return std::make_unique<SurfaceInputCommand>(false);
  }
  if (n == QLatin1String("fillet_surface") || n == QLatin1String("filletsrf")) {
    return std::make_unique<SurfaceInputCommand>(true);
  }
  return nullptr;
}

QStringList surface_command_ids() {
  return {QStringLiteral("spline"),         QStringLiteral("plane"),        QStringLiteral("extrude"),
          QStringLiteral("revolve"),        QStringLiteral("loft"),         QStringLiteral("sweep1"),
          QStringLiteral("sweep2"),         QStringLiteral("coons"),        QStringLiteral("gordon"),
          QStringLiteral("offset_curve"),   QStringLiteral("fillet_curve"), QStringLiteral("offset_surface"),
          QStringLiteral("fillet_surface")};
}

}  // namespace detail

}  // namespace nfcad::commands
