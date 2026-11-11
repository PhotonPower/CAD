#pragma once

#include <QObject>
#include <QThreadPool>
#include <memory>
#include <unordered_map>

#include "document/entity.hpp"
#include "tessellation/display_mesh.hpp"
#include "tessellation/tessellator.hpp"

namespace nfcad::tessellation {

using DisplayMeshPtr = std::shared_ptr<const DisplayMesh>;

/// Tessellates entities in worker threads (ADR 0004). Results arrive in the GUI thread via
/// meshReady(); outdated results (a newer request for the same entity exists) are dropped.
class TessellationService : public QObject {
  Q_OBJECT
 public:
  explicit TessellationService(QObject* parent = nullptr);
  ~TessellationService() override;

  void request(document::EntityId id, std::uint64_t revision, document::GeometryPtr geometry);
  void cancel(document::EntityId id);
  void cancelAll();
  [[nodiscard]] int pending() const { return static_cast<int>(latest_.size()); }

 signals:
  void meshReady(nfcad::document::EntityId id, std::uint64_t revision,
                 nfcad::tessellation::DisplayMeshPtr mesh);
  void idle();

 private:
  QThreadPool pool_;
  std::unordered_map<document::EntityId, std::uint64_t> latest_;  // pending requests
};

}  // namespace nfcad::tessellation
