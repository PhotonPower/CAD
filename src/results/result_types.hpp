#pragma once

#include <QString>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "document/entity.hpp"
#include "results/colormap.hpp"

namespace nfcad::results {

using ResultId = std::uint64_t;

/// Value range of a colour-mapped quantity (empty: min / max of the data).
struct Range {
  double min = 0.0;
  double max = 1.0;
};

/// Many polylines (rays, field lines, paths) with optional per-point values – e.g. a
/// Raytatouille RayPaths: one polyline per ray, the values its wavelength or intensity.
/// Display only (docs/plugins.md, D6); selected polylines can be promoted to CAD curves.
struct LineSet {
  std::vector<float> points;          ///< x, y, z per point
  std::vector<std::uint32_t> starts;  ///< first point of each polyline; size = polylines + 1
  std::vector<float> values;          ///< optional: one value per point (colour map)
  Colormap colormap = Colormap::Viridis;
  std::optional<Range> range;         ///< empty: data range
  std::array<float, 4> color{1.0F, 0.85F, 0.3F, 0.9F};  ///< used without values
  QString unit;
  bool on_top = true;  ///< drawn without depth test (rays inside lenses stay visible)

  [[nodiscard]] std::size_t polylineCount() const { return starts.empty() ? 0 : starts.size() - 1; }
  [[nodiscard]] std::size_t pointCount() const { return points.size() / 3; }
};

/// A scalar field or an image on one face of an entity, in the face's normalised surface
/// parameters (u, v) ∈ [0, 1]² – e.g. the irradiance on a detector (docs/plugins.md, D7).
struct FaceField {
  document::EntityId entity = 0;
  int face = 0;          ///< face index (shells); 0 for surfaces / trimmed surfaces
  int width = 0;         ///< samples along u
  int height = 0;        ///< samples along v
  std::vector<float> values;           ///< scalar grid, row-major: values[j * width + i] at (u_i, v_j)
  std::vector<unsigned char> rgba;     ///< or an image, 4 bytes per sample (used if values is empty)
  Colormap colormap = Colormap::Inferno;
  std::optional<Range> range;
  QString unit;

  /// Colour-mapped texture (width × height × 4), rows in v.
  [[nodiscard]] std::vector<unsigned char> texture() const;
  [[nodiscard]] Range effectiveRange() const;
};

/// A 2D image with a physical extent for the image dock – e.g. a camera image (irradiance on a
/// sensor) computed with many rays. Row-major, values[j * width + i]; row 0 at y0 (bottom).
struct Image {
  int width = 0;
  int height = 0;
  std::vector<float> values;
  double x0 = 0.0, x1 = 1.0, y0 = 0.0, y1 = 1.0;  ///< extent of the grid (cell edges), e.g. mm
  QString x_label;
  QString y_label;
  Colormap colormap = Colormap::Inferno;
  std::optional<Range> range;
  QString unit;
  QString description;  ///< e.g. number of rays, surface

  [[nodiscard]] Range effectiveRange() const;
  [[nodiscard]] float value(int i, int j) const { return values[static_cast<std::size_t>(j) * width + i]; }
};

/// x / y data for the plot dock (convergence curves, profiles, spectra).
struct Curve {
  std::vector<double> x;
  std::vector<double> y;
  QString x_label;
  QString y_label;
};

/// One displayable result.
struct ResultItem {
  enum class Kind { Lines, Field, Curve, Image };
  ResultId id = 0;
  QString name;
  Kind kind = Kind::Lines;
  bool visible = true;
  std::shared_ptr<const LineSet> lines;
  std::shared_ptr<const FaceField> field;
  std::shared_ptr<const Curve> curve;
  std::shared_ptr<const Image> image;
};

/// The results of one run (one trace, one optimisation, …).
struct ResultSet {
  ResultId id = 0;
  QString name;
  QString source;  ///< producer, e.g. the plugin id
  bool visible = true;
  std::vector<ResultItem> items;
};

/// Data range of a value array (NaN ignored); {0, 1} for no data.
[[nodiscard]] Range data_range(const std::vector<float>& values);

}  // namespace nfcad::results
