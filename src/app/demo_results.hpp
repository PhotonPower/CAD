#pragma once

#include <QString>

#include "document/entity.hpp"

namespace nfcad::render {
class Scene;
}
namespace nfcad::results {
class ResultStore;
}

namespace nfcad::app {

/// Demo data for the result display (docs/plugins.md, P2) without a plugin: a ray bundle from a
/// point above the entity onto its first face and reflected, a spot as field on that face, and
/// a decaying "merit" curve. Returns an error message, empty on success.
QString make_demo_results(results::ResultStore& store, const render::Scene& scene,
                          document::EntityId entity);

}  // namespace nfcad::app
