# ADR 0008 – Plugin architecture: library / CAD core / signed Python plugins

Status: **accepted** 2026-11-11 (see [docs/plugins.md](../plugins.md))

**Context.** Several computation kernels (ray tracer, field solvers, …) build on Nurbforge and
need a common interactive front end: model, parameters, runs, results on the model,
optimisation with live geometry updates.

**Decision (proposed).**
- Three layers: physics and numerics in the kernel libraries (C++ with Python bindings);
  generic visualisation and interaction in the CAD core (C++); the kernel-specific glue in
  Python plugins, signed with Ed25519 by a single publisher (the project owner's key is built
  in; unsigned plugins only in an explicit developer mode).
- Plugins run in a separate Python process per plugin, controlled by CAD; commands as JSON over
  a local socket, large arrays in shared memory; geometry exchanged as NURBS data and turned into
  `nurbforge` objects by the client package.
- The plugin API (`nfcad`) is versioned; plugins declare the supported range.

**Consequences.** CAD stays independent of each kernel's build and Python environment; crashes
and long runs do not block or kill the UI; generic features (result layers, face fields, model
tree, parameter panel, jobs) are written once and shared by all kernels. Costs: an IPC layer
and a client package to maintain; signing only establishes origin and integrity, not a sandbox.
