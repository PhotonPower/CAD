"""Path Tracer plugin: NURBS evaluation, tessellation with trimming, ray intersection, and the
three methods against closed-form cases and against each other; then the plugin's actions with
a stand-in for CAD. Needs numpy only (skips with 77 without it)."""

import json
import math
import os
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:
    sys.exit(77)

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "python"))
sys.path.insert(0, str(root / "python" / "plugins" / "pathtracer"))

import pt_bsdf  # noqa: E402
import pt_bvh  # noqa: E402
import pt_integrator  # noqa: E402
import pt_mesh  # noqa: E402
import pt_nurbs  # noqa: E402
import pt_scene  # noqa: E402
import pt_setup  # noqa: E402


def rect(p0, du, dv):
    """A flat face: corner and two edges; the normal is du x dv."""
    p0, du, dv = (np.asarray(v, dtype=float) for v in (p0, du, dv))
    pts = [p0, p0 + dv, p0 + du, p0 + du + dv]                       # index = i_u * num_v + i_v
    return {"type": "surface", "degree_u": 1, "degree_v": 1, "knots_u": [0, 0, 1, 1], "knots_v": [0, 0, 1, 1],
            "num_u": 2, "num_v": 2, "points": [float(c) for p in pts for c in p]}


def box(lo, hi):
    lo, hi = np.asarray(lo, dtype=float), np.asarray(hi, dtype=float)
    ex, ey, ez = np.diag(hi - lo)
    faces = [rect(lo, ey, ex), rect(lo + ez, ex, ey), rect(lo, ex, ez), rect(lo + ey, ez, ex),
             rect(lo, ez, ey), rect(lo + ex, ey, ez)]                # normals outwards
    return {"type": "shell", "faces": [{"surface": f} for f in faces]}


def polyline(points):
    n = len(points)
    return {"degree": 1, "knots": [0.0] + [i / (n - 1) for i in range(n)] + [1.0],
            "points": [float(c) for p in points for c in (p[0], p[1], 0.0)]}


def sphere_patch(radius):
    """A quarter circle (rational, degree 2) revolved by a quarter turn: an eighth of a sphere."""
    w = math.sqrt(0.5)
    arc = [(1, 0), (1, 1), (0, 1)]
    pts, weights = [], []
    for (cx, cz), wu in zip(arc, (1, w, 1)):                         # u: from the equator to the pole
        for (ax, ay), wv in zip(arc, (1, w, 1)):                     # v: around the axis
            pts.append((radius * cx * ax, radius * cx * ay, radius * cz))
            weights.append(wu * wv)
    return {"type": "surface", "degree_u": 2, "degree_v": 2, "knots_u": [0, 0, 0, 1, 1, 1],
            "knots_v": [0, 0, 0, 1, 1, 1], "num_u": 3, "num_v": 3, "points": [float(c) for p in pts for c in p],
            "weights": weights}


MATERIALS = pt_bsdf.Materials.from_rows([
    {"name": "black", "kind": "absorber"},
    {"name": "white", "kind": "diffuse", "reflectance": 0.6},
    {"name": "mirror", "kind": "mirror", "reflectance": 0.9},
    {"name": "glass", "kind": "glass", "ior": 1.5},
    {"name": "glossy", "kind": "glossy", "reflectance": 0.7, "roughness": 0.3},
    {"name": "tinted", "kind": "glass", "ior": 1.5, "absorption": 0.05},
    {"name": "sheet", "kind": "diffuse", "reflectance": 0.3, "transmittance": 0.6},
    {"name": "detector", "kind": "detector"},
])
BLACK, WHITE, MIRROR, GLASS, GLOSSY, TINTED, SHEET, DETECTOR = range(8)


def scene_of(geometries, materials, sources, closed=()):
    mesh = pt_mesh.build([{"id": k + 1, "geometry": g} for k, g in enumerate(geometries)], 1e-3)
    return pt_scene.Scene(mesh, MATERIALS, np.asarray(materials), sources, closed)


SEED = int(os.environ.get("PT_TEST_SEED", "1"))                     # other seeds: how much room the bounds leave


def values(scene, sensor, method, samples, depth=6):
    return pt_integrator.render(scene, sensor, method, samples, depth, seed=SEED)["values"]


# ---- NURBS: partition of unity, a rational patch lies on its sphere, derivatives by differences
b, db = pt_nurbs.basis(3, [0, 0, 0, 0, 0.3, 0.6, 1, 1, 1, 1], np.linspace(0, 1, 11), derivative=True)
assert np.allclose(b.sum(axis=1), 1) and np.allclose(db.sum(axis=1), 0)
patch = sphere_patch(2.0)
us = np.linspace(0, 1, 9)
s, su, sv = pt_nurbs.surface_grid(patch, us, us)
assert np.allclose(np.linalg.norm(s, axis=-1), 2.0)
h = 1e-6
s1, _, _ = pt_nurbs.surface_grid(patch, us[1:-1] + h, us)
s0, _, _ = pt_nurbs.surface_grid(patch, us[1:-1] - h, us)
assert np.allclose((s1 - s0) / (2 * h), su[1:-1], atol=1e-6)
normal = np.cross(su, sv)[:-1]                                       # (not at the pole)
normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
assert np.allclose(np.abs(np.einsum("abk,abk->ab", normal, s[:-1])), 2.0)   # radial

# ---- mesh: the patch within the tolerance, its area
mesh = pt_mesh.build([{"id": 7, "geometry": patch}], 1e-3)
corners = np.concatenate([mesh.v0, mesh.v0 + mesh.e1, mesh.v0 + mesh.e2])
centres = mesh.v0 + (mesh.e1 + mesh.e2) / 3
assert np.allclose(np.linalg.norm(corners, axis=1), 2.0)
assert 0 < 2.0 - np.linalg.norm(centres, axis=1).min() < 2e-3        # chordal tolerance 1e-3
assert abs(mesh.face_area[0] - 4 * math.pi * 4 / 8) < 0.01 and mesh.entity_ids == [7]
n, uv = mesh.shade(np.arange(mesh.triangles), np.full(mesh.triangles, 1 / 3), np.full(mesh.triangles, 1 / 3))
radial = np.einsum("ij,ij->i", n, centres)                           # S_u x S_v points inwards here
assert np.allclose(np.linalg.norm(n, axis=1), 1) and (radial < -1.99).all(), (radial.min(), radial.max())
assert (uv >= 0).all() and (uv <= 1).all()

# ---- trimming: a disc with a square hole on a 100 mm square
angle = np.linspace(0, 2 * math.pi, 257)
disc = {"type": "trimmed_surface", "surface": rect((0, 0, 0), (100, 0, 0), (0, 100, 0)),
        "outer": [polyline(np.column_stack([0.5 + 0.4 * np.cos(angle), 0.5 + 0.4 * np.sin(angle)]))],
        "holes": [[polyline([(0.45, 0.45), (0.55, 0.45), (0.55, 0.55), (0.45, 0.55), (0.45, 0.45)])]]}
mesh = pt_mesh.build([{"id": 1, "geometry": disc}], 1e-3)
exact = math.pi * 40 ** 2 - 100
assert abs(mesh.face_area[0] / exact - 1) < 2e-3, mesh.face_area
texel = mesh.texel_areas(0, 20, 20)                                  # 5 mm texels: (row v, column u)
assert abs(texel.sum() - mesh.face_area[0]) < 1e-6 and texel[0, 0] == 0 and texel[10, 10] == 0
assert abs(texel[5, 10] - 25) < 1e-9 and 0 < texel[10, 1] < 25       # inside; cut by the circle
tri, b1, b2 = mesh.sample(np.array([0]), 5000, np.random.default_rng(0))
p = mesh.v0[tri] + b1[:, None] * mesh.e1[tri] + b2[:, None] * mesh.e2[tri]
r = np.hypot(p[:, 0] - 50, p[:, 1] - 50)
assert r.max() < 40.3 and not ((abs(p[:, 0] - 50) < 4.8) & (abs(p[:, 1] - 50) < 4.8)).any()
assert abs(r.mean() / 26.75 - 1) < 0.03                              # uniform on the disc: ~ 2/3 · 40

# ---- bodies of the geometry kernel (trimmed faces as CAD sends them), if nurbforge is installed
try:
    import nurbforge as nf
    from nfcad import geometry as cad_geometry
except ImportError:
    nf = None
if nf is not None:
    def shell(body):
        faces = []
        for f in body.faces:
            face = {"surface": cad_geometry.from_nurbforge(f.surface)}
            if not f.is_untrimmed:
                face["outer"] = [cad_geometry.from_nurbforge(c) for c in f.outer.curves]
                face["holes"] = [[cad_geometry.from_nurbforge(c) for c in hole.curves] for hole in f.holes]
            faces.append(face)
        return {"type": "shell", "faces": faces}

    plate = nf.boolean(nf.make_box(nf.Vec3(-20, -20, 0), nf.Vec3(40, 40, 10)),
                       nf.make_cylinder(nf.Vec3(0, 0, -5), nf.Vec3(0, 0, 1), 8.0, 20.0), nf.BooleanOp.Difference)
    for body in (nf.make_cylinder(nf.Vec3(0, 0, 0), nf.Vec3(0, 0, 1), 10.0, 30.0), plate):
        mesh = pt_mesh.build([{"id": 1, "geometry": shell(body)}], 0.01)
        assert abs(mesh.face_area.sum() / body.area() - 1) < 2e-3, (mesh.face_area.sum(), body.area())
        assert (mesh.mask_res > 0).sum() >= 2                            # trimmed faces among them

# ---- BVH against testing every triangle
rng = np.random.default_rng(3)
v0 = rng.uniform(-10, 10, (300, 3))
e1, e2 = rng.normal(0, 2, (300, 3)), rng.normal(0, 2, (300, 3))
e1[:6] *= 15                                                         # a few large ones among the small
e2[:6] *= 15
bvh = pt_bvh.BVH(v0, e1, e2)
assert bvh.count[bvh.left < 0].sum() == 300 and sorted(bvh.order) == list(range(300)) and bvh.leaf <= pt_bvh.LEAF
o = rng.uniform(-15, 15, (500, 3))
d = rng.normal(size=(500, 3))
d /= np.linalg.norm(d, axis=1, keepdims=True)
t, tri, b1, b2 = bvh.intersect(o, d)
best = np.full(500, np.inf)
for k in range(300):                                                 # Möller–Trumbore, one triangle at a time
    pv = np.cross(d, e2[k])
    det = pv @ e1[k]
    sv_ = o - v0[k]
    u = np.einsum("ij,ij->i", sv_, pv) / det
    q = np.cross(sv_, e1[k])
    v = np.einsum("ij,ij->i", d, q) / det
    tk = (q @ e2[k]) / det
    ok = (u >= 0) & (v >= 0) & (u + v <= 1) & (tk > 0)
    best = np.where(ok & (tk < best), tk, best)
assert np.array_equal(np.isfinite(best), tri >= 0) and np.allclose(t[tri >= 0], best[tri >= 0])
hit = tri >= 0
assert np.allclose(o[hit] + t[hit, None] * d[hit], v0[tri[hit]] + b1[hit, None] * e1[tri[hit]] + b2[hit, None] * e2[tri[hit]])
assert np.array_equal(bvh.occluded(o, d * 1000.0), hit)
assert not bvh.occluded(o[hit], d[hit] * (t[hit, None] * 0.999)).any()

# ---- a point source 50 mm above a black plane: E = Phi / (4 pi) · h / (h² + r²)^(3/2)
plane = rect((-100, -100, 0), (200, 0, 0), (0, 200, 0))
scene = scene_of([plane], [BLACK], [pt_scene.SourceSpec("p", 1.0, points=((0, 0, 50),))])
sensor = pt_scene.TexelSensor(scene, [0], 10)
def solid_angle(a, b, height):
    """Of the rectangle |x| < a, |y| < b seen from the point at `height` above its centre."""
    return 4 * math.atan(a * b / (height * math.sqrt(a * a + b * b + height * height)))


for method in pt_integrator.METHODS:
    flux = values(scene, sensor, method, 60_000)                     # 20 mm texels
    assert abs(flux.sum() / (solid_angle(100, 100, 50) / (4 * math.pi)) - 1) < 0.02, (method, flux.sum())
    centre = flux.reshape(10, 10)[4:6, 4:6].sum() / (solid_angle(20, 20, 50) / (4 * math.pi))
    assert abs(centre - 1) < 0.08, (method, centre)
    assert np.allclose(sensor.texel_area, 400)

# ---- detectors: the light passes them. Two between the source and a white floor measure the solid
# angles they fill (also what the floor sends back up); the floor gets what it gets without them
floor = rect((-100, -100, 0), (200, 0, 0), (0, 200, 0))
upper, lower = rect((-25, -25, 25), (50, 0, 0), (0, 50, 0)), rect((-10, -10, 10), (20, 0, 0), (0, 20, 0))
source = [pt_scene.SourceSpec("p", 1.0, points=((0, 0, 50),))]
plain = scene_of([floor], [BLACK], source)
scene = scene_of([floor, upper, lower], [BLACK, DETECTOR, DETECTOR], source)
assert scene.detectors is not None and scene.hit(np.array([[0.0, 0, 50]]), np.array([[0.0, 0, -1]]))[0][0] == 50
sensor = pt_scene.TexelSensor(scene, [0, 1, 2], 2)
assert sensor.crossed and not pt_scene.TexelSensor(plain, [0], 2).crossed
expected = [solid_angle(100, 100, 50), solid_angle(25, 25, 25), solid_angle(10, 10, 40)]
for method in pt_integrator.METHODS:
    flux = values(scene, sensor, method, 40_000).reshape(3, 4).sum(axis=1) * 4 * math.pi
    assert (np.abs(flux / expected - 1) < [0.05, 0.05, 0.12]).all(), (method, flux / expected)   # (few samples
                                                                                                # on the small one)
scene = scene_of([floor, upper], [WHITE, DETECTOR], source)          # the floor reflects 0.6: more passes
sensor = pt_scene.TexelSensor(scene, [1], 2)
reference = values(scene, sensor, "light", 80_000).sum()
assert reference * 4 * math.pi > 1.1 * expected[1]
for method in ("path", "bidir"):
    got = values(scene, sensor, method, 40_000).sum()
    assert abs(got / reference - 1) < 0.04, (method, got, reference)
only = scene_of([upper], [DETECTOR], source)                         # nothing but a detector
flux = values(only, pt_scene.TexelSensor(only, [0], 2), "light", 20_000).sum() * 4 * math.pi
assert only.bvh is None and abs(flux / expected[1] - 1) < 0.05
camera = pt_scene.Camera((0, 0, 200), (0, 0, 0), (0, 1, 0), 20.0, 2, 2)      # and a camera does not see them
seen = [values(sc, camera, "path", 4_000, 2).mean() for sc in (scene_of([floor], [WHITE], source), scene)]
assert abs(seen[1] / seen[0] - 1) < 1e-9
try:
    scene_of([floor, upper], [BLACK, DETECTOR], [pt_scene.SourceSpec("a", 1.0, entities=(1,))])
    raise AssertionError("not refused")
except ValueError as e:
    assert "detector cannot be a source" in str(e), e

# ---- a closed diffuse box with a point source inside: what arrives is Phi / (1 − rho)
scene = scene_of([box((0, 0, 0), (100, 100, 100))], [WHITE], [pt_scene.SourceSpec("p", 1.0, points=((50, 50, 50),))], (0,))
assert scene.flipped == []
sensor = pt_scene.TexelSensor(scene, range(6), 2)
for method in pt_integrator.METHODS:
    total = values(scene, sensor, method, 20_000, 40).sum()
    assert abs(total / 2.5 - 1) < 0.03, (method, total)

# ---- closed bodies are oriented: two faces of a box turned the wrong way are turned back
turned = box((0, 0, 0), (10, 20, 30))
for k in (1, 4):
    surface = turned["faces"][k]["surface"]
    surface["points"] = [float(c) for c in np.asarray(surface["points"]).reshape(2, 2, 3).transpose(1, 0, 2).ravel()]
scene = scene_of([turned], [WHITE], [pt_scene.SourceSpec("p", 1.0, points=((5, 5, 5),))], (0,))
assert scene.flipped == [1, 4]
outward = scene.mesh.v0 + (scene.mesh.e1 + scene.mesh.e2) / 3 - [5, 10, 15]
assert (np.einsum("ij,ij->i", outward, scene.mesh.ng) > 0).all()

# ---- a glass plate (n = 1.5) in a narrow beam: T = (1 − R) / (1 + R) with R = 0.04, all orders
scene = scene_of([box((-50, -50, 0), (50, 50, 10)), rect((-1, -1, -20), (2, 0, 0), (0, 2, 0)),
                  rect((-40, -40, 30), (80, 0, 0), (0, 80, 0))], [GLASS, BLACK, BLACK],
                 [pt_scene.SourceSpec("beam", 1.0, 0.5, "front", entities=(1,))], (0,))
sensor = pt_scene.TexelSensor(scene, [7], 4)
for method in ("light", "bidir"):
    total = values(scene, sensor, method, 30_000, 30).sum()
    assert abs(total - 0.96 / 1.04) < 0.01, (method, total)

# ---- the same plate absorbing 0.05 / mm: T = (1 − R)² a / (1 − R² a²) with a = exp(−0.05 · 10)
scene = scene_of([box((-50, -50, 0), (50, 50, 10)), rect((-1, -1, -20), (2, 0, 0), (0, 2, 0)),
                  rect((-40, -40, 30), (80, 0, 0), (0, 80, 0))], [TINTED, BLACK, BLACK],
                 [pt_scene.SourceSpec("beam", 1.0, 0.5, "front", entities=(1,))], (0,))
inner = math.exp(-0.5)
total = values(scene, pt_scene.TexelSensor(scene, [7], 4), "light", 30_000, 30).sum()
assert abs(total - 0.96 ** 2 * inner / (1 - 0.04 ** 2 * inner ** 2)) < 0.01, total

# ---- a diffusing sheet (reflects 0.3, lets 0.6 through) between a lamp and the floor of a closed box
scene = scene_of([box((0, 0, 0), (100, 100, 100)), rect((0.5, 0.5, 50), (99, 0, 0), (0, 99, 0)),
                  rect((40, 40, 80), (0, 20, 0), (20, 0, 0))], [WHITE, SHEET, BLACK],
                 [pt_scene.SourceSpec("a", 1.0, 90, "front", entities=(2,))], (0,))
sensor = pt_scene.TexelSensor(scene, [0, 6], 2)                      # the floor and the sheet
reference = values(scene, sensor, "light", 60_000, 10)
assert 0.2 < reference[:4].sum() < reference[4:].sum()
for method in ("path", "bidir"):
    got = values(scene, sensor, method, 30_000, 10)
    assert abs(got[:4].sum() / reference[:4].sum() - 1) < 0.04, (method, got[:4].sum(), reference[:4].sum())
    assert abs(got[4:].sum() / reference[4:].sum() - 1) < 0.04, (method, got[4:].sum(), reference[4:].sum())

# ---- an area source in a box with a glossy plate and a mirror wall: the methods agree
scene = scene_of([rect((-100, -100, 0), (200, 0, 0), (0, 200, 0)), rect((-100, -100, 0), (0, 200, 0), (0, 0, 100)),
                  rect((100, -100, 0), (0, 0, 100), (0, 200, 0)), rect((-20, -20, 90), (40, 0, 0), (0, 40, 0))],
                 [WHITE, MIRROR, GLOSSY, BLACK], [pt_scene.SourceSpec("a", 1.0, 60, "back", entities=(3,))])
sensor = pt_scene.TexelSensor(scene, [0, 2], 3)
reference = values(scene, sensor, "light", 150_000)
assert 0.8 < reference.sum() < 1.2
for method in ("path", "bidir"):
    got = values(scene, sensor, method, 60_000)
    assert abs(got.sum() / reference.sum() - 1) < 0.03, (method, got.sum(), reference.sum())
    assert np.sqrt(np.mean((got / reference - 1) ** 2)) < 0.15, method

# ---- camera: a Lambertian emitter of 1 W on 20 x 20 mm has the radiance 1 / (pi · 400) W/(mm² sr)
scene = scene_of([rect((-10, -10, 0), (20, 0, 0), (0, 20, 0))], [BLACK], [pt_scene.SourceSpec("a", 1.0, 90, "front", entities=(0,))])
camera = pt_scene.Camera((0, 0, 100), (0, 0, 0), (0, 1, 0), 5.0, 4, 4)
for method in pt_integrator.METHODS:                                 # (light tracing: few connections land in 5°)
    image = values(scene, camera, method, 20_000, 3)
    assert abs(image.mean() * math.pi * 400 - 1) < (0.15 if method == "light" else 0.03), (method, image.mean())
camera = pt_scene.Camera((0, 0, -100), (0, 0, 0), (0, 1, 0), 5.0, 4, 4)       # from behind: dark
assert values(scene, camera, "path", 2_000, 3).max() == 0

# ---- camera: a diffuse wall under a point source, L = rho · E / pi; pixel (i, j): i right, j up
wall = rect((-100, -100, 0), (200, 0, 0), (0, 200, 0))
scene = scene_of([wall], [WHITE], [pt_scene.SourceSpec("p", 1.0, points=((0, 0, 50),))])
camera = pt_scene.Camera((0, 0, 200), (0, 0, 0), (0, 1, 0), 2.0, 4, 4)
exact = 0.6 / math.pi / (4 * math.pi * 50 ** 2)
for method in ("path", "bidir"):
    image = values(scene, camera, method, 40_000, 2)
    assert abs(image.mean() / exact - 1) < 0.02, (method, image.mean() / exact)
camera = pt_scene.Camera((0, 0, 200), (0, 0, 0), (0, 1, 0), 40.0, 4, 4)       # wide, for light tracing
reference = values(scene, camera, "path", 40_000, 2)
for method in ("light", "bidir"):
    image = values(scene, camera, method, 80_000, 2)
    assert abs(image.sum() / reference.sum() - 1) < 0.06, (method, image.sum() / reference.sum())
scene = scene_of([wall], [WHITE], [pt_scene.SourceSpec("p", 1.0, points=((60, 0, 20),))])     # light at +x
camera = pt_scene.Camera((0, 0, 300), (0, 0, 0), (0, 1, 0), 30.0, 2, 2)      # up = +y: right = +x
image = values(scene, camera, "path", 20_000, 2).reshape(2, 2)
assert image[:, 1].sum() > 3 * image[:, 0].sum() and abs(image[0].sum() / image[1].sum() - 1) < 0.1

# ---- the model and the panel -> scene; the plugin's actions with a stand-in for CAD
params = {"materials": pt_setup.DEFAULT_MATERIALS, "default_material": "white", "sources": pt_setup.DEFAULT_SOURCES,
          "tolerance": 0.01}
model = [
    {"id": 11, "name": "floor", "attributes": {}, "geometry": rect((-100, -100, 0), (200, 0, 0), (0, 200, 0))},
    {"id": 12, "name": "lamp", "attributes": {"pt.source": "lamp", "pt.material": "black"},
     "geometry": rect((-10, -10, 80), (0, 20, 0), (20, 0, 0))},      # normal -z: shines down
    {"id": 13, "name": "ball", "attributes": {"pt.material": "glass"}, "geometry": box((-10, -10, 20), (10, 10, 40))},
    {"id": 14, "name": "off", "attributes": {"sim.inactive": True}, "geometry": rect((-100, -100, 60), (200, 0, 0), (0, 200, 0))},
    {"id": 15, "name": "axis", "attributes": {}, "geometry": {"type": "curve", "degree": 1, "knots": [0, 0, 1, 1],
                                                              "points": [0, 0, 0, 0, 0, 1]}},
]
cache = pt_setup.MeshCache()
scene = pt_setup.build_scene(model, params, cache)
assert scene.mesh.entity_ids == [11, 12, 13] and len(scene.mesh.face_area) == 8
assert pt_setup.build_scene(model, params, cache).mesh is scene.mesh                      # not tessellated again
assert [scene.materials.names[m] for m in scene.tri_material[scene.mesh.face_first[:3]]] == ["white", "black", "glass"]
assert pt_setup.sensor_faces(scene, [13]) == list(range(2, 8)) and len(pt_setup.sensor_faces(scene, [])) == 8
assert pt_setup.assignments(model, params) == [["floor", "surface", "(white)", ""], ["lamp", "surface", "black", "lamp"],
                                               ["ball", "shell", "glass", ""]]
for change, why in (({"default_material": "gold"}, "default material"), ({"sources": []}, "not in the table of sources"),
                    ({"materials": [{"name": "white", "kind": "wood"}]}, "unknown kind")):
    try:
        pt_setup.build_scene(model, {**params, **change})
        raise AssertionError("not refused")
    except ValueError as e:
        assert why in str(e), e
try:
    pt_setup.build_scene([model[0]], params)
    raise AssertionError("not refused")
except ValueError as e:
    assert "no object is a source" in str(e), e
sensor = pt_scene.TexelSensor(scene, pt_setup.sensor_faces(scene, [11]), 8)
out = pt_setup.irradiance(sensor, values(scene, sensor, "light", 20_000))
assert [(f["entity"], f["face"], f["w"], f["h"]) for f in out] == [(11, 0, 8, 8)]
assert 0.3 < out[0]["flux"] < 1.0 and abs(out[0]["area"] - 40_000) < 1e-6 and np.isfinite(out[0]["values"]).all()

from nfcad import UserError  # noqa: E402
from pathtracer_plugin import PathTracerPlugin  # noqa: E402


class FakeCad:
    """What the actions use of nfcad.Context."""

    def __init__(self, entities, params, selection=()):
        self.model, self.params, self.selection = entities, params, list(selection)
        self.fields, self.images, self.tables, self.line_sets, self.sets, self.logs = [], [], [], [], [], []

    def entities(self, ids=None, selection=False):
        return [e for e in self.model if not selection or e["id"] in self.selection]

    def set_attribute(self, entity, key, value):
        attributes = next(e for e in self.model if e["id"] == entity)["attributes"]
        attributes.pop(key, None) if value is None else attributes.__setitem__(key, value)

    def progress(self, value, text=""):
        assert 0 <= value <= 1

    def log(self, text):
        self.logs.append(text)

    def cancelled(self):
        return False

    def result_set(self, name):
        self.sets.append(name)
        return len(self.sets)

    def field(self, run, name, entity, data, width, height, **kw):
        assert len(data) == width * height
        self.fields.append((name, entity, kw))

    def image(self, run, name, data, width, height, **kw):
        assert len(data) == width * height
        self.images.append((name, np.asarray(data), kw))

    def table(self, run, name, columns, rows, **kw):
        assert all(len(r) == len(columns) for r in rows)
        self.tables.append((name, rows))

    def lines(self, run, name, points, starts, line_values, **kw):
        assert len(points) == len(line_values) == starts[-1]
        self.line_sets.append((name, len(starts) - 1))


plugin = PathTracerPlugin()
schema = plugin.params_schema()
json.dumps(schema)                                                   # plain JSON for the hello
assert set(plugin.actions()) == {"set_material", "set_source", "clear", "show", "irradiance", "camera_image",
                                 "show_camera", "show_paths"}
assert [c["name"] for c in next(s for s in schema if s["name"] == "materials")["columns"]][:2] == ["name", "kind"]
p = plugin.convert_params({"seed": SEED, "samples": 20_000, "texels": 8, "camera_width": 16, "camera_height": 12,
                           "camera_position": [0, -200, 120], "camera_target": [0, 0, 20], "preview_paths": 20})
assert p["method"] == "light tracing" and p["materials"][0]["name"] == "white" and p["irradiance_on"] == []
plain = [{**e, "attributes": {}} for e in model[:3]]
cad = FakeCad(plain, p, selection=[12])
assert plugin.set_source(cad) == "1 object(s): source lamp" and plain[1]["attributes"] == {"pt.source": "lamp"}
cad.selection = [13]
p["assign_material"] = "glass"
plugin.set_material(cad)
assert plain[2]["attributes"] == {"pt.material": "glass"}
p["assign_material"] = "gold"
for call, selection in ((plugin.set_material, [13]), (plugin.set_source, [])):
    cad.selection = selection
    try:
        call(cad)
        raise AssertionError("not refused")
    except UserError:
        pass
plugin.show(cad)
assert cad.tables[-1][1][1] == ["lamp", "surface", "(white)", "lamp"]
for method in pt_setup.METHODS:
    p["method"] = method
    assert plugin.irradiance(cad).startswith("Irradiance on 8 face(s)")
    assert plugin.camera_image(cad).startswith("Camera image 16 × 12")
assert len(cad.fields) == 24 and cad.fields[0][0] == "E floor" and cad.fields[2][0] == "E ball · face 1"
assert cad.fields[0][2]["unit"] == "W/m²" and cad.fields[0][2]["value_range"][1] > 0 and cad.fields[3][2]["face"] == 1
means = [img.mean() for _, img, _ in cad.images]
assert min(means) > 0 and max(means) / min(means) < 1.5, means       # the three methods see the same scene
assert plugin.show_paths(cad) == "20 light paths" and plugin.show_camera(cad) == "Camera drawn"
assert cad.line_sets == [("Light paths", 20), ("Viewing pyramid", 5)]
cad.selection = [12, 13]
plugin.clear(cad)
assert plain[1]["attributes"] == {} and plain[2]["attributes"] == {}
try:
    plugin.irradiance(cad)                                           # no source any more
    raise AssertionError("not refused")
except UserError as e:
    assert "no object is a source" in str(e)
print("ok")
