#pragma once

#include <QJsonObject>
#include <QString>

#include "document/document.hpp"

namespace nfcad::document {

/// Native document format `.nfcad`, version 1: UTF-8 JSON (docs/file-format.md).
/// Geometry is stored exactly (degrees, knots, control points, weights, trim loops).
inline constexpr int kNfcadFormatVersion = 1;

/// Writes the document. Returns an error message, empty on success.
[[nodiscard]] QString save_nfcad(const Document& doc, const QString& path);

/// Reads a file into `doc` (replacing its content). Returns an error message, empty on success.
[[nodiscard]] QString load_nfcad(Document& doc, const QString& path);

// Exposed for tests.
[[nodiscard]] QJsonObject geometry_to_json(const Geometry& g);
[[nodiscard]] Geometry geometry_from_json(const QJsonObject& o);

}  // namespace nfcad::document
