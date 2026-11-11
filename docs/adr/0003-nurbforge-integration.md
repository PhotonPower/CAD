# ADR 0003 – Nurbforge via its C++ API behind an adapter layer

**Context.** Nurbforge is a C++20 static library (`Nurbforge::core`) with an installable CMake
package, plus Python bindings. Its API is still 0.x and may change.

**Decision.**
- Link the C++ library directly. CMake first tries `find_package(Nurbforge)` (installed or
  `CMAKE_PREFIX_PATH`), then a local checkout (`NFCAD_NURBFORGE_SOURCE_DIR`, default
  `../Nurbforge` if present), then FetchContent of a pinned git tag (`NFCAD_NURBFORGE_TAG`).
- Only `src/kernel/` includes `<nurbforge/...>`. The rest of the application uses the adapter
  types (`nf::Vec3`, `nf::NurbsCurve`, ... may be passed through as values, but calls into the
  kernel go through `kernel/`).

**Amendment (M2).** Strictly limiting all `<nurbforge/...>` includes to `kernel/` would mean
wrapping every geometry type. Instead the immutable value types are used directly in
`document/` and `tessellation/`; algorithms are wrapped in `kernel/`. The widget layers
(`ui/`, `viewport/`, `render/`) never see Nurbforge types.

**Consequences.** API changes in Nurbforge touch one directory. The kernel is built with the
application's compiler and flags, so no ABI issues on Windows (Debug/Release runtime).
Features the GUI needs from the kernel (e.g. tessellation helpers) are first written in
`kernel/` or `tessellation/` and moved upstream into Nurbforge when stable.
