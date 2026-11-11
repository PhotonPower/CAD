#pragma once

#include <QString>
#include <vector>

#include "tessellation/display_mesh.hpp"

namespace nfcad::tessellation {

/// Writes the triangles of the display meshes as binary STL. Returns an error message (empty
/// on success). The accuracy is that of the display tessellation (docs/rendering.md).
[[nodiscard]] QString write_stl(const QString& path, const std::vector<const DisplayMesh*>& meshes);

}  // namespace nfcad::tessellation
