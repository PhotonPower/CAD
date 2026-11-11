// Editing and analysis on the selection (docs/commands.md, M6c): Join, Explode, Split, Trim,
// Cap, Curvature, Area, Mass properties, Intersect.
#include <QObject>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <nurbforge/analysis/closest_point.hpp>
#include <nurbforge/analysis/intersect.hpp>
#include <nurbforge/analysis/properties.hpp>
#include <nurbforge/analysis/surface_intersect.hpp>
#include <nurbforge/topology/trimmed_surface.hpp>

#include "commands/command_util.hpp"
#include "kernel/curve_features.hpp"
#include "kernel/geometry_ops.hpp"

namespace nfcad::commands {

namespace {

using namespace util;

const nf::NurbsCurve* curve_of(const document::Entity& e) {
  return std::get_if<nf::NurbsCurve>(e.geometry.get());
}

document::GeometryPtr G(document::Geometry g) {
  return std::make_shared<const document::Geometry>(std::move(g));
}

/// Copy of `like` with new geometry (name, layer, colour kept).
document::Entity derived(const document::Entity& like, document::Geometry g) {
  document::Entity e = like;
  e.id = 0;
  e.revision = 0;
  e.geometry = G(std::move(g));
  return e;
}

QString num(double v) { return QString::number(v, 'g', 8); }

/// Selection command that does all its work in begin() (no point input).
class ImmediateCommand : public Command {
 public:
  using Run = std::function<void(ImmediateCommand&, const std::vector<document::Entity>&)>;
  ImmediateCommand(QString id, QString title, Run run)
      : id_(std::move(id)), title_(std::move(title)), run_(std::move(run)) {}
  QString id() const override { return id_; }
  QString title() const override { return title_; }
  QString prompt() const override { return title_; }
  bool usesSelection() const override { return true; }
  bool immediate() const override { return true; }
  bool begin(const std::vector<document::Entity>& selection) override {
    if (selection.empty()) {
      message_ = QObject::tr("%1: select objects first").arg(title_);
      return false;
    }
    try {
      run_(*this, selection);
    } catch (const std::exception& e) {
      message_ = QObject::tr("%1 failed: %2").arg(title_, QString::fromUtf8(e.what()));
      entities_.clear();
      remove_.clear();
      replace_.clear();
      return false;
    }
    return true;
  }
  Result point(const Point3&, const PlaneFrame&) override { return Result::Rejected; }
  Result finish() override { return Result::Done; }

  void add(document::Entity e) { entities_.push_back(std::move(e)); }
  void remove(document::EntityId id) { remove_.push_back(id); }
  void replace(document::EntityId id, document::Geometry g) { replace_.emplace_back(id, G(std::move(g))); }
  void say(const QString& m) { message_ = m; }

 private:
  QString id_, title_;
  Run run_;
};

// ------------------------------------------------------------------------------ Join
void run_join(ImmediateCommand& c, const std::vector<document::Entity>& sel) {
  std::vector<nf::NurbsCurve> curves;
  std::vector<nf::TrimmedSurface> faces;
  for (const document::Entity& e : sel) {
    if (const nf::NurbsCurve* cv = curve_of(e)) {
      curves.push_back(*cv);
    } else {
      for (auto& f : kernel::faces_of(*e.geometry)) faces.push_back(std::move(f));
    }
  }
  if (!curves.empty() && !faces.empty()) {
    throw std::runtime_error(QObject::tr("select either curves or surfaces").toStdString());
  }
  if (sel.size() < 2) throw std::runtime_error(QObject::tr("select at least two objects").toStdString());
  if (curves.empty()) {
    nf::Shell shell = nf::Shell::sew(faces, 1e-6).oriented();
    c.say(QObject::tr("Joined %1 faces, %2")
              .arg(shell.faces().size())
              .arg(shell.is_closed() ? QObject::tr("closed solid") : QObject::tr("open")));
    c.replace(sel.front().id, std::move(shell));
  } else {
    c.replace(sel.front().id, kernel::join_chain(curves));
  }
  for (std::size_t i = 1; i < sel.size(); ++i) c.remove(sel[i].id);
}

// ------------------------------------------------------------------------------ Explode
void run_explode(ImmediateCommand& c, const std::vector<document::Entity>& sel) {
  std::size_t pieces = 0;
  for (const document::Entity& e : sel) {
    if (const auto* sh = std::get_if<nf::Shell>(e.geometry.get())) {
      for (const nf::TrimmedSurface& f : sh->faces()) {
        c.add(derived(e, f));
        ++pieces;
      }
      c.remove(e.id);
    } else if (const nf::NurbsCurve* cv = curve_of(e)) {
      // split at kinks (interior knots of full multiplicity)
      const auto [values, mult] = nf::unique_knots(cv->knots());
      std::vector<double> cuts;
      for (std::size_t i = 1; i + 1 < values.size(); ++i) {
        if (static_cast<std::size_t>(mult[i]) >= cv->degree()) cuts.push_back(values[i]);
      }
      if (cuts.empty()) continue;
      double a = cv->domain_start();
      for (double t : cuts) {
        c.add(derived(e, cv->sub_curve(a, t)));
        a = t;
        ++pieces;
      }
      c.add(derived(e, cv->sub_curve(a, cv->domain_end())));
      ++pieces;
      c.remove(e.id);
    }
  }
  if (pieces == 0) throw std::runtime_error(QObject::tr("nothing to explode").toStdString());
  c.say(QObject::tr("%n piece(s)", nullptr, static_cast<int>(pieces)));
}

// ------------------------------------------------------------------------------ Cap
void run_cap(ImmediateCommand& c, const std::vector<document::Entity>& sel) {
  std::vector<nf::TrimmedSurface> faces;
  for (const document::Entity& e : sel) {
    for (auto& f : kernel::faces_of(*e.geometry)) faces.push_back(std::move(f));
  }
  if (faces.empty()) throw std::runtime_error(QObject::tr("select surfaces").toStdString());
  const nf::Shell shell = kernel::cap_planar_holes(faces);
  c.replace(sel.front().id, shell);
  for (std::size_t i = 1; i < sel.size(); ++i) c.remove(sel[i].id);
  c.say(shell.is_closed() ? QObject::tr("Capped: closed solid, volume %1").arg(num(std::abs(shell.volume())))
                          : QObject::tr("Capped (still open)"));
}

// ------------------------------------------------------------------------------ Area / mass
void run_area(ImmediateCommand& c, const std::vector<document::Entity>& sel) {
  double area = 0.0;
  double length = 0.0;
  for (const document::Entity& e : sel) {
    if (const nf::NurbsCurve* cv = curve_of(e)) {
      length += nf::arc_length(*cv);
    } else {
      for (const nf::TrimmedSurface& f : kernel::faces_of(*e.geometry)) area += f.area();
    }
  }
  QStringList parts;
  if (area > 0) parts << QObject::tr("Area %1").arg(num(area));
  if (length > 0) parts << QObject::tr("Length %1").arg(num(length));
  if (parts.isEmpty()) throw std::runtime_error(QObject::tr("no curves or surfaces selected").toStdString());
  c.say(parts.join(QStringLiteral("   ")));
}

void run_mass(ImmediateCommand& c, const std::vector<document::Entity>& sel) {
  double volume = 0.0;
  nf::Vec3 moment{0, 0, 0};
  for (const document::Entity& e : sel) {
    const auto* sh = std::get_if<nf::Shell>(e.geometry.get());
    if (sh == nullptr || !sh->is_closed()) continue;
    const nf::MassProperties m = sh->mass_properties();
    volume += m.volume;
    moment += m.centroid * m.volume;
  }
  if (volume == 0.0) throw std::runtime_error(QObject::tr("select closed solids").toStdString());
  const nf::Vec3 g = moment / volume;
  c.say(QObject::tr("Volume %1   centroid (%2, %3, %4)")
            .arg(num(volume), num(g.x), num(g.y), num(g.z)));
}

// ------------------------------------------------------------------------------ Intersect
void run_intersect(ImmediateCommand& c, const std::vector<document::Entity>& sel) {
  std::vector<std::pair<const document::Entity*, nf::NurbsCurve>> curves;
  std::vector<std::pair<const document::Entity*, nf::NurbsSurface>> surfaces;
  for (const document::Entity& e : sel) {
    if (const nf::NurbsCurve* cv = curve_of(e)) {
      curves.emplace_back(&e, *cv);
    } else {
      for (const nf::TrimmedSurface& f : kernel::faces_of(*e.geometry)) surfaces.emplace_back(&e, f.surface());
    }
  }
  if (curves.size() + surfaces.size() < 2) {
    throw std::runtime_error(QObject::tr("select at least two objects").toStdString());
  }
  std::size_t points = 0;
  std::size_t lines = 0;
  for (std::size_t i = 0; i < curves.size(); ++i) {
    for (std::size_t j = i + 1; j < curves.size(); ++j) {
      for (const nf::Vec3& p : kernel::curve_intersections(curves[i].second, curves[j].second)) {
        c.add(document::make_entity(p));
        ++points;
      }
    }
    for (const auto& [e, s] : surfaces) {
      for (const auto& x : nf::intersect(curves[i].second, s)) {
        c.add(document::make_entity(x.point));
        ++points;
      }
    }
  }
  for (std::size_t i = 0; i < surfaces.size(); ++i) {
    for (std::size_t j = i + 1; j < surfaces.size(); ++j) {
      if (surfaces[i].first == surfaces[j].first) continue;  // faces of the same object
      const auto x = nf::intersect(surfaces[i].second, surfaces[j].second);
      for (const auto& cv : x.curves) {
        c.add(document::make_entity(cv.curve));
        ++lines;
      }
      for (const auto& p : x.points) {
        c.add(document::make_entity(p.point));
        ++points;
      }
    }
  }
  c.say(QObject::tr("Intersections: %1 curve(s), %2 point(s)").arg(lines).arg(points));
}

// ------------------------------------------------------------------------------ Split / Trim
/// Split: one curve at a clicked point; or the first surface by the second (immediately).
/// Trim: the first selected curve / surface is cut by the others, the clicked part is removed.
class SplitTrimCommand : public Command {
 public:
  explicit SplitTrimCommand(bool trim) : trim_(trim) {}
  QString id() const override { return trim_ ? QStringLiteral("trim") : QStringLiteral("split"); }
  QString title() const override { return trim_ ? QObject::tr("Trim") : QObject::tr("Split"); }
  QString prompt() const override {
    if (trim_) return QObject::tr("Trim: click the part of the first selected object to remove");
    return QObject::tr("Split: click the point on the curve");
  }
  bool usesSelection() const override { return true; }
  bool immediate() const override { return immediate_; }
  bool begin(const std::vector<document::Entity>& sel) override {
    if (sel.empty()) {
      message_ = QObject::tr("%1: select objects first").arg(title());
      return false;
    }
    target_ = sel.front();
    for (std::size_t i = 1; i < sel.size(); ++i) cutters_.push_back(sel[i]);
    const bool curve = curve_of(target_) != nullptr;
    if (!trim_ && curve && cutters_.empty()) return true;  // split a curve at a point
    if (!curve && kernel::faces_of(*target_.geometry).size() == 1 && !cutters_.empty() &&
        !kernel::faces_of(*cutters_.front().geometry).empty()) {
      if (trim_) return true;  // trim a surface by a surface: wait for the click
      return attempt([&] {
        const auto parts = splitSurface();
        for (const auto& p : parts) entities_.push_back(derived(target_, p));
        remove_.push_back(target_.id);
        immediate_ = true;
      });
    }
    if (trim_ && curve && !cutters_.empty()) return true;
    message_ = trim_ ? QObject::tr("Trim: select the object to trim first, then the cutting objects")
                     : QObject::tr("Split: select one curve, or a surface and then a cutting surface");
    return false;
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    const bool ok = attempt([&] {
      if (const nf::NurbsCurve* cv = curve_of(target_)) {
        const double t = nf::closest_point(*cv, V(p)).u;
        if (!trim_) {
          if (t <= cv->domain_start() + 1e-9 || t >= cv->domain_end() - 1e-9) {
            throw std::runtime_error(QObject::tr("the point is at an end of the curve").toStdString());
          }
          const auto [a, b] = cv->split(t);
          entities_.push_back(derived(target_, a));
          entities_.push_back(derived(target_, b));
          remove_.push_back(target_.id);
          return;
        }
        trimCurve(*cv, t);
        return;
      }
      trimSurface(V(p));
    });
    return ok ? Result::Done : Result::Rejected;
  }
  Result finish() override { return immediate_ ? Result::Done : Result::Cancelled; }

 private:
  bool attempt(const std::function<void()>& f) {
    try {
      f();
      return true;
    } catch (const std::exception& e) {
      message_ = QObject::tr("%1 failed: %2").arg(title(), QString::fromUtf8(e.what()));
      entities_.clear();
      remove_.clear();
      return false;
    }
  }
  std::vector<nf::TrimmedSurface> splitSurface() const {
    const nf::TrimmedSurface face = kernel::faces_of(*target_.geometry).front();
    const nf::NurbsSurface cutter = kernel::faces_of(*cutters_.front().geometry).front().surface();
    auto parts = nf::split(face.surface(), cutter);
    if (parts.size() < 2) throw std::runtime_error(QObject::tr("the surfaces do not intersect").toStdString());
    return parts;
  }
  void trimCurve(const nf::NurbsCurve& cv, double clicked) {
    std::vector<double> ts = {cv.domain_start(), cv.domain_end()};
    for (const document::Entity& cut : cutters_) {
      if (const nf::NurbsCurve* other = curve_of(cut)) {
        for (const auto& x : nf::intersect(cv, *other)) ts.push_back(x.s);
      } else {
        for (const nf::TrimmedSurface& f : kernel::faces_of(*cut.geometry)) {
          for (const auto& x : nf::intersect(cv, f.surface())) ts.push_back(x.t);
        }
      }
    }
    std::sort(ts.begin(), ts.end());
    ts.erase(std::unique(ts.begin(), ts.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }),
             ts.end());
    if (ts.size() < 3) throw std::runtime_error(QObject::tr("the cutting objects do not cross the curve").toStdString());
    for (std::size_t i = 0; i + 1 < ts.size(); ++i) {
      const bool removed = clicked >= ts[i] && clicked <= ts[i + 1];
      if (!removed) entities_.push_back(derived(target_, cv.sub_curve(ts[i], ts[i + 1])));
    }
    remove_.push_back(target_.id);
  }
  void trimSurface(const nf::Vec3& p) {
    const auto parts = splitSurface();
    const nf::SurfacePoint sp = nf::closest_point(parts.front().surface(), p);
    bool dropped = false;
    for (const nf::TrimmedSurface& part : parts) {
      if (!dropped && part.contains(sp.u, sp.v)) {
        dropped = true;
        continue;
      }
      entities_.push_back(derived(target_, part));
    }
    if (!dropped) throw std::runtime_error(QObject::tr("click on the part to remove").toStdString());
    remove_.push_back(target_.id);
  }

  bool trim_;
  bool immediate_ = false;
  document::Entity target_;
  std::vector<document::Entity> cutters_;
};

// ------------------------------------------------------------------------------ Curvature
class CurvatureCommand : public Command {
 public:
  QString id() const override { return QStringLiteral("curvature"); }
  QString title() const override { return QObject::tr("Curvature"); }
  QString prompt() const override { return QObject::tr("Curvature: point on the selected curve or surface"); }
  bool usesSelection() const override { return true; }
  bool begin(const std::vector<document::Entity>& sel) override {
    if (sel.size() != 1) {
      message_ = QObject::tr("Curvature: select one curve or surface");
      return false;
    }
    target_ = sel.front();
    if (curve_of(target_) == nullptr && kernel::faces_of(*target_.geometry).empty()) {
      message_ = QObject::tr("Curvature: select one curve or surface");
      return false;
    }
    return true;
  }
  Result point(const Point3& p, const PlaneFrame&) override {
    try {
      if (const nf::NurbsCurve* cv = curve_of(target_)) {
        const double k = nf::curvature(*cv, nf::closest_point(*cv, V(p)).u);
        message_ = k > 1e-14 ? QObject::tr("Curvature %1   radius %2").arg(num(k), num(1.0 / k))
                             : QObject::tr("Curvature 0 (straight)");
      } else {
        // the face whose surface is closest to the point
        double best = 1e300;
        nf::SurfaceCurvature sc;
        for (const nf::TrimmedSurface& f : kernel::faces_of(*target_.geometry)) {
          const nf::SurfacePoint sp = nf::closest_point(f.surface(), V(p));
          if (sp.distance < best) {
            best = sp.distance;
            sc = nf::curvature(f.surface(), sp.u, sp.v);
          }
        }
        message_ = QObject::tr("k1 %1   k2 %2   Gaussian %3   mean %4")
                       .arg(num(sc.k1), num(sc.k2), num(sc.gaussian), num(sc.mean));
      }
    } catch (const std::exception& e) {
      message_ = QObject::tr("Curvature: %1").arg(QString::fromUtf8(e.what()));
      return Result::Rejected;
    }
    return Result::Done;
  }

 private:
  document::Entity target_;
};

}  // namespace

namespace detail {

std::unique_ptr<Command> create_edit_command(const QString& n) {
  if (n == QLatin1String("join") || n == QLatin1String("j")) {
    return std::make_unique<ImmediateCommand>(QStringLiteral("join"), QObject::tr("Join"), run_join);
  }
  if (n == QLatin1String("explode") || n == QLatin1String("x")) {
    return std::make_unique<ImmediateCommand>(QStringLiteral("explode"), QObject::tr("Explode"), run_explode);
  }
  if (n == QLatin1String("cap")) {
    return std::make_unique<ImmediateCommand>(QStringLiteral("cap"), QObject::tr("Cap planar holes"), run_cap);
  }
  if (n == QLatin1String("area")) {
    return std::make_unique<ImmediateCommand>(QStringLiteral("area"), QObject::tr("Area / length"), run_area);
  }
  if (n == QLatin1String("mass_properties") || n == QLatin1String("volume")) {
    return std::make_unique<ImmediateCommand>(QStringLiteral("mass_properties"),
                                              QObject::tr("Volume / mass properties"), run_mass);
  }
  if (n == QLatin1String("intersection_check") || n == QLatin1String("intersect")) {
    return std::make_unique<ImmediateCommand>(QStringLiteral("intersection_check"),
                                              QObject::tr("Intersect"), run_intersect);
  }
  if (n == QLatin1String("split")) return std::make_unique<SplitTrimCommand>(false);
  if (n == QLatin1String("trim") || n == QLatin1String("tr")) return std::make_unique<SplitTrimCommand>(true);
  if (n == QLatin1String("curvature") || n == QLatin1String("curv")) return std::make_unique<CurvatureCommand>();
  return nullptr;
}

QStringList edit_command_ids() {
  return {QStringLiteral("join"),  QStringLiteral("explode"),        QStringLiteral("cap"),
          QStringLiteral("area"),  QStringLiteral("mass_properties"), QStringLiteral("intersection_check"),
          QStringLiteral("split"), QStringLiteral("trim"),           QStringLiteral("curvature")};
}

}  // namespace detail

}  // namespace nfcad::commands
