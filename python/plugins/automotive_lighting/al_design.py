"""The mirror strips of a clear-lens headlamp: surfaces whose filament images touch the cut-off.

**The condition.** A mirror point P with unit normal n sends the light of the filament point Q
into d = R_n (P - Q) / |P - Q| (R_n: the reflection). The filament image of P – all these
directions – lies below a branch of the cut-off (plane m · d = 0, al_optics) and touches it when

    F(P, n) = m' · (C - P) - σ(m') = 0,      m' = R_n m = m - 2 (m · n) n,

i.e. when the cut-off plane through P, mirrored at the facet, touches the filament (σ: the support
function of the cylinder about its centre C, here the origin). F = 0 is a first-order partial
differential equation for the mirror surface. Its characteristic strips – curves on the mirror with
the normals along them – obey the ordinary differential equations

    dP/ds = b = -2 [(g · n) m_t + (m · n) g_t],      dn/ds = m_t,

with g = Q* - P towards the filament point Q* that touches, and m_t, g_t the parts of m and g in
the tangent plane (b = ∂F/∂n, m_t = -∂F/∂P, both tangential). Along such a curve the facet may
tilt about the curve's tangent without lifting the image off the cut-off: the image only slides
along the line.

**A facet** is the family of characteristic strips through a start curve T across the strip. On T
the normal is fully prescribed: the image touches the cut-off (F = 0) and its centre – the image of
the filament centre – lies at the horizontal position `aim`, which runs linearly across the strip
(the spread). T is the solution of dx/dp = ∂f/∂p with that normal, in the section q = q0; its
middle lies on the base paraboloid. Everywhere on the facet the image touches the cut-off exactly;
the aim holds on T and drifts with the distance from it.

Front view: (y, z) about the lamp axis; every zone has its own rotated frame (p, q) with q along
its strips. The facet is delivered as a graph x = f(p, q), a bicubic B-spline (al_spline.Graph).
NumPy only."""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

from al_optics import Filament
from al_spline import Graph


MIN_ALONG = 0.5        # the strips of a facet keep at least this cosine (60°) to the direction of its strip


class DesignError(ValueError):
    """The facet cannot be built with these settings (the message says what to change)."""


# ------------------------------------------------------------------ the normal on the start curve
def normal_for_aim(P: np.ndarray, m: np.ndarray, fil: Filament, aim) -> np.ndarray:
    """Unit normals (n, 3) at the points P (n, 3) for which the filament image touches the plane m
    from below and the image of the filament centre lies at h = aim (the tangent; scalar or (n,))."""
    P = np.atleast_2d(np.asarray(P, dtype=float))
    aim = np.broadcast_to(np.asarray(aim, dtype=float), (len(P),))
    D = np.linalg.norm(P, axis=1)
    to_centre = -P / D[:, None]
    sigma = np.zeros(len(P))
    v = (-m[0] + m[1] * aim) / m[2]
    n = to_centre
    for _ in range(80):
        length = np.sqrt(1.0 + aim * aim + v * v)
        v = (-(sigma / D) * length - m[0] + m[1] * aim) / m[2]   # m · d0 = -σ / D
        d0 = np.stack([np.ones_like(v), -aim, v], axis=1)
        d0 /= np.linalg.norm(d0, axis=1, keepdims=True)
        n = d0 + to_centre
        n /= np.linalg.norm(n, axis=1, keepdims=True)
        new = fil.support(m - 2.0 * (n @ m)[:, None] * n)[0]
        done = np.max(np.abs(new - sigma)) < 1e-13
        sigma = new
        if done:
            break
    return n


def _normal_scalar(px: float, py: float, pz: float, m, e, ell: float, rho: float, aim: float):
    """normal_for_aim for one point with plain floats (the start curve is integrated point by
    point; this is ten times faster than arrays of one)."""
    D = math.sqrt(px * px + py * py + pz * pz)
    wx, wy, wz = -px / D, -py / D, -pz / D
    m0, m1, m2 = m
    e0, e1, e2 = e
    sigma = 0.0
    v = (-m0 + m1 * aim) / m2
    nx = ny = nz = 0.0
    for _ in range(80):
        length = math.sqrt(1.0 + aim * aim + v * v)
        v = (-(sigma / D) * length - m0 + m1 * aim) / m2
        length = math.sqrt(1.0 + aim * aim + v * v)
        nx, ny, nz = 1.0 / length + wx, -aim / length + wy, v / length + wz
        norm = math.sqrt(nx * nx + ny * ny + nz * nz)
        nx, ny, nz = nx / norm, ny / norm, nz / norm
        mu = nx * m0 + ny * m1 + nz * m2
        a = (m0 - 2.0 * mu * nx) * e0 + (m1 - 2.0 * mu * ny) * e1 + (m2 - 2.0 * mu * nz) * e2
        new = ell * abs(a) + rho * math.sqrt(max(1.0 - a * a, 0.0))
        done = abs(new - sigma) < 1e-13
        sigma = new
        if done:
            break
    return nx, ny, nz


def cutoff_residual(P: np.ndarray, n: np.ndarray, m: np.ndarray, fil: Filament) -> np.ndarray:
    """Angle (rad, small) by which the highest point of the filament image of P lies above the
    plane m; 0 on a facet of that branch."""
    mp = m - 2.0 * (n @ m)[:, None] * n
    F = -np.einsum("ni,ni->n", mp, P) - fil.support(mp)[0]
    return -F / np.linalg.norm(P, axis=1)


# ------------------------------------------------------------------ characteristic strips
def _active(P: np.ndarray, n: np.ndarray, m: np.ndarray, fil: Filament) -> np.ndarray:
    """m' · axis: its sign names the end of the coil that touches."""
    return (m - 2.0 * (n @ m)[:, None] * n) @ fil.axis


def _rhs(P, n, m, fil: Filament, along: np.ndarray, sign: np.ndarray):
    """dP/dq and dn/dq of the characteristic strips, q = P · along; the touching end is `sign`."""
    mu = n @ m
    mp = m - 2.0 * mu[:, None] * n
    g = fil.contact(mp, sign) - P
    gamma = np.einsum("ni,ni->n", g, n)
    m_t = m - mu[:, None] * n
    b = -2.0 * (gamma[:, None] * m_t + mu[:, None] * (g - gamma[:, None] * n))
    rate = b @ along
    return b / rate[:, None], m_t / rate[:, None]


def _rk4(P, n, h, m, fil, along, sign):
    h = np.broadcast_to(np.asarray(h, dtype=float), (len(P),))[:, None]
    k1p, k1n = _rhs(P, n, m, fil, along, sign)
    k2p, k2n = _rhs(P + 0.5 * h * k1p, n + 0.5 * h * k1n, m, fil, along, sign)
    k3p, k3n = _rhs(P + 0.5 * h * k2p, n + 0.5 * h * k2n, m, fil, along, sign)
    k4p, k4n = _rhs(P + h * k3p, n + h * k3n, m, fil, along, sign)
    P1 = P + h / 6.0 * (k1p + 2.0 * k2p + 2.0 * k3p + k4p)
    n1 = n + h / 6.0 * (k1n + 2.0 * k2n + 2.0 * k3n + k4n)
    return P1, n1 / np.linalg.norm(n1, axis=1, keepdims=True)


def characteristic_step(P, n, h: float, m, fil: Filament, along, sign):
    """One step of length h in q for all strips. Where the touching end of the coil changes inside
    the step (the image's long edge passes the direction of the cut-off: the right-hand side
    jumps), the step is split at the change. Returns P, n and the new signs."""
    P1, n1 = _rk4(P, n, h, m, fil, along, sign)
    a1 = _active(P1, n1, m, fil)
    flip = (a1 * sign < 0.0) & (np.abs(a1) > 1e-12)
    if flip.any():
        a0 = _active(P[flip], n[flip], m, fil)
        lam = np.clip(a0 / (a0 - a1[flip]), 0.0, 1.0)
        Pm, nm = _rk4(P[flip], n[flip], lam * h, m, fil, along, sign[flip])
        P1[flip], n1[flip] = _rk4(Pm, nm, (1.0 - lam) * h, m, fil, along, -sign[flip])
        sign = np.where(flip, -sign, sign)
    return P1, n1, sign


# ------------------------------------------------------------------ strips in the front view
@dataclass(frozen=True)
class StripFrame:
    """The strips of a zone in the front view. (p, q) is (y, z) turned by `beta`; q runs along the
    strips. The strip coordinate c = p + kappa r² / (4 f) is constant on the strip borders: they
    are the sections of the base paraboloid (focal length f) with parallel planes – circles in the
    front view, straight lines p = c for kappa = 0 (strips aimed straight ahead)."""

    beta: float
    kappa: float
    focal: float

    @property
    def e_p(self) -> np.ndarray:
        return np.array([math.cos(self.beta), math.sin(self.beta)])

    @property
    def e_q(self) -> np.ndarray:
        return np.array([-math.sin(self.beta), math.cos(self.beta)])

    def pq(self, y, z):
        c, s = math.cos(self.beta), math.sin(self.beta)
        return c * y + s * z, -s * y + c * z

    def yz(self, p, q):
        c, s = math.cos(self.beta), math.sin(self.beta)
        return c * p - s * q, s * p + c * q

    def strip_coordinate(self, p, q):
        return p + self.kappa * (p * p + q * q) / (4.0 * self.focal)

    def p_of(self, c, q):
        """p of the point with strip coordinate c in the section q."""
        if abs(self.kappa) < 1e-12:
            return c + 0.0 * q
        k = self.kappa / (4.0 * self.focal)
        disc = 1.0 - 4.0 * k * (k * q * q - c)
        if np.any(disc < 0.0):
            raise DesignError("a strip border does not reach this section: the zone is aimed too far "
                              "sideways for its extent – narrow the zone or reduce its aim")
        return (2.0 * (c - k * q * q)) / (1.0 + np.sqrt(disc))

    @staticmethod
    def of(m: np.ndarray, aim: float, focal: float) -> "StripFrame":
        """Strips along the characteristics of a facet aimed at h = aim under the plane m: they lie
        in planes normal to τ, the direction of the cut-off line at the aim."""
        v = (-m[0] + m[1] * aim) / m[2]
        tau = np.cross(m, [1.0, -aim, v])
        if tau[1] < 0.0:
            tau = -tau
        across = math.hypot(tau[1], tau[2])
        return StripFrame(math.atan2(tau[2], tau[1]), tau[0] / across, focal)


def base_x(focal: float, r2):
    """The base paraboloid with its focus at the filament centre."""
    return r2 / (4.0 * focal) - focal


# ------------------------------------------------------------------ a facet
@dataclass(frozen=True)
class FacetSpec:
    m: np.ndarray                  # cut-off plane (unit normal) of the facet's branch
    frame: StripFrame
    c_range: tuple[float, float]   # the strip: strip coordinate from … to
    aim_range: tuple[float, float]  # aim (tangent of H) at these borders
    rect: tuple[float, float, float, float]   # p0, p1, q0, q1 the facet must cover
    q_start: float                 # section of the start curve


@dataclass
class Facet:
    spec: FacetSpec
    graph: Graph
    extrapolated: bool = False     # the strips did not reach every corner of the rectangle
    aim_limited: bool = False      # the aim was held back where the mirror cannot throw its light there
    start_curve: np.ndarray = field(default_factory=lambda: np.zeros((0, 3)))   # x, y, z along T
    strips: np.ndarray = field(default_factory=lambda: np.zeros((0, 0, 3)))     # some strips: (k, nodes, 3)
    net: tuple | None = None       # (P, n) of every strip node, kept on request (tests)

    def evaluate(self, y, z) -> tuple[np.ndarray, np.ndarray]:
        """x and the unit normals (towards the light) at front-view points."""
        y, z = np.asarray(y, dtype=float), np.asarray(z, dtype=float)
        fr = self.spec.frame
        p, q = fr.pq(y, z)
        f, fp, fq = self.graph.evaluate(p, q)
        e_p, e_q = fr.e_p, fr.e_q
        n = np.stack([np.ones_like(f), -(fp * e_p[0] + fq * e_q[0]), -(fp * e_p[1] + fq * e_q[1])], axis=-1)
        return f, n / np.linalg.norm(n, axis=-1, keepdims=True)


def _hermite(x: np.ndarray, y: np.ndarray, d: np.ndarray, xq: np.ndarray) -> tuple[np.ndarray, bool]:
    """Cubic Hermite interpolation of (x, y, dy/dx), x increasing; linear continuation outside."""
    i = np.clip(np.searchsorted(x, xq) - 1, 0, len(x) - 2)
    h = x[i + 1] - x[i]
    t = (xq - x[i]) / h
    inside = (xq >= x[0]) & (xq <= x[-1])
    t_in = np.clip(t, 0.0, 1.0)
    h00 = (1 + 2 * t_in) * (1 - t_in) ** 2
    h10 = t_in * (1 - t_in) ** 2
    h01 = t_in * t_in * (3 - 2 * t_in)
    h11 = t_in * t_in * (t_in - 1)
    out = h00 * y[i] + h10 * h * d[i] + h01 * y[i + 1] + h11 * h * d[i + 1]
    left, right = xq < x[0], xq > x[-1]
    out = np.where(left, y[0] + d[0] * (xq - x[0]), out)
    out = np.where(right, y[-1] + d[-1] * (xq - x[-1]), out)
    return out, bool(inside.all())


def build_facet(spec: FacetSpec, fil: Filament, *, step: float = 0.5, knot_spacing: float = 1.5,
                keep_net: bool = False) -> Facet:
    """The facet over spec.rect. `step`: spacing of the strips and step along them (mm);
    `knot_spacing`: of the B-spline (rounded to a multiple of 3 · step along the strips)."""
    fr, m = spec.frame, spec.m
    p0, p1, q0, q1 = spec.rect
    e_p3 = np.array([0.0, *fr.e_p])
    e_q3 = np.array([0.0, *fr.e_q])
    c_lo, c_hi = spec.c_range
    a_lo, a_hi = spec.aim_range

    a_mid = 0.5 * (a_lo + a_hi)
    limited = [0]

    def along(a: float, y: float, z: float) -> float:
        """Cosine (front view) of the angle between the strips of a facet aimed at `a` on the base
        paraboloid at (y, z) and the direction q; signed."""
        v = (-m[0] + m[1] * a) / m[2]
        tx, ty, tz = m[1] * v + m[2] * a, m[2] - m[0] * v, -m[0] * a - m[1]          # m x (1, -a, v)
        ny, nz = -y / (2.0 * fr.focal), -z / (2.0 * fr.focal)                        # normal (1, ny, nz)
        cy, cz = nz * tx - tz, ty - ny * tx                                          # (normal x tau) in the front view
        return (cy * e_q3[1] + cz * e_q3[2]) / max(math.hypot(cy, cz), 1e-300)

    def aim_at(p: float, q: float) -> float:
        # linear across the strip; beyond its borders it levels off smoothly (10 % further at most):
        # the facet continues there without aiming ever further sideways
        s = (fr.strip_coordinate(p, q) - c_lo) / (c_hi - c_lo)
        if s > 1.0:
            t = min(s - 1.0, 0.2)
            s = 1.0 + t - t * t / 0.4
        elif s < 0.0:
            t = min(-s, 0.2)
            s = -(t - t * t / 0.4)
        a = a_lo + (a_hi - a_lo) * s
        # a mirror wall cannot throw its light across itself: where the aim asks for that, the
        # strips would turn along the start section. The aim is held back to MIN_ALONG there.
        y, z = fr.yz(p, q)
        reference = along(a_mid, y, z)
        sign = 1.0 if reference >= 0.0 else -1.0
        if sign * along(a, y, z) >= MIN_ALONG:
            return a
        if abs(reference) < MIN_ALONG:
            raise DesignError("this part of the mirror cannot throw its light where the zone is aimed (it would "
                              "have to cross the mirror's own wall) – aim the zone less far sideways")
        good, bad = a_mid, a
        for _ in range(40):
            middle = 0.5 * (good + bad)
            if sign * along(middle, y, z) >= MIN_ALONG:
                good = middle
            else:
                bad = middle
        if 0.0 <= s <= 1.0:
            limited[0] += 1
        return good

    # grid along the strips: q_start is a node, the B-spline sites are nodes (knot spacing 3 h)
    h = step
    spans_q = max(1, round(knot_spacing / (3.0 * h)))
    H = 3.0 * h * spans_q
    j_lo = math.ceil((spec.q_start - q0) / h - 1e-9)
    n_spans = max(1, math.ceil((q1 - (spec.q_start - j_lo * h)) / H - 1e-9))
    qa = spec.q_start - j_lo * h
    qb = qa + n_spans * H
    j_hi = round((qb - spec.q_start) / h)
    n_q = n_spans + 3
    n_p = max(4, math.ceil((p1 - p0) / knot_spacing - 1e-9) + 3)
    graph = Graph((p0, p1), (qa, qb), n_p, n_q)
    rows = np.rint((graph.sites_q - qa) / h).astype(int)   # node index of every site, from qa

    def slope_T(p: float, x: float) -> float:
        y, z = fr.yz(p, spec.q_start)
        nx, ny, nz = _normal_scalar(x, y, z, m, fil.axis, fil.half_length, fil.radius, aim_at(p, spec.q_start))
        return -(ny * e_p3[1] + nz * e_p3[2]) / nx

    def start_curve(p_from: float, n_steps: int, dp: float) -> tuple[np.ndarray, np.ndarray]:
        ps = p_from + dp * np.arange(n_steps + 1)
        xs = np.empty(n_steps + 1)
        xs[0] = base_x(fr.focal, p_from * p_from + spec.q_start ** 2)
        for i in range(n_steps):
            p, x = ps[i], xs[i]
            k1 = slope_T(p, x)
            k2 = slope_T(p + 0.5 * dp, x + 0.5 * dp * k1)
            k3 = slope_T(p + 0.5 * dp, x + 0.5 * dp * k2)
            k4 = slope_T(p + dp, x + dp * k3)
            xs[i + 1] = x + dp / 6.0 * (k1 + 2 * k2 + 2 * k3 + k4)
        return ps, xs

    # the strips fan out or run askew: start wide enough to cover the rectangle, widen if needed
    p_mid = float(fr.p_of(0.5 * (c_lo + c_hi), spec.q_start))
    edge_q = np.linspace(qa, qb, 9)
    corners_c = np.concatenate([fr.strip_coordinate(p0, edge_q), fr.strip_coordinate(p1, edge_q)])
    want_lo = min(float(fr.p_of(corners_c.min(), spec.q_start)), p0) - 2.0 * h
    want_hi = max(float(fr.p_of(corners_c.max(), spec.q_start)), p1) + 2.0 * h
    extrapolated = False
    for attempt in range(4):
        n_left = max(2, math.ceil((p_mid - want_lo) / h))
        n_right = max(2, math.ceil((want_hi - p_mid) / h))
        pl, xl = start_curve(p_mid, n_left, -h)
        pr, xr = start_curve(p_mid, n_right, h)
        pT = np.concatenate([pl[:0:-1], pr])
        xT = np.concatenate([xl[:0:-1], xr])
        yT, zT = fr.yz(pT, spec.q_start)
        PT = np.stack([xT, yT, zT], axis=1)
        aims = np.array([aim_at(p, spec.q_start) for p in pT])
        nT = normal_for_aim(PT, m, fil, aims)
        sign0 = np.where(_active(PT, nT, m, fil) >= 0.0, 1.0, -1.0)
        askew = _rhs(PT, nT, m, fil, e_q3, sign0)[0] @ e_p3   # dp/dq of the strips at the start section
        if not np.all(np.isfinite(askew)) or np.any(np.abs(askew) > 3.0):
            raise DesignError("the strips of this zone run along its start section: its aim is too far "
                              "from the direction the zone's strips are laid out for")
        P = np.empty((len(pT), j_lo + j_hi + 1, 3))
        N = np.empty_like(P)
        P[:, j_lo], N[:, j_lo] = PT, nT
        for direction, count in ((1, j_hi), (-1, j_lo)):
            Pc, nc, sign = PT, nT, sign0.copy()
            for j in range(1, count + 1):
                Pc, nc, sign = characteristic_step(Pc, nc, direction * h, m, fil, e_q3, sign)
                P[:, j_lo + direction * j], N[:, j_lo + direction * j] = Pc, nc
        if not np.all(np.isfinite(P)) or np.any(N[..., 0] <= 0.02):
            raise DesignError("a facet becomes too steep or folds – reduce the spread (aims) of its zone, "
                              "the strip width or the zone's extent")
        pp = P[..., 1] * fr.e_p[0] + P[..., 2] * fr.e_p[1]     # (strips, nodes)
        near = (pp[:-1] <= p1 + 2.0 * h) & (pp[1:] >= p0 - 2.0 * h)      # pairs of strips that pass the rectangle
        if np.any((np.diff(pp, axis=0) <= 0.0) & near):
            raise DesignError("the strips of a facet cross: its aims converge too strongly (let the aims of "
                              "the zone run the other way or reduce the spread), or it is aimed so far sideways "
                              "that the light would have to pass the mirror's own wall")
        lo, hi = pp[0].max(), pp[-1].min()
        if lo <= p0 + 1e-9 and hi >= p1 - 1e-9:
            break
        if attempt == 3:
            extrapolated = True
            break
        want_lo -= max(0.0, lo - p0) * 1.5 + h
        want_hi += max(0.0, p1 - hi) * 1.5 + h

    # every node row: x and its slope across the strips, interpolated onto the sites
    fp_all = -(N[..., 1] * fr.e_p[0] + N[..., 2] * fr.e_p[1]) / N[..., 0]
    values = np.empty((n_p, n_q))
    for col, j in enumerate(rows):
        use = np.flatnonzero((pp[:, j] >= p0 - 2.0 * h) & (pp[:, j] <= p1 + 2.0 * h))   # the strips at the rectangle
        lo_i, hi_i = max(use[0] - 1, 0), min(use[-1] + 2, len(pp))                       # (those far off may cross)
        values[:, col], _ = _hermite(pp[lo_i:hi_i, j], P[lo_i:hi_i, j, 0], fp_all[lo_i:hi_i, j], graph.sites_p)
    graph.interpolate(values)

    every = max(1, round(2.5 / h))
    facet = Facet(spec, graph, extrapolated=extrapolated, start_curve=PT,
                  strips=P[len(P) // 2 % every::every].astype(np.float32), aim_limited=limited[0] > 0)
    if keep_net:
        facet.net = (P, N)
    return facet
