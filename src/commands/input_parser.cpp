#include "commands/input_parser.hpp"

#include <QLocale>
#include <QObject>
#include <QStringList>
#include <cmath>
#include <numbers>

namespace nfcad::commands {

namespace {

std::optional<double> to_number(const QString& s) {
  bool ok = false;
  const double v = QLocale::c().toDouble(s.trimmed(), &ok);
  if (!ok || !std::isfinite(v)) return std::nullopt;
  return v;
}

Point3 add(const Point3& a, const Point3& b, double s = 1.0) {
  return {a.x + s * b.x, a.y + s * b.y, a.z + s * b.z};
}

ParsedInput invalid(const QString& msg) {
  ParsedInput r;
  r.kind = ParsedInput::Kind::Invalid;
  r.error = msg;
  return r;
}

}  // namespace

ParsedInput parse_input(const QString& raw, const PlaneFrame& plane,
                        const std::optional<Point3>& base) {
  ParsedInput r;
  QString text = raw.trimmed();
  text.remove(QLatin1Char(' '));
  if (text.isEmpty()) return r;

  if (const auto n = to_number(text)) {
    r.kind = ParsedInput::Kind::Number;
    r.number = *n;
    return r;
  }
  if (text.front().isLetter()) {
    r.kind = ParsedInput::Kind::Name;
    r.name = text.toLower();
    return r;
  }

  const bool relative = text.startsWith(QLatin1Char('@'));
  if (relative) {
    text.remove(0, 1);
    if (!base) return invalid(QObject::tr("relative input needs a previous point"));
  }
  const Point3 ref = relative ? *base : plane.origin;

  if (text.contains(QLatin1Char('<'))) {
    const QStringList parts = text.split(QLatin1Char('<'));
    if (parts.size() != 2) return invalid(QObject::tr("polar input: distance<angle"));
    const auto d = to_number(parts[0]);
    const auto a = to_number(parts[1]);
    if (!d || !a) return invalid(QObject::tr("polar input: distance<angle"));
    const double rad = *a * std::numbers::pi / 180.0;
    r.kind = ParsedInput::Kind::Point;
    r.point = add(add(ref, plane.u, *d * std::cos(rad)), plane.v, *d * std::sin(rad));
    return r;
  }

  const QStringList parts = text.split(QLatin1Char(','));
  if (parts.size() != 2 && parts.size() != 3) {
    return invalid(QObject::tr("expected x,y or x,y,z"));
  }
  double c[3] = {0, 0, 0};
  for (qsizetype i = 0; i < parts.size(); ++i) {
    const auto v = to_number(parts[i]);
    if (!v) return invalid(QObject::tr("'%1' is not a number").arg(parts[i]));
    c[i] = *v;
  }
  r.kind = ParsedInput::Kind::Point;
  if (parts.size() == 3) {  // world coordinates / world offset
    r.point = relative ? Point3{ref.x + c[0], ref.y + c[1], ref.z + c[2]} : Point3{c[0], c[1], c[2]};
  } else {  // construction-plane coordinates
    r.point = add(add(ref, plane.u, c[0]), plane.v, c[1]);
  }
  return r;
}

}  // namespace nfcad::commands
