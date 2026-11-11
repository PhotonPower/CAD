#include "tessellation/tessellation_service.hpp"

#include <QMetaObject>
#include <QPointer>

namespace nfcad::tessellation {

TessellationService::TessellationService(QObject* parent) : QObject(parent) {
  qRegisterMetaType<DisplayMeshPtr>("nfcad::tessellation::DisplayMeshPtr");
  qRegisterMetaType<document::EntityId>("nfcad::document::EntityId");
}

TessellationService::~TessellationService() {
  latest_.clear();
  pool_.clear();
  pool_.waitForDone();
}

void TessellationService::request(document::EntityId id, std::uint64_t revision,
                                  document::GeometryPtr geometry) {
  latest_[id] = revision;
  QPointer<TessellationService> self(this);
  pool_.start([self, id, revision, geometry = std::move(geometry)] {
    auto mesh = std::make_shared<const DisplayMesh>(
        tessellate(*geometry, default_params(*geometry)));
    if (self.isNull()) return;
    QMetaObject::invokeMethod(
        self.data(),
        [self, id, revision, mesh] {
          if (self.isNull()) return;
          const auto it = self->latest_.find(id);
          if (it == self->latest_.end() || it->second != revision) return;  // outdated
          self->latest_.erase(it);
          emit self->meshReady(id, revision, mesh);
          if (self->latest_.empty()) emit self->idle();
        },
        Qt::QueuedConnection);
  });
}

void TessellationService::cancel(document::EntityId id) { latest_.erase(id); }

void TessellationService::cancelAll() {
  latest_.clear();
  pool_.clear();
}

}  // namespace nfcad::tessellation
