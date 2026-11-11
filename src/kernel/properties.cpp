#include "kernel/properties.hpp"

#include <QObject>
#include <exception>
#include <nurbforge/analysis/properties.hpp>
#include <nurbforge/nurbs/knot_vector.hpp>

namespace nfcad::kernel {

namespace {

QString num(double v) { return QString::number(v, 'g', 8); }

QString vec(const nf::Vec3& p) {
  return QStringLiteral("%1, %2, %3").arg(num(p.x), num(p.y), num(p.z));
}

bool rational(const std::vector<double>& w) {
  for (double x : w) {
    if (x != 1.0) return true;
  }
  return false;
}

template <class F>
QString safe(F&& f) {
  try {
    return f();
  } catch (const std::exception& e) {
    return QObject::tr("n/a (%1)").arg(QString::fromUtf8(e.what()));
  }
}

void curve_props(const nf::NurbsCurve& c, PropertyList& out) {
  out.emplace_back(QObject::tr("Degree"), QString::number(c.degree()));
  out.emplace_back(QObject::tr("Control points"), QString::number(c.num_control_points()));
  out.emplace_back(QObject::tr("Rational"), rational(c.weights()) ? QObject::tr("yes") : QObject::tr("no"));
  out.emplace_back(QObject::tr("Knots"), knots_text(c.knots()));
  out.emplace_back(QObject::tr("Domain"),
                   QStringLiteral("[%1, %2]").arg(num(c.domain_start()), num(c.domain_end())));
  const nf::Vec3 a = c.evaluate(c.domain_start());
  const nf::Vec3 b = c.evaluate(c.domain_end());
  out.emplace_back(QObject::tr("Start"), vec(a));
  out.emplace_back(QObject::tr("End"), vec(b));
  out.emplace_back(QObject::tr("Closed"),
                   nf::distance(a, b) < 1e-9 ? QObject::tr("yes") : QObject::tr("no"));
  out.emplace_back(QObject::tr("Length"), safe([&] { return num(nf::arc_length(c)); }));
}

void surface_props(const nf::NurbsSurface& s, PropertyList& out) {
  out.emplace_back(QObject::tr("Degree (u, v)"),
                   QStringLiteral("%1, %2").arg(s.degree_u()).arg(s.degree_v()));
  out.emplace_back(QObject::tr("Control net"), QStringLiteral("%1 × %2").arg(s.num_u()).arg(s.num_v()));
  out.emplace_back(QObject::tr("Rational"), rational(s.weights()) ? QObject::tr("yes") : QObject::tr("no"));
  out.emplace_back(QObject::tr("Knots u"), knots_text(s.knots_u()));
  out.emplace_back(QObject::tr("Knots v"), knots_text(s.knots_v()));
}

void face_props(const nf::TrimmedSurface& f, PropertyList& out) {
  surface_props(f.surface(), out);
  out.emplace_back(QObject::tr("Trimmed"), f.is_untrimmed() ? QObject::tr("no") : QObject::tr("yes"));
  out.emplace_back(QObject::tr("Holes"), QString::number(f.holes().size()));
  if (!f.label().empty()) out.emplace_back(QObject::tr("Label"), QString::fromStdString(f.label()));
  out.emplace_back(QObject::tr("Area"), safe([&] { return num(f.area()); }));
}

}  // namespace

QString knots_text(const std::vector<double>& knots) {
  const auto [values, mult] = nf::unique_knots(knots);
  QStringList parts;
  for (std::size_t i = 0; i < values.size(); ++i) {
    parts << (mult[i] > 1 ? QStringLiteral("%1 (×%2)").arg(num(values[i])).arg(mult[i])
                          : num(values[i]));
  }
  if (parts.size() > 12) {
    const auto n = parts.size();
    parts = parts.mid(0, 5) + QStringList{QStringLiteral("…")} + parts.mid(n - 5);
  }
  return parts.join(QStringLiteral(", "));
}

PropertyList describe(const document::Geometry& g, const document::SubElement& sub) {
  using Kind = document::SubElement::Kind;
  PropertyList out;
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, nf::Vec3>) {
          out.emplace_back(QObject::tr("Type"), QObject::tr("Point"));
          out.emplace_back(QObject::tr("Position"), vec(v));
        } else if constexpr (std::is_same_v<T, nf::NurbsCurve>) {
          out.emplace_back(QObject::tr("Type"), QObject::tr("NURBS curve"));
          curve_props(v, out);
        } else if constexpr (std::is_same_v<T, nf::NurbsSurface>) {
          out.emplace_back(QObject::tr("Type"), QObject::tr("NURBS surface"));
          surface_props(v, out);
          out.emplace_back(QObject::tr("Area"), safe([&] { return num(nf::area(v)); }));
        } else if constexpr (std::is_same_v<T, nf::TrimmedSurface>) {
          out.emplace_back(QObject::tr("Type"), QObject::tr("Trimmed surface"));
          face_props(v, out);
        } else {
          const nf::Shell& sh = v;
          if (sub.kind == Kind::Face && sub.index >= 0 &&
              static_cast<std::size_t>(sub.index) < sh.faces().size()) {
            out.emplace_back(QObject::tr("Type"), QObject::tr("Face %1 of a shell").arg(sub.index));
            face_props(sh.faces()[static_cast<std::size_t>(sub.index)], out);
          } else if (sub.kind == Kind::Edge && sub.index >= 0 &&
                     static_cast<std::size_t>(sub.index) < sh.edges().size()) {
            const nf::ShellEdge& e = sh.edges()[static_cast<std::size_t>(sub.index)];
            out.emplace_back(QObject::tr("Type"), QObject::tr("Edge %1 of a shell").arg(sub.index));
            out.emplace_back(QObject::tr("Used by faces"), QString::number(e.uses.size()));
            curve_props(e.curve, out);
          } else if (sub.kind == Kind::Vertex && sub.index >= 0 &&
                     static_cast<std::size_t>(sub.index) < sh.vertices().size()) {
            out.emplace_back(QObject::tr("Type"), QObject::tr("Vertex %1 of a shell").arg(sub.index));
            out.emplace_back(QObject::tr("Position"),
                             vec(sh.vertices()[static_cast<std::size_t>(sub.index)]));
          } else {
            out.emplace_back(QObject::tr("Type"), QObject::tr("Shell (B-rep)"));
            out.emplace_back(QObject::tr("Faces"), QString::number(sh.faces().size()));
            out.emplace_back(QObject::tr("Edges"), QString::number(sh.edges().size()));
            out.emplace_back(QObject::tr("Vertices"), QString::number(sh.vertices().size()));
            const bool closed = sh.is_closed();
            out.emplace_back(QObject::tr("Closed"), closed ? QObject::tr("yes") : QObject::tr("no"));
            out.emplace_back(QObject::tr("Area"), safe([&] { return num(sh.area()); }));
            if (closed) out.emplace_back(QObject::tr("Volume"), safe([&] { return num(sh.volume()); }));
          }
        }
      },
      g);
  return out;
}

}  // namespace nfcad::kernel
