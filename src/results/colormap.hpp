#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <array>

namespace nfcad::results {

/// Colour maps for result values (perceptually uniform ones first). Values are mapped
/// linearly from [min, max] to [0, 1]; NaN gives a transparent colour.
enum class Colormap { Viridis, Inferno, Plasma, Coolwarm, Grey };

[[nodiscard]] QStringList colormap_names();
[[nodiscard]] Colormap colormap_from_name(const QString& name);  ///< unknown: Viridis
[[nodiscard]] QString colormap_name(Colormap c);

/// RGBA (0..255) of t in [0, 1] (clamped).
[[nodiscard]] std::array<unsigned char, 4> colormap_rgba(Colormap c, double t);
[[nodiscard]] QColor colormap_color(Colormap c, double t);

}  // namespace nfcad::results
