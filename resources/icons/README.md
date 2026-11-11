# Icons

`<group>/<tool-id>.svg` – group and tool ids from `src/ui/tool_catalog.cpp`. Monochrome SVG on a
24 px grid, strokes in `currentColor`. Until an icon exists the button shows a short text label.
Register new icons in `CMakeLists.txt` (`qt_add_resources`).
