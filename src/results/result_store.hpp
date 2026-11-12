#pragma once

#include <QObject>
#include <vector>

#include "results/result_types.hpp"

namespace nfcad::document {
class Document;
}

namespace nfcad::results {

/// The result sets of the session (docs/plugins.md, P2): created by plugins (P3) or scripts,
/// shown by the viewports (line sets, face fields), the plot dock (curves) and the Results
/// panel. Results reference entities by id; results of removed entities are hidden.
class ResultStore : public QObject {
  Q_OBJECT
 public:
  explicit ResultStore(QObject* parent = nullptr);

  [[nodiscard]] const std::vector<ResultSet>& sets() const { return sets_; }
  [[nodiscard]] const ResultSet* set(ResultId id) const;

  ResultId createSet(const QString& name, const QString& source = {});
  ResultId addLines(ResultId set, const QString& name, LineSet lines);
  ResultId addField(ResultId set, const QString& name, FaceField field);
  ResultId addCurve(ResultId set, const QString& name, Curve curve);
  ResultId addImage(ResultId set, const QString& name, Image image);
  /// Sets the key of an item (ResultItem::key).
  void setKey(ResultId item, const QString& key);
  /// Items with `key` (of `kind`) from the newest result set that has any (empty if none).
  [[nodiscard]] std::vector<const ResultItem*> itemsWithKey(const QString& key, ResultItem::Kind kind) const;
  /// Appends points to a curve (live plots, e.g. a merit function per iteration).
  void appendToCurve(ResultId item, double x, double y);

  void setSetVisible(ResultId set, bool visible);
  void setItemVisible(ResultId item, bool visible);
  void removeSet(ResultId set);
  void clear();

  /// Visible items of visible sets.
  [[nodiscard]] std::vector<const ResultItem*> visibleItems(ResultItem::Kind kind) const;
  [[nodiscard]] const ResultItem* item(ResultId id) const;

  /// Creates CAD polylines from every n-th polyline of a line set (at most `max_count`);
  /// returns the entities (the caller adds them as one undo step).
  [[nodiscard]] static std::vector<document::Entity> promote(const LineSet& lines, std::size_t every,
                                                             std::size_t max_count);

 signals:
  void changed();
  /// A curve got new points (the plot redraws without rebuilding the lists).
  void curveUpdated(nfcad::results::ResultId item);

 private:
  ResultItem* mutableItem(ResultId id);
  ResultId add(ResultId set, ResultItem item);

  std::vector<ResultSet> sets_;
  ResultId next_ = 1;
};

}  // namespace nfcad::results
