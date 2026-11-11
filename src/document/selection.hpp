#pragma once

#include <QObject>
#include <optional>
#include <set>
#include <vector>

#include "document/entity.hpp"

namespace nfcad::document {

/// Part of an entity: a face, an edge or a B-rep vertex of a shell or face (index into the
/// display mesh's sub-elements, which follow the B-rep numbering).
struct SubElement {
  enum class Kind : std::uint8_t { None, Face, Edge, Vertex };
  Kind kind = Kind::None;
  int index = -1;
  friend auto operator<=>(const SubElement&, const SubElement&) = default;
};

struct SelectionItem {
  EntityId entity = 0;
  SubElement sub;  ///< Kind::None: the whole entity
  friend auto operator<=>(const SelectionItem&, const SelectionItem&) = default;
};

[[nodiscard]] QString sub_element_name(const SubElement& s);

/// Selected and hovered items (M3). Kept consistent with the document: removed entities
/// leave the selection. Not part of the undo history.
class Selection : public QObject {
  Q_OBJECT
 public:
  enum class Mode { Replace, Add, Remove, Toggle };

  explicit Selection(QObject* parent = nullptr);
  /// Follows entity removal / document reset.
  void attach(class Document* doc);

  [[nodiscard]] const std::set<SelectionItem>& items() const { return items_; }
  /// Items in the order they were selected (Boolean difference: first minus the others).
  [[nodiscard]] const std::vector<SelectionItem>& ordered() const { return order_; }
  [[nodiscard]] bool empty() const { return items_.empty(); }
  [[nodiscard]] bool isSelected(EntityId id) const;  ///< whole entity selected
  /// Selected sub-elements of an entity.
  [[nodiscard]] std::vector<SubElement> selectedParts(EntityId id) const;
  /// Entities with any selected item (whole or parts).
  [[nodiscard]] std::vector<EntityId> entities() const;

  void apply(const std::vector<SelectionItem>& items, Mode mode);
  void clear();

  [[nodiscard]] std::optional<SelectionItem> hovered() const { return hovered_; }
  void setHovered(std::optional<SelectionItem> item);

  /// Incremented on every change (renderers compare it to skip work).
  [[nodiscard]] std::uint64_t version() const { return version_; }

 signals:
  void changed();
  void hoverChanged();

 private:
  void dropEntity(EntityId id);
  void syncOrder();
  std::set<SelectionItem> items_;
  std::vector<SelectionItem> order_;
  std::optional<SelectionItem> hovered_;
  std::uint64_t version_ = 0;
};

}  // namespace nfcad::document
