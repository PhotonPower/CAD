"""Bridge Raytatouille -> NURBS (docs/plugins.md, decision D10: first in the plugin, later the
optional module ``raytatouille.nurbs``).

Every surface of a compiled system becomes a NURBS face in the .nfcad geometry format
(docs/file-format.md):

* the sag z(x, y) of the shape is interpolated by a bicubic B-spline over the bounding box of
  the aperture in the local frame of the surface; the control net is refined until the error
  at the mid-points between the samples is below ``tol`` (planes are exact, degree 1);
* parameters (u, v) are the local x and y in mm, so the aperture is an exact trim curve in the
  parameter plane: a circle / ellipse (rational quadratic) with an optional central hole; a
  rectangle needs no trim;
* the net is placed with the surface pose (rotation, translation), which keeps it exact.

Lens rims (the edge between the two surfaces of a two-surface lens with circular apertures of
the same radius) are ruled surfaces between the two rim curves.

Only NumPy and raytatouille are needed; no CAD and no nurbforge.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Any

import numpy as np

import raytatouille as rt

DEGREE = 3
SIZES = (6, 10, 18, 34, 66)  # control points per direction, tried in this order
DEFAULT_TOL = 1e-4  # mm


# ------------------------------------------------------------------ B-spline basics
def clamped_uniform_knots(n: int, degree: int, a: float, b: float) -> np.ndarray:
    inner = np.linspace(a, b, n - degree + 1)
    return np.concatenate([[a] * degree, inner, [b] * degree])


def greville(knots: np.ndarray, n: int, degree: int) -> np.ndarray:
    return np.array([knots[i + 1:i + degree + 1].mean() for i in range(n)])


def basis_matrix(knots: np.ndarray, n: int, degree: int, t: np.ndarray) -> np.ndarray:
    """B[k, i] = N_i,p(t_k) (Cox-de Boor); the last knot belongs to the last span."""
    t = np.asarray(t, dtype=float)
    m = len(knots)
    N = np.zeros((len(t), m - 1))
    for j in range(m - 1):
        if knots[j] < knots[j + 1]:
            N[:, j] = (t >= knots[j]) & (t < knots[j + 1])
    last = np.max(np.nonzero(knots[:-1] < knots[1:])[0])
    N[t >= knots[-1], last] = 1.0
    for p in range(1, degree + 1):
        nxt = np.zeros((len(t), m - 1 - p))
        for j in range(m - 1 - p):
            d0 = knots[j + p] - knots[j]
            d1 = knots[j + p + 1] - knots[j + 1]
            if d0 > 0:
                nxt[:, j] += (t - knots[j]) / d0 * N[:, j]
            if d1 > 0:
                nxt[:, j] += (knots[j + p + 1] - t) / d1 * N[:, j + 1]
        N = nxt
    return N[:, :n]


def circle_curve(cx: float, cy: float, rx: float, ry: float) -> dict:
    """Exact ellipse (circle if rx == ry) as a rational quadratic NURBS curve in a plane z = 0,
    counter-clockwise."""
    w = math.sqrt(0.5)
    unit = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1), (1, 0)]
    pts = [(cx + rx * x, cy + ry * y, 0.0) for x, y in unit]
    return {"degree": 2,
            "knots": [0, 0, 0, 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1, 1, 1],
            "points": [c for p in pts for c in p],
            "weights": [1, w, 1, w, 1, w, 1, w, 1]}


# ------------------------------------------------------------------ surfaces
@dataclass
class Face:
    geometry: dict          # .nfcad trimmed_surface
    max_error: float        # mm, interpolation error inside the aperture (0 for planes)
    half_x: float           # extent of the parameter domain (local mm)
    half_y: float


def _aperture(layout, fallback: float) -> tuple[str, float, float, float]:
    """(kind, half x, half y, inner radius)."""
    if layout.aperture is None:
        return "rectangular", fallback, fallback, 0.0
    kind, p = layout.aperture
    if kind == "circular":
        return "circular", p["radius"], p["radius"], p.get("inner_radius", 0.0)
    if kind == "elliptical":
        return "elliptical", p["semi_axis_x"], p["semi_axis_y"], 0.0
    return "rectangular", p["half_width_x"], p["half_width_y"], 0.0


def _local_sag(system, index: int, layout, x: np.ndarray, y: np.ndarray) -> np.ndarray:
    """Sag with a C1 radial continuation where the shape is not defined (corners of the box
    around a steep sphere); those points are trimmed away, they only keep the net smooth."""
    z = np.asarray(rt.layout.sag(system, index, x, y), dtype=float)
    bad = ~np.isfinite(z)
    if bad.any():
        r = np.hypot(x, y)
        good_r = r[~bad].max() if (~bad).any() else 0.0
        r0 = 0.999 * good_r
        with np.errstate(invalid="ignore", divide="ignore"):
            ux = np.where(r > 0, x / np.where(r > 0, r, 1), 1.0)
            uy = np.where(r > 0, y / np.where(r > 0, r, 1), 0.0)
        x0, y0 = ux[bad] * r0, uy[bad] * r0
        z0 = np.asarray(rt.layout.sag(system, index, x0, y0), dtype=float)
        h = 1e-4 * max(r0, 1e-3)
        z1 = np.asarray(rt.layout.sag(system, index, x0 - ux[bad] * h, y0 - uy[bad] * h), dtype=float)
        slope = (z0 - z1) / h
        z[bad] = z0 + slope * (r[bad] - r0)
    return z


STOP_RING = 1.5  # outer radius of a drawn aperture stop, relative to its opening


def surface_face(system, index: int, *, tol: float = DEFAULT_TOL, fallback_half: float = 10.0,
                 half_override: tuple[float, float] | None = None) -> Face:
    """The surface ``index`` of the compiled ``system`` as a trimmed NURBS face (global). An
    aperture stop with a circular / elliptical opening is drawn as a ring around the opening."""
    layout = rt.layout.surfaces(system)[index]
    kind, hx, hy, inner = _aperture(layout, fallback_half)
    if half_override is not None:
        kind, (hx, hy), inner = "rectangular", half_override, 0.0
    hole = None
    if layout.kind == "stop" and kind in ("circular", "elliptical") and layout.shape == "plane":
        hole, hx, hy, inner = (hx, hy), hx * STOP_RING, hy * STOP_RING, 0.0
    # domain slightly larger than the aperture so the trim curve lies inside it
    margin = 1.0 if kind == "rectangular" else 1.02
    ax, ay = hx * margin, hy * margin

    if layout.shape == "plane":
        n, degree = 2, 1
        ku = clamped_uniform_knots(2, 1, -ax, ax)
        kv = clamped_uniform_knots(2, 1, -ay, ay)
        gx, gy = np.array([-ax, ax]), np.array([-ay, ay])
        Z = np.zeros((2, 2))
        err = 0.0
    else:
        degree = DEGREE
        err = math.inf
        for n in SIZES:
            ku = clamped_uniform_knots(n, degree, -ax, ax)
            kv = clamped_uniform_knots(n, degree, -ay, ay)
            gx, gy = greville(ku, n, degree), greville(kv, n, degree)
            Bx, By = basis_matrix(ku, n, degree, gx), basis_matrix(kv, n, degree, gy)
            X, Y = np.meshgrid(gx, gy, indexing="ij")
            S = _local_sag(system, index, layout, X, Y)
            # S = Bx Z By^T  ->  Z = Bx^-1 S By^-T
            Z = np.linalg.solve(Bx, np.linalg.solve(By, S.T).T)
            # error at the mid-points, inside the aperture only
            mx = np.concatenate([gx, (gx[:-1] + gx[1:]) / 2])
            my = np.concatenate([gy, (gy[:-1] + gy[1:]) / 2])
            MX, MY = np.meshgrid(mx, my, indexing="ij")
            inside = (MX / hx) ** 2 + (MY / hy) ** 2 <= 1.0 if kind != "rectangular" else np.ones_like(MX, bool)
            approx = basis_matrix(ku, n, degree, mx) @ Z @ basis_matrix(kv, n, degree, my).T
            exact = np.asarray(rt.layout.sag(system, index, MX, MY), dtype=float)
            diff = np.abs(approx - exact)[inside & np.isfinite(exact)]
            err = float(diff.max()) if diff.size else 0.0
            if err <= tol:
                break
    # control points: (x, y) = Greville abscissae reproduce the linear parameterisation exactly
    X, Y = np.meshgrid(gx, gy, indexing="ij")
    local = np.stack([X, Y, Z], axis=-1).reshape(-1, 3)  # row-major, i along u
    world = local @ np.asarray(layout.rotation).T + np.asarray(layout.translation)
    surface = {"degree_u": degree, "degree_v": degree, "knots_u": list(map(float, ku)),
               "knots_v": list(map(float, kv)), "num_u": n, "num_v": n,
               "points": [float(c) for c in world.reshape(-1)]}
    geometry: dict[str, Any] = {"type": "trimmed_surface", "surface": surface, "label": layout.id}
    if kind in ("circular", "elliptical"):
        geometry["outer"] = [circle_curve(0.0, 0.0, hx, hy)]
        if inner > 0:
            geometry["holes"] = [[circle_curve(0.0, 0.0, inner, inner)]]
        if hole is not None:
            geometry["holes"] = [[circle_curve(0.0, 0.0, hole[0], hole[1])]]
    return Face(geometry, err, ax, ay)


def rim_faces(system, element) -> list[dict]:
    """Edges of a lens (also cemented: every pair of consecutive surfaces) whose surfaces have
    circular apertures of the same radius and are rotationally symmetric at the rim: ruled
    surfaces between the two rim circles (exact). Pairs that do not qualify get no rim."""
    layouts = rt.layout.surfaces(system)
    out = []
    for k in range(element.surface_count - 1):
        rim = _rim(system, layouts, element.first_surface + k)
        if rim is not None:
            out.append(rim)
    return out


def _rim(system, layouts, i0: int) -> dict | None:
    a, b = layouts[i0], layouts[i0 + 1]
    if a.aperture is None or b.aperture is None or a.aperture[0] != "circular" or b.aperture[0] != "circular":
        return None
    r = a.aperture[1]["radius"]
    if abs(b.aperture[1]["radius"] - r) > 1e-9 * max(r, 1.0):
        return None
    circle = circle_curve(0.0, 0.0, r, r)
    rows = []
    for idx, L in ((i0, a), (i0 + 1, b)):
        p = np.array(circle["points"], dtype=float).reshape(-1, 3)
        theta = np.linspace(0, 2 * np.pi, 16, endpoint=False)
        z = np.asarray(rt.layout.sag(system, idx, r * np.cos(theta), r * np.sin(theta)), dtype=float)
        if not np.all(np.isfinite(z)) or np.ptp(z) > 1e-9 * max(r, 1.0):
            return None  # not rotationally symmetric at the rim: no exact rim
        p[:, 2] = z[0]
        rows.append(p @ np.asarray(L.rotation).T + np.asarray(L.translation))
    pts = np.stack(rows, axis=1).reshape(-1, 3)  # u: around (9 points), v: the two surfaces
    return {"type": "surface", "degree_u": 2, "degree_v": 1, "knots_u": circle["knots"],
            "knots_v": [0.0, 0.0, 1.0, 1.0], "num_u": 9, "num_v": 2,
            "points": [float(c) for c in pts.reshape(-1)],
            "weights": [w for w in circle["weights"] for _ in range(2)]}
