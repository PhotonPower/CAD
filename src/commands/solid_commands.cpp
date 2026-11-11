// Solid primitives, Booleans and the Angle measurement (docs/commands.md, M6).
#include <QObject>
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <numbers>
#include <nurbforge/geometry/primitives.hpp>
#include <nurbforge/topology/solids.hpp>

#include "commands/command_util.hpp"
#include "kernel/geometry_ops.hpp"

namespace nfcad::commands {

namespace util {
Polyline3 circle(const nf::Vec3& centre, const nf::Vec3& normal, double r, std::size_t n) {
  const nf::Vec3 x = nf::any_perpendicular(normal);
  const nf::Vec3 y = nf::cross(normal, x);
  Polyline3 out;
  for (std::size_t i = 0; i <= n; ++i) {
    const double a = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n);
    out.push_back(P(centre + x * (r * std::cos(a)) + y * (r * std::sin(a))));
  }
  return out;
}
}  // namespace util

namespace {

using namespace util;

/// Adds a shell entity built by `make`; false (and message) on a construction error.
bool add_shell(std::vector<document::Entity>& out, QString& message,
               const std::function<nf::Shell()>& make) {
  try {
    out.push_back(document::make_entity(make()));
    return true;
  } catch (const std::exception& e) {
    message = QString::fromUtf8(e.what());
    return false;
  }
}

// ------------------------------------------------------------------------------ Box
class BoxCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("box"); }
  QString title() const override { return QObject::tr("Box"); }
  QString prompt() const override {
    switch (pts_.size()) {
      case 0: return QObject::tr("Box: first corner of the base");
      case 1: return QObject::tr("Box: opposite corner of the base");
      default: return QObject::tr("Box: height (point or number)");
    }
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (pts_.empty()) plane_ = plane;
    if (pts_.size() < 2) {
      if (pts_.size() == 1) {
        const nf::Vec3 d = in_plane(p, pts_[0], plane_);
        if (std::abs(nf::dot(d, V(plane_.u))) < kEps || std::abs(nf::dot(d, V(plane_.v))) < kEps) {
          return Result::Rejected;
        }
      }
      pts_.push_back(p);
      return Result::Continue;
    }
    return create(height(p, pts_[0], plane_));
  }
  Result number(double v) override { return pts_.size() == 2 ? create(v) : Result::Rejected; }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (pts_.empty()) return {};
    const auto corners = base(pts_.size() == 1 ? c : pts_[1]);
    Polyline3 b(corners.begin(), corners.end());
    b.push_back(corners[0]);
    if (pts_.size() == 1) return {b};
    const nf::Vec3 h = V(plane_.n) * height(c, pts_[0], plane_);
    Polyline3 top;
    std::vector<Polyline3> out = {b};
    for (const Point3& q : corners) {
      top.push_back(P(V(q) + h));
      out.push_back({q, P(V(q) + h)});
    }
    top.push_back(top.front());
    out.push_back(top);
    return out;
  }
  std::optional<Point3> basePoint() const override {
    return pts_.empty() ? std::nullopt : std::optional(pts_.back());
  }

 private:
  std::array<Point3, 4> base(const Point3& opposite) const {
    const nf::Vec3 o = V(pts_[0]);
    const nf::Vec3 d = V(opposite) - o;
    const nf::Vec3 du = V(plane_.u) * nf::dot(d, V(plane_.u));
    const nf::Vec3 dv = V(plane_.v) * nf::dot(d, V(plane_.v));
    return {P(o), P(o + du), P(o + du + dv), P(o + dv)};
  }
  Result create(double h) {
    if (std::abs(h) < kEps) return Result::Rejected;
    // the standard construction planes are axis aligned: the box is the bounding box of its corners
    nf::Vec3 lo{1e300, 1e300, 1e300};
    nf::Vec3 hi{-1e300, -1e300, -1e300};
    for (const Point3& q : base(pts_[1])) {
      for (const nf::Vec3 p : {V(q), V(q) + V(plane_.n) * h}) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
      }
    }
    return add_shell(entities_, message_, [&] { return nf::make_box(lo, hi - lo); }) ? Result::Done
                                                                                    : Result::Rejected;
  }
  Polyline3 pts_;
  PlaneFrame plane_;
};

// ------------------------------------------------------------------------------ Sphere
class SphereCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("sphere"); }
  QString title() const override { return QObject::tr("Sphere"); }
  QString prompt() const override {
    return centre_ ? QObject::tr("Sphere: radius (point or number)") : QObject::tr("Sphere: centre");
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!centre_) {
      centre_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    return create(dist(*centre_, p));
  }
  Result number(double v) override { return centre_ ? create(v) : Result::Rejected; }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!centre_) return {};
    const double r = dist(*centre_, c);
    return {circle(V(*centre_), V(plane_.n), r), circle(V(*centre_), V(plane_.u), r),
            circle(V(*centre_), V(plane_.v), r)};
  }
  std::optional<Point3> basePoint() const override { return centre_; }

 private:
  Result create(double r) {
    if (r < kEps) return Result::Rejected;
    return add_shell(entities_, message_, [&] { return nf::make_sphere(V(*centre_), r); })
               ? Result::Done
               : Result::Rejected;
  }
  std::optional<Point3> centre_;
  PlaneFrame plane_;
};

// ------------------------------------------------------------------------------ Cylinder / Cone
class AxialCommand : public Command {
 public:
  explicit AxialCommand(bool cone) : cone_(cone) {}
  QString id() const override { return cone_ ? QStringLiteral("cone") : QStringLiteral("cylinder"); }
  QString title() const override { return cone_ ? QObject::tr("Cone") : QObject::tr("Cylinder"); }
  QString prompt() const override {
    const QString t = title();
    if (!centre_) return QObject::tr("%1: centre of the base").arg(t);
    if (radius_ <= 0) return QObject::tr("%1: radius (point or number)").arg(t);
    return QObject::tr("%1: height (point or number)").arg(t);
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!centre_) {
      centre_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    if (radius_ <= 0) return setRadius(nf::norm(in_plane(p, *centre_, plane_)));
    return create(height(p, *centre_, plane_));
  }
  Result number(double v) override {
    if (!centre_) return Result::Rejected;
    return radius_ <= 0 ? setRadius(v) : create(v);
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!centre_) return {};
    const nf::Vec3 n = V(plane_.n);
    if (radius_ <= 0) return {circle(V(*centre_), n, nf::norm(in_plane(c, *centre_, plane_)))};
    const double h = height(c, *centre_, plane_);
    const nf::Vec3 top = V(*centre_) + n * h;
    std::vector<Polyline3> out = {circle(V(*centre_), n, radius_), {*centre_, P(top)}};
    if (cone_) {
      const nf::Vec3 x = nf::any_perpendicular(n) * radius_;
      out.push_back({P(V(*centre_) + x), P(top), P(V(*centre_) - x)});
    } else {
      out.push_back(circle(top, n, radius_));
    }
    return out;
  }
  std::optional<Point3> basePoint() const override { return centre_; }

 private:
  Result setRadius(double r) {
    if (r < kEps) return Result::Rejected;
    radius_ = r;
    return Result::Continue;
  }
  Result create(double h) {
    if (std::abs(h) < kEps) return Result::Rejected;
    const nf::Vec3 axis = V(plane_.n) * (h > 0 ? 1.0 : -1.0);
    const bool ok = add_shell(entities_, message_, [&] {
      return cone_ ? nf::make_cone(V(*centre_), axis, radius_, 0.0, std::abs(h))
                   : nf::make_cylinder(V(*centre_), axis, radius_, std::abs(h));
    });
    return ok ? Result::Done : Result::Rejected;
  }
  bool cone_;
  std::optional<Point3> centre_;
  PlaneFrame plane_;
  double radius_ = 0.0;
};

// ------------------------------------------------------------------------------ Torus
class TorusCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("torus"); }
  QString title() const override { return QObject::tr("Torus"); }
  QString prompt() const override {
    if (!centre_) return QObject::tr("Torus: centre");
    if (major_ <= 0) return QObject::tr("Torus: major radius (point or number)");
    return QObject::tr("Torus: tube radius (point or number)");
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!centre_) {
      centre_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    if (major_ <= 0) return setMajor(nf::norm(in_plane(p, *centre_, plane_)));
    return create(std::abs(nf::norm(in_plane(p, *centre_, plane_)) - major_));
  }
  Result number(double v) override {
    if (!centre_) return Result::Rejected;
    return major_ <= 0 ? setMajor(v) : create(v);
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!centre_) return {};
    const nf::Vec3 n = V(plane_.n);
    const double r = nf::norm(in_plane(c, *centre_, plane_));
    if (major_ <= 0) return {circle(V(*centre_), n, r)};
    const double t = std::abs(r - major_);
    return {circle(V(*centre_), n, major_), circle(V(*centre_), n, major_ + t),
            circle(V(*centre_), n, std::max(major_ - t, 0.0))};
  }
  std::optional<Point3> basePoint() const override { return centre_; }

 private:
  Result setMajor(double r) {
    if (r < kEps) return Result::Rejected;
    major_ = r;
    return Result::Continue;
  }
  Result create(double r) {
    if (r < kEps || r >= major_) return Result::Rejected;
    return add_shell(entities_, message_,
                     [&] { return nf::make_torus(V(*centre_), V(plane_.n), major_, r); })
               ? Result::Done
               : Result::Rejected;
  }
  std::optional<Point3> centre_;
  PlaneFrame plane_;
  double major_ = 0.0;
};

// ------------------------------------------------------------------------------ Booleans
class BooleanCommand : public Command {
 public:
  explicit BooleanCommand(kernel::BooleanOp op) : op_(op) {}
  QString id() const override {
    switch (op_) {
      case kernel::BooleanOp::Union: return QStringLiteral("union");
      case kernel::BooleanOp::Intersection: return QStringLiteral("intersection");
      case kernel::BooleanOp::Difference: return QStringLiteral("difference");
    }
    return {};
  }
  QString title() const override {
    switch (op_) {
      case kernel::BooleanOp::Union: return QObject::tr("Boolean union");
      case kernel::BooleanOp::Intersection: return QObject::tr("Boolean intersection");
      case kernel::BooleanOp::Difference: return QObject::tr("Boolean difference");
    }
    return {};
  }
  QString prompt() const override { return title(); }
  bool usesSelection() const override { return true; }
  bool immediate() const override { return true; }
  bool begin(const std::vector<document::Entity>& selection) override {
    std::vector<nf::Shell> shells;
    for (const document::Entity& e : selection) {
      if (const auto* s = std::get_if<nf::Shell>(e.geometry.get())) {
        shells.push_back(*s);
        remove_.push_back(e.id);
      }
    }
    if (shells.size() < 2) {
      message_ = op_ == kernel::BooleanOp::Difference
                     ? QObject::tr("Select the solid to cut first, then the solids to subtract")
                     : QObject::tr("Select at least two solids");
      remove_.clear();
      return false;
    }
    try {
      document::Entity result = document::make_entity(kernel::boolean(shells, op_));
      result.name = selection.front().name;
      result.layer = selection.front().layer;
      result.color = selection.front().color;
      entities_.push_back(std::move(result));
    } catch (const std::exception& e) {
      message_ = QObject::tr("%1 failed: %2").arg(title(), QString::fromUtf8(e.what()));
      remove_.clear();
      return false;
    }
    return true;
  }
  Result point(const Point3&, const PlaneFrame&) override { return Result::Rejected; }
  Result finish() override { return Result::Done; }

 private:
  kernel::BooleanOp op_;
};

// ------------------------------------------------------------------------------ Angle
class AngleCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("angle"); }
  QString title() const override { return QObject::tr("Angle"); }
  QString prompt() const override {
    switch (pts_.size()) {
      case 0: return QObject::tr("Angle: vertex");
      case 1: return QObject::tr("Angle: point on the first leg");
      default: return QObject::tr("Angle: point on the second leg");
    }
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!pts_.empty() && dist(pts_[0], p) < kEps) return Result::Rejected;
    pts_.push_back(p);
    if (pts_.size() < 3) return Result::Continue;
    const nf::Vec3 a = V(pts_[1]) - V(pts_[0]);
    const nf::Vec3 b = V(pts_[2]) - V(pts_[0]);
    const double deg =
        std::atan2(nf::norm(nf::cross(a, b)), nf::dot(a, b)) * 180.0 / std::numbers::pi;
    message_ = QObject::tr("Angle %1°").arg(deg, 0, 'g', 10);
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (pts_.empty()) return {};
    if (pts_.size() == 1) return {{pts_[0], c}};
    return {{pts_[1], pts_[0], c}};
  }
  std::optional<Point3> basePoint() const override {
    return pts_.empty() ? std::nullopt : std::optional(pts_.front());
  }

 private:
  Polyline3 pts_;
};

}  // namespace

namespace detail {

std::unique_ptr<Command> create_solid_command(const QString& n) {
  if (n == QLatin1String("box") || n == QLatin1String("b")) return std::make_unique<BoxCommand>();
  if (n == QLatin1String("sphere") || n == QLatin1String("sph")) return std::make_unique<SphereCommand>();
  if (n == QLatin1String("cylinder") || n == QLatin1String("cyl")) {
    return std::make_unique<AxialCommand>(false);
  }
  if (n == QLatin1String("cone")) return std::make_unique<AxialCommand>(true);
  if (n == QLatin1String("torus") || n == QLatin1String("tor")) return std::make_unique<TorusCommand>();
  if (n == QLatin1String("union") || n == QLatin1String("bu")) {
    return std::make_unique<BooleanCommand>(kernel::BooleanOp::Union);
  }
  if (n == QLatin1String("difference") || n == QLatin1String("bd")) {
    return std::make_unique<BooleanCommand>(kernel::BooleanOp::Difference);
  }
  if (n == QLatin1String("intersection") || n == QLatin1String("bi")) {
    return std::make_unique<BooleanCommand>(kernel::BooleanOp::Intersection);
  }
  if (n == QLatin1String("angle") || n == QLatin1String("ang")) return std::make_unique<AngleCommand>();
  return nullptr;
}

QStringList solid_command_ids() {
  return {QStringLiteral("box"),          QStringLiteral("sphere"),     QStringLiteral("cylinder"),
          QStringLiteral("cone"),         QStringLiteral("torus"),      QStringLiteral("union"),
          QStringLiteral("difference"),   QStringLiteral("intersection"), QStringLiteral("angle")};
}

}  // namespace detail

}  // namespace nfcad::commands
