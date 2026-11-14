"""Connecting faces between the facets: walls with draft and fillets, so that the whole mirror can
be moulded (demoulding along X) and has no sharp edge.

Neighbouring facets meet with a step that changes along their border. Across the border (ξ, in the
front view) the mirror is first the raw profile G – facet a for ξ < 0, facet b for ξ > 0 – and then

    F1 = median(L, w, U)      the wall w through the middle of the step with the slope cot(draft),
                              clipped between the cone envelopes L <= G <= U of that slope:
                              no part of the profile is steeper than the draft allows;
    F2 = open(close(F1))      a ball of the fillet radius rolled over both sides of the profile:
                              the foot and the head of the wall are rounded.

Where the step vanishes the wall does too and only a crease is rounded. The profiles of all
stations along a border make a band – a bicubic B-spline over (u along, ξ across) that runs into
the facets on both sides; the facets are trimmed back to the band.

Two levels: the bands between the strips of a zone first (they reach up to the zone's rays), then
the bands along the rays between two zones, whose two sides are the zones as built so far – strips
and their bands – so that the junctions are covered by one surface. NumPy only."""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

import al_layout
from al_design import DesignError, StripFrame
from al_layout import Circle, Line
from al_spline import DEGREE, Graph, basis_matrix

FINE = 0.04            # mm, raster of the profiles across a border (at most radius / 8)
SITE = 0.1             # mm, spacing of a band's B-spline sites across the border
ALONG_STRIP = 0.5      # mm, stations along a border between two strips
ALONG_ZONE = 0.1       # mm, stations along a border between two zones (strips' walls cross it)
TOLERANCE = 0.003      # mm: a band reaches as far as its profile differs from the two sides by more
TAPER = 0.2            # mm: beyond that the profile runs out into the sides (the band's rim lies on them)


# ------------------------------------------------------------------ borders: (u, ξ) <-> (y, z)
@dataclass(frozen=True)
class LineBorder:
    """Straight border: (y, z) = origin + u along + ξ across, (along, across) right-handed."""

    origin: np.ndarray
    along: np.ndarray

    @property
    def across(self) -> np.ndarray:
        return np.array([-self.along[1], self.along[0]])

    def uxi(self, y, z):
        dy, dz = y - self.origin[0], z - self.origin[1]
        return dy * self.along[0] + dz * self.along[1], -dy * self.along[1] + dz * self.along[0]

    def yz(self, u, xi):
        a, c = self.along, self.across
        return self.origin[0] + u * a[0] + xi * c[0], self.origin[1] + u * a[1] + xi * c[1]

    def gradients(self, y, z):
        """∇u and ∇ξ at the points, each (n, 2)."""
        one = np.ones_like(np.asarray(y, dtype=float))
        return np.stack([one * self.along[0], one * self.along[1]], -1), \
            np.stack([one * self.across[0], one * self.across[1]], -1)

    def side(self, half_width: float, positive: bool):
        """Clip curve keeping ξ <= half_width (positive) or ξ >= -half_width."""
        normal = -self.across if positive else self.across
        return Line(normal, float(normal @ self.origin) - half_width)

    exact = True


@dataclass(frozen=True)
class CircleBorder:
    """Circular border about `centre`: ξ = sign (ρ - radius), u = -sign · radius · (θ - θ0); θ0
    points from the centre to the lamp axis, so the cut of θ lies away from the reflector."""

    centre: np.ndarray
    radius: float
    sign: float

    @property
    def theta0(self) -> float:
        return math.atan2(-self.centre[1], -self.centre[0])

    def uxi(self, y, z):
        dy, dz = y - self.centre[0], z - self.centre[1]
        theta = (np.arctan2(dz, dy) - self.theta0 + math.pi) % (2.0 * math.pi) - math.pi
        return -self.sign * self.radius * theta, self.sign * (np.hypot(dy, dz) - self.radius)

    def yz(self, u, xi):
        theta = self.theta0 - self.sign * np.asarray(u, dtype=float) / self.radius
        rho = self.radius + self.sign * np.asarray(xi, dtype=float)
        return self.centre[0] + rho * np.cos(theta), self.centre[1] + rho * np.sin(theta)

    def gradients(self, y, z):
        dy, dz = y - self.centre[0], z - self.centre[1]
        rho = np.hypot(dy, dz)
        radial = np.stack([dy / rho, dz / rho], -1)
        tangent = np.stack([-dz / rho, dy / rho], -1)
        return -self.sign * self.radius * tangent / rho[..., None], self.sign * radial

    def side(self, half_width: float, positive: bool):
        outer = (self.sign > 0) == positive      # the kept side's limit is the larger circle
        radius = self.radius + half_width if outer else self.radius - half_width
        if radius <= 0.0:
            return None
        return Circle(self.centre, radius, inside=outer)

    exact = False


def strip_border(frame: StripFrame, c: float):
    """The border c = const between two strips of a zone; ξ grows with the strip coordinate."""
    if abs(frame.kappa) < 1e-7:
        return LineBorder(np.array(frame.yz(c, 0.0)), -frame.e_q)
    centre = np.array(frame.yz(-2.0 * frame.focal / frame.kappa, 0.0))
    radius = 2.0 * frame.focal / abs(frame.kappa) * math.sqrt(1.0 + frame.kappa * c / frame.focal)
    return CircleBorder(centre, radius, 1.0 if frame.kappa > 0.0 else -1.0)


def ray_border(azimuth_deg: float) -> LineBorder:
    """The ray between two zones; ξ > 0 is the zone that starts at it."""
    a = math.radians(azimuth_deg)
    return LineBorder(np.zeros(2), np.array([math.cos(a), math.sin(a)]))


# ------------------------------------------------------------------ the profile across a border
def _extend(F: np.ndarray, count: int) -> np.ndarray:
    """F (stations, n) continued by `count` samples at both ends with the end slopes."""
    k = np.arange(1, count + 1)
    left = F[:, :1] - (F[:, 1:2] - F[:, :1]) * k[::-1]
    right = F[:, -1:] + (F[:, -1:] - F[:, -2:-1]) * k
    return np.concatenate([left, F, right], axis=1)


def _ball(F: np.ndarray, radius: float, h: float, grow: bool) -> np.ndarray:
    """Dilation (grow) or erosion of profiles (stations, n) with a ball of `radius`. The profile
    counts as piecewise linear between its samples and the ball may touch anywhere on it – on a
    smooth profile the result is exact to the raster's interpolation, not to the raster."""
    J = int(radius / h) + 1
    E = _extend(F, J + 1)
    n = F.shape[1]
    sign = 1.0 if grow else -1.0
    out = np.full_like(F, -np.inf)
    for j in range(-J, J + 1):
        left = sign * E[:, J + 1 + j:J + 1 + j + n]                  # the profile at offset j h …
        right = sign * E[:, J + 2 + j:J + 2 + j + n]                 # … and at (j + 1) h
        if abs(j * h) <= radius:
            out = np.maximum(out, left + math.sqrt(radius * radius - (j * h) ** 2) - radius)
        slope = (right - left) / h
        t = radius * slope / np.sqrt(1.0 + slope * slope)            # where the ball touches a line of this slope
        inside = (t > j * h) & (t < (j + 1) * h)
        if inside.any():
            touch = left + slope * (t - j * h) + np.sqrt(np.maximum(radius * radius - t * t, 0.0)) - radius
            out = np.where(inside, np.maximum(out, touch), out)
    return sign * out


def _cone(G: np.ndarray, rise: float, upper: bool) -> np.ndarray:
    """The smallest profile above G (upper) or the largest below it whose slope is at most
    `rise` per sample."""
    out = G.copy()
    n = G.shape[1]
    for i in range(1, n):
        out[:, i] = np.maximum(out[:, i], out[:, i - 1] - rise) if upper else np.minimum(out[:, i], out[:, i - 1] + rise)
    for i in range(n - 2, -1, -1):
        out[:, i] = np.maximum(out[:, i], out[:, i + 1] - rise) if upper else np.minimum(out[:, i], out[:, i + 1] + rise)
    return out


def blend(a: np.ndarray, b: np.ndarray, h: float, draft_deg: float, radius: float) -> tuple[np.ndarray, np.ndarray]:
    """Profiles (stations, n) across a border on the raster ξ_i = (i - n/2 + 1/2) h: `a` is used
    for ξ < 0, `b` for ξ > 0. Returns the blended profiles and the raw ones."""
    n = a.shape[1]
    half = n // 2
    G = np.concatenate([a[:, :half], b[:, half:]], axis=1)
    xi = (np.arange(n) - half + 0.5) * h
    a0 = 1.5 * a[:, half - 1] - 0.5 * a[:, half - 2]        # both sides continued to ξ = 0
    b0 = 1.5 * b[:, half] - 0.5 * b[:, half + 1]
    slope = 1.0 / math.tan(math.radians(draft_deg))
    wall = 0.5 * (a0 + b0)[:, None] + np.sign(b0 - a0)[:, None] * slope * xi[None, :]
    lower, upper = _cone(G, slope * h, False), _cone(G, slope * h, True)
    F = np.maximum(lower, np.minimum(upper, wall))
    if radius > 0.0:
        F = _ball(_ball(F, radius, h, True), radius, h, False)       # close: the wall's foot
        F = _ball(_ball(F, radius, h, False), radius, h, True)       # open: its head
    return F, G


# ------------------------------------------------------------------ a band
@dataclass
class Band:
    """The connecting face along one border: x over (u, ξ), |ξ| <= half_width."""

    border: object
    graph: Graph                   # x(u, ξ)
    half_width: float
    zones: tuple[int, ...]         # one zone: between two of its strips; two: between these zones
    strip: int = 0                 # between strips `strip - 1` and `strip`
    step: float = 0.0              # largest step along the border, mm
    polygons: list[np.ndarray] = field(default_factory=list)   # its part of the front view

    def covers(self, y, z) -> np.ndarray:
        u, xi = self.border.uxi(y, z)
        return (np.abs(xi) <= self.half_width) & (u >= self.graph.knots_p[0]) & (u <= self.graph.knots_p[-1])

    def evaluate(self, y, z) -> tuple[np.ndarray, np.ndarray]:
        """x and ∇x = (∂x/∂y, ∂x/∂z) at front-view points."""
        u, xi = self.border.uxi(y, z)
        f, fu, fxi = self.graph.evaluate(u, xi)
        gu, gxi = self.border.gradients(y, z)
        return f, fu[..., None] * gu + fxi[..., None] * gxi

    def surface(self, origin=(0.0, 0.0, 0.0)) -> dict:
        """The band as a .nfcad surface with the parameters (u, ξ)."""
        g = self.graph
        U, X = np.meshgrid(g.sites_p, g.sites_q, indexing="ij")
        y, z = self.border.yz(U, X)
        if not self.border.exact:      # an arc: (y, z)(u, ξ) interpolated like x
            Bp = basis_matrix(g.knots_p, g.n_p, DEGREE, g.sites_p)
            Bq = basis_matrix(g.knots_q, g.n_q, DEGREE, g.sites_q)
            y = np.linalg.solve(Bp, np.linalg.solve(Bq, y.T).T)
            z = np.linalg.solve(Bp, np.linalg.solve(Bq, z.T).T)
        pts = np.stack([g.coeff + origin[0], y + origin[1], z + origin[2]], axis=-1)
        return {"degree_u": 3, "degree_v": 3, "knots_u": [float(k) for k in g.knots_p],
                "knots_v": [float(k) for k in g.knots_q], "num_u": g.n_p, "num_v": g.n_q,
                "points": [round(float(c), 8) for c in pts.reshape(-1)]}


class NeedsMargin(Exception):
    """The facets must reach further beyond their cells for the connecting faces (the value, mm)."""

    def __init__(self, margin: float):
        super().__init__(f"facet margin {margin:.2f} mm needed")
        self.margin = margin


@dataclass
class Profiles:
    """The blended profiles of the stations along one border, before the band is cut to width."""

    border: object
    u_range: tuple[float, float]
    n_u: int
    xi: np.ndarray                 # fine raster across
    F: np.ndarray                  # (n_u, len(xi)) blended
    G: np.ndarray                  # the two sides as they are
    step: float                    # largest step along the border
    reach: float                   # |ξ| up to which a profile differs from the sides
    window: float
    cramped: bool = False          # the profile would reach further than its window has room for


def profiles(border, u_range: tuple[float, float], along: float, draft_deg: float, radius: float,
             side_a, side_b, margin: float, room: float, mirror, extra: float = 0.0) -> Profiles | None:
    """The blended profiles along `border` for u in u_range. side_a(y, z) and side_b give x of the
    two sides (for ξ < 0 and ξ > 0). `margin`: how far beyond their cells the sides are defined;
    `room`: the width of the narrower strip beside the border; mirror(y, z): whether a point belongs
    to the mirror (what the sides are beyond the frame or in the lamp hole does not count);
    `extra`: more room across, for the walls of strips that end at this border. None where nothing
    is to connect."""
    h = min(FINE, radius / 8.0) if radius > 0.0 else FINE
    spans = max(1, math.ceil((u_range[1] - u_range[0]) / along))
    n_u = spans + 3
    stations = Graph(u_range, (-1.0, 1.0), n_u, 4).sites_p
    # the step at the border first: it sets the room the wall and its fillets need
    near = np.array([-1.5 * h, -0.5 * h, 0.5 * h, 1.5 * h])
    U, X = np.meshgrid(stations, near, indexing="ij")
    y, z = border.yz(U, X)
    a, b = side_a(y[:, :2], z[:, :2]), side_b(y[:, 2:], z[:, 2:])
    on_mirror = mirror(y[:, 1], z[:, 1]) | mirror(y[:, 2], z[:, 2])
    if not on_mirror.any():
        return None
    step = float(np.abs(1.5 * a[:, 1] - 0.5 * a[:, 0] - 1.5 * b[:, 0] + 0.5 * b[:, 1])[on_mirror].max())
    window = 1.3 * step * math.tan(math.radians(draft_deg)) + 3.0 * radius + 0.4 + extra
    if window + 0.3 > margin:
        raise NeedsMargin(window + 0.5)
    if 2.2 * window > room:
        raise DesignError(f"the strips are too narrow ({room:.1f} mm) for a connecting face over a step of "
                          f"{step:.2f} mm – wider strips, a smaller draft angle or a smaller fillet radius")
    half = max(4, math.ceil(window / h))
    xi = (np.arange(2 * half) - half + 0.5) * h
    U, X = np.meshgrid(stations, xi, indexing="ij")
    y, z = border.yz(U, X)
    a = np.empty_like(U)
    b = np.empty_like(U)
    a[:, :half] = side_a(y[:, :half], z[:, :half])
    b[:, half:] = side_b(y[:, half:], z[:, half:])
    a[:, half:] = a[:, half - 1:half]      # not used beyond its side
    b[:, :half] = b[:, half:half + 1]
    F, G = blend(a, b, h, draft_deg, radius)
    usable = window - radius - 4.0 * h          # the ball feels the window's ends within its radius
    reach = _reach((np.abs(F - G) > TOLERANCE) & mirror(y.ravel(), z.ravel()).reshape(y.shape), half, h)
    if reach == 0.0:
        return None
    cramped = reach + TAPER > usable             # runs out into the sides earlier than it should
    return Profiles(border, u_range, n_u, xi, F, G, step, min(reach, usable - TAPER), window, cramped)


def _reach(changed: np.ndarray, half: int, h: float) -> float:
    """How far from the border (ξ = 0, between the samples half - 1 and half) a profile is changed
    in one piece: on each side up to the first 0.1 mm without a change; the largest of all
    stations. Changes further out – the wall of a strip that only passes by – are not the
    border's."""
    quiet = max(2, round(0.1 / h))
    reach = 0
    for side in (changed[:, half:], changed[:, :half][:, ::-1]):
        n = side.shape[1]
        calm = ~side
        runs = np.ones((len(side), n - quiet + 1), dtype=bool)        # runs[:, i]: quiet samples from i on
        for k in range(quiet):
            runs &= calm[:, k:k + n - quiet + 1]
        first = np.where(runs.any(axis=1), runs.argmax(axis=1), n)    # where the first calm run begins
        reach = max(reach, int(first.max()))
    return reach * h


def band_of(pr: Profiles, half_width: float, zones: tuple[int, ...], strip: int = 0) -> "Band":
    """The band of the profiles, |ξ| <= half_width (at least their reach + TAPER): the blended
    profile within the reach, running out into the two sides up to the rim."""
    weight = np.clip((half_width - np.abs(pr.xi)) / TAPER, 0.0, 1.0)
    weight = weight * weight * (3.0 - 2.0 * weight)
    profile = pr.G + weight[None, :] * (pr.F - pr.G)
    n_xi = max(4, round(2.0 * half_width / SITE) + 3)
    graph = Graph(pr.u_range, (-half_width, half_width), pr.n_u, n_xi)
    values = np.empty((pr.n_u, n_xi))
    for i in range(pr.n_u):
        values[i] = np.interp(graph.sites_q, pr.xi, profile[i])
    graph.interpolate(values)
    return Band(pr.border, graph, half_width, zones, strip, pr.step)


def _width(reach: float) -> float:
    return math.ceil((reach + TAPER) / SITE - 1e-9) * SITE if reach > 0.0 else 0.0


# ------------------------------------------------------------------ all bands of a reflector
@dataclass
class Transitions:
    draft: float                   # degrees between the walls and the demoulding direction X
    radius: float                  # mm, fillets
    strip_bands: list[Band]
    zone_bands: list[Band]
    cell_polygons: list[list[np.ndarray]]     # per cell: what is left of it between the bands
    max_step: float = 0.0
    warnings: list[str] = field(default_factory=list)

    @property
    def bands(self) -> list[Band]:
        return self.zone_bands + self.strip_bands        # in the order of their priority


def _u_range(border, polys: list[np.ndarray]) -> tuple[float, float] | None:
    if not polys:
        return None
    u = np.concatenate([border.uxi(p[:, 0], p[:, 1])[0] for p in polys])
    return float(u.min()), float(u.max())


def _within(border, half_width: float, polys: list[np.ndarray]) -> list[np.ndarray]:
    for positive in (True, False):
        curve = border.side(half_width, positive)
        if curve is not None:
            polys = al_layout.clip_all(polys, curve)
    return polys


def build(reflector, draft_deg: float, radius: float, progress=None) -> Transitions:
    """The bands of `reflector` (al_reflector.Reflector). Raises NeedsMargin if its facets do not
    reach far enough beyond their cells."""
    s = reflector.settings
    zones = list(s.zones)
    cells, facets = reflector.cells, reflector.facets
    by_strip = {(c.zone, c.strip): i for i, c in enumerate(cells)}
    frames = {c.zone: c.spec.frame for c in cells}

    def facet_height(index: int):
        return lambda y, z: facets[index].evaluate(y, z)[0]

    outline = al_layout.simplified(reflector.frame, 0.05)       # only to tell the mirror from what lies beyond

    def mirror(y, z):
        return al_layout.contains(outline, np.column_stack([y, z])) & (np.hypot(y, z) >= s.hole_radius)

    # ---- level 1: between the strips of a zone
    strip_bands: list[Band] = []
    warnings: list[str] = []
    regions = {zi: al_layout.zone_region(reflector.frame, zone) for zi, zone in enumerate(zones)}
    pairs = [(zi, k) for (zi, k) in sorted(by_strip) if (zi, k - 1) in by_strip]
    for count, (zi, k) in enumerate(pairs):
        if progress is not None:
            progress(0.5 * count / max(len(pairs), 1), "connecting the strips …")
        border = strip_border(frames[zi], (k - 0.5) * s.strip_width)
        # the stations cover the band at its full width (it may end askew at a ray or the frame)
        near = al_layout.outside_hole(_within(border, s.margin, regions[zi]), s.hole_radius)
        u_range = _u_range(border, near)
        if u_range is None or u_range[1] - u_range[0] < 1e-6:
            continue
        pr = profiles(border, u_range, ALONG_STRIP, draft_deg, radius, facet_height(by_strip[(zi, k - 1)]),
                      facet_height(by_strip[(zi, k)]), s.margin, s.strip_width, mirror)
        if pr is not None:
            strip_bands.append(band_of(pr, _width(pr.reach), (zi,), k))
            if pr.cramped:
                warnings.append(f"zone {zi + 1}, between the strips {k - 1} and {k}: the connecting face over a "
                                f"step of {pr.step:.2f} mm needs more room than it has (facets nearly as steep "
                                "as the draft angle allows?) – it ends earlier")
    bands_of = {zi: [b for b in strip_bands if b.zones == (zi,)] for zi in range(len(zones))}

    def zone_height(zi: int):
        """x of a zone as built so far: its strips with the bands between them."""
        frame = frames[zi]
        existing = np.array(sorted(k for (z, k) in by_strip if z == zi))

        def height(y, z):
            shape = np.shape(y)
            y, z = np.ravel(y), np.ravel(z)
            k = np.floor(frame.strip_coordinate(*frame.pq(y, z)) / s.strip_width + 0.5).astype(int)
            k = existing[np.argmin(np.abs(k[:, None] - existing[None, :]), axis=1)]     # the nearest strip there is
            x = np.empty(len(y))
            for value in np.unique(k):
                sel = k == value
                x[sel] = facets[by_strip[(zi, int(value))]].evaluate(y[sel], z[sel])[0]
            for band in bands_of[zi]:
                sel = band.covers(y, z)
                if sel.any():
                    x[sel] = band.evaluate(y[sel], z[sel])[0]
            return x.reshape(shape)

        return height

    # ---- level 2: along the rays between two zones; all of one width, so the zones' corners at the
    # lamp axis stay simple
    found = []
    if len(zones) > 1:
        for zi, zone in enumerate(zones):
            before = next((j for j, other in enumerate(zones)
                           if abs((other.az_to - zone.az_from + 180.0) % 360.0 - 180.0) < 1e-9), None)
            if before is None or zi not in frames or before not in frames:
                continue
            if progress is not None:
                progress(0.5 + 0.5 * zi / len(zones), "connecting the zones …")
            border = ray_border(zone.az_from)
            near = al_layout.clip(reflector.frame, Line(border.along, 0.0))      # the ray's side of the lamp axis
            near = al_layout.outside_hole(_within(border, s.margin, near), s.hole_radius)
            u_range = _u_range(border, near)
            if u_range is None or u_range[1] - u_range[0] < 1e-6:
                continue
            pr = profiles(border, u_range, ALONG_ZONE, draft_deg, radius, zone_height(before), zone_height(zi),
                          s.margin, s.strip_width, mirror, extra=0.8)
            if pr is not None:
                found.append((pr, before, zi))
                if pr.cramped:
                    warnings.append(f"between the zones {before + 1} and {zi + 1}: the connecting face needs more "
                                    "room than it has where a strip's wall joins it – it ends earlier there")
    ray_width = _width(max((pr.reach for pr, _, _ in found), default=0.0))
    zone_bands = [band_of(pr, ray_width, (za, zb)) for pr, za, zb in found]
    if len(zone_bands) > 1:      # near the lamp axis the bands of two rays would run into each other
        spans = [(zone.az_to - zone.az_from) % 360.0 or 360.0 for zone in zones]
        need = ray_width / math.sin(math.radians(min(min(spans), 180.0) / 2.0))
        if s.hole_radius < need:
            raise DesignError(f"the lamp hole (radius {s.hole_radius:g} mm) is too small for the connecting faces "
                              f"between the zones, which meet at the lamp axis: at least {need:.1f} mm")

    # ---- the front view: zone bands first, strip bands between them, the cells keep the rest
    inner = {zi: al_layout.zone_region(reflector.frame, zone, inset=ray_width if zone_bands else 0.0)
             for zi, zone in enumerate(zones)}
    for band in zone_bands:
        near = al_layout.clip(reflector.frame, Line(band.border.along, 0.0))
        band.polygons = al_layout.outside_hole(_within(band.border, band.half_width, near), s.hole_radius)
    for band in strip_bands:
        band.polygons = al_layout.outside_hole(_within(band.border, band.half_width, inner[band.zones[0]]),
                                               s.hole_radius)
    width = {(b.zones[0], b.strip): b.half_width for b in strip_bands}
    cell_polygons = []
    for cell in cells:
        frame = cell.spec.frame
        c0, c1 = cell.spec.c_range
        polys = inner[cell.zone]
        for curve in (al_layout.strip_curve(frame, c0, True, width.get((cell.zone, cell.strip), 0.0)),
                      al_layout.strip_curve(frame, c1, False, width.get((cell.zone, cell.strip + 1), 0.0))):
            if curve is None:
                continue
            polys = [] if isinstance(curve, str) else al_layout.clip_all(polys, curve)
        cell_polygons.append(al_layout.outside_hole(polys, s.hole_radius))
    step = max([b.step for b in strip_bands + zone_bands], default=0.0)
    return Transitions(draft_deg, radius, strip_bands, zone_bands, cell_polygons, step, warnings)
