# ADR 0004 – Display tessellation in the application

**Context.** Nurbforge deliberately does not tessellate: it hands exact NURBS to gmsh. The GUI
needs triangles and polylines for display, picking and the snap pre-search, quickly and
incrementally.

**Decision.** Tessellation for display is done in `src/tessellation/` with adaptive chordal and
angular tolerances, in worker threads, cached per entity. Trimmed faces are tessellated in
parameter space (grid clipped by trim loops); shared B-rep edges are tessellated once and used by
both neighbouring faces.

**Consequences.** Display meshes are never used for modelling or export; snaps are refined on the
exact NURBS. If a tessellator is later wanted in the kernel (e.g. for STL export), this code is
the candidate to move upstream.
