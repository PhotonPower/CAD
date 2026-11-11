#pragma once

#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "document/entity.hpp"
#include "snap/snap_types.hpp"

namespace nfcad::commands {

using Point3 = snap::SnapPoint;
using Polyline3 = std::vector<Point3>;

/// Construction plane of the view in which a point was given (orthonormal u, v; n = u × v).
struct PlaneFrame {
  Point3 origin{0, 0, 0};
  Point3 u{1, 0, 0};
  Point3 v{0, 1, 0};
  Point3 n{0, 0, 1};
};

/// An interactive command (docs/commands.md): a state machine that asks for points and numbers,
/// shows a rubber-band preview and produces entities. No widgets – the CommandController in
/// app/ feeds it with clicked / typed input.
class Command {
 public:
  enum class Result {
    Continue,   ///< waiting for more input
    Done,       ///< finished; take the entities / message
    Cancelled,  ///< nothing created
    Rejected,   ///< input not accepted in this state (state unchanged)
  };

  virtual ~Command() = default;
  [[nodiscard]] virtual QString id() const = 0;
  [[nodiscard]] virtual QString title() const = 0;   ///< undo text, e.g. "Line"
  [[nodiscard]] virtual QString prompt() const = 0;  ///< e.g. "Line: end point"

  virtual Result point(const Point3& p, const PlaneFrame& plane) = 0;
  /// A typed number (radius, number of sides, turns, ...). Rejected: the controller then uses
  /// it as a distance from basePoint() towards the cursor.
  virtual Result number(double /*value*/) { return Result::Rejected; }
  /// Enter / right-click.
  virtual Result finish() { return Result::Cancelled; }

  /// Rubber band for the cursor position.
  [[nodiscard]] virtual std::vector<Polyline3> preview(const Point3& /*cursor*/,
                                                       const PlaneFrame& /*plane*/) const {
    return {};
  }
  /// Reference for relative input (@dx,dy,dz), ortho and typed distances.
  [[nodiscard]] virtual std::optional<Point3> basePoint() const { return std::nullopt; }

  /// Entities created since the last call (the controller adds them as one undo step).
  [[nodiscard]] std::vector<document::Entity> takeEntities() { return std::exchange(entities_, {}); }
  /// Result text (e.g. a measured distance).
  [[nodiscard]] QString message() const { return message_; }

 protected:
  std::vector<document::Entity> entities_;
  QString message_;
};

/// Creates a command by id or alias (case-insensitive): point/pt, line/l, polyline/pl,
/// rectangle/rec, polygon/pol, circle/c, circle3p/c3, arc/a, helix/h, distance/dist.
/// Returns null for unknown names.
[[nodiscard]] std::unique_ptr<Command> create_command(const QString& name);
/// Ids of all commands.
[[nodiscard]] QStringList command_ids();

}  // namespace nfcad::commands
