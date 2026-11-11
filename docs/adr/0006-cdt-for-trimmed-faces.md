# ADR 0006 – Constrained Delaunay triangulation (CDT) for trimmed faces

**Context.** Trimmed faces (holes, arbitrary outer loops) must be triangulated in parameter
space with the boundary as constraint and interior points for curvature. Options: own
grid-cell clipping + ear clipping (fragile for concave loops and holes that cross cells),
own constrained Delaunay implementation (large effort), the header-only library
[CDT](https://github.com/artem-ogre/CDT) (MPL-2.0), Triangle (non-free licence), CGAL (GPL).

**Decision.** Use CDT 1.4.4, header-only, fetched by `cmake/FetchCDT.cmake` (only its headers are
used, its CMake project is not added). Input: adaptive boundary polylines (constraint edges) and
the interior points of the adaptive (u, v) grid that are inside the loops and not too close to
the boundary; (u, v) is scaled by the mean |S_u|, |S_v| so that triangles are well shaped in 3D.
Untrimmed faces skip CDT and use the grid directly.

**Consequences.**
- Robust triangulation including holes and touching loops (`TryResolve` for intersecting edges).
- MPL-2.0 is file-level copyleft: unmodified use in an MIT application is fine; changes to CDT's
  own files would have to be published under MPL-2.0. The licence text ships with binary
  packages (M7/M8).
- Dependency only of the application, not of the Nurbforge kernel (ADR 0002 there stays intact).
