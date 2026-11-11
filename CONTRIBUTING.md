# Contributing to Nurbforge CAD

## Workflow

1. Open an issue; assign it to a milestone (M0 … M8, see [docs/plan.md](docs/plan.md)).
2. Branch from `main`: `feature/<topic>`, `fix/<topic>`, `docs/<topic>`.
3. Add tests in `tests/` – everything below the widget layer (camera, snaps, tessellation,
   document) must be tested without opening a window.
4. Run locally:
   ```bash
   cmake --preset debug && cmake --build --preset debug && ctest --preset debug
   ```
5. Open a pull request; the macOS CI job must pass. Windows and AlmaLinux jobs are informative
   until M7, required afterwards.

## Code style

- C++20, `clang-format` and `clang-tidy` configs in the repository (same as Nurbforge).
- Namespace `nfcad` (sub-namespaces per layer: `nfcad::snap`, `nfcad::render`, …);
  types `PascalCase`, functions/variables `snake_case`, members end in `_`.
  Qt overrides (`paintEvent`, …) and signals/slots keep Qt's camelCase.
- One class per file pair, file names `snake_case.hpp/.cpp`.
- Respect the layer rules in [docs/architecture.md](docs/architecture.md): Nurbforge types stay
  below the widget layer (algorithms in `kernel/`), only `render/` and `viewport/` use the RHI,
  only `platform/` has `#ifdef Q_OS_*`.
- User-visible strings go through `tr()`; English source strings, German translation in
  `resources/i18n/` (later).

## Adding a tool

1. Add an entry to the tool catalog (`src/ui/tool_catalog.cpp`): id, group, label, shortcut.
2. Add the icon `resources/icons/<group>/<id>.svg` (24 px grid, `currentColor`).
3. Implement the command in `src/commands/` and register it under the same id.
4. Document it in the user docs and in `CHANGELOG.md`.

## Commit messages

Conventional style: `feat(snap): add quadrant snap`, `fix(viewport): ...`, `docs: ...`.
