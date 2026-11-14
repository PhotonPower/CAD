"""B-spline basics for the reflector facets: clamped uniform knots, basis functions with their
first derivative, a graph surface x = f(p, q) over a rectangle and NURBS curves as polylines.
NumPy only."""

from __future__ import annotations

import numpy as np

DEGREE = 3


def clamped_uniform_knots(n: int, degree: int, a: float, b: float) -> np.ndarray:
    inner = np.linspace(a, b, n - degree + 1)
    return np.concatenate([[a] * degree, inner, [b] * degree])


def greville(knots: np.ndarray, n: int, degree: int) -> np.ndarray:
    g = np.array([knots[i + 1:i + degree + 1].mean() for i in range(n)])
    return np.clip(g, knots[0], knots[-1])


def _basis(knots: np.ndarray, degree: int, t: np.ndarray) -> np.ndarray:
    """N[k, i] = N_i,degree(t_k) for all i (Cox-de Boor); the last knot belongs to the last span."""
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
    return N


def basis_matrix(knots: np.ndarray, n: int, degree: int, t, deriv: int = 0) -> np.ndarray:
    """B[k, i] = N_i,degree(t_k), or its first derivative with deriv = 1. Parameters outside the
    domain are clamped onto it."""
    t = np.clip(np.atleast_1d(np.asarray(t, dtype=float)), knots[0], knots[-1])
    if deriv == 0:
        return _basis(knots, degree, t)[:, :n]
    low = _basis(knots, degree - 1, t)  # N_i,degree-1 for i = 0 … n
    out = np.zeros((len(t), n))
    for i in range(n):
        d0 = knots[i + degree] - knots[i]
        d1 = knots[i + degree + 1] - knots[i + 1]
        if d0 > 0:
            out[:, i] += degree / d0 * low[:, i]
        if d1 > 0:
            out[:, i] -= degree / d1 * low[:, i + 1]
    return out


def local_basis(knots: np.ndarray, n: int, degree: int, t: np.ndarray, deriv: int = 0) -> tuple[np.ndarray, np.ndarray]:
    """The degree + 1 basis functions that do not vanish at each t (or their first derivatives):
    (first index (m,), values (m, degree + 1)) – the functions first … first + degree. t within
    the domain."""
    t = np.asarray(t, dtype=float)
    span = np.clip(np.searchsorted(knots, t, side="right") - 1, degree, n - 1)

    def values(p: int) -> np.ndarray:        # N_{span-p … span, p}
        N = np.zeros((len(t), p + 1))
        N[:, 0] = 1.0
        for j in range(1, p + 1):
            saved = np.zeros(len(t))
            for r in range(j):
                right = knots[span + r + 1] - t
                left = t - knots[span + 1 - j + r]
                temp = N[:, r] / (right + left)
                N[:, r] = saved + right * temp
                saved = left * temp
            N[:, j] = saved
        return N

    if deriv == 0:
        return span - degree, values(degree)
    low = values(degree - 1)                 # N_{span-degree+1 … span, degree-1}
    D = np.zeros((len(t), degree + 1))
    for a in range(degree + 1):
        i = span - degree + a
        if a >= 1:
            d0 = knots[i + degree] - knots[i]
            D[:, a] += degree * low[:, a - 1] / np.where(d0 > 0, d0, 1.0)
        if a <= degree - 1:
            d1 = knots[i + degree + 1] - knots[i + 1]
            D[:, a] -= degree * low[:, a] / np.where(d1 > 0, d1, 1.0)
    return span - degree, D


class Graph:
    """x = f(p, q): a bicubic B-spline over [p0, p1] x [q0, q1], interpolating values given at the
    Greville abscissae. The control points of (p, q) at the Greville abscissae make the
    parameterisation exact: (u, v) = (p, q) in mm."""

    def __init__(self, p_range: tuple[float, float], q_range: tuple[float, float], n_p: int, n_q: int):
        self.n_p, self.n_q = int(n_p), int(n_q)
        self.knots_p = clamped_uniform_knots(self.n_p, DEGREE, *p_range)
        self.knots_q = clamped_uniform_knots(self.n_q, DEGREE, *q_range)
        self.sites_p = greville(self.knots_p, self.n_p, DEGREE)
        self.sites_q = greville(self.knots_q, self.n_q, DEGREE)
        self.coeff = np.zeros((self.n_p, self.n_q))

    def interpolate(self, values: np.ndarray) -> None:
        """values[i, j] = f(sites_p[i], sites_q[j])."""
        Bp = basis_matrix(self.knots_p, self.n_p, DEGREE, self.sites_p)
        Bq = basis_matrix(self.knots_q, self.n_q, DEGREE, self.sites_q)
        self.coeff = np.linalg.solve(Bp, np.linalg.solve(Bq, np.asarray(values, dtype=float).T).T)

    def evaluate(self, p, q) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """f, ∂f/∂p, ∂f/∂q at scattered points (arrays of the same shape). Outside the rectangle
        the surface continues with the slope of its rim (C1)."""
        p = np.asarray(p, dtype=float)
        shape = p.shape
        p, q = p.reshape(-1), np.asarray(q, dtype=float).reshape(-1)
        pc = np.clip(p, self.knots_p[0], self.knots_p[-1])
        qc = np.clip(q, self.knots_q[0], self.knots_q[-1])
        ip, Bp = local_basis(self.knots_p, self.n_p, DEGREE, pc)
        iq, Bq = local_basis(self.knots_q, self.n_q, DEGREE, qc)
        _, Dp = local_basis(self.knots_p, self.n_p, DEGREE, pc, deriv=1)
        _, Dq = local_basis(self.knots_q, self.n_q, DEGREE, qc, deriv=1)
        offsets = np.arange(DEGREE + 1)
        local = self.coeff[(ip[:, None] + offsets)[:, :, None], (iq[:, None] + offsets)[:, None, :]]   # (N, 4, 4)
        along_q = np.einsum("nab,nb->na", local, Bq)
        f = np.einsum("na,na->n", along_q, Bp)
        fp = np.einsum("na,na->n", along_q, Dp)
        fq = np.einsum("nab,na,nb->n", local, Bp, Dq)
        f = f + fp * (p - pc) + fq * (q - qc)
        return f.reshape(shape), fp.reshape(shape), fq.reshape(shape)

    def evaluate_grid(self, p, q) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """f, ∂f/∂p, ∂f/∂q on the grid p x q: arrays (len(p), len(q))."""
        Bp = basis_matrix(self.knots_p, self.n_p, DEGREE, p)
        Bq = basis_matrix(self.knots_q, self.n_q, DEGREE, q)
        Dp = basis_matrix(self.knots_p, self.n_p, DEGREE, p, deriv=1)
        Dq = basis_matrix(self.knots_q, self.n_q, DEGREE, q, deriv=1)
        return Bp @ self.coeff @ Bq.T, Dp @ self.coeff @ Bq.T, Bp @ self.coeff @ Dq.T


def curve_points(geometry: dict, tolerance: float = 0.002) -> np.ndarray:
    """Points (n, 3) on a NURBS curve in the .nfcad format (docs/file-format.md), so dense that the
    polyline stays within `tolerance` of the curve; the ends of the knot spans are included (the
    corners of polylines stay)."""
    degree = int(geometry["degree"])
    knots = np.asarray(geometry["knots"], dtype=float)
    ctrl = np.asarray(geometry["points"], dtype=float).reshape(-1, 3)
    n = len(ctrl)
    weights = np.asarray(geometry.get("weights") or np.ones(n), dtype=float)
    breaks = np.unique(knots[degree:n + 1])

    def points(per_span: int) -> np.ndarray:
        t = np.concatenate([np.linspace(a, b, per_span, endpoint=False) for a, b in zip(breaks[:-1], breaks[1:])]
                           + [breaks[-1:]])
        B = _basis(knots, degree, np.clip(t, knots[0], knots[-1]))[:, :n] * weights
        return (B @ ctrl) / B.sum(axis=1, keepdims=True)

    if degree == 1:
        return points(1)
    per_span, coarse = 8, points(8)
    while per_span < 2048:
        fine = points(2 * per_span)          # every second point lies between two coarse ones
        sag = np.linalg.norm(fine[1::2] - 0.5 * (coarse[:-1] + coarse[1:]), axis=1).max()
        per_span, coarse = 2 * per_span, fine
        if sag <= 4.0 * tolerance:           # the finer polyline's error is a quarter of this
            break
    return coarse
