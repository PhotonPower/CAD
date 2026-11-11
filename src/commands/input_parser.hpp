#pragma once

#include <QString>
#include <optional>

#include "commands/command.hpp"

namespace nfcad::commands {

/// Typed input of the command line (docs/commands.md, "Keyboard input"):
///   x,y,z      absolute world coordinates        @dx,dy,dz   relative to the base point
///   x,y        construction-plane coordinates    @dx,dy      relative, in the plane
///   d<a        polar in the plane (a in degrees) @d<a        polar relative to the base point
///   number     a number for the command (radius, sides, length along the cursor direction)
///   name       a command name or alias
/// Decimal point '.', separators ',' (spaces allowed).
struct ParsedInput {
  enum class Kind { Empty, Point, Number, Name, Invalid };
  Kind kind = Kind::Empty;
  Point3 point;
  double number = 0.0;
  QString name;
  QString error;
};

[[nodiscard]] ParsedInput parse_input(const QString& text, const PlaneFrame& plane,
                                      const std::optional<Point3>& base);

}  // namespace nfcad::commands
