"""Materials: how a surface scatters light, for many points at once.

All directions point away from the surface point; `n` is the surface normal (unit) on the front
side. One channel (radiometric, no colour). Kinds:

  absorber     takes everything
  diffuse      Lambertian: reflects `reflectance`, lets `transmittance` through (diffusely)
  mirror       ideal mirror with `reflectance`
  glossy       a lobe around the mirror direction (normalised Phong), width from `roughness`
  glass        smooth dielectric with refractive index `ior` inside (behind the front side),
               Fresnel reflection and refraction; `absorption` (1/mm) inside (Beer–Lambert)

mirror and glass scatter into single directions ("specular"); they cannot be connected to
other points, only followed."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

ABSORBER, DIFFUSE, MIRROR, GLOSSY, GLASS = range(5)
KINDS = ["absorber", "diffuse", "mirror", "glossy", "glass"]


@dataclass
class Materials:
    """Material table: one entry per material, arrays indexed by material number."""
    kind: np.ndarray
    reflectance: np.ndarray
    transmittance: np.ndarray
    exponent: np.ndarray                 # glossy: Phong exponent
    ior: np.ndarray
    absorption: np.ndarray               # 1/mm
    names: list[str]

    @staticmethod
    def from_rows(rows: list[dict]) -> "Materials":
        """rows: dicts with name, kind and (optional) reflectance, transmittance, roughness, ior,
        absorption. ValueError for values that make no sense."""
        if not rows:
            raise ValueError("no materials")
        names = []
        cols: dict[str, list[float]] = {k: [] for k in ("kind", "rho", "tau", "exp", "ior", "abs")}
        for row in rows:
            name = str(row.get("name", "")).strip()
            kind = str(row.get("kind", "diffuse")).strip().lower()
            if not name:
                raise ValueError("a material has no name")
            if name in names:
                raise ValueError(f"material {name!r} is defined twice")
            if kind not in KINDS:
                raise ValueError(f"material {name!r}: unknown kind {kind!r} (one of {', '.join(KINDS)})")
            rho = float(row.get("reflectance", 0.0))
            tau = float(row.get("transmittance", 0.0)) if kind == "diffuse" else 0.0
            rough = float(row.get("roughness", 0.2))
            ior = float(row.get("ior", 1.5))
            absorb = float(row.get("absorption", 0.0))
            if rho < 0 or tau < 0 or rho + tau > 1 + 1e-12:
                raise ValueError(f"material {name!r}: reflectance + transmittance must lie in 0 … 1")
            if kind == "glossy" and not 0 < rough <= 1:
                raise ValueError(f"material {name!r}: roughness must lie in 0 … 1 (not 0)")
            if kind == "glass" and ior < 1:
                raise ValueError(f"material {name!r}: refractive index below 1")
            if absorb < 0:
                raise ValueError(f"material {name!r}: negative absorption")
            names.append(name)
            cols["kind"].append(KINDS.index(kind))
            cols["rho"].append(rho)
            cols["tau"].append(tau)
            cols["exp"].append(2.0 / max(rough, 1e-3) ** 2 - 2.0)
            cols["ior"].append(ior)
            cols["abs"].append(absorb)
        return Materials(np.asarray(cols["kind"], dtype=np.int64), np.asarray(cols["rho"]), np.asarray(cols["tau"]),
                         np.asarray(cols["exp"]), np.asarray(cols["ior"]), np.asarray(cols["abs"]), names)

    def specular(self, m: np.ndarray) -> np.ndarray:
        k = self.kind[m]
        return (k == MIRROR) | (k == GLASS)

    # ------------------------------------------------------------------ evaluation
    def eval(self, m: np.ndarray, n: np.ndarray, wo: np.ndarray, wi: np.ndarray):
        """f(wo, wi) in 1/sr and the densities (per solid angle) of sampling wi from wo and wo
        from wi. Zero for specular materials."""
        k = self.kind[m]
        co = np.einsum("ij,ij->i", n, wo)
        ci = np.einsum("ij,ij->i", n, wi)
        same = co * ci > 0
        f = np.zeros(len(m))
        p_fwd = np.zeros(len(m))
        p_rev = np.zeros(len(m))
        d = np.nonzero(k == DIFFUSE)[0]
        if d.size:
            rho, tau = self.reflectance[m[d]], self.transmittance[m[d]]
            part = np.where(same[d], rho, tau)
            total = rho + tau
            pick = np.divide(part, total, out=np.zeros(len(d)), where=total > 0)
            f[d] = part / np.pi
            p_fwd[d] = pick * np.abs(ci[d]) / np.pi
            p_rev[d] = pick * np.abs(co[d]) / np.pi
        g = np.nonzero((k == GLOSSY) & same)[0]
        if g.size:
            e = self.exponent[m[g]]
            # cosine between wi and the mirror direction of wo (symmetric in wo and wi)
            c = np.clip(2 * co[g] * ci[g] - np.einsum("ij,ij->i", wo[g], wi[g]), 0.0, 1.0)
            lobe = c ** e
            f[g] = self.reflectance[m[g]] * (e + 2) / (2 * np.pi) * lobe
            p_fwd[g] = (e + 1) / (2 * np.pi) * lobe
            p_rev[g] = p_fwd[g]
        return f, p_fwd, p_rev

    # ------------------------------------------------------------------ sampling
    def sample(self, m: np.ndarray, n: np.ndarray, wo: np.ndarray, rng: np.random.Generator, radiance: bool):
        """A scattered direction wi for each point: (wi, weight, pdf) with weight = f |cos| / pdf
        (0: absorbed) and pdf per solid angle (0 for specular materials). `radiance`: the path
        comes from the sensor (refraction then scales by the squared ratio of the indices)."""
        count = len(m)
        k = self.kind[m]
        co = np.einsum("ij,ij->i", n, wo)
        side = np.where(co >= 0, 1.0, -1.0)
        wi = np.zeros((count, 3))
        weight = np.zeros(count)
        pdf = np.zeros(count)
        u1, u2, u3 = rng.random(count), rng.random(count), rng.random(count)

        d = np.nonzero(k == DIFFUSE)[0]
        if d.size:
            rho, tau = self.reflectance[m[d]], self.transmittance[m[d]]
            total = rho + tau
            reflect = u3[d] * total < rho
            axis = n[d] * (side[d] * np.where(reflect, 1.0, -1.0))[:, None]
            w, cos = _cosine(axis, u1[d], u2[d])
            wi[d] = w
            weight[d] = total
            pdf[d] = np.divide(np.where(reflect, rho, tau), total, out=np.zeros(len(d)), where=total > 0) * cos / np.pi
        s = np.nonzero(k == MIRROR)[0]
        if s.size:
            wi[s] = 2 * co[s][:, None] * n[s] - wo[s]
            weight[s] = self.reflectance[m[s]]
        g = np.nonzero(k == GLOSSY)[0]
        if g.size:
            e = self.exponent[m[g]]
            mirror = 2 * co[g][:, None] * n[g] - wo[g]
            cos_a = u1[g] ** (1.0 / (e + 1))
            w = _around(mirror, cos_a, 2 * np.pi * u2[g])
            ci = np.einsum("ij,ij->i", n[g], w)
            ok = ci * co[g] > 0
            wi[g] = w
            weight[g] = np.where(ok, self.reflectance[m[g]] * (e + 2) / (e + 1) * np.abs(ci), 0.0)
            pdf[g] = (e + 1) / (2 * np.pi) * cos_a ** e
        t = np.nonzero(k == GLASS)[0]
        if t.size:
            ior = self.ior[m[t]]
            eta = np.where(co[t] >= 0, 1.0 / ior, ior)             # n(side of wo) / n(other side)
            cos_o = np.abs(co[t])
            sin2 = eta ** 2 * (1 - cos_o ** 2)
            cos_t = np.sqrt(np.clip(1 - sin2, 0.0, None))
            with np.errstate(divide="ignore", invalid="ignore"):
                rs = (eta * cos_o - cos_t) / (eta * cos_o + cos_t)
                rp = (cos_o - eta * cos_t) / (cos_o + eta * cos_t)
            fresnel = np.where(sin2 >= 1, 1.0, 0.5 * (rs ** 2 + rp ** 2))
            reflect = u3[t] < fresnel
            nn = n[t] * side[t][:, None]
            refl = 2 * cos_o[:, None] * nn - wo[t]
            refr = -eta[:, None] * wo[t] + (eta * cos_o - cos_t)[:, None] * nn
            wi[t] = np.where(reflect[:, None], refl, refr)
            weight[t] = np.where(reflect | (not radiance), 1.0, eta ** 2)
        return wi, weight, pdf


def _frame(axis: np.ndarray):
    """Two unit vectors perpendicular to each axis (unit) and to each other."""
    helper = np.where((np.abs(axis[:, 0]) < 0.9)[:, None], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0])
    a = np.cross(axis, helper)
    a /= np.linalg.norm(a, axis=1, keepdims=True)
    return a, np.cross(axis, a)


def _around(axis: np.ndarray, cos: np.ndarray, phi: np.ndarray) -> np.ndarray:
    a, b = _frame(axis)
    sin = np.sqrt(np.clip(1 - cos ** 2, 0.0, None))
    return (sin * np.cos(phi))[:, None] * a + (sin * np.sin(phi))[:, None] * b + cos[:, None] * axis


def _cosine(axis: np.ndarray, u1: np.ndarray, u2: np.ndarray):
    """Directions with density cos / pi around each axis, and their cosines."""
    cos = np.sqrt(1 - u1)
    return _around(axis, cos, 2 * np.pi * u2), cos


def cone(axis: np.ndarray, sin_max: np.ndarray, u1: np.ndarray, u2: np.ndarray):
    """Directions with density cos / (pi sin_max²) within the cone of half-angle asin(sin_max)
    around each axis, and their cosines."""
    cos = np.sqrt(1 - sin_max ** 2 * u1)
    return _around(axis, cos, 2 * np.pi * u2), cos
