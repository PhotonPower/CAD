"""NURBS curves and surfaces of the .nfcad format (docs/file-format.md), evaluated with NumPy:
points and first derivatives on a grid of parameters. No CAD, no nurbforge here."""

from __future__ import annotations

import numpy as np


def basis(degree: int, knots, u, derivative: bool = False):
    """B-spline basis functions at the parameters `u`: a dense matrix (len(u), n) with
    n = len(knots) - degree - 1, and its derivative with `derivative`."""
    k = np.asarray(knots, dtype=float)
    u = np.atleast_1d(np.asarray(u, dtype=float))
    n = len(k) - degree - 1
    if n < 1:
        raise ValueError("knot vector too short for the degree")
    span = np.clip(np.searchsorted(k, u, side="right") - 1, degree, n - 1)
    m = len(u)
    cur = np.zeros((m, len(k) - 1))               # degree 0: one per knot span
    cur[np.arange(m), span] = 1.0
    prev = cur
    for p in range(1, degree + 1):
        prev = cur
        cols = len(k) - 1 - p
        j = np.arange(cols)
        d1 = k[j + p] - k[j]
        d2 = k[j + p + 1] - k[j + 1]
        a = np.divide(1.0, d1, out=np.zeros(cols), where=d1 > 0)
        b = np.divide(1.0, d2, out=np.zeros(cols), where=d2 > 0)
        cur = (u[:, None] - k[j]) * a * prev[:, :cols] + (k[j + p + 1] - u[:, None]) * b * prev[:, 1:cols + 1]
        if derivative and p == degree:
            return cur, degree * (a * prev[:, :cols] - b * prev[:, 1:cols + 1])
    if derivative:                                # degree 0
        return cur[:, :n], np.zeros((m, n))
    return cur[:, :n]


def _points(g: dict, count: int) -> tuple[np.ndarray, np.ndarray]:
    p = np.asarray(g["points"], dtype=float).reshape(-1, 3)
    w = np.asarray(g.get("weights") or [], dtype=float)
    if w.size == 0:
        w = np.ones(len(p))
    if len(p) != count or len(w) != count:
        raise ValueError("control points do not match the knot vectors")
    return p, w


def curve_points(g: dict, u) -> np.ndarray:
    """Points (len(u), 3) of a curve dict at the parameters u."""
    degree = int(g["degree"])
    n = len(g["knots"]) - degree - 1
    p, w = _points(g, n)
    b = basis(degree, g["knots"], u)
    return (b @ (p * w[:, None])) / (b @ w)[:, None]


def curve_domain(g: dict) -> tuple[float, float]:
    d = int(g["degree"])
    return float(g["knots"][d]), float(g["knots"][-d - 1])


def surface_domain(g: dict) -> tuple[float, float, float, float]:
    """(u0, u1, v0, v1) of a surface dict."""
    du, dv = int(g["degree_u"]), int(g["degree_v"])
    return (float(g["knots_u"][du]), float(g["knots_u"][-du - 1]),
            float(g["knots_v"][dv]), float(g["knots_v"][-dv - 1]))


def surface_grid(g: dict, us, vs):
    """Points and first derivatives of a surface dict on the grid us x vs:
    S, S_u, S_v with shape (len(us), len(vs), 3). Control points: index = i_u * num_v + i_v."""
    nu, nv = int(g["num_u"]), int(g["num_v"])
    p, w = _points(g, nu * nv)
    pw = np.concatenate([p * w[:, None], w[:, None]], axis=1).reshape(nu, nv, 4)
    bu, dbu = basis(int(g["degree_u"]), g["knots_u"], us, derivative=True)
    bv, dbv = basis(int(g["degree_v"]), g["knots_v"], vs, derivative=True)
    if bu.shape[1] != nu or bv.shape[1] != nv:
        raise ValueError("control points do not match the knot vectors")
    a = np.einsum("ai,bj,ijk->abk", bu, bv, pw)
    au = np.einsum("ai,bj,ijk->abk", dbu, bv, pw)
    av = np.einsum("ai,bj,ijk->abk", bu, dbv, pw)
    wgt = a[..., 3:4]
    s = a[..., :3] / wgt
    su = (au[..., :3] - au[..., 3:4] * s) / wgt
    sv = (av[..., :3] - av[..., 3:4] * s) / wgt
    return s, su, sv


def distinct_knots(degree: int, knots) -> np.ndarray:
    """The distinct knot values of the domain (span borders)."""
    k = np.asarray(knots, dtype=float)
    return np.unique(k[degree:len(k) - degree])
