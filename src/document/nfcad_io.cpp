#include "document/nfcad_io.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <exception>
#include <stdexcept>

namespace nfcad::document {

namespace {

// ------------------------------------------------------------------ helpers
QJsonArray doubles(const std::vector<double>& v) {
  QJsonArray a;
  for (double d : v) a.append(d);
  return a;
}

std::vector<double> to_doubles(const QJsonValue& v) {
  std::vector<double> out;
  for (const QJsonValue& x : v.toArray()) out.push_back(x.toDouble());
  return out;
}

QJsonArray points(const std::vector<nf::Vec3>& pts) {
  QJsonArray a;  // flat x, y, z, x, y, z, ...
  for (const nf::Vec3& p : pts) {
    a.append(p.x);
    a.append(p.y);
    a.append(p.z);
  }
  return a;
}

std::vector<nf::Vec3> to_points(const QJsonValue& v) {
  const QJsonArray a = v.toArray();
  if (a.size() % 3 != 0) throw std::runtime_error("point array length is not a multiple of 3");
  std::vector<nf::Vec3> out;
  out.reserve(static_cast<std::size_t>(a.size() / 3));
  for (qsizetype i = 0; i + 2 < a.size(); i += 3) {
    out.emplace_back(a[i].toDouble(), a[i + 1].toDouble(), a[i + 2].toDouble());
  }
  return out;
}

bool all_ones(const std::vector<double>& w) {
  for (double x : w) {
    if (x != 1.0) return false;
  }
  return true;
}

std::size_t to_size(const QJsonValue& v) {
  const int i = v.toInt(-1);
  if (i < 0) throw std::runtime_error("invalid size or degree");
  return static_cast<std::size_t>(i);
}

QJsonObject curve_json(const nf::NurbsCurve& c) {
  QJsonObject o{{"degree", static_cast<int>(c.degree())},
                {"knots", doubles(c.knots())},
                {"points", points(c.control_points())}};
  if (!all_ones(c.weights())) o["weights"] = doubles(c.weights());
  return o;
}

nf::NurbsCurve curve_from(const QJsonObject& o) {
  return {to_size(o["degree"]), to_doubles(o["knots"]), to_points(o["points"]),
          to_doubles(o["weights"])};
}

QJsonObject surface_json(const nf::NurbsSurface& s) {
  QJsonObject o{{"degree_u", static_cast<int>(s.degree_u())},
                {"degree_v", static_cast<int>(s.degree_v())},
                {"knots_u", doubles(s.knots_u())},
                {"knots_v", doubles(s.knots_v())},
                {"num_u", static_cast<int>(s.num_u())},
                {"num_v", static_cast<int>(s.num_v())},
                {"points", points(s.control_points())}};
  if (!all_ones(s.weights())) o["weights"] = doubles(s.weights());
  return o;
}

nf::NurbsSurface surface_from(const QJsonObject& o) {
  return {to_size(o["degree_u"]), to_size(o["degree_v"]), to_doubles(o["knots_u"]),
          to_doubles(o["knots_v"]),  to_size(o["num_u"]),    to_size(o["num_v"]),
          to_points(o["points"]),     to_doubles(o["weights"])};
}

QJsonArray loop_json(const nf::TrimLoop& loop) {
  QJsonArray a;
  for (const nf::NurbsCurve& c : loop.curves) a.append(curve_json(c));
  return a;
}

nf::TrimLoop loop_from(const QJsonValue& v) {
  nf::TrimLoop loop;
  for (const QJsonValue& c : v.toArray()) loop.curves.push_back(curve_from(c.toObject()));
  return loop;
}

QJsonObject face_json(const nf::TrimmedSurface& f) {
  QJsonObject o{{"surface", surface_json(f.surface())}};
  if (!f.is_untrimmed()) {
    o["outer"] = loop_json(f.outer());
    QJsonArray holes;
    for (const nf::TrimLoop& h : f.holes()) holes.append(loop_json(h));
    if (!holes.isEmpty()) o["holes"] = holes;
  }
  if (!f.label().empty()) o["label"] = QString::fromStdString(f.label());
  return o;
}

nf::TrimmedSurface face_from(const QJsonObject& o) {
  nf::NurbsSurface s = surface_from(o["surface"].toObject());
  std::string label = o["label"].toString().toStdString();
  if (!o.contains("outer")) return nf::TrimmedSurface(std::move(s)).with_label(std::move(label));
  std::vector<nf::TrimLoop> holes;
  for (const QJsonValue& h : o["holes"].toArray()) holes.push_back(loop_from(h));
  return nf::TrimmedSurface(std::move(s), loop_from(o["outer"]), std::move(holes))
      .with_label(std::move(label));
}

}  // namespace

// ------------------------------------------------------------------ geometry
QJsonObject geometry_to_json(const Geometry& g) {
  return std::visit(
      [](const auto& v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, nf::Vec3>) {
          return {{"type", "point"}, {"point", points({v})}};
        } else if constexpr (std::is_same_v<T, nf::NurbsCurve>) {
          QJsonObject o = curve_json(v);
          o["type"] = "curve";
          return o;
        } else if constexpr (std::is_same_v<T, nf::NurbsSurface>) {
          QJsonObject o = surface_json(v);
          o["type"] = "surface";
          return o;
        } else if constexpr (std::is_same_v<T, nf::TrimmedSurface>) {
          QJsonObject o = face_json(v);
          o["type"] = "trimmed_surface";
          return o;
        } else {
          QJsonArray faces;
          for (const nf::TrimmedSurface& f : v.faces()) faces.append(face_json(f));
          return {{"type", "shell"}, {"faces", faces}};
        }
      },
      g);
}

Geometry geometry_from_json(const QJsonObject& o) {
  const QString type = o["type"].toString();
  if (type == QLatin1String("point")) {
    const auto p = to_points(o["point"]);
    if (p.size() != 1) throw std::runtime_error("point needs 3 coordinates");
    return p.front();
  }
  if (type == QLatin1String("curve")) return curve_from(o);
  if (type == QLatin1String("surface")) return surface_from(o);
  if (type == QLatin1String("trimmed_surface")) return face_from(o);
  if (type == QLatin1String("shell")) {
    std::vector<nf::TrimmedSurface> faces;
    for (const QJsonValue& f : o["faces"].toArray()) faces.push_back(face_from(f.toObject()));
    return nf::Shell::sew(std::move(faces));
  }
  throw std::runtime_error("unknown geometry type '" + type.toStdString() + "'");
}

// ------------------------------------------------------------------ document
QString save_nfcad(const Document& doc, const QString& path) {
  QJsonArray layers;
  for (const Layer& l : doc.layers()) {
    layers.append(QJsonObject{{"id", static_cast<int>(l.id)},
                              {"name", l.name},
                              {"color", l.color.name()},
                              {"visible", l.visible}});
  }
  QJsonArray groups;
  for (const Group& g : doc.groups()) {
    groups.append(QJsonObject{{"id", static_cast<int>(g.id)},
                              {"name", g.name},
                              {"parent", static_cast<int>(g.parent)},
                              {"visible", g.visible}});
  }
  QJsonArray entities;
  for (const auto& [id, e] : doc.entities()) {
    QJsonObject o{{"id", QString::number(id)},
                  {"name", e.name},
                  {"layer", static_cast<int>(e.layer)},
                  {"visible", e.visible},
                  {"geometry", geometry_to_json(*e.geometry)}};
    if (e.color) o["color"] = e.color->name();
    if (e.group != 0) o["group"] = static_cast<int>(e.group);
    if (!e.attributes.isEmpty()) o["attributes"] = QJsonObject::fromVariantMap(e.attributes);
    entities.append(o);
  }
  QJsonObject root{{"format", "nfcad"},
                         {"version", kNfcadFormatVersion},
                         {"units", "mm"},
                         {"layers", layers},
                         {"groups", groups},
                         {"entities", entities}};
  if (!doc.allPluginParameters().isEmpty()) {
    QJsonObject plugins;
    for (auto it = doc.allPluginParameters().begin(); it != doc.allPluginParameters().end(); ++it) {
      plugins.insert(it.key(), QJsonObject{{"params", it.value()}});
    }
    root.insert("plugins", plugins);  // v2, optional: parameter values per plugin (P4)
  }

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return file.errorString();
  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  if (!file.commit()) return file.errorString();
  return {};
}

QString load_nfcad(Document& doc, const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return file.errorString();
  QJsonParseError parse_error;
  const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (json.isNull()) return parse_error.errorString();
  const QJsonObject root = json.object();
  if (root["format"].toString() != QLatin1String("nfcad")) {
    return QObject::tr("not a Raytatouille Fusion document");
  }
  if (root["version"].toInt() > kNfcadFormatVersion) {
    return QObject::tr("document version %1 is newer than this program supports (%2)")
        .arg(root["version"].toInt())
        .arg(kNfcadFormatVersion);
  }

  std::vector<Layer> layers;
  for (const QJsonValue& v : root["layers"].toArray()) {
    const QJsonObject l = v.toObject();
    layers.push_back({static_cast<LayerId>(l["id"].toInt()), l["name"].toString(),
                      QColor(l["color"].toString()), l["visible"].toBool(true)});
  }
  std::vector<Group> groups;
  for (const QJsonValue& v : root["groups"].toArray()) {  // version 2
    const QJsonObject g = v.toObject();
    groups.push_back({static_cast<GroupId>(g["id"].toInt()), g["name"].toString(),
                      static_cast<GroupId>(g["parent"].toInt()), g["visible"].toBool(true)});
  }
  std::vector<Entity> entities;
  try {
    for (const QJsonValue& v : root["entities"].toArray()) {
      const QJsonObject o = v.toObject();
      Entity e = make_entity(geometry_from_json(o["geometry"].toObject()), o["name"].toString());
      e.id = o["id"].toString().toULongLong();
      e.layer = static_cast<LayerId>(o["layer"].toInt());
      e.visible = o["visible"].toBool(true);
      if (o.contains("color")) e.color = QColor(o["color"].toString());
      e.group = static_cast<GroupId>(o["group"].toInt());
      e.attributes = o["attributes"].toObject().toVariantMap();
      entities.push_back(std::move(e));
    }
  } catch (const std::exception& ex) {
    return QObject::tr("invalid geometry: %1").arg(QString::fromUtf8(ex.what()));
  }
  doc.reset(std::move(layers), std::move(entities), std::move(groups));
  QJsonObject params;
  const QJsonObject plugins = root["plugins"].toObject();
  for (auto it = plugins.begin(); it != plugins.end(); ++it) params.insert(it.key(), it.value().toObject()["params"]);
  doc.setAllPluginParameters(params);
  doc.setFilePath(path);
  return {};
}

}  // namespace nfcad::document
