"""CAD geometry (.nfcad dicts: surface, trimmed_surface, shell) as a triangle mesh for tracing.

Every face is tessellated on a grid of its surface parameters, fine enough for a chordal
tolerance. A triangle vertex carries the surface normal and (u, v) normalised to [0, 1] over the
surface domain – the coordinates of fields on faces (docs/rendering.md). Trimming is not cut into
the triangles: every trimmed face has a mask (a raster over (u, v), even-odd rule over its loops),
and a hit outside the mask is no hit. Areas (faces, texels) are integrated over the same mask, so
sampling, tracing and normalisation agree. NumPy only."""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

import pt_nurbs as nb

SURFACE_KINDS = ("surface", "trimmed_surface", "shell")


def faces_of(geometry: dict) -> list[dict]:
    """The faces of an entity's geometry as trimmed-surface dicts ({"surface", "outer", "holes"})."""
    kind = geometry.get("type")
    if kind == "surface":
        return [{"surface": geometry}]
    if kind == "trimmed_surface":
        return [geometry]
    if kind == "shell":
        return list(geometry.get("faces") or [])
    return []


# ---------------------------------------------------------------------------- trimming
def _loop_polygon(loop: list[dict], domain) -> np.ndarray:
    """A loop (curves in parameter space) as a polygon in normalised (u, v)."""
    u0, u1, v0, v1 = domain
    pts = []
    for c in loop:
        degree = int(c["degree"])
        spans = nb.distinct_knots(degree, c["knots"])
        if degree <= 1:
            t = spans
        else:
            t = np.unique(np.concatenate([np.linspace(a, b, 25) for a, b in zip(spans[:-1], spans[1:])]))
        pts.append(nb.curve_points(c, t)[:, :2])
    p = np.concatenate(pts) if pts else np.zeros((0, 2))
    return np.column_stack([(p[:, 0] - u0) / (u1 - u0), (p[:, 1] - v0) / (v1 - v0)])


def trim_mask(loops: list[list[dict]], domain, res: int) -> np.ndarray:
    """Inside (True) per cell of a res x res raster over the normalised domain, [row v, column u]:
    even-odd rule over all loops, tested at the cell centres."""
    toggles = np.zeros((res, res + 1), dtype=np.int32)
    yc = (np.arange(res) + 0.5) / res
    for loop in loops:
        p = _loop_polygon(loop, domain)
        if len(p) < 3:
            continue
        q = np.roll(p, -1, axis=0)
        x0, y0, x1, y1 = p[:, 0:1], p[:, 1:2], q[:, 0:1], q[:, 1:2]
        crosses = (np.minimum(y0, y1) <= yc) & (yc < np.maximum(y0, y1))        # (edges, rows)
        with np.errstate(divide="ignore", invalid="ignore"):
            x = x0 + (yc - y0) / (y1 - y0) * (x1 - x0)
        e, r = np.nonzero(crosses)
        col = np.clip(np.ceil(x[e, r] * res - 0.5), 0, res).astype(np.int64)    # first centre right of x
        np.add.at(toggles, (r, col), 1)
    return (np.cumsum(toggles, axis=1)[:, :res] & 1).astype(bool)


# ---------------------------------------------------------------------------- one face
def _lines(spans: np.ndarray, per_span: int) -> np.ndarray:
    parts = [np.linspace(a, b, per_span + 1)[:-1] for a, b in zip(spans[:-1], spans[1:])]
    return np.concatenate(parts + [spans[-1:]])


def _subdivisions(surface: dict, tol: float, limit: int) -> tuple[np.ndarray, np.ndarray]:
    """Grid lines in u and v so that the triangles stay within `tol` of the surface (estimated
    from second differences of a sample with 8 steps per knot span)."""
    ku = nb.distinct_knots(int(surface["degree_u"]), surface["knots_u"])
    kv = nb.distinct_knots(int(surface["degree_v"]), surface["knots_v"])
    n = 8
    s, _, _ = nb.surface_grid(surface, _lines(ku, n), _lines(kv, n))

    def second(a: np.ndarray) -> float:                 # along axis 0, inside the spans only
        d = np.linalg.norm(a[2:] - 2 * a[1:-1] + a[:-2], axis=-1)
        inner = (np.arange(1, a.shape[0] - 1) % n) != 0
        return float(d[inner].max()) if inner.any() else 0.0

    twist = float(np.linalg.norm(s[1:, 1:] - s[1:, :-1] - s[:-1, 1:] + s[:-1, :-1], axis=-1).max())
    out = []
    for d2, spans in ((second(s), ku), (second(s.transpose(1, 0, 2)), kv)):
        err = max(d2 / 8.0, twist / 4.0)                # at the sample step
        m = int(np.ceil(n * np.sqrt(err / tol))) if err > 0 else 1
        m = max(1, min(m, max(1, limit // (len(spans) - 1))))
        out.append(_lines(spans, m))
    return out[0], out[1]


def _overlap(lines_a: np.ndarray, lines_b: np.ndarray) -> np.ndarray:
    """Length of the overlap of every interval of a with every interval of b: (len(a)-1, len(b)-1)."""
    lo = np.maximum(lines_a[:-1, None], lines_b[None, :-1])
    hi = np.minimum(lines_a[1:, None], lines_b[None, 1:])
    return np.clip(hi - lo, 0.0, None)


@dataclass
class FaceGrid:
    """What the area integrals of one face need: grid lines (normalised), the area density of
    every grid cell (mm² per unit of normalised uv) and the trim mask."""
    ug: np.ndarray
    vg: np.ndarray
    density: np.ndarray                  # (len(ug)-1, len(vg)-1)
    mask: np.ndarray | None              # (res, res) [v, u] or None (untrimmed)
    length_u: float = 0.0                # mean arc length along u and v / mm
    length_v: float = 0.0

    def areas(self, lines_u: np.ndarray, lines_v: np.ndarray) -> np.ndarray:
        """Area / mm² of the face inside every cell of the grid lines_u x lines_v (normalised):
        (len(lines_u)-1, len(lines_v)-1)."""
        if self.mask is None:
            wu = _overlap(lines_u, self.ug)              # (target u, grid u)
            wv = _overlap(lines_v, self.vg)
            return wu @ self.density @ wv.T
        res = self.mask.shape[0]
        cells = np.arange(res + 1) / res
        # common refinement in each direction: segments lying in one grid cell and one mask cell
        su = np.unique(np.concatenate([self.ug, cells]))
        sv = np.unique(np.concatenate([self.vg, cells]))
        mu, mv = 0.5 * (su[:-1] + su[1:]), 0.5 * (sv[:-1] + sv[1:])
        gu = np.clip(np.searchsorted(self.ug, mu) - 1, 0, len(self.ug) - 2)
        gv = np.clip(np.searchsorted(self.vg, mv) - 1, 0, len(self.vg) - 2)
        cu = np.minimum((mu * res).astype(np.int64), res - 1)
        cv = np.minimum((mv * res).astype(np.int64), res - 1)
        x = self.density[np.ix_(gu, gv)] * self.mask[np.ix_(cv, cu)].T      # (seg u, seg v)
        return _overlap(lines_u, su) @ x @ _overlap(lines_v, sv).T


def _tessellate(face: dict, tol: float, mask_res: int, limit: int = 192):
    """Triangles of one face: v0, e1, e2, vertex normals, vertex uv (normalised), and its FaceGrid.
    Triangles of grid cells entirely outside the trimming are left out."""
    surface = face["surface"]
    dom = nb.surface_domain(surface)
    us, vs = _subdivisions(surface, tol, limit)
    loops = ([face["outer"]] if face.get("outer") else []) + list(face.get("holes") or [])
    if loops and (len(us) < 9 or len(vs) < 9):          # masks need a parametrisation that is
        ku = nb.distinct_knots(int(surface["degree_u"]), surface["knots_u"])   # not too coarse
        kv = nb.distinct_knots(int(surface["degree_v"]), surface["knots_v"])
        if len(us) < 9:
            us = _lines(ku, int(np.ceil(8 / (len(ku) - 1))))
        if len(vs) < 9:
            vs = _lines(kv, int(np.ceil(8 / (len(kv) - 1))))
    s, su, sv = nb.surface_grid(surface, us, vs)
    nrm = np.cross(su, sv)
    length = np.linalg.norm(nrm, axis=-1, keepdims=True)
    with np.errstate(divide="ignore", invalid="ignore"):
        nrm = np.where(length > 1e-300, nrm / length, 0.0)       # poles: filled per triangle later
    ug = (us - dom[0]) / (dom[1] - dom[0])
    vg = (vs - dom[2]) / (dom[3] - dom[2])
    uu, vv = np.meshgrid(ug, vg, indexing="ij")
    uv = np.stack([uu, vv], axis=-1)

    a, b, c, d = s[:-1, :-1], s[1:, :-1], s[1:, 1:], s[:-1, 1:]

    def tri_area(p, q, r):
        return 0.5 * np.linalg.norm(np.cross(q - p, r - p), axis=-1)

    area1, area2 = tri_area(a, b, c), tri_area(a, c, d)
    cell = np.diff(ug)[:, None] * np.diff(vg)[None, :]
    grid = FaceGrid(ug, vg, (area1 + area2) / cell,
                    trim_mask(loops, dom, mask_res) if loops else None,
                    float(np.linalg.norm(np.diff(s, axis=0), axis=-1).sum(axis=0).mean()),
                    float(np.linalg.norm(np.diff(s, axis=1), axis=-1).sum(axis=1).mean()))
    keep = grid.areas(ug, vg) > 0 if loops else np.ones(cell.shape, dtype=bool)

    def corners(arr):
        return arr[:-1, :-1], arr[1:, :-1], arr[1:, 1:], arr[:-1, 1:]

    out = []
    for (i0, i1, i2), area in (((0, 1, 2), area1), ((0, 2, 3), area2)):
        sel = keep & (area > 0)
        p = [x[sel] for x in corners(s)]
        n = [x[sel] for x in corners(nrm)]
        t = [x[sel] for x in corners(uv)]
        out.append((p[i0], p[i1] - p[i0], p[i2] - p[i0],
                    np.stack([n[i0], n[i1], n[i2]], axis=1), np.stack([t[i0], t[i1], t[i2]], axis=1)))
    v0, e1, e2, vn, vt = (np.concatenate([o[k] for o in out]) for k in range(5))
    return v0, e1, e2, vn, vt, grid


# ---------------------------------------------------------------------------- the mesh
@dataclass
class Mesh:
    v0: np.ndarray                       # (T, 3) first vertex
    e1: np.ndarray                       # (T, 3) edges from it
    e2: np.ndarray
    ng: np.ndarray                       # (T, 3) unit normal of the triangle (side of S_u x S_v)
    vn: np.ndarray                       # (T, 3, 3) vertex normals
    vt: np.ndarray                       # (T, 3, 2) vertex (u, v), normalised
    area: np.ndarray                     # (T,)
    face: np.ndarray                     # (T,) face of the triangle
    face_entity: np.ndarray              # (F,) index of the entity in the list given to build()
    face_index: np.ndarray               # (F,) face number within the entity
    face_area: np.ndarray                # (F,) area inside the trimming / mm²
    face_first: np.ndarray               # (F + 1,) triangles of face f: face_first[f] : face_first[f + 1]
    grids: list[FaceGrid] = field(default_factory=list)
    mask_res: np.ndarray = None          # (F,) 0: untrimmed
    mask_off: np.ndarray = None          # (F,) offset into masks
    masks: np.ndarray = None             # all masks, flat
    entity_ids: list[int] = field(default_factory=list)

    @property
    def triangles(self) -> int:
        return len(self.v0)

    def inside(self, face: np.ndarray, u: np.ndarray, v: np.ndarray) -> np.ndarray:
        """Whether (u, v) (normalised) lies inside the trimming of the face."""
        res = self.mask_res[face]
        out = res == 0
        t = np.nonzero(~out)[0]
        if t.size:
            r = res[t]
            i = np.clip((u[t] * r).astype(np.int64), 0, r - 1)
            j = np.clip((v[t] * r).astype(np.int64), 0, r - 1)
            out[t] = self.masks[self.mask_off[face[t]] + j * r + i]
        return out

    def accept(self, tri: np.ndarray, b1: np.ndarray, b2: np.ndarray) -> np.ndarray:
        """Hit filter for the BVH: hits outside the trimming of their face do not count."""
        f = self.face[tri]
        out = self.mask_res[f] == 0
        t = np.nonzero(~out)[0]
        if t.size:
            vt = self.vt[tri[t]]
            uv = vt[:, 0] * (1 - b1[t] - b2[t])[:, None] + vt[:, 1] * b1[t][:, None] + vt[:, 2] * b2[t][:, None]
            out[t] = self.inside(f[t], uv[:, 0], uv[:, 1])
        return out

    def shade(self, tri: np.ndarray, b1: np.ndarray, b2: np.ndarray):
        """Shading normal (unit, on the side of the triangle normal) and (u, v) at hits."""
        b0 = 1 - b1 - b2
        vn, vt = self.vn[tri], self.vt[tri]
        n = vn[:, 0] * b0[:, None] + vn[:, 1] * b1[:, None] + vn[:, 2] * b2[:, None]
        uv = vt[:, 0] * b0[:, None] + vt[:, 1] * b1[:, None] + vt[:, 2] * b2[:, None]
        ng = self.ng[tri]
        length = np.linalg.norm(n, axis=1)
        bad = (length < 1e-12) | (np.einsum("ij,ij->i", n, ng) <= 0)
        n = np.where(bad[:, None], ng, n / np.where(length > 0, length, 1.0)[:, None])
        return n, uv

    def flip_faces(self, faces) -> None:
        """Turns the normals of faces (their other side becomes the front)."""
        sel = np.isin(self.face, np.asarray(list(faces), dtype=np.int64))
        self.e1[sel], self.e2[sel] = self.e2[sel].copy(), self.e1[sel].copy()
        self.ng[sel] *= -1
        self.vn[sel] = -self.vn[sel][:, [0, 2, 1]]
        self.vt[sel] = self.vt[sel][:, [0, 2, 1]]

    def sample(self, faces: np.ndarray, n: int, rng: np.random.Generator):
        """n points uniform by area on the given faces (inside their trimming):
        triangle, barycentric b1, b2. The density is 1 / sum(face_area[faces])."""
        faces = np.asarray(faces, dtype=np.int64)
        tri = np.empty(n, dtype=np.int64)
        b1 = np.empty(n)
        b2 = np.empty(n)
        cum_face = np.cumsum(self.face_area[faces])
        f = faces[np.minimum(np.searchsorted(cum_face, rng.random(n) * cum_face[-1], side="right"), len(faces) - 1)]
        cum = self._cum_area()
        todo = np.arange(n)
        for _ in range(1000):
            ft = f[todo]
            lo = np.where(self.face_first[ft] > 0, cum[self.face_first[ft] - 1], 0.0)
            hi = cum[self.face_first[ft + 1] - 1]
            t = np.searchsorted(cum, lo + rng.random(len(todo)) * (hi - lo), side="right")
            t = np.clip(t, self.face_first[ft], self.face_first[ft + 1] - 1)
            r1, r2 = rng.random(len(todo)), rng.random(len(todo))
            fold = r1 + r2 > 1
            r1, r2 = np.where(fold, 1 - r1, r1), np.where(fold, 1 - r2, r2)
            tri[todo], b1[todo], b2[todo] = t, r1, r2
            ok = self.accept(t, r1, r2)
            todo = todo[~ok]
            if todo.size == 0:
                break
        return tri, b1, b2

    def _cum_area(self) -> np.ndarray:
        if getattr(self, "_cum", None) is None or len(self._cum) != len(self.area):
            self._cum = np.cumsum(self.area)
        return self._cum

    def texels(self, face: int, long_side: int) -> tuple[int, int]:
        """Texture size (w along u, h along v) of a face with `long_side` texels on its longer
        side, the other side by the face's proportions."""
        g = self.grids[face]
        lu, lv = max(g.length_u, 1e-300), max(g.length_v, 1e-300)
        if lu >= lv:
            return long_side, max(1, int(round(long_side * lv / lu)))
        return max(1, int(round(long_side * lu / lv))), long_side

    def texel_areas(self, face: int, w: int, h: int) -> np.ndarray:
        """Area / mm² of the face in every texel: (h, w), [row v, column u]."""
        return self.grids[face].areas(np.arange(w + 1) / w, np.arange(h + 1) / h).T


def mask_resolution(trimmed_faces: int) -> int:
    return 512 if trimmed_faces <= 64 else 256 if trimmed_faces <= 1024 else 128


def build(entities: list[dict], tol: float) -> Mesh:
    """The mesh of the surfaces, trimmed surfaces and shells among `entities` (dicts with "id" and
    "geometry"); `tol`: chordal tolerance / mm."""
    todo = []
    for k, e in enumerate(entities):
        for i, f in enumerate(faces_of(e.get("geometry") or {})):
            todo.append((k, i, f))
    if not todo:
        raise ValueError("no surfaces to trace")
    res = mask_resolution(sum(1 for _, _, f in todo if f.get("outer") or f.get("holes")))
    parts, grids, ent, idx, first, areas = [], [], [], [], [0], []
    mask_res, mask_off, masks, offset = [], [], [], 0
    for k, i, f in todo:
        v0, e1, e2, vn, vt, grid = _tessellate(f, tol, res)
        parts.append((v0, e1, e2, vn, vt))
        grids.append(grid)
        ent.append(k)
        idx.append(i)
        first.append(first[-1] + len(v0))
        areas.append(float(grid.areas(np.array([0.0, 1.0]), np.array([0.0, 1.0]))[0, 0]))
        if grid.mask is None:
            mask_res.append(0)
            mask_off.append(0)
        else:
            mask_res.append(grid.mask.shape[0])
            mask_off.append(offset)
            masks.append(grid.mask.ravel())
            offset += grid.mask.size
    v0, e1, e2, vn, vt = (np.concatenate([p[j] for p in parts]) for j in range(5))
    cross = np.cross(e1, e2)
    length = np.linalg.norm(cross, axis=1)
    ng = cross / np.where(length > 0, length, 1.0)[:, None]
    face = np.repeat(np.arange(len(todo)), np.diff(first))
    return Mesh(v0, e1, e2, ng, vn, vt, 0.5 * length, face,
                np.asarray(ent, dtype=np.int64), np.asarray(idx, dtype=np.int64), np.asarray(areas),
                np.asarray(first, dtype=np.int64), grids, np.asarray(mask_res, dtype=np.int64),
                np.asarray(mask_off, dtype=np.int64),
                np.concatenate(masks) if masks else np.zeros(0, dtype=bool),
                [int(e.get("id", k)) for k, e in enumerate(entities)])
