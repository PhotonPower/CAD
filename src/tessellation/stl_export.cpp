#include "tessellation/stl_export.hpp"

#include <QSaveFile>
#include <cstring>

namespace nfcad::tessellation {

QString write_stl(const QString& path, const std::vector<const DisplayMesh*>& meshes) {
  std::uint32_t count = 0;
  for (const DisplayMesh* m : meshes) count += static_cast<std::uint32_t>(m->triangleCount());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return f.errorString();
  char header[80] = {};
  std::strncpy(header, "Raytatouille Fusion binary STL", sizeof(header) - 1);
  f.write(header, 80);
  f.write(reinterpret_cast<const char*>(&count), 4);  // little endian on all supported targets
  for (const DisplayMesh* m : meshes) {
    for (std::size_t t = 0; t < m->triangleCount(); ++t) {
      const QVector3D a = m->vertex(m->indices[3 * t]);
      const QVector3D b = m->vertex(m->indices[3 * t + 1]);
      const QVector3D c = m->vertex(m->indices[3 * t + 2]);
      const QVector3D n = QVector3D::crossProduct(b - a, c - a).normalized();
      float rec[12] = {n.x(), n.y(), n.z(), a.x(), a.y(), a.z(),
                       b.x(), b.y(), b.z(), c.x(), c.y(), c.z()};
      f.write(reinterpret_cast<const char*>(rec), sizeof(rec));
      const std::uint16_t attr = 0;
      f.write(reinterpret_cast<const char*>(&attr), 2);
    }
  }
  if (!f.commit()) return f.errorString();
  return {};
}

}  // namespace nfcad::tessellation
