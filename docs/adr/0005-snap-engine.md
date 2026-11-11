# ADR 0005 – Snap engine with screen-space tolerance and priorities

**Context.** Object snaps must feel "magnetic" and predictable at every zoom level, work in
orthographic and perspective views, and stay fast on large models.

**Decision.** A Qt-Widgets-free `SnapEngine` (`src/snap/`) searches candidates in a pixel radius
around the cursor, ranks them by a fixed type priority, then pixel distance, then depth, and
refines the best candidates on the exact geometry with Nurbforge. Cheap snap points are
precomputed per entity (`SnapCache`); a BVH over display geometry limits the search. Details in
[snapping.md](../snapping.md).

**Consequences.** Behaviour is testable without a window. The priority order is fixed in code at
first; making it user-configurable is a later option.
