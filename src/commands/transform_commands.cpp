// Transforms and arrays of the selection (docs/commands.md, M6).
#include <QObject>
#include <cmath>
#include <numbers>
#include <nurbforge/geometry/transform.hpp>
#include <nurbforge/modeling/pattern.hpp>

#include "commands/command_util.hpp"
#include "kernel/geometry_ops.hpp"

namespace nfcad::commands {

namespace {

using namespace util;

constexpr std::size_t kMaxPreviewCopies = 64;

double deg2rad(double d) { return d * std::numbers::pi / 180.0; }

/// Signed angle from a to b about the normal n (radians).
double signed_angle(const nf::Vec3& a, const nf::Vec3& b, const nf::Vec3& n) {
  return std::atan2(nf::dot(nf::cross(a, b), n), nf::dot(a, b));
}

/// Base of all commands that transform or copy the selection.
class SelectionCommand : public Command {
 public:
  bool usesSelection() const override { return true; }
  bool begin(const std::vector<document::Entity>& selection) override {
    if (selection.empty()) {
      message_ = QObject::tr("%1: select objects first").arg(title());
      return false;
    }
    selection_ = selection;
    for (const document::Entity& e : selection_) {
      for (auto& pl : kernel::wire(*e.geometry, 4000 / selection_.size() + 8)) {
        wire_.push_back(std::move(pl));
      }
    }
    return true;
  }

 protected:
  /// Moves the selection (replace) or adds transformed copies.
  void apply(const nf::Transform& t, bool copy) {
    for (const document::Entity& e : selection_) {
      auto g = std::make_shared<const document::Geometry>(kernel::transformed(*e.geometry, t));
      if (copy) {
        document::Entity c = e;
        c.id = 0;
        c.geometry = std::move(g);
        c.revision = 0;
        entities_.push_back(std::move(c));
      } else {
        replace_.emplace_back(e.id, std::move(g));
      }
    }
  }
  /// Preview: the selection's wireframe under the given transforms.
  std::vector<Polyline3> wireUnder(const std::vector<nf::Transform>& ts) const {
    std::vector<Polyline3> out;
    std::size_t copies = 0;
    for (const nf::Transform& t : ts) {
      if (++copies > kMaxPreviewCopies) break;
      for (const auto& pl : wire_) {
        Polyline3 q;
        q.reserve(pl.size());
        for (const nf::Vec3& p : pl) q.push_back(P(t.apply(p)));
        out.push_back(std::move(q));
      }
    }
    return out;
  }

  std::vector<document::Entity> selection_;
  std::vector<std::vector<nf::Vec3>> wire_;
};

// ------------------------------------------------------------------------------ Move / Copy
class MoveCommand : public SelectionCommand {
 public:
  explicit MoveCommand(bool copy) : copy_(copy) {}
  QString id() const override { return copy_ ? QStringLiteral("copy") : QStringLiteral("move"); }
  QString title() const override { return copy_ ? QObject::tr("Copy") : QObject::tr("Move"); }
  QString prompt() const override {
    if (!from_) return QObject::tr("%1: point to move from").arg(title());
    return copy_ ? QObject::tr("Copy: point to copy to (Enter: done)")
                 : QObject::tr("Move: point to move to");
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!from_) {
      from_ = p;
      return Result::Continue;
    }
    apply(nf::Transform::translation(V(p) - V(*from_)), copy_);
    ++placed_;
    return copy_ ? Result::Continue : Result::Done;
  }
  Result finish() override { return placed_ > 0 ? Result::Done : Result::Cancelled; }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!from_) return {};
    auto out = wireUnder({nf::Transform::translation(V(c) - V(*from_))});
    out.push_back({*from_, c});
    return out;
  }
  std::optional<Point3> basePoint() const override { return from_; }

 private:
  bool copy_;
  std::optional<Point3> from_;
  int placed_ = 0;
};

// ------------------------------------------------------------------------------ Rotate
class RotateCommand : public SelectionCommand {
 public:
  QString id() const override { return QStringLiteral("rotate"); }
  QString title() const override { return QObject::tr("Rotate"); }
  QString prompt() const override {
    if (!centre_) return QObject::tr("Rotate: centre (axis: normal of the construction plane)");
    if (!ref_) return QObject::tr("Rotate: angle in degrees or first reference point");
    return QObject::tr("Rotate: second reference point");
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!centre_) {
      centre_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    if (!ref_) {
      if (nf::norm(in_plane(p, *centre_, plane_)) < kEps) return Result::Rejected;
      ref_ = p;
      return Result::Continue;
    }
    return rotate(angleTo(p));
  }
  Result number(double deg) override { return centre_ && !ref_ ? rotate(deg2rad(deg)) : Result::Rejected; }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!centre_) return {};
    if (!ref_) return {{*centre_, c}};
    auto out = wireUnder({transform(angleTo(c))});
    out.push_back({*ref_, *centre_, c});
    return out;
  }
  std::optional<Point3> basePoint() const override { return centre_; }

 private:
  double angleTo(const Point3& p) const {
    return signed_angle(in_plane(*ref_, *centre_, plane_), in_plane(p, *centre_, plane_), V(plane_.n));
  }
  nf::Transform transform(double rad) const {
    return nf::Transform::rotation(V(*centre_), V(plane_.n), rad);
  }
  Result rotate(double rad) {
    apply(transform(rad), false);
    return Result::Done;
  }
  std::optional<Point3> centre_;
  std::optional<Point3> ref_;
  PlaneFrame plane_;
};

// ------------------------------------------------------------------------------ Scale
class ScaleCommand : public SelectionCommand {
 public:
  QString id() const override { return QStringLiteral("scale"); }
  QString title() const override { return QObject::tr("Scale"); }
  QString prompt() const override {
    if (!base_) return QObject::tr("Scale: base point");
    if (!ref_) return QObject::tr("Scale: factor or first reference point");
    return QObject::tr("Scale: second reference point");
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!base_) {
      base_ = p;
      return Result::Continue;
    }
    if (!ref_) {
      if (dist(*base_, p) < kEps) return Result::Rejected;
      ref_ = p;
      return Result::Continue;
    }
    return scale(dist(*base_, p) / dist(*base_, *ref_));
  }
  Result number(double f) override { return base_ && !ref_ ? scale(f) : Result::Rejected; }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!base_) return {};
    if (!ref_) return {{*base_, c}};
    const double f = dist(*base_, c) / dist(*base_, *ref_);
    if (f < kEps) return {};
    auto out = wireUnder({nf::Transform::scaling(V(*base_), f)});
    out.push_back({*base_, c});
    return out;
  }
  std::optional<Point3> basePoint() const override { return base_; }

 private:
  Result scale(double f) {
    if (f <= kEps) return Result::Rejected;
    apply(nf::Transform::scaling(V(*base_), f), false);
    return Result::Done;
  }
  std::optional<Point3> base_;
  std::optional<Point3> ref_;
};

// ------------------------------------------------------------------------------ Mirror
class MirrorCommand : public SelectionCommand {
 public:
  QString id() const override { return QStringLiteral("mirror"); }
  QString title() const override { return QObject::tr("Mirror"); }
  QString prompt() const override {
    return start_ ? QObject::tr("Mirror: end of the mirror line")
                  : QObject::tr("Mirror: start of the mirror line (copies, plane through the line "
                                "and the construction-plane normal)");
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (!start_) {
      start_ = p;
      plane_ = plane;
      return Result::Continue;
    }
    const auto t = transform(p);
    if (!t) return Result::Rejected;
    apply(*t, true);
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (!start_) return {};
    std::vector<Polyline3> out;
    if (const auto t = transform(c)) out = wireUnder({*t});
    out.push_back({*start_, c});
    return out;
  }
  std::optional<Point3> basePoint() const override { return start_; }

 private:
  std::optional<nf::Transform> transform(const Point3& p) const {
    const nf::Vec3 normal = nf::cross(V(p) - V(*start_), V(plane_.n));
    if (nf::norm(normal) < kEps) return std::nullopt;
    return nf::Transform::mirror(V(*start_), normal / nf::norm(normal));
  }
  std::optional<Point3> start_;
  PlaneFrame plane_;
};

// ------------------------------------------------------------------------------ Arrays
class LinearArrayCommand : public SelectionCommand {
 public:
  QString id() const override { return QStringLiteral("array_linear"); }
  QString title() const override { return QObject::tr("Linear array"); }
  QString prompt() const override {
    return base_ ? QObject::tr("Linear array (%1 items): step to the next item").arg(count_)
                 : QObject::tr("Linear array (%1 items): base point (type a number to change the count)")
                       .arg(count_);
  }
  Result number(double v) override {
    if (base_ || v < 2 || std::floor(v) != v) return Result::Rejected;
    count_ = static_cast<std::size_t>(v);
    return Result::Continue;
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!base_) {
      base_ = p;
      return Result::Continue;
    }
    if (dist(*base_, p) < kEps) return Result::Rejected;
    for (const nf::Transform& t : copies(p)) apply(t, true);
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    return base_ ? wireUnder(copies(c)) : std::vector<Polyline3>{};
  }
  std::optional<Point3> basePoint() const override { return base_; }

 private:
  std::vector<nf::Transform> copies(const Point3& p) const {
    auto ts = nf::linear_pattern(V(p) - V(*base_), count_);
    ts.erase(ts.begin());  // the first item is the original
    return ts;
  }
  std::size_t count_ = 3;
  std::optional<Point3> base_;
};

class GridArrayCommand : public SelectionCommand {
 public:
  QString id() const override { return QStringLiteral("array_grid"); }
  QString title() const override { return QObject::tr("Grid array"); }
  QString prompt() const override {
    switch (pts_.size()) {
      case 0:
        return QObject::tr("Grid array (%1 × %2): base point (type two numbers to change the counts)")
            .arg(na_)
            .arg(nb_);
      case 1: return QObject::tr("Grid array (%1 × %2): step in the first direction").arg(na_).arg(nb_);
      default: return QObject::tr("Grid array (%1 × %2): step in the second direction").arg(na_).arg(nb_);
    }
  }
  Result number(double v) override {
    if (!pts_.empty() || v < 1 || std::floor(v) != v) return Result::Rejected;
    (numbers_++ % 2 == 0 ? na_ : nb_) = static_cast<std::size_t>(v);
    return Result::Continue;
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    if (!pts_.empty() && dist(pts_[0], p) < kEps) return Result::Rejected;
    pts_.push_back(p);
    if (pts_.size() < 3) return Result::Continue;
    for (const nf::Transform& t : copies(p)) apply(t, true);
    return Result::Done;
  }
  std::vector<Polyline3> preview(const Point3& c, const PlaneFrame&) const override {
    if (pts_.empty()) return {};
    if (pts_.size() == 1) {
      auto ts = nf::linear_pattern(V(c) - V(pts_[0]), na_);
      ts.erase(ts.begin());
      return wireUnder(ts);
    }
    return wireUnder(copies(c));
  }
  std::optional<Point3> basePoint() const override {
    return pts_.empty() ? std::nullopt : std::optional(pts_.front());
  }

 private:
  std::vector<nf::Transform> copies(const Point3& b) const {
    auto ts = nf::grid_pattern(V(pts_[1]) - V(pts_[0]), na_, V(b) - V(pts_[0]), nb_);
    ts.erase(ts.begin());
    return ts;
  }
  std::size_t na_ = 3;
  std::size_t nb_ = 3;
  int numbers_ = 0;
  Polyline3 pts_;
};

class PolarArrayCommand : public SelectionCommand {
 public:
  QString id() const override { return QStringLiteral("array_polar"); }
  QString title() const override { return QObject::tr("Polar array"); }
  QString prompt() const override {
    return centre_ ? QObject::tr("Polar array (%1 items): total angle in degrees (Enter: 360)").arg(count_)
                   : QObject::tr("Polar array (%1 items): centre (type a number to change the count; "
                                 "axis: construction-plane normal)")
                         .arg(count_);
  }
  Result number(double v) override {
    if (!centre_) {
      if (v < 2 || std::floor(v) != v) return Result::Rejected;
      count_ = static_cast<std::size_t>(v);
      return Result::Continue;
    }
    if (v == 0.0 || std::abs(v) > 360.0) return Result::Rejected;
    return create(deg2rad(v));
  }
  Result point(const Point3& p, const PlaneFrame& plane) override {
    if (centre_) return Result::Rejected;
    centre_ = p;
    plane_ = plane;
    return Result::Continue;
  }
  Result finish() override { return centre_ ? create(2.0 * std::numbers::pi) : Result::Cancelled; }
  std::vector<Polyline3> preview(const Point3&, const PlaneFrame&) const override {
    return centre_ ? wireUnder(copies(2.0 * std::numbers::pi)) : std::vector<Polyline3>{};
  }
  std::optional<Point3> basePoint() const override { return centre_; }

 private:
  std::vector<nf::Transform> copies(double total) const {
    auto ts = nf::polar_pattern(V(*centre_), V(plane_.n), count_, total);
    ts.erase(ts.begin());
    return ts;
  }
  Result create(double total) {
    for (const nf::Transform& t : copies(total)) apply(t, true);
    return Result::Done;
  }
  std::size_t count_ = 6;
  std::optional<Point3> centre_;
  PlaneFrame plane_;
};

}  // namespace

namespace detail {

std::unique_ptr<Command> create_transform_command(const QString& n) {
  if (n == QLatin1String("move") || n == QLatin1String("m")) return std::make_unique<MoveCommand>(false);
  if (n == QLatin1String("copy") || n == QLatin1String("co") || n == QLatin1String("cp")) {
    return std::make_unique<MoveCommand>(true);
  }
  if (n == QLatin1String("rotate") || n == QLatin1String("ro")) return std::make_unique<RotateCommand>();
  if (n == QLatin1String("scale") || n == QLatin1String("sc")) return std::make_unique<ScaleCommand>();
  if (n == QLatin1String("mirror") || n == QLatin1String("mi")) return std::make_unique<MirrorCommand>();
  if (n == QLatin1String("array_linear") || n == QLatin1String("array")) {
    return std::make_unique<LinearArrayCommand>();
  }
  if (n == QLatin1String("array_grid")) return std::make_unique<GridArrayCommand>();
  if (n == QLatin1String("array_polar") || n == QLatin1String("arraypolar")) {
    return std::make_unique<PolarArrayCommand>();
  }
  return nullptr;
}

QStringList transform_command_ids() {
  return {QStringLiteral("move"),         QStringLiteral("copy"),       QStringLiteral("rotate"),
          QStringLiteral("scale"),        QStringLiteral("mirror"),     QStringLiteral("array_linear"),
          QStringLiteral("array_grid"),   QStringLiteral("array_polar")};
}

}  // namespace detail

}  // namespace nfcad::commands
