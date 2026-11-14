"""The scene of a computation: mesh, materials, sources – and the sensors (irradiance on faces,
camera). Lengths in mm, power in W: radiance in W/(mm² sr), irradiance in W/mm². NumPy only.

Sources
  surfaces   emit with constant radiance into a cone around the normal (half-angle 90°: a
             Lambertian emitter), from the front, the back or both sides; all surfaces of one
             source share its flux by area
  points     emit evenly into all directions; the points of one source share its flux

Sensors
  TexelSensor   irradiance on faces: the flux arriving (from both sides) in every texel; also on
                faces with a detector material, which the light passes
  Camera        a pinhole camera: radiance per pixel"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

import pt_bsdf
import pt_bvh
import pt_mesh


class _Part:
    """The BVH of some of the mesh's triangles; hits name the triangles of the mesh."""

    def __init__(self, mesh: pt_mesh.Mesh, tris: np.ndarray):
        self.tris = tris
        self.bvh = pt_bvh.BVH(mesh.v0[tris], mesh.e1[tris], mesh.e2[tris])

    def intersect(self, o, d, tmax=None, *, any_hit: bool = False, accept=None):
        inner = None if accept is None else (lambda ray, t, b1, b2: accept(ray, self.tris[t], b1, b2))
        t, tri, b1, b2 = self.bvh.intersect(o, d, tmax, any_hit=any_hit, accept=inner)
        return t, np.where(tri >= 0, self.tris[np.maximum(tri, 0)], -1), b1, b2


@dataclass
class SourceSpec:
    name: str
    flux: float                          # W
    half_angle: float = 90.0             # degrees, surfaces only
    side: str = "front"                  # front, back, both
    entities: tuple = ()                 # indices into the entity list of the mesh (surfaces)
    points: tuple = ()                   # (x, y, z) of point sources


class Scene:
    def __init__(self, mesh: pt_mesh.Mesh, materials: pt_bsdf.Materials, entity_material: np.ndarray,
                 sources: list[SourceSpec], closed: tuple = ()):
        """entity_material: material number per entity of the mesh; closed: entities (indices) whose
        faces should be turned so that their front is the outside (closed bodies)."""
        self.mesh = mesh
        self.materials = materials
        self.bvh = pt_bvh.BVH(mesh.v0, mesh.e1, mesh.e2)
        lo, hi = self.bvh.bmin[0], self.bvh.bmax[0]
        self.size = float(np.linalg.norm(hi - lo))
        self.eps = 1e-7 * max(self.size, 1e-12)
        self.flipped = self._orient(closed) if len(closed) else []
        self.face_material = np.asarray(entity_material, dtype=np.int64)[mesh.face_entity]
        self.tri_material = self.face_material[mesh.face]
        # detectors are no surfaces for the light: they get a BVH of their own, used only to find
        # where rays cross them
        virtual = materials.kind[self.tri_material] == pt_bsdf.DETECTOR
        self.face_virtual = materials.kind[self.face_material] == pt_bsdf.DETECTOR
        self.detectors = None
        if virtual.any():
            self.detectors = _Part(mesh, np.nonzero(virtual)[0])
            self.bvh = _Part(mesh, np.nonzero(~virtual)[0]) if not virtual.all() else None
        self._sources(sources)

    # ------------------------------------------------------------------ rays
    def _accept(self, ray, tri, b1, b2):
        return self.mesh.accept(tri, b1, b2)

    def hit(self, o: np.ndarray, d: np.ndarray):
        """Nearest hit: t, triangle (-1: none), b1, b2. Detectors are not hit."""
        if self.bvh is None:
            n = len(o)
            return np.full(n, np.inf), np.full(n, -1, dtype=np.int64), np.zeros(n), np.zeros(n)
        return self.bvh.intersect(o, d, accept=self._accept if self.mesh.masks.size else None)

    def occluded(self, o: np.ndarray, target: np.ndarray) -> np.ndarray:
        if self.bvh is None:
            return np.zeros(len(o), dtype=bool)
        return self.bvh.intersect(o, target - o, 1.0, any_hit=True,
                                  accept=self._accept if self.mesh.masks.size else None)[1] >= 0

    def crossing(self, o: np.ndarray, d: np.ndarray, tmax: np.ndarray):
        """Nearest crossing of a detector before tmax: t, triangle (-1: none), b1, b2."""
        return self.detectors.intersect(o, d, tmax, accept=self._accept if self.mesh.masks.size else None)

    def offset(self, p: np.ndarray, ng: np.ndarray, towards: np.ndarray) -> np.ndarray:
        """p moved a hair off its surface to the side of the direction `towards`."""
        s = np.sign(np.einsum("ij,ij->i", ng, towards))
        return p + ng * (s * self.eps)[:, None]

    # ------------------------------------------------------------------ orientation
    def _orient(self, entities) -> list[int]:
        """Turns faces of closed bodies whose normal points inwards: a ray from a face along its
        normal leaves a closed body through an even number of its faces, the opposite ray through
        an odd number. Faces without a clear answer (open shells) stay. Returns the turned faces."""
        mesh = self.mesh
        rng = np.random.default_rng(1)
        faces = np.nonzero(np.isin(mesh.face_entity, np.asarray(list(entities))) & (mesh.face_area > 0))[0]
        if not faces.size:
            return []
        per = 5
        f = np.repeat(faces, per)
        tri = np.empty(len(f), dtype=np.int64)
        b1, b2 = np.empty(len(f)), np.empty(len(f))
        for k, face in enumerate(faces):                        # points on every face
            tri[k * per:(k + 1) * per], b1[k * per:(k + 1) * per], b2[k * per:(k + 1) * per] = \
                mesh.sample(np.array([face]), per, rng)
        p = mesh.v0[tri] + b1[:, None] * mesh.e1[tri] + b2[:, None] * mesh.e2[tri]
        n = mesh.ng[tri]
        ent = mesh.face_entity[f]
        counts = []
        for sign in (1.0, -1.0):
            d = n * sign
            o = p + d * self.eps
            count = np.zeros(len(f), dtype=np.int64)
            act = np.arange(len(f))
            for _ in range(256):
                own = ent[act]

                def accept(ray, t, c1, c2, own=own):
                    return (mesh.face_entity[mesh.face[t]] == own[ray]) & mesh.accept(t, c1, c2)

                t, hit, _, _ = self.bvh.intersect(o[act], d[act], accept=accept)
                got = hit >= 0
                act = act[got]
                if not act.size:
                    break
                count[act] += 1
                o[act] = o[act] + d[act] * (t[got] + self.eps)[:, None]
            counts.append(count)
        out_even = (counts[0] % 2 == 0) & (counts[1] % 2 == 1)      # normal points outwards
        in_odd = (counts[0] % 2 == 1) & (counts[1] % 2 == 0)        # normal points inwards
        slot = np.repeat(np.arange(len(faces)), per)
        keep = np.bincount(slot, weights=out_even, minlength=len(faces))
        turn = np.bincount(slot, weights=in_odd, minlength=len(faces))
        flip = faces[turn > np.maximum(keep, per - turn - keep)]
        if flip.size:
            mesh.flip_faces(flip)
            self.bvh.refresh(mesh.v0, mesh.e1, mesh.e2)
        return [int(v) for v in flip]

    # ------------------------------------------------------------------ sources
    def _sources(self, specs: list[SourceSpec]) -> None:
        mesh = self.mesh
        flux, sin_max, sides, area, point, is_point = [], [], [], [], [], []
        faces: list[np.ndarray] = []
        self.face_source = np.full(len(mesh.face_area), -1, dtype=np.int64)
        self.source_names = []
        for spec in specs:
            if spec.flux < 0:
                raise ValueError(f"source {spec.name!r}: negative flux")
            if spec.entities and spec.points:
                raise ValueError(f"source {spec.name!r} is on surfaces and on points: use two sources")
            if spec.flux == 0 or not (spec.entities or spec.points):
                continue
            if spec.entities:
                if not 0 < spec.half_angle <= 90:
                    raise ValueError(f"source {spec.name!r}: half-angle must lie in 0 … 90°")
                if spec.side not in ("front", "back", "both"):
                    raise ValueError(f"source {spec.name!r}: side must be front, back or both")
                fs = np.nonzero(np.isin(mesh.face_entity, np.asarray(spec.entities)) & (mesh.face_area > 0))[0]
                if not fs.size:
                    raise ValueError(f"source {spec.name!r}: its surfaces have no area")
                if self.face_virtual[fs].any():
                    raise ValueError(f"source {spec.name!r}: a detector cannot be a source (light passes it "
                                     f"as if it were not there)")
                if (self.face_source[fs] >= 0).any():
                    raise ValueError(f"source {spec.name!r}: a surface already belongs to another source")
                self.face_source[fs] = len(flux)
                faces.append(fs)
                flux.append(spec.flux)
                sin_max.append(np.sin(np.radians(spec.half_angle)))
                sides.append({"front": 1, "back": -1, "both": 0}[spec.side])
                area.append(float(mesh.face_area[fs].sum()))
                point.append((np.nan,) * 3)
                is_point.append(False)
                self.source_names.append(spec.name)
            else:
                for p in spec.points:
                    faces.append(np.zeros(0, dtype=np.int64))
                    flux.append(spec.flux / len(spec.points))
                    sin_max.append(1.0)
                    sides.append(0)
                    area.append(1.0)
                    point.append(tuple(float(c) for c in p))
                    is_point.append(True)
                    self.source_names.append(spec.name)
        if not flux:
            raise ValueError("no source: give at least one object a source with a flux above 0")
        self.src_flux = np.asarray(flux)
        self.src_sin = np.asarray(sin_max)
        self.src_cos = np.sqrt(np.clip(1 - self.src_sin ** 2, 0.0, None))
        self.src_sides = np.asarray(sides, dtype=np.int64)
        self.src_area = np.asarray(area)
        self.src_point = np.asarray(point, dtype=float).reshape(-1, 3)
        self.src_is_point = np.asarray(is_point, dtype=bool)
        self.src_faces = faces
        self.flux = float(self.src_flux.sum())
        self.src_prob = self.src_flux / self.flux
        nsides = np.where(self.src_sides == 0, 2.0, 1.0)
        # radiance of surfaces W/(mm² sr), intensity of points W/sr
        self.src_radiance = np.where(self.src_is_point, self.src_flux / (4 * np.pi),
                                     self.src_flux / (nsides * self.src_area * np.pi * self.src_sin ** 2))
        self.src_dir_norm = np.where(self.src_is_point, 1 / (4 * np.pi), 1 / (nsides * np.pi * self.src_sin ** 2))

    def _emits(self, src: np.ndarray, cos: np.ndarray) -> np.ndarray:
        """Whether a surface source emits into a direction with the cosine `cos` to its normal."""
        sides = self.src_sides[src]
        return (np.abs(cos) >= self.src_cos[src]) & (np.abs(cos) > 0) & ((sides == 0) | (np.sign(cos) == sides))

    def emitted(self, src: np.ndarray, n: np.ndarray, w: np.ndarray):
        """Radiance (points: intensity) of source `src` into the direction w and the density (per
        solid angle) with which emission picks w; n: normal at the point (ignored for points)."""
        cos = np.einsum("ij,ij->i", n, w)
        pt = self.src_is_point[src]
        ok = pt | self._emits(src, cos)
        le = np.where(ok, self.src_radiance[src], 0.0)
        pdf = np.where(ok, self.src_dir_norm[src] * np.where(pt, 1.0, np.abs(cos)), 0.0)
        return le, pdf

    def emit_pdf_pos(self, src: np.ndarray) -> np.ndarray:
        """Density of picking a point of the source (per mm²; points: the probability)."""
        return self.src_prob[src] / np.where(self.src_is_point[src], 1.0, self.src_area[src])

    def emit(self, n: int, rng: np.random.Generator) -> dict:
        """n emitted rays: position p, normals ns / ng (zero for points), source, direction d, the
        densities pdf_pos and pdf_dir, and whether the ray is usable (ok)."""
        mesh = self.mesh
        src = np.minimum(np.searchsorted(np.cumsum(self.src_prob), rng.random(n), side="right"), len(self.src_prob) - 1)
        p = np.zeros((n, 3))
        ns = np.zeros((n, 3))
        ng = np.zeros((n, 3))
        d = np.zeros((n, 3))
        cos = np.ones(n)
        u1, u2, u3 = rng.random(n), rng.random(n), rng.random(n)
        for k in np.unique(src):
            sel = np.nonzero(src == k)[0]
            if self.src_is_point[k]:
                p[sel] = self.src_point[k]
                z = 1 - 2 * u1[sel]
                r = np.sqrt(np.clip(1 - z * z, 0.0, None))
                d[sel] = np.column_stack([r * np.cos(2 * np.pi * u2[sel]), r * np.sin(2 * np.pi * u2[sel]), z])
                continue
            tri, b1, b2 = mesh.sample(self.src_faces[k], len(sel), rng)
            p[sel] = mesh.v0[tri] + b1[:, None] * mesh.e1[tri] + b2[:, None] * mesh.e2[tri]
            normal, _ = mesh.shade(tri, b1, b2)
            ns[sel], ng[sel] = normal, mesh.ng[tri]
            side = float(self.src_sides[k]) if self.src_sides[k] else np.where(u3[sel] < 0.5, 1.0, -1.0)
            w, c = pt_bsdf.cone(normal * np.reshape(side, (-1, 1)), np.full(len(sel), self.src_sin[k]), u1[sel], u2[sel])
            d[sel], cos[sel] = w, c
        pt = self.src_is_point[src]
        pdf_dir = self.src_dir_norm[src] * np.where(pt, 1.0, cos)
        ok = pt | ((cos > 0) & (np.einsum("ij,ij->i", ng, d) * np.einsum("ij,ij->i", ns, d) > 0))
        return {"p": p, "ns": ns, "ng": ng, "src": src, "d": d, "pdf_pos": self.emit_pdf_pos(src),
                "pdf_dir": pdf_dir, "ok": ok, "point": pt}


# ---------------------------------------------------------------------------- sensors
class TexelSensor:
    """Irradiance on faces: every face gets a texture over its normalised (u, v); a texel collects
    the flux that arrives in it, from both sides."""
    camera = False

    def __init__(self, scene: Scene, faces, long_side: int):
        mesh = scene.mesh
        self.scene = scene
        self.faces = np.asarray([f for f in faces if mesh.face_area[f] > 0], dtype=np.int64)
        if not self.faces.size:
            raise ValueError("no face to compute the irradiance on")
        self.slot = np.full(len(mesh.face_area), -1, dtype=np.int64)
        self.slot[self.faces] = np.arange(len(self.faces))
        self.size = [mesh.texels(int(f), long_side) for f in self.faces]          # (w, h) per face
        self.w = np.asarray([s[0] for s in self.size], dtype=np.int64)
        self.h = np.asarray([s[1] for s in self.size], dtype=np.int64)
        self.first = np.concatenate([[0], np.cumsum(self.w * self.h)])
        self.count = int(self.first[-1])
        self.texel_area = np.concatenate([mesh.texel_areas(int(f), w, h).ravel()
                                          for f, (w, h) in zip(self.faces, self.size)])
        self.area = float(mesh.face_area[self.faces].sum())
        self.crossed = bool(scene.face_virtual[self.faces].any())   # light passes some of the faces

    def texel(self, tri: np.ndarray, uv: np.ndarray) -> np.ndarray:
        """Texel of points on triangles (-1: not on a sensor face)."""
        s = self.slot[self.scene.mesh.face[tri]]
        on = s >= 0
        out = np.full(len(tri), -1, dtype=np.int64)
        so = s[on]
        w, h = self.w[so], self.h[so]
        i = np.clip((uv[on, 0] * w).astype(np.int64), 0, w - 1)
        j = np.clip((uv[on, 1] * h).astype(np.int64), 0, h - 1)
        out[on] = self.first[so] + j * w + i
        return out

    def sample(self, n: int, rng: np.random.Generator) -> dict:
        """n rays from points uniform on the sensor faces, into both half-spaces by the cosine."""
        mesh = self.scene.mesh
        tri, b1, b2 = mesh.sample(self.faces, n, rng)
        p = mesh.v0[tri] + b1[:, None] * mesh.e1[tri] + b2[:, None] * mesh.e2[tri]
        ns, uv = mesh.shade(tri, b1, b2)
        ng = mesh.ng[tri]
        side = np.where(rng.random(n) < 0.5, 1.0, -1.0)
        d, cos = pt_bsdf._cosine(ns * side[:, None], rng.random(n), rng.random(n))
        ok = (cos > 0) & (np.einsum("ij,ij->i", ng, d) * np.einsum("ij,ij->i", ns, d) > 0)
        return {"p": p, "ns": ns, "ng": ng, "d": d, "texel": self.texel(tri, uv), "ok": ok,
                "pdf_pos": np.full(n, 1.0 / self.area), "pdf_dir": cos / (2 * np.pi),
                "beta": np.full(n, 2 * np.pi * self.area), "point": np.zeros(n, dtype=bool)}

    def response(self, ns: np.ndarray, w: np.ndarray):
        """Importance into the direction w from a sensor point, the density of picking w, and
        the texel it belongs to (None: the point's own)."""
        return np.ones(len(w)), np.abs(np.einsum("ij,ij->i", ns, w)) / (2 * np.pi), None

    def pdf_pos(self, n: int) -> np.ndarray:
        return np.full(n, 1.0 / self.area)


class Camera:
    """A pinhole camera at `position` looking at `target`; `fov`: full horizontal angle in
    degrees; width x height pixels. Pixel (i, j): i to the right, j upwards."""
    camera = True

    def __init__(self, position, target, up, fov: float, width: int, height: int):
        self.position = np.asarray(position, dtype=float)
        forward = np.asarray(target, dtype=float) - self.position
        if np.linalg.norm(forward) < 1e-12:
            raise ValueError("camera: position and target are the same point")
        forward /= np.linalg.norm(forward)
        right = np.cross(forward, np.asarray(up, dtype=float))
        if np.linalg.norm(right) < 1e-9:
            raise ValueError("camera: the view direction is parallel to the up direction")
        right /= np.linalg.norm(right)
        if not 0 < fov < 180:
            raise ValueError("camera: field of view must lie in 0 … 180°")
        self.forward, self.right, self.up = forward, right, np.cross(right, forward)
        self.w, self.h = int(width), int(height)
        self.tan_x = float(np.tan(np.radians(fov) / 2))
        self.tan_y = self.tan_x * self.h / self.w
        self.count = self.w * self.h
        self.film = 4 * self.tan_x * self.tan_y              # film area at distance 1
        self.area = 1.0

    def sample(self, n: int, rng: np.random.Generator) -> dict:
        x = (2 * rng.random(n) - 1) * self.tan_x
        y = (2 * rng.random(n) - 1) * self.tan_y
        d = self.forward + x[:, None] * self.right + y[:, None] * self.up
        length = np.linalg.norm(d, axis=1)
        d /= length[:, None]
        cos = 1.0 / length
        return {"p": np.broadcast_to(self.position, (n, 3)).copy(), "ns": np.zeros((n, 3)), "ng": np.zeros((n, 3)),
                "d": d, "texel": self._pixel(x, y), "ok": np.ones(n, dtype=bool), "pdf_pos": np.ones(n),
                "pdf_dir": 1.0 / (self.film * cos ** 3), "beta": np.full(n, float(self.count)),
                "point": np.ones(n, dtype=bool)}

    def _pixel(self, x: np.ndarray, y: np.ndarray) -> np.ndarray:
        i = np.floor((x / self.tan_x + 1) * 0.5 * self.w).astype(np.int64)
        j = np.floor((y / self.tan_y + 1) * 0.5 * self.h).astype(np.int64)
        return np.where((i >= 0) & (i < self.w) & (j >= 0) & (j < self.h), j * self.w + i, -1)

    def response(self, ns: np.ndarray, w: np.ndarray):
        """Importance of the camera into the direction w (the mean radiance of a pixel is the
        integral of radiance x importance over the directions), the density with which the
        camera picks w, and the pixel of w (-1: outside the image)."""
        cos = w @ self.forward
        front = cos > 1e-9
        c = np.where(front, cos, 1.0)
        pixel = np.where(front, self._pixel((w @ self.right) / c, (w @ self.up) / c), -1)
        inside = pixel >= 0
        pdf = np.where(inside, 1.0 / (self.film * c ** 3), 0.0)
        return pdf * self.count, pdf, pixel

    def pdf_pos(self, n: int) -> np.ndarray:
        return np.ones(n)

    def frustum(self, length: float) -> np.ndarray:
        """Polylines of the viewing pyramid up to `length`: (points (10, 3)) – apex to the four
        corners and the rectangle; for drawing."""
        corners = [self.position + length * (self.forward + sx * self.tan_x * self.right + sy * self.tan_y * self.up)
                   for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        return np.asarray(corners)
