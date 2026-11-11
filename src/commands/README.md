# commands/ (M5)

Command framework: each tool from the tool catalog is a `Command` state machine that prompts in
the command line, requests points (with snapping) and options, and edits the document through the
undo stack. Command ids equal tool ids in `src/ui/tool_catalog.cpp`.
