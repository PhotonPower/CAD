#pragma once

#include <QString>
#include <optional>
#include <vector>

#include <nurbforge/geometry/transform.hpp>

#include "document/entity.hpp"

namespace nfcad::kernel {

/// Geometry moved / rotated / scaled / mirrored (exact; trim loops stay in parameter space).
[[nodiscard]] document::Geometry transformed(const document::Geometry& g, const nf::Transform& t);

/// Light wireframe of a geometry for rubber-band previews: curves and shell edges sampled, at
/// most `max_points` points in total.
[[nodiscard]] std::vector<std::vector<nf::Vec3>> wire(const document::Geometry& g,
                                                      std::size_t max_points = 4000);

enum class BooleanOp { Union, Intersection, Difference };

/// Boolean of closed shells: Union / Intersection fold over all inputs, Difference subtracts
/// the others from the first. Throws std::runtime_error with a readable message.
[[nodiscard]] nf::Shell boolean(const std::vector<nf::Shell>& shells, BooleanOp op);

/// Faces of a surface-like geometry (surface, trimmed surface, shell); empty for others.
[[nodiscard]] std::vector<nf::TrimmedSurface> faces_of(const document::Geometry& g);

/// Closes the planar boundary loops (free edges) of the faces with trimmed planar caps and
/// sews everything into one shell (e.g. an extruded closed profile becomes a solid). Throws
/// std::runtime_error if there is no planar open loop.
[[nodiscard]] nf::Shell cap_planar_holes(const std::vector<nf::TrimmedSurface>& faces,
                                         double tolerance = 1e-6);

/// Chains curves end to start (reversing where needed, any input order) and joins them.
/// Throws std::runtime_error if they do not form one chain.
[[nodiscard]] nf::NurbsCurve join_chain(std::vector<nf::NurbsCurve> curves, double tolerance = 1e-6);

/// Writes the shells of `entities` to a STEP file (AP214, mm). Returns the number of shells
/// written; non-shell entities are skipped. Throws on I/O errors.
std::size_t export_step(const QString& path, const std::vector<const document::Entity*>& entities);

}  // namespace nfcad::kernel
