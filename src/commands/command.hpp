#pragma once

#include <QString>
#include <QStringList>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "document/document.hpp"
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

  /// Commands on the selection (transforms, Booleans): called once at the start with the
  /// selected entities in selection order. Return false (and set message_) if unsuitable.
  [[nodiscard]] virtual bool usesSelection() const { return false; }
  virtual bool begin(const std::vector<document::Entity>& /*selection*/) { return true; }
  /// True if the command is complete right after begin() (e.g. Booleans).
  [[nodiscard]] virtual bool immediate() const { return false; }

  /// Entities created since the last call.
  [[nodiscard]] std::vector<document::Entity> takeEntities() { return std::exchange(entities_, {}); }
  /// All changes since the last call: created entities, removed ids, replaced geometry
  /// (the controller applies them as one undo step).
  [[nodiscard]] document::Edit takeEdit() {
    return {std::exchange(entities_, {}), std::exchange(remove_, {}), std::exchange(replace_, {})};
  }
  /// Result text (e.g. a measured distance).
  [[nodiscard]] QString message() const { return message_; }

 protected:
  std::vector<document::Entity> entities_;
  std::vector<document::EntityId> remove_;
  std::vector<std::pair<document::EntityId, document::GeometryPtr>> replace_;
  QString message_;
};

/// Creates a command by id or alias (case-insensitive): point/pt, line/l, polyline/pl,
/// rectangle/rec, polygon/pol, circle/c, circle3p/c3, arc/a, helix/h, distance/dist.
/// Returns null for unknown names.
[[nodiscard]] std::unique_ptr<Command> create_command(const QString& name);
/// Ids of all commands.
[[nodiscard]] QStringList command_ids();

namespace detail {
// Factories of the command groups (drawing_commands.cpp, solid_commands.cpp,
// transform_commands.cpp); create_command searches them in this order.
std::unique_ptr<Command> create_solid_command(const QString& name);
std::unique_ptr<Command> create_transform_command(const QString& name);
QStringList solid_command_ids();
QStringList transform_command_ids();
}  // namespace detail

}  // namespace nfcad::commands
