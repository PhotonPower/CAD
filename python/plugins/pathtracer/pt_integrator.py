"""Light transport: light tracing, path tracing and bidirectional path tracing as one estimator.

A light path x0 … x(n-1) runs from a source (x0) to the sensor (x(n-1)). It can be sampled in
n + 1 ways: s vertices from the source ("light subpath") and t = n - s from the sensor ("eye
subpath"), joined by a connection (a visibility test) – or, for s = 0 / t = 0, an eye subpath
that happens to hit a source / a light subpath that happens to hit a sensor face. The methods
differ only in the set of (s, t) they use:

  light   light tracing: t = 0 for irradiance on faces (particles deposit their flux where they
          land), t = 1 for a camera (every vertex of a light path is connected to the camera)
  path    path tracing from the sensor: s = 0 (the path hits a source) and s = 1 (connection to a
          point of a source, "next event estimation")
  bidir   bidirectional path tracing: all of them

When several ways can produce the same path, their results are combined with weights (multiple
importance sampling, balance heuristic) that add up to one – so every method estimates the
same quantity, and they agree within noise wherever a method can sample all paths. (Path
tracing cannot find light that reaches a point source through mirrors or glass.)

Sampled quantities: for a TexelSensor the flux per texel (W), for a Camera the mean radiance per
pixel (W/(mm² sr)). NumPy only."""

from __future__ import annotations

import numpy as np

import pt_bsdf

METHODS = ("light", "path", "bidir")


def enabled(method: str, camera: bool, s: int, t: int) -> bool:
    """Whether the method uses the way with s light and t eye vertices."""
    if s + t < 2 or (camera and t == 0):
        return False
    if method == "light":
        return t == (1 if camera else 0)
    if method == "path":
        return s <= 1 and t >= (2 if camera else 1)
    return True


class Path:
    """Vertices of n subpaths, arrays [vertex, path]. Vertex 0 is the point on the source / the
    sensor. beta: weight of the subpath on arrival; pf: density (per area) with which the subpath
    made the vertex; pr: density with which the opposite direction would make it (known once the
    next vertex has scattered); wo: unit direction to the previous vertex."""

    def __init__(self, k: int, n: int):
        self.p = np.zeros((k, n, 3))
        self.ns = np.zeros((k, n, 3))
        self.ng = np.zeros((k, n, 3))
        self.wo = np.zeros((k, n, 3))
        self.m = np.zeros((k, n), dtype=np.int64)
        self.tex = np.full((k, n), -1, dtype=np.int64)
        self.src = np.full((k, n), -1, dtype=np.int64)
        self.beta = np.zeros((k, n))
        self.pf = np.zeros((k, n))
        self.pr = np.zeros((k, n))
        self.spec = np.zeros((k, n), dtype=bool)
        self.valid = np.zeros((k, n), dtype=bool)
        self.point = np.zeros(n, dtype=bool)                 # vertex 0 is a point (no surface)
        self.escape = np.full(n, -1, dtype=np.int64)         # vertex whose ray left the scene
        self.escape_dir = np.zeros((n, 3))
        self.rays = 0

    def cos(self, k: int, j: np.ndarray, w: np.ndarray) -> np.ndarray:
        """|cos| between the normal of vertex k and w; 1 for points."""
        c = np.abs(np.einsum("ij,ij->i", self.ns[k, j], w))
        return np.where(self.point[j], 1.0, c) if k == 0 else c


def _dot(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.einsum("ij,ij->i", a, b)


def _walk(scene, sensor, path: Path, start: dict, beta0: np.ndarray, rng, radiance: bool) -> None:
    """Follows the rays leaving vertex 0 through the scene: vertices 1 … of the subpaths."""
    k_max, n = path.valid.shape
    mesh, mats = scene.mesh, scene.materials
    alive = start["ok"].copy()
    d = start["d"].copy()
    beta = beta0.copy()
    pdf = start["pdf_dir"].copy()
    for k in range(1, k_max):
        idx = np.nonzero(alive)[0]
        if not idx.size:
            break
        dk = d[idx]
        o = scene.offset(path.p[k - 1, idx], path.ng[k - 1, idx], dk)
        t, tri, b1, b2 = scene.hit(o, dk)
        path.rays += len(idx)
        got = tri >= 0
        lost = idx[~got]
        alive[lost] = False
        path.escape[lost] = k - 1
        path.escape_dir[lost] = dk[~got]
        idx, dk, o, t, tri, b1, b2 = idx[got], dk[got], o[got], t[got], tri[got], b1[got], b2[got]
        if not idx.size:
            break
        ns, uv = mesh.shade(tri, b1, b2)
        ng = mesh.ng[tri]
        cs = -_dot(ns, dk)
        good = cs * -_dot(ng, dk) > 0                        # same side for both normals
        alive[idx[~good]] = False
        idx, dk, o, t, tri, ns, ng, uv, cs = (a[good] for a in (idx, dk, o, t, tri, ns, ng, uv, cs))
        if not idx.size:
            break
        p = o + t[:, None] * dk
        dist2 = np.sum((p - path.p[k - 1, idx]) ** 2, axis=1)
        m = scene.tri_material[tri]
        b = beta[idx]
        inside = (mats.kind[m] == pt_bsdf.GLASS) & (cs < 0) & (mats.absorption[m] > 0)
        if inside.any():                                     # the segment ran through absorbing glass
            b = np.where(inside, b * np.exp(-mats.absorption[m] * np.sqrt(dist2)), b)
        path.p[k, idx], path.ns[k, idx], path.ng[k, idx], path.wo[k, idx] = p, ns, ng, -dk
        path.m[k, idx] = m
        if sensor is not None and not sensor.camera:
            path.tex[k, idx] = sensor.texel(tri, uv)
        path.src[k, idx] = scene.face_source[mesh.face[tri]]
        path.beta[k, idx] = b
        path.pf[k, idx] = pdf[idx] * np.abs(cs) / dist2
        spec = mats.specular(m)
        path.spec[k, idx] = spec
        path.valid[k, idx] = True
        if k == k_max - 1:
            break
        wi, weight, pd = mats.sample(m, ns, -dk, rng, radiance)
        _, _, p_rev = mats.eval(m, ns, -dk, wi)
        path.pr[k - 1, idx] = p_rev * path.cos(k - 1, idx, dk) / dist2
        ok = (weight > 0) & (_dot(ns, wi) * _dot(ng, wi) > 0)
        if k >= 3:                                           # Russian roulette on dim paths
            q = np.where(spec, 1.0, np.clip(weight, 0.1, 1.0))
            ok &= rng.random(len(idx)) < q
            weight = weight / q
        beta[idx] = b * weight
        d[idx] = wi
        pdf[idx] = pd
        alive[idx] = ok


def light_subpaths(scene, sensor, n: int, k: int, rng) -> Path:
    """n subpaths from the sources with up to k vertices."""
    path = Path(max(k, 1), n)
    e = scene.emit(n, rng)
    path.p[0], path.ns[0], path.ng[0], path.src[0] = e["p"], e["ns"], e["ng"], e["src"]
    path.point = e["point"]
    path.beta[0] = 1.0 / e["pdf_pos"]
    path.pf[0] = e["pdf_pos"]
    path.valid[0] = True
    _walk(scene, sensor, path, e, np.full(n, scene.flux), rng, radiance=False)
    return path


def eye_subpaths(scene, sensor, n: int, k: int, rng) -> Path:
    """n subpaths from the sensor with up to k vertices."""
    path = Path(max(k, 1), n)
    e = sensor.sample(n, rng)
    path.p[0], path.ns[0], path.ng[0], path.tex[0] = e["p"], e["ns"], e["ng"], e["texel"]
    path.point = e["point"]
    path.beta[0] = 1.0 / e["pdf_pos"]
    path.pf[0] = e["pdf_pos"]
    path.valid[0] = True
    _walk(scene, sensor, path, e, e["beta"], rng, radiance=True)
    return path


def _strategy(scene, sensor, light: Path, eye: Path | None, s: int, t: int, method: str, accum: np.ndarray) -> int:
    """Adds what the way (s, t) contributes for every pair of subpaths; returns the number of
    visibility rays."""
    n = s + t
    mats = scene.materials
    camera = sensor.camera
    valid = np.ones(light.valid.shape[1], dtype=bool)
    if s:
        valid &= light.valid[s - 1]
    if t:
        valid &= eye.valid[t - 1]
    if s == 0:
        valid &= eye.src[t - 1] >= 0
    if t == 0:
        valid &= light.tex[s - 1] >= 0
    j = np.nonzero(valid)[0]
    if not j.size:
        return 0
    rays = 0
    con: dict = {}
    if s == 0:                                               # the eye subpath hit a source
        le, _ = scene.emitted(eye.src[t - 1, j], eye.ns[t - 1, j], eye.wo[t - 1, j])
        c = eye.beta[t - 1, j] * le
        tex = eye.tex[0, j]
    elif t == 0:                                             # the light subpath hit a sensor face
        c = light.beta[s - 1, j].copy()
        tex = light.tex[s - 1, j]
    else:                                                    # connection y -> z
        y, z = light.p[s - 1, j], eye.p[t - 1, j]
        w = z - y
        dist2 = np.sum(w * w, axis=1)
        near = dist2 > 1e-24
        w = w / np.sqrt(np.where(near, dist2, 1.0))[:, None]
        if s == 1:
            fy, dens_y = scene.emitted(light.src[0, j], light.ns[0, j], w)
            dens_y_rev = None
        else:
            fy, dens_y, dens_y_rev = mats.eval(light.m[s - 1, j], light.ns[s - 1, j], light.wo[s - 1, j], w)
        if t == 1:
            fz, dens_z, pixel = sensor.response(eye.ns[0, j], -w)
            tex = eye.tex[0, j] if pixel is None else pixel
            dens_z_rev = None
        else:
            fz, dens_z, dens_z_rev = mats.eval(eye.m[t - 1, j], eye.ns[t - 1, j], eye.wo[t - 1, j], -w)
            tex = eye.tex[0, j]
        cy, cz = light.cos(s - 1, j, w), eye.cos(t - 1, j, w)
        c = light.beta[s - 1, j] * fy * cy * cz / np.where(near, dist2, 1.0) * fz * eye.beta[t - 1, j]
        # both normals of a surface vertex must see the connection on the same side
        side_y = _dot(light.ns[s - 1, j], w) * _dot(light.ng[s - 1, j], w) > 0
        side_z = _dot(eye.ns[t - 1, j], w) * _dot(eye.ng[t - 1, j], w) > 0
        if s == 1:
            side_y |= light.point[j]
        if t == 1:
            side_z |= eye.point[j]
        c = np.where(near & side_y & side_z & (tex >= 0), c, 0.0)
        if camera and n == 2:                                # a point source has no image
            c = np.where(light.point[j], 0.0, c)
        k = np.nonzero(c > 0)[0]
        if k.size:
            o = scene.offset(y[k], light.ng[s - 1, j[k]], w[k])
            target = scene.offset(z[k], eye.ng[t - 1, j[k]], -w[k])
            c[k[scene.occluded(o, target)]] = 0.0
            rays = len(k)
        con = {"w": w, "dist2": dist2, "dens_y": dens_y, "dens_z": dens_z, "dens_y_rev": dens_y_rev,
               "dens_z_rev": dens_z_rev, "cy": cy, "cz": cz}
    keep = np.nonzero((c > 0) & (tex >= 0) & np.isfinite(c))[0]
    if not keep.size:
        return rays
    j, c, tex = j[keep], c[keep], tex[keep]
    con = {key: (v[keep] if v is not None else None) for key, v in con.items()}

    ways = [sp for sp in range(n + 1) if enabled(method, camera, sp, n - sp)]
    if ways != [s]:
        c = c * _weight(scene, sensor, light, eye, s, t, j, con, ways)
    accum += np.bincount(tex, weights=c, minlength=len(accum))
    return rays


def _weight(scene, sensor, light: Path, eye: Path | None, s: int, t: int, j: np.ndarray, con: dict,
            ways: list[int]) -> np.ndarray:
    """Balance-heuristic weight of the way (s, t) among `ways` (numbers of light vertices) for
    the full paths j: 1 / sum over the ways of (their density / this way's density)."""
    n = s + t
    count = len(j)
    pf: list = [None] * n                                    # density of x_i when made from the source side
    pr: list = [None] * n                                    # … from the sensor side
    for i in range(n):
        if i < s:
            pf[i] = light.pf[i, j]
        else:
            pr[i] = eye.pf[n - 1 - i, j]
    for i in range(0, s - 2):
        pr[i] = light.pr[i, j]
    for i in range(s + 2, n):
        pf[i] = eye.pr[n - 1 - i, j]

    def gap2(path: Path, k: int) -> np.ndarray:
        return np.sum((path.p[k, j] - path.p[k - 1, j]) ** 2, axis=1)

    if s >= 1 and t >= 1:
        pf[s] = con["dens_y"] * con["cz"] / con["dist2"]
        pr[s - 1] = con["dens_z"] * con["cy"] / con["dist2"]
        if s >= 2:
            pr[s - 2] = con["dens_y_rev"] * light.cos(s - 2, j, light.wo[s - 1, j]) / gap2(light, s - 1)
        if t >= 2:
            pf[s + 1] = con["dens_z_rev"] * eye.cos(t - 2, j, eye.wo[t - 1, j]) / gap2(eye, t - 1)
    elif s == 0:
        src = eye.src[t - 1, j]
        pf[0] = scene.emit_pdf_pos(src)
        _, dens = scene.emitted(src, eye.ns[t - 1, j], eye.wo[t - 1, j])
        pf[1] = dens * eye.cos(t - 2, j, eye.wo[t - 1, j]) / gap2(eye, t - 1)
    else:
        pr[n - 1] = sensor.pdf_pos(count)
        _, dens, _ = sensor.response(light.ns[s - 1, j], light.wo[s - 1, j])
        pr[n - 2] = dens * light.cos(s - 2, j, light.wo[s - 1, j]) / gap2(light, s - 1)

    # specular vertices: their directions are certain – the densities cancel, and no connection
    # can end on them
    spec = [np.zeros(count, dtype=bool)] * n
    for i in range(1, n - 1):
        spec[i] = light.spec[i, j] if i < s else eye.spec[n - 1 - i, j]
        if i + 1 < n:
            pf[i + 1] = np.where(spec[i], 1.0, pf[i + 1])
        pr[i - 1] = np.where(spec[i], 1.0, pr[i - 1])
    light_point = light.point[j] if s >= 1 else np.zeros(count, dtype=bool)

    def possible(sp: int):
        if sp == 0:
            return ~light_point
        if sp == n:
            return np.ones(count, dtype=bool)
        ok = ~spec[sp - 1] & ~spec[sp]
        if sensor.camera and n == 2:
            ok = ok & ~light_point
        return ok

    total = np.ones(count)
    with np.errstate(divide="ignore", invalid="ignore", over="ignore"):
        r = np.ones(count)
        for sp in range(s + 1, n + 1):
            r = np.nan_to_num(r * pf[sp - 1] / pr[sp - 1], nan=0.0, posinf=1e300)
            if sp in ways:
                total += np.where(possible(sp), r, 0.0)
        r = np.ones(count)
        for sp in range(s - 1, -1, -1):
            r = np.nan_to_num(r * pr[sp] / pf[sp], nan=0.0, posinf=1e300)
            if sp in ways:
                total += np.where(possible(sp), r, 0.0)
    return 1.0 / total


def lengths(method: str, camera: bool, depth: int) -> tuple[int, int]:
    """Most vertices of the light and the eye subpaths for paths of up to `depth` segments."""
    if method == "light":
        return (depth if camera else depth + 1), (1 if camera else 0)
    if method == "path":
        return 1, depth + 1
    return (depth if camera else depth + 1), depth + 1


def batch_size(k_light: int, k_eye: int) -> int:
    """Subpaths per batch: large batches trace faster per ray, but every vertex of every subpath
    is kept (about 170 bytes) – this aims at some 100 MB."""
    return int(np.clip(100e6 / (170 * (k_light + k_eye + 1)), 10_000, 100_000))


def render(scene, sensor, method: str, samples: int, depth: int, *, seed: int = 0, batch: int | None = None,
           progress=None, cancelled=None) -> dict:
    """Estimates the sensor's values with `samples` pairs of subpaths; paths have up to `depth`
    segments (source – … – sensor). Returns {"values": per texel the flux / W or per pixel the
    radiance / W/(mm² sr), "samples": done, "rays": traced}. progress(fraction) and cancelled()
    are called between batches; a cancelled run returns what it has."""
    if method not in METHODS:
        raise ValueError(f"unknown method {method!r}")
    if depth < 1:
        raise ValueError("paths need at least one segment")
    rng = np.random.default_rng(seed)
    k_light, k_eye = lengths(method, sensor.camera, depth)
    if batch is None:
        batch = batch_size(k_light, k_eye)
    accum = np.zeros(sensor.count)
    done = rays = 0
    while done < samples:
        n = min(batch, samples - done)
        light = light_subpaths(scene, sensor, n, k_light, rng)
        eye = eye_subpaths(scene, sensor, n, k_eye, rng) if k_eye else None
        rays += light.rays + (eye.rays if eye is not None else 0)
        for s in range(0, k_light + 1):
            for t in range(0, k_eye + 1):
                if 2 <= s + t <= depth + 1 and enabled(method, sensor.camera, s, t):
                    rays += _strategy(scene, sensor, light, eye, s, t, method, accum)
        done += n
        if progress is not None:
            progress(done / samples)
        if cancelled is not None and cancelled():
            break
    return {"values": accum / max(done, 1), "samples": done, "rays": rays}


def light_paths(scene, n: int, depth: int, seed: int = 0):
    """n light paths as polylines for drawing: (points (m, 3), starts (n + 1,)). A path that
    leaves the scene ends a tenth of the scene size further on."""
    path = light_subpaths(scene, None, n, depth + 1, np.random.default_rng(seed))
    points, starts = [], [0]
    for i in range(n):
        k = int(path.valid[:, i].sum())
        pts = [path.p[v, i] for v in range(k)]
        if path.escape[i] == k - 1:
            pts.append(pts[-1] + path.escape_dir[i] * 0.1 * scene.size)
        if len(pts) >= 2:
            points.extend(pts)
            starts.append(len(points))
    return np.asarray(points, dtype=float).reshape(-1, 3), np.asarray(starts, dtype=np.int64)
