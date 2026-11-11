#pragma once

#include <QKeySequence>
#include <QString>
#include <vector>

namespace nfcad::ui {

/// One tool button in the tool column (docs/ui-concept.md, "Tool column").
struct ToolDef {
  QString id;          ///< command id, also icon file name: resources/icons/<group>/<id>.svg
  QString label;       ///< tooltip / menu text
  QString short_label; ///< fallback text on the button while no icon exists
  QKeySequence shortcut;
};

struct ToolGroupDef {
  QString id;
  QString title;
  bool expanded_by_default = false;
  std::vector<ToolDef> tools;
};

/// All tool groups in display order.
[[nodiscard]] const std::vector<ToolGroupDef>& tool_catalog();

}  // namespace nfcad::ui
