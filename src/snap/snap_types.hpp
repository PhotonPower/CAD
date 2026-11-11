#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace nfcad::snap {

/// Object snap types, see docs/snapping.md. Values are bit flags for SnapMask.
enum class SnapType : std::uint16_t {
  Point = 1U << 0U,
  End = 1U << 1U,
  Mid = 1U << 2U,
  Centre = 1U << 3U,
  Quadrant = 1U << 4U,
  Near = 1U << 5U,
  Intersection = 1U << 6U,
  FaceCorner = 1U << 7U,
  FaceEdge = 1U << 8U,
  Grid = 1U << 9U,
};

/// All snap types, ordered by priority (highest first).
inline constexpr std::array<SnapType, 10> kSnapPriority = {
    SnapType::Point,    SnapType::End,  SnapType::Intersection, SnapType::Centre,
    SnapType::Quadrant, SnapType::Mid,  SnapType::FaceCorner,   SnapType::FaceEdge,
    SnapType::Near,     SnapType::Grid};

/// Rank in kSnapPriority (0 = highest priority).
[[nodiscard]] int priority_rank(SnapType type);

/// Short label shown in the snap bar and the snap tooltip ("End", "Mid", ...).
[[nodiscard]] std::string_view short_name(SnapType type);

/// Set of enabled snap types.
class SnapMask {
 public:
  constexpr SnapMask() = default;
  constexpr explicit SnapMask(std::uint16_t bits) : bits_(bits) {}

  /// Default set: everything except Grid.
  [[nodiscard]] static SnapMask defaults();

  [[nodiscard]] constexpr bool test(SnapType t) const {
    return (bits_ & static_cast<std::uint16_t>(t)) != 0;
  }
  constexpr void set(SnapType t, bool on) {
    const auto bit = static_cast<std::uint16_t>(t);
    bits_ = on ? static_cast<std::uint16_t>(bits_ | bit)
               : static_cast<std::uint16_t>(bits_ & static_cast<std::uint16_t>(~bit));
  }
  [[nodiscard]] constexpr std::uint16_t bits() const { return bits_; }

 private:
  std::uint16_t bits_ = 0;
};

}  // namespace nfcad::snap
