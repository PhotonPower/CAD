#include "ui/tool_catalog.hpp"

namespace nfcad::ui {

namespace {

ToolDef t(const char* id, const char* label, const char* short_label, QKeySequence key = {}) {
  return {QString::fromLatin1(id), QString::fromUtf8(label), QString::fromUtf8(short_label),
          std::move(key)};
}

std::vector<ToolGroupDef> build() {
  return {
      {"draw", "Draw", true,
       {t("point", "Point", "Pt"), t("line", "Line", "Ln", QKeySequence(Qt::Key_L)),
        t("polyline", "Polyline", "PL"), t("rectangle", "Rectangle", "Rec"),
        t("polygon", "Polygon", "Pol")}},
      {"curves", "Curves", true,
       {t("circle", "Circle (centre, radius)", "Ci", QKeySequence(Qt::Key_C)),
        t("circle3p", "Circle (3 points)", "C3"), t("arc", "Arc", "Arc"),
        t("helix", "Helix", "Hx"), t("spline", "Interpolated spline", "Sp"),
        t("offset_curve", "Offset curve", "Off"), t("fillet_curve", "Fillet curves", "Fil")}},
      {"surfaces", "Surfaces", false,
       {t("plane", "Plane", "Pln"), t("extrude", "Extrude", "Ext"),
        t("revolve", "Revolve", "Rev"), t("loft", "Loft", "Lft"),
        t("sweep1", "Sweep 1 rail", "Sw1"), t("sweep2", "Sweep 2 rails", "Sw2"),
        t("coons", "Coons patch", "Cns"), t("gordon", "Gordon surface", "Gor"),
        t("offset_surface", "Offset surface", "OfS"), t("fillet_surface", "Fillet surfaces", "FiS"),
        t("cap", "Cap planar holes", "Cap")}},
      {"solids", "Solids", false,
       {t("box", "Box", "Box"), t("sphere", "Sphere", "Sph"), t("cylinder", "Cylinder", "Cyl"),
        t("cone", "Cone", "Con"), t("torus", "Torus", "Tor"), t("union", "Boolean union", "∪"),
        t("difference", "Boolean difference", "−"), t("intersection", "Boolean intersection", "∩")}},
      {"transform", "Transform", false,
       {t("move", "Move", "Mv", QKeySequence(Qt::Key_M)), t("copy", "Copy", "Cp"),
        t("rotate", "Rotate", "Rot"), t("scale", "Scale", "Scl"), t("mirror", "Mirror", "Mir"),
        t("array_linear", "Linear array", "ArL"), t("array_grid", "Grid array", "ArG"),
        t("array_polar", "Polar array", "ArP")}},
      {"edit", "Edit", false,
       {t("trim", "Trim", "Trm"), t("split", "Split", "Spl"), t("join", "Join", "Jn"),
        t("explode", "Explode", "Exp"), t("delete", "Delete", "Del", QKeySequence::Delete)}},
      {"analyze", "Analyze", false,
       {t("distance", "Distance", "Dst"), t("angle", "Angle", "Ang"),
        t("curvature", "Curvature", "Crv"), t("area", "Area", "Are"),
        t("mass_properties", "Volume / mass properties", "Vol"),
        t("intersection_check", "Intersection check", "Chk")}},
  };
}

}  // namespace

const std::vector<ToolGroupDef>& tool_catalog() {
  static const std::vector<ToolGroupDef> catalog = build();
  return catalog;
}

}  // namespace nfcad::ui
