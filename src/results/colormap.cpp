#include "results/colormap.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nfcad::results {

namespace {

struct Stop {
  double t;
  unsigned char r, g, b;
};

// Key colours sampled from the matplotlib maps (viridis, inferno, plasma, coolwarm);
// linear interpolation between them is visually indistinguishable at display resolution.
const std::vector<Stop>& stops(Colormap c) {
  static const std::vector<Stop> viridis = {
      {0.0, 68, 1, 84},     {0.125, 71, 44, 122}, {0.25, 59, 81, 139},  {0.375, 44, 113, 142},
      {0.5, 33, 144, 141},  {0.625, 39, 173, 129}, {0.75, 92, 200, 99},  {0.875, 170, 220, 50},
      {1.0, 253, 231, 37}};
  static const std::vector<Stop> inferno = {
      {0.0, 0, 0, 4},       {0.125, 31, 12, 72},  {0.25, 85, 15, 109},  {0.375, 136, 34, 106},
      {0.5, 186, 54, 85},   {0.625, 227, 89, 51}, {0.75, 249, 140, 10}, {0.875, 249, 201, 50},
      {1.0, 252, 255, 164}};
  static const std::vector<Stop> plasma = {
      {0.0, 13, 8, 135},    {0.125, 75, 3, 161},  {0.25, 125, 3, 168},  {0.375, 168, 34, 150},
      {0.5, 203, 70, 121},  {0.625, 229, 107, 93}, {0.75, 248, 148, 65}, {0.875, 253, 195, 40},
      {1.0, 240, 249, 33}};
  static const std::vector<Stop> coolwarm = {
      {0.0, 59, 76, 192},   {0.25, 124, 159, 249}, {0.5, 221, 221, 221}, {0.75, 245, 152, 121},
      {1.0, 180, 4, 38}};
  static const std::vector<Stop> grey = {{0.0, 0, 0, 0}, {1.0, 255, 255, 255}};
  switch (c) {
    case Colormap::Viridis: return viridis;
    case Colormap::Inferno: return inferno;
    case Colormap::Plasma: return plasma;
    case Colormap::Coolwarm: return coolwarm;
    case Colormap::Grey: return grey;
  }
  return viridis;
}

}  // namespace

QStringList colormap_names() {
  return {QStringLiteral("viridis"), QStringLiteral("inferno"), QStringLiteral("plasma"),
          QStringLiteral("coolwarm"), QStringLiteral("grey")};
}

Colormap colormap_from_name(const QString& name) {
  const QString n = name.toLower();
  if (n == QLatin1String("inferno")) return Colormap::Inferno;
  if (n == QLatin1String("plasma")) return Colormap::Plasma;
  if (n == QLatin1String("coolwarm")) return Colormap::Coolwarm;
  if (n == QLatin1String("grey") || n == QLatin1String("gray")) return Colormap::Grey;
  return Colormap::Viridis;
}

QString colormap_name(Colormap c) { return colormap_names()[static_cast<int>(c)]; }

std::array<unsigned char, 4> colormap_rgba(Colormap c, double t) {
  if (std::isnan(t)) return {0, 0, 0, 0};
  t = std::clamp(t, 0.0, 1.0);
  const auto& s = stops(c);
  std::size_t i = 1;
  while (i + 1 < s.size() && s[i].t < t) ++i;
  const Stop& a = s[i - 1];
  const Stop& b = s[i];
  const double f = b.t > a.t ? (t - a.t) / (b.t - a.t) : 0.0;
  auto mix = [f](unsigned char x, unsigned char y) {
    return static_cast<unsigned char>(std::lround(x + f * (y - x)));
  };
  return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255};
}

QColor colormap_color(Colormap c, double t) {
  const auto v = colormap_rgba(c, t);
  return QColor(v[0], v[1], v[2], v[3]);
}

}  // namespace nfcad::results
