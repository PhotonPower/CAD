#include "kernel/geometry_ops.hpp"

#include <QObject>
#include <exception>
#include <nurbforge/io/step.hpp>
#include <nurbforge/topology/boolean.hpp>
#include <stdexcept>

namespace nfcad::kernel {

namespace {
nf::TrimmedSurface transform_face(const nf::TrimmedSurface& f, const nf::Transform& t) {
  nf::NurbsSurface s = f.surface().transformed(t);
  if (f.is_untrimmed()) return nf::TrimmedSurface(std::move(s)).with_label(f.label());
  return nf::TrimmedSurface(std::move(s), f.outer(), f.holes()).with_label(f.label());
}
}  // namespace

document::Geometry transformed(const document::Geometry& g, const nf::Transform& t) {
  return std::visit(
      [&](const auto& v) -> document::Geometry {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, nf::Vec3>) {
          return t.apply(v);
        } else if constexpr (std::is_same_v<T, nf::TrimmedSurface>) {
          return transform_face(v, t);
        } else {
          return v.transformed(t);
        }
      },
      g);
}

std::vector<std::vector<nf::Vec3>> wire(const document::Geometry& g, std::size_t max_points) {
  std::vector<std::vector<nf::Vec3>> out;
  std::vector<const nf::NurbsCurve*> curves;
  std::vector<nf::NurbsCurve> owned;
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, nf::Vec3>) {
          const double h = 0.5;
          out.push_back({v - nf::Vec3{h, 0, 0}, v + nf::Vec3{h, 0, 0}});
          out.push_back({v - nf::Vec3{0, h, 0}, v + nf::Vec3{0, h, 0}});
        } else if constexpr (std::is_same_v<T, nf::NurbsCurve>) {
          curves.push_back(&v);
        } else if constexpr (std::is_same_v<T, nf::NurbsSurface>) {
          owned.push_back(v.iso_curve_u(v.domain_u()[0]));
          owned.push_back(v.iso_curve_u(v.domain_u()[1]));
          owned.push_back(v.iso_curve_v(v.domain_v()[0]));
          owned.push_back(v.iso_curve_v(v.domain_v()[1]));
        } else if constexpr (std::is_same_v<T, nf::TrimmedSurface>) {
          try {
            for (auto& loop : v.boundary_curves(1e-4)) {
              for (auto& c : loop) owned.push_back(std::move(c));
            }
          } catch (const std::exception&) {
          }
        } else {
          for (const nf::ShellEdge& e : v.edges()) curves.push_back(&e.curve);
        }
      },
      g);
  for (const auto& c : owned) curves.push_back(&c);
  if (curves.empty()) return out;
  const std::size_t per = std::max<std::size_t>(2, max_points / curves.size());
  for (const nf::NurbsCurve* c : curves) {
    const std::size_t n = c->degree() == 1 ? c->num_control_points() : std::min<std::size_t>(per, 64);
    out.push_back(c->degree() == 1 ? c->control_points() : c->sample(n));
  }
  return out;
}

nf::Shell boolean(const std::vector<nf::Shell>& shells, BooleanOp op) {
  if (shells.size() < 2) throw std::runtime_error(QObject::tr("select at least two solids").toStdString());
  for (const nf::Shell& s : shells) {
    if (!s.is_closed()) {
      throw std::runtime_error(QObject::tr("Booleans need closed solids").toStdString());
    }
  }
  const nf::BooleanOp nop = op == BooleanOp::Union          ? nf::BooleanOp::Union
                            : op == BooleanOp::Intersection ? nf::BooleanOp::Intersection
                                                            : nf::BooleanOp::Difference;
  nf::Shell result = shells.front();
  for (std::size_t i = 1; i < shells.size(); ++i) result = nf::boolean(result, shells[i], nop);
  if (result.faces().empty()) {
    throw std::runtime_error(QObject::tr("the result is empty").toStdString());
  }
  return result;
}

std::size_t export_step(const QString& path, const std::vector<const document::Entity*>& entities) {
  std::vector<nf::Shell> shells;
  nf::StepOptions options;
  options.name = "Nurbforge CAD";
  for (const document::Entity* e : entities) {
    if (const auto* s = std::get_if<nf::Shell>(e->geometry.get())) {
      shells.push_back(*s);
      options.shell_names.push_back(e->name.isEmpty() ? "shell_" + std::to_string(e->id)
                                                      : e->name.toStdString());
    } else if (const auto* f = std::get_if<nf::TrimmedSurface>(e->geometry.get())) {
      shells.push_back(nf::Shell::sew({*f}));
      options.shell_names.push_back(e->name.toStdString());
    } else if (const auto* sf = std::get_if<nf::NurbsSurface>(e->geometry.get())) {
      shells.push_back(nf::Shell::sew({nf::TrimmedSurface(*sf)}));
      options.shell_names.push_back(e->name.toStdString());
    }
  }
  if (!shells.empty()) nf::write_step(path.toStdString(), shells, options);
  return shells.size();
}

}  // namespace nfcad::kernel
