#include "document/entity.hpp"

#include <QObject>

namespace nfcad::document {

QString kind_name(GeometryKind kind) {
  switch (kind) {
    case GeometryKind::Point: return QObject::tr("Point");
    case GeometryKind::Curve: return QObject::tr("Curve");
    case GeometryKind::Surface: return QObject::tr("Surface");
    case GeometryKind::TrimmedSurface: return QObject::tr("Trimmed surface");
    case GeometryKind::Shell: return QObject::tr("Shell");
  }
  return {};
}

Entity make_entity(Geometry geometry, QString name) {
  Entity e;
  e.name = std::move(name);
  e.geometry = std::make_shared<const Geometry>(std::move(geometry));
  return e;
}

Entity make_point_entity(double x, double y, double z, QString name) {
  return make_entity(nf::Vec3{x, y, z}, std::move(name));
}

}  // namespace nfcad::document
