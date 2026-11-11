#pragma once

#include <QString>
#include <utility>
#include <vector>

#include "document/entity.hpp"
#include "document/selection.hpp"

namespace nfcad::kernel {

using PropertyList = std::vector<std::pair<QString, QString>>;

/// Human-readable properties of a geometry or one of its sub-elements for the property panel:
/// type, degrees, control points, knots, closedness, length / area / volume (computed with
/// Nurbforge). Thread-safe.
[[nodiscard]] PropertyList describe(const document::Geometry& g,
                                    const document::SubElement& sub = {});

/// "0 0 0 0.5 1 1 1" -> "0 (×3), 0.5, 1 (×3)"
[[nodiscard]] QString knots_text(const std::vector<double>& knots);

}  // namespace nfcad::kernel
