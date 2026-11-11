# ADR 0007 – Picking on the CPU with BVHs over the display meshes

**Context.** The plan proposed GPU id-buffer picking (render entity / sub-element ids into an
integer texture and read back the pixels under the cursor). Picking is needed on every mouse
move (hover highlight) and, from M4 on, the object snaps need a spatial search over the same
geometry anyway.

**Decision.** Picking runs on the CPU (`src/pick/`) on the display meshes:
- every `DisplayMesh` gets a BVH over its triangles and one over its line segments (edges,
  curves, iso-curves), built in the tessellation worker threads;
- faces: closest ray–triangle hit; curves, edges, points: screen-space distance ≤ 6 px, BVH
  traversal with the ray inflated by the pixel radius at the far end of the object;
- curves behind a face hit are discarded (surfaces / shaded modes);
- window / crossing selection projects the display geometry into the view.

**Consequences.**
- Testable without a GPU (unit tests in CI), identical on Metal, Direct3D and OpenGL/Vulkan, no
  asynchronous texture read-back.
- The same BVHs serve the snap engine (ADR 0005).
- Memory: about one BVH node per two primitives. Measured: one pick on a 65 k-triangle mesh takes
  about 1 µs (release, Apple Silicon; test `pickTimingOnDenseMesh`).
- Supersedes the "GPU id-buffer picking" line in the M3 plan.
