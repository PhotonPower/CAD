"""Automotive Lighting plugin: the cut-off condition and its characteristic strips, the front-view
layout, a small reflector with its light distribution, and the plugin's actions against a recorded
context. NumPy only, no CAD; exit code 77 (skipped) without NumPy."""

import json
import math
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:
    print("skipped: numpy is not installed")
    sys.exit(77)

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "python"))
sys.path.insert(0, str(root / "python" / "plugins" / "automotive_lighting"))

import al_design  # noqa: E402
import al_layout  # noqa: E402
import al_optics  # noqa: E402
import al_photometry  # noqa: E402
import al_plugin  # noqa: E402
import al_reflector  # noqa: E402
import al_spline  # noqa: E402
import al_transitions  # noqa: E402

H7 = al_optics.LAMPS["H7"]
fil = al_optics.Filament.of(H7)
cut = al_optics.Cutoff()
tan = lambda deg: math.tan(math.radians(deg))  # noqa: E731

# ---- the cut-off: both branches pass the kink; a direction on a branch lies in its plane
for branch, v_at_2 in ((al_optics.HORIZONTAL, tan(-0.57)), (al_optics.RISING, tan(-0.57) + tan(15) * tan(2))):
    m = cut.plane(branch)
    assert abs(np.linalg.norm(m) - 1) < 1e-12
    assert abs(al_optics.direction(tan(2), v_at_2) @ m) < 1e-12
    assert al_optics.direction(tan(2), v_at_2 + 0.01) @ m > 0          # above: the dark side
assert cut.above(al_optics.direction(tan(3), tan(0.0))[None])[0] < 0 < cut.above(al_optics.direction(tan(-3), 0.0)[None])[0]
left = al_optics.Cutoff(right_hand_traffic=False)
assert np.allclose(left.line(np.array([-0.05, 0.05])), cut.line(np.array([0.05, -0.05])))

# ---- the normal on the start curve: the image touches the cut-off, its centre lies at the aim
rng = np.random.default_rng(3)
P = np.column_stack([rng.uniform(-24, 30, 40), rng.uniform(-80, 80, 40), rng.uniform(-50, 50, 40)])
P = P[np.hypot(P[:, 1], P[:, 2]) > 12]
aims = rng.uniform(-0.4, 0.4, len(P))
for branch in (al_optics.HORIZONTAL, al_optics.RISING):
    m = cut.plane(branch)
    n = al_design.normal_for_aim(P, m, fil, aims)
    assert np.abs(al_design.cutoff_residual(P, n, m, fil)).max() < 1e-12
    chief = al_photometry.reflect(P / np.linalg.norm(P, axis=1, keepdims=True), n)
    assert np.abs(al_optics.screen(chief)[0] - aims).max() < 1e-12
    # brute force: no point of the coil is imaged above the plane, one touches it
    Q = fil.outline_samples(180)
    d = P[:, None, :] - Q[None, :, :]
    d /= np.linalg.norm(d, axis=2, keepdims=True)
    top = (al_photometry.reflect(d, n[:, None, :]) @ m).max(axis=1)
    assert top.max() < 1e-9 and top.min() > -2e-5, (top.min(), top.max())
    one = al_design._normal_scalar(*P[0], m, fil.axis, fil.half_length, fil.radius, float(aims[0]))
    assert np.allclose(one, n[0], atol=1e-12)

# ---- a facet: the characteristic strips keep F = 0 and form a surface with their normals
m = cut.plane(al_optics.RISING)
frame = al_design.StripFrame.of(m, tan(4), 25.0)
assert abs(math.degrees(frame.beta) + 15) < 0.5 and abs(frame.kappa) > 0.01      # strips normal to the rising line
spec = al_design.FacetSpec(m, frame, (15.0, 25.0), (tan(7), tan(1)), (13.0, 27.0, -5.0, 40.0), 0.0)
facet = al_design.build_facet(spec, fil, keep_net=True)
Pn, Nn = facet.net
assert np.abs(al_design.cutoff_residual(Pn.reshape(-1, 3), Nn.reshape(-1, 3), m, fil)).max() < 1e-6
across, along = Pn[2:, 1:-1] - Pn[:-2, 1:-1], Pn[1:-1, 2:] - Pn[1:-1, :-2]
normal = np.cross(across, along)
normal /= np.linalg.norm(normal, axis=2, keepdims=True)
normal *= np.sign(normal[..., :1])
assert np.degrees(np.arccos(np.clip(np.einsum("ijk,ijk->ij", normal, Nn[1:-1, 1:-1]), -1, 1))).max() < 0.1
pg, qg = np.meshgrid(np.linspace(13, 27, 15), np.linspace(-5, 40, 46), indexing="ij")
y, z = frame.yz(pg.ravel(), qg.ravel())
x, n = facet.evaluate(y, z)
assert np.degrees(np.abs(al_design.cutoff_residual(np.column_stack([x, y, z]), n, m, fil))).max() < 0.08
p_start = frame.pq(facet.start_curve[:, 1], facet.start_curve[:, 2])[0]      # the middle of the strip's start
k = int(np.argmin(np.abs(p_start - frame.p_of(20.0, 0.0))))                  # curve lies on the base paraboloid
assert not facet.extrapolated and abs(facet.start_curve[k, 0] - al_design.base_x(25.0, p_start[k] ** 2)) < 1e-9
# aims that converge too strongly: the strips cross, said so
try:
    al_design.build_facet(al_design.FacetSpec(cut.plane(al_optics.HORIZONTAL), al_design.StripFrame.of(
        cut.plane(al_optics.HORIZONTAL), 0.0, 25.0), (-45.0, -35.0), (tan(-25), tan(25)), (-47, -33, 10, 50), 10.0), fil)
    raise AssertionError("crossing strips not reported")
except al_design.DesignError as e:
    assert "cross" in str(e)

# ---- polygons: clipping with lines and circles
square = np.array([[-10.0, -10.0], [10.0, -10.0], [10.0, 10.0], [-10.0, 10.0]])
half = al_layout.clip(square, al_layout.Line(np.array([1.0, 0.0]), 2.0))
assert len(half) == 1 and abs(al_layout.area(half[0]) - 160.0) < 1e-9
disc = al_layout.clip(square, al_layout.Circle(np.array([10.0, 0.0]), 5.0, inside=True))
assert len(disc) == 1 and abs(al_layout.area(disc[0]) - 0.5 * math.pi * 25) < 0.05
rest = al_layout.clip(square, al_layout.Circle(np.array([10.0, 0.0]), 5.0, inside=False))
assert abs(al_layout.area(rest[0]) - (400 - 0.5 * math.pi * 25)) < 0.05
band = al_layout.clip(square, al_layout.Circle(np.array([0.0, -30.0]), 25.0, inside=False))   # cuts the lower edge twice
assert len(band) == 1 and al_layout.area(band[0]) < 400
two = al_layout.clip(np.array([[-10, 0], [-6, 0], [-6, 8], [6, 8], [6, 0], [10, 0], [10, 10], [-10, 10.0]]),
                     al_layout.Line(np.array([0.0, -1.0]), -5.0))                             # a U: two pieces
assert len(two) == 2 and abs(sum(al_layout.area(p) for p in two) - 40.0) < 1e-9
assert al_layout.clip(square, al_layout.Line(np.array([1.0, 0.0]), 20.0)) == []
ring = al_layout.chain([np.array([[0.0, 0.0], [4.0, 0.0]]), np.array([[4.0, 3.0], [4.0, 0.0]]),
                        np.array([[4.0, 3.0], [0.0, 0.0]])])
assert abs(al_layout.area(ring) - 6.0) < 1e-12

# ---- the layout fills the frame outside the lamp hole, without overlap; curved strips where aimed sideways
outline = al_layout.rounded_rectangle(120.0, 70.0, 15.0, (-5.0, 3.0))
expected = al_layout.area(outline) - math.pi * 10.0 ** 2
for rows in (al_plugin.default_zones(True), al_plugin.default_zones(False),
             [{"from": 0.0, "branch": "horizontal", "aim_right": -8.0, "aim_left": -20.0, "below": 0.0}]):
    cells = al_layout.layout(outline, 10.0, list(al_plugin.zones_of(rows)), cut, 25.0, 12.0)
    total = sum(c.area for c in cells)
    assert abs(total - expected) < 0.2, (total, expected)      # arcs are polylines
    probe = rng.uniform([-65, -32], [55, 38], (400, 2))
    hits = sum(al_layout.contains(piece, probe).astype(int) for c in cells for piece in c.pieces)
    inside = al_layout.contains(outline, probe) & (np.hypot(probe[:, 0], probe[:, 1]) > 10.0)
    assert np.array_equal(hits, inside.astype(int))
assert any(abs(c.spec.frame.kappa) > 0.1 for c in cells)

# ---- a small reflector without connecting faces: every image at the cut-off, no light above it,
# mirror symmetry of the traffic sides
def reflector(right: bool, connect: bool = False) -> al_reflector.Reflector:
    settings = al_reflector.Settings(H7, al_optics.Cutoff(right_hand_traffic=right),
                                     al_plugin.zones_of(al_plugin.default_zones(right)), strip_width=15.0,
                                     hole_radius=10.0, connect=connect)
    return al_reflector.build(settings, al_layout.rounded_rectangle(100.0, 64.0, 12.0))


r = reflector(True)
assert len(r.cells) == len(r.facets) > 10 and not r.warnings and r.transitions is None
assert all(len(c.pieces) == 1 for c in r.cells if c.strip != 0)             # one polygon per cell (the lamp hole
assert r.cutoff_error < 0.1, r.cutoff_error                                 # splits the strip through the axis)
Pm, Nm, Am, index = r.samples(2.0)
assert abs(Am.sum() / np.sum(4.0 / Nm[:, 0]) - 1) < 1e-12 and abs(len(Pm) * 4.0 / r.area - 1) < 0.03
Q = fil.outline_samples(90)
d = Pm[:, None, :] - Q[None, :, :]
d /= np.linalg.norm(d, axis=2, keepdims=True)
out = al_photometry.reflect(d, Nm[:, None, :])
assert np.degrees(np.arcsin(cut.above(out.reshape(-1, 3)).max())) < 0.1     # every image below the cut-off line
dist = al_photometry.light_distribution(r, spacing=2.0, n_axial=6, n_around=12)
assert 0 < dist.flux_beam < dist.flux_mirror < H7.flux and dist.flux_connections == 0.0
assert al_photometry.glare(dist, cut) == 0.0 and dist.intensity.max() > 10000
rows = {row[0]: row for row in al_photometry.test_points(dist, cut)}
assert rows["B50L"][3] == 0.0 and rows["zone III"][6] == "ok" and rows["50R"][3] > rows["75L"][3]
g, v_cut = al_photometry.sharpness(dist, cut)
assert g > 0.13 and abs(v_cut + 0.57) < 0.15
mirrored = reflector(False)
Pl, _, _, _ = mirrored.samples(2.0)
order = lambda p: np.lexsort((np.round(p[:, 2]), np.round(p[:, 1])))  # noqa: E731
assert np.allclose(Pm[order(Pm)], (Pl * [1, -1, 1])[order(Pl * [1, -1, 1])], atol=1e-9)

# ---- connecting faces: the profile across a border
h = 0.02
xi = (np.arange(200) - 99.5) * h
low, high = (0.5 * xi + 0.02 * xi ** 2)[None, :], (1.0 + 0.3 * xi)[None, :]
F, G = al_transitions.blend(low, high, h, 5.0, 0.3)                       # a step of 1 mm
slope = np.diff(F[0]) / h
assert slope.max() < 1.001 / tan(5.0) and slope.max() > 0.99 / tan(5.0)     # the wall has the draft angle
curvature = np.abs(np.diff(F[0], 2)) / h ** 2 / (1 + ((F[0][2:] - F[0][:-2]) / (2 * h)) ** 2) ** 1.5
assert curvature.max() < 1.3 / 0.3                                          # no edge sharper than the fillet
changed = np.abs(F - G)[0] > 1e-4
inner = np.abs(xi) < 1.5                                                    # (the window's ends feel the ball)
assert 0.1 < np.abs(xi[changed & inner]).max() < 0.4 and np.abs(F - G)[0][inner & (np.abs(xi) > 0.5)].max() < 2e-5
assert abs(np.interp(0.0, xi, F[0]) - 0.5) < 0.01                           # through the middle of the step
F, G = al_transitions.blend(low, (0.1 * xi)[None, :], h, 5.0, 0.3)          # no step: only the crease is rounded
assert 0 < np.abs(F - G)[0][inner].max() < 0.01 and np.abs(xi[(np.abs(F - G)[0] > 1e-4) & inner]).max() < 0.2
F, G = al_transitions.blend(low, low, h, 5.0, 0.3)                          # one smooth surface stays as it is
assert np.abs(F - G)[:, 20:-20].max() < 2e-5
assert al_transitions._reach(np.abs(xi)[None, :] < 0.3, 100, h) == 0.3 and \
    al_transitions._reach((np.abs(xi) < 0.2)[None, :] | (np.abs(xi - 1.0) < 0.1)[None, :], 100, h) == 0.2
for border in (al_transitions.LineBorder(np.array([3.0, -2.0]), np.array([0.6, 0.8])),
               al_transitions.CircleBorder(np.array([-120.0, 10.0]), 130.0, 1.0),
               al_transitions.CircleBorder(np.array([150.0, 0.0]), 140.0, -1.0)):
    y0, z0 = rng.uniform(-30, 30, 20), rng.uniform(-30, 30, 20)
    u, x_ = border.uxi(y0, z0)
    assert np.allclose(border.yz(u, x_), (y0, z0), atol=1e-9)
    gu, gx = border.gradients(y0, z0)
    eps = 1e-6
    assert np.allclose((np.array(border.uxi(y0 + eps, z0)) - (u, x_)) / eps, (gu[:, 0], gx[:, 0]), atol=1e-5)
    assert np.all(gu[:, 0] * gx[:, 1] - gu[:, 1] * gx[:, 0] > 0)             # (u, ξ) keeps the orientation of (y, z)

# ---- the reflector with its connecting faces: one mirror surface
rc = reflector(True, connect=True)
t = rc.transitions
assert t is not None and len(t.strip_bands) > 5 and len(t.zone_bands) == 4 and 0.5 < t.max_step < 20
parts = rc.parts()
kinds = {k: sum(p.area for p in parts if p.kind == k) for k in (al_reflector.FACET, al_reflector.STRIP_BAND,
                                                               al_reflector.ZONE_BAND)}
assert abs(sum(kinds.values()) - rc.area) < 0.05 and 0.02 < (rc.area - kinds[al_reflector.FACET]) / rc.area < 0.4
probe = rng.uniform([-50, -32], [50, 32], (1500, 2))                        # the parts tile the frame
hits = sum(al_layout.contains(poly, probe).astype(int) for p in parts for poly in p.polygons)
inside = al_layout.contains(rc.frame, probe) & (np.hypot(probe[:, 0], probe[:, 1]) > 10.0)
assert np.array_equal(hits, inside.astype(int))


def mirror_x(y, z):
    """x and |∇x| of the whole mirror at front-view points (nan outside)."""
    pts = np.column_stack([y, z])
    x, steep = np.full(len(pts), np.nan), np.zeros(len(pts))
    for part in parts:
        for poly in part.polygons:
            sel = al_layout.contains(poly, pts)
            if sel.any():
                x[sel], grad = part.height(pts[sel, 0], pts[sel, 1])
                steep[sel] = np.hypot(grad[:, 0], grad[:, 1])
    return x, steep


for start, end in (((-45.0, 20.0), (45.0, 21.0)), ((-40.0, -25.0), (40.0, -18.0)), ((-30.0, -30.0), (35.0, 30.0))):
    s_ = np.linspace(0.0, 1.0, 12001)                                       # lines across many borders
    y_, z_ = start[0] + s_ * (end[0] - start[0]), start[1] + s_ * (end[1] - start[1])
    x_, steep = mirror_x(y_, z_)
    ok = np.isfinite(x_[1:]) & np.isfinite(x_[:-1])
    ds = math.hypot(end[0] - start[0], end[1] - start[1]) / 12000
    jump = np.abs(np.diff(x_))[ok] - 1.5 * ds * np.maximum(steep[1:], steep[:-1])[ok]
    assert jump.max() < 2e-3, jump.max()                                    # continuous: no step left anywhere
_, Nb, _, ib = rc.samples(0.2, kinds=(al_reflector.STRIP_BAND,))
assert np.max(np.hypot(Nb[:, 1], Nb[:, 2]) / Nb[:, 0]) < 1.03 / tan(5.0)   # the walls keep the draft angle
_, Nz, _, _ = rc.samples(0.2, kinds=(al_reflector.ZONE_BAND,))
assert np.max(np.hypot(Nz[:, 1], Nz[:, 2]) / Nz[:, 0]) < 1.5 / tan(5.0)    # (a little less where two walls meet)
assert rc.cutoff_error < 0.1                                                # the facets between them are untouched
dist_c = al_photometry.light_distribution(rc, spacing=1.0, n_axial=6, n_around=12)
assert 0 < dist_c.flux_connections < 0.4 * dist_c.flux_beam
rows_c = {row[0]: row for row in al_photometry.test_points(dist_c, cut)}
assert rows_c["50R"][3] > 10000 and rows_c["B50L"][3] < 350                # stray light of the walls stays low
g_c, v_c = al_photometry.sharpness(dist_c, cut)
assert g_c > 0.13 and abs(v_c + 0.57) < 0.2
for bad, message in ((dict(fillet=4.0), "narrow"), (dict(hole_radius=1.0), "lamp hole")):
    try:
        al_reflector.build(al_reflector.Settings(H7, cut, al_plugin.zones_of(al_plugin.default_zones(True)),
                                                 **{"strip_width": 15.0, "hole_radius": 10.0, **bad}),
                           al_layout.rounded_rectangle(100.0, 64.0, 12.0))
        raise AssertionError(f"{bad} not refused")
    except al_design.DesignError as e:
        assert message in str(e), e

# the faces for CAD: the kernel's (u, v) are the zone's (p, q); the trim loop is the cell
faces = al_reflector.faces(r, (10.0, 20.0, 30.0))
assert len(faces) == sum(len(c.pieces) for c in r.cells)
part, face = faces[0]
i = part.cell
s = face["surface"]
assert len(s["points"]) == 3 * s["num_u"] * s["num_v"] and len(s["knots_u"]) == s["num_u"] + 4
ctrl = np.array(s["points"]).reshape(s["num_u"], s["num_v"], 3)
Bu = al_spline.basis_matrix(np.array(s["knots_u"]), s["num_u"], 3, np.array([s["knots_u"][0] + 3.0]))
Bv = al_spline.basis_matrix(np.array(s["knots_v"]), s["num_v"], 3, np.array([s["knots_v"][0] + 7.0]))
point = np.einsum("i,ijk,j->k", Bu[0], ctrl, Bv[0])
yz = r.facets[i].spec.frame.yz(s["knots_u"][0] + 3.0, s["knots_v"][0] + 7.0)
assert np.allclose(point, [r.facets[i].evaluate(*map(np.array, yz))[0] + 10.0, yz[0] + 20.0, yz[1] + 30.0], atol=1e-7)
loop = np.array(face["outer"][0]["points"]).reshape(-1, 3)
assert np.allclose(loop[0], loop[-1]) and al_layout.area(loop[:-1, :2]) > 0 and np.all(loop[:, 2] == 0)
# a connecting face along an arc: its control net gives the surface point of its own evaluation
faces_c = al_reflector.faces(rc)
assert len(faces_c) == sum(len(p.polygons) for p in parts)
for part, face in faces_c:
    if part.kind == al_reflector.FACET:
        continue
    s = face["surface"]
    loop = np.array(face["outer"][0]["points"]).reshape(-1, 3)
    assert al_layout.area(loop[:-1, :2]) > 0
    assert loop[:, 0].min() >= s["knots_u"][0] - 1e-6 and loop[:, 0].max() <= s["knots_u"][-1] + 1e-6
    assert loop[:, 1].min() >= s["knots_v"][0] - 1e-6 and loop[:, 1].max() <= s["knots_v"][-1] + 1e-6
    ctrl = np.array(s["points"]).reshape(s["num_u"], s["num_v"], 3)
    u0, v0 = loop[:-1, 0].mean(), 0.3 * loop[:-1, 1].max()
    Bu = al_spline.basis_matrix(np.array(s["knots_u"]), s["num_u"], 3, np.array([u0]))
    Bv = al_spline.basis_matrix(np.array(s["knots_v"]), s["num_v"], 3, np.array([v0]))
    point = np.einsum("i,ijk,j->k", Bu[0], ctrl, Bv[0])
    y1, z1 = point[1], point[2]
    assert np.allclose(part.parameters(np.array([y1]), np.array([z1])), ([u0], [v0]), atol=1e-5)
    assert abs(part.height(np.array([y1]), np.array([z1]))[0][0] - point[0]) < 1e-6
cyl = al_reflector.filament_surface(fil)
assert len(cyl["points"]) == 54 and len(cyl["weights"]) == 18

# a frame drawn in CAD: curves that join to the outline (a circle as rational quadratic arcs)
w = math.sqrt(0.5)
circle = {"type": "curve", "degree": 2, "knots": [0, 0, 0, 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1, 1, 1],
          "points": [c for a, b in [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1), (1, 0)]
                     for c in (5.0, 40.0 * a, 40.0 * b)], "weights": [1, w, 1, w, 1, w, 1, w, 1]}
pts = al_spline.curve_points(circle)
assert np.allclose(np.hypot(pts[:, 1], pts[:, 2]), 40.0) and np.allclose(pts[:, 0], 5.0)


# ---- the plugin's actions through nfcad's Context, CAD played by a recording host
class Host:
    """Answers the requests of nfcad.Context as CAD would, and keeps what the plugin sent."""

    def __init__(self):
        self.model, self.results, self.group_list, self.params_set = [], [], [], {}

    def send(self, message):
        pass

    def request(self, method, args):
        json.dumps(args)                                   # everything must travel as JSON
        if method == "model.entities":
            return [e for e in self.model if "ids" not in args or e["id"] in args["ids"]]
        if method == "model.add":
            self.model.append({"id": 100 + len(self.model), **args})
            return self.model[-1]["id"]
        if method == "model.remove":
            self.model = [e for e in self.model if e["id"] != args["id"]]
            return None
        if method == "model.groups":
            return self.group_list
        if method == "model.add_group":
            self.group_list.append({"id": len(self.group_list) + 1, **args})
            return len(self.group_list)
        if method == "params.set":
            self.params_set.update(args["values"])
            return None
        if method == "results.create_set":
            return 1
        if method.startswith("results."):
            kind = method.split(".")[1]
            if kind == "image":
                assert args["values"]["count"] == args["width"] * args["height"] and len(args["extent"]) == 4
            if kind == "table":
                assert all(len(row) == len(args["columns"]) for row in args["rows"])
            if kind == "curve":
                assert len(args["x"]) == len(args["y"]) > 10
            if kind == "lines":
                assert args["points"]["count"] % 3 == 0 and args["starts"]["count"] >= 2
            for value in args.values():                    # CAD reads and deletes the array files
                if isinstance(value, dict) and "file" in value:
                    Path(value["file"]).unlink()
            self.results.append(kind)
            return len(self.results)
        raise AssertionError(f"unexpected request {method}")


from nfcad import Context  # noqa: E402

plugin = al_plugin.AutomotiveLighting()
json.dumps(plugin.params_schema())
assert {"generate", "filament_images", "light_distribution", "show_strips", "set_default_zones"} <= set(plugin.actions())
host = Host()
ctx = Context(host, 1, plugin.convert_params({"frame_width": 100.0, "frame_height": 64.0, "frame_radius": 12.0,
                                              "strip_width": 15.0, "sampling": 2.0, "image_points": 40}), [])
summary = plugin.generate(ctx)
built = plugin.reflector(ctx)
facets = [e for e in host.model if e["attributes"].get("al.part") == "facet"]
connections = [e for e in host.model if e["attributes"].get("al.part") == "connection"]
assert "facets" in summary and "connecting faces" in summary
assert len(facets) == sum(len(p.polygons) for p in built.parts() if p.kind == al_reflector.FACET)
assert len(connections) == sum(len(p.polygons) for p in built.parts() if p.kind != al_reflector.FACET) > 10
assert all("al.zone" in e["attributes"] for e in facets)
assert sum(e["attributes"].get("al.part") == "filament" for e in host.model) == 1
assert all(e["group"] == 1 for e in host.model) and host.group_list[0]["name"] == "Reflector H7"
count = len(host.model)
plugin.generate(ctx)                                    # again: the last reflector is replaced
assert len(host.model) == count and len(host.group_list) == 1
plugin.filament_images(ctx)
plugin.light_distribution(ctx)
plugin.show_strips(ctx)
assert host.results == ["image", "image", "table", "curve", "curve", "lines", "lines"], host.results
# the frame from CAD curves about the filament centre
ctx.params.update({"position": (5.0, 1.0, -2.0), "frame_curves": [7]})
host.model.append({"id": 7, "name": "frame", "attributes": {}, "group": 0,
                   "geometry": {**circle, "points": [c + o for p in np.array(circle["points"]).reshape(-1, 3)
                                                     for c, o in zip(p, (0.0, 1.0, -2.0))]}})
outline = plugin.frame_of(ctx)
assert np.allclose(np.hypot(outline[:, 0], outline[:, 1]), 40.0) and al_layout.area(outline) > 0
ctx.params["connect"] = False
assert abs(plugin.reflector(ctx).area - math.pi * (40.0 ** 2 - 12.0 ** 2)) < 2.0
assert plugin.reflector(ctx).transitions is None
# left-hand traffic: the zones are mirrored, the lamp of the panel is used
ctx.params.update({"traffic": al_plugin.LEFT, "lamp": "H4 (low beam)", "frame_curves": []})
plugin.set_default_zones(ctx)
assert host.params_set["zones"] == ctx.params["zones"] == al_plugin.default_zones(False)
assert [z["branch"] for z in ctx.params["zones"]].count("rising") == 1
assert plugin.reflector(ctx).settings.lamp.window == (-15.0, 180.0)
try:
    ctx.params["zones"] = []
    plugin.reflector(ctx)
    raise AssertionError("empty zone table not refused")
except al_plugin.UserError as e:
    assert "zone" in str(e)

print("al plugin ok")
