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

# ---- a small reflector: every image at the cut-off, no light above it, mirror symmetry of the traffic sides
def reflector(right: bool) -> al_reflector.Reflector:
    settings = al_reflector.Settings(H7, al_optics.Cutoff(right_hand_traffic=right),
                                     al_plugin.zones_of(al_plugin.default_zones(right)), strip_width=15.0,
                                     hole_radius=10.0)
    return al_reflector.build(settings, al_layout.rounded_rectangle(100.0, 64.0, 12.0))


r = reflector(True)
assert len(r.cells) == len(r.facets) > 10 and not r.warnings
assert r.cutoff_error < 0.1, r.cutoff_error
Pm, Nm, Am, index = r.samples(2.0)
assert abs(Am.sum() / np.sum(4.0 / Nm[:, 0]) - 1) < 1e-12 and abs(len(Pm) * 4.0 / r.area - 1) < 0.03
Q = fil.outline_samples(90)
d = Pm[:, None, :] - Q[None, :, :]
d /= np.linalg.norm(d, axis=2, keepdims=True)
out = al_photometry.reflect(d, Nm[:, None, :])
assert np.degrees(np.arcsin(cut.above(out.reshape(-1, 3)).max())) < 0.1     # every image below the cut-off line
dist = al_photometry.light_distribution(r, spacing=2.0, n_axial=6, n_around=12)
assert 0 < dist.flux_beam < dist.flux_mirror < H7.flux
assert al_photometry.glare(dist, cut) == 0.0 and dist.intensity.max() > 10000
rows = {row[0]: row for row in al_photometry.test_points(dist, cut)}
assert rows["B50L"][3] == 0.0 and rows["zone III"][6] == "ok" and rows["50R"][3] > rows["75L"][3]
g, v_cut = al_photometry.sharpness(dist, cut)
assert g > 0.13 and abs(v_cut + 0.57) < 0.15
mirrored = reflector(False)
Pl, _, _, _ = mirrored.samples(2.0)
order = lambda p: np.lexsort((np.round(p[:, 2]), np.round(p[:, 1])))  # noqa: E731
assert np.allclose(Pm[order(Pm)], (Pl * [1, -1, 1])[order(Pl * [1, -1, 1])], atol=1e-9)

# the faces for CAD: the kernel's (u, v) are the zone's (p, q); the trim loop is the cell
faces = al_reflector.faces(r, (10.0, 20.0, 30.0))
assert len(faces) == sum(len(c.pieces) for c in r.cells)
i, face = faces[0]
s = face["surface"]
assert len(s["points"]) == 3 * s["num_u"] * s["num_v"] and len(s["knots_u"]) == s["num_u"] + 4
ctrl = np.array(s["points"]).reshape(s["num_u"], s["num_v"], 3)
Bu = al_spline.basis_matrix(np.array(s["knots_u"]), s["num_u"], 3, np.array([s["knots_u"][0] + 3.0]))
Bv = al_spline.basis_matrix(np.array(s["knots_v"]), s["num_v"], 3, np.array([s["knots_v"][0] + 7.0]))
point = np.einsum("i,ijk,j->k", Bu[0], ctrl, Bv[0])
yz = r.facets[i].spec.frame.yz(s["knots_u"][0] + 3.0, s["knots_v"][0] + 7.0)
assert np.allclose(point, [r.facets[i].evaluate(*map(np.array, yz))[0] + 10.0, yz[0] + 20.0, yz[1] + 30.0], atol=1e-9)
loop = np.array(face["outer"][0]["points"]).reshape(-1, 3)
assert np.allclose(loop[0], loop[-1]) and al_layout.area(loop[:-1, :2]) > 0 and np.all(loop[:, 2] == 0)
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
facets = [e for e in host.model if e["attributes"].get("al.part") == "facet"]
assert "facets" in summary and len(facets) == sum(len(c.pieces) for c in plugin.reflector(ctx).cells)
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
assert abs(plugin.reflector(ctx).area - math.pi * (40.0 ** 2 - 12.0 ** 2)) < 2.0
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
