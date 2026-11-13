"""FourierGrating configuration <-> the plugin's panel (no FourierGrating needed).

The panel shows the parts of a configuration (FourierGrating docs/config_format.md) a user edits
most – structure, materials, layers, incidence, RCWA slices – as fields, lists and tables; every
other key (solver options, sweeps, near field, output, other layer and material forms) travels
unchanged as JSON ("advanced", or the "json" entry of a layer or material). `to_config(to_params(c))`
gives `c` back.
"""

from __future__ import annotations

import copy
from typing import Any

#: top-level keys shown as fields
_SHOWN = ("dimension", "solver", "truncation", "period_um", "incidence", "materials", "superstrate",
          "substrate", "layers")
#: incidence keys shown as fields (number, list "a, b" or range "a:b:n")
INCIDENCE = ("lambda_um", "phi_deg", "psi_deg", "delta_deg")


def _number_text(v: Any) -> str:
    return repr(float(v)) if isinstance(v, float) else str(v)


def incidence_text(v: Any) -> str:
    """A number, a list or {"from", "to", "steps"} as text: "0.6", "0.5, 0.6", "0.5:0.7:21"."""
    if isinstance(v, dict) and set(v) == {"from", "to", "steps"}:
        return f"{_number_text(v['from'])}:{_number_text(v['to'])}:{int(v['steps'])}"
    if isinstance(v, list):
        return ", ".join(_number_text(x) for x in v)
    return _number_text(v)


def incidence_value(text: str) -> Any:
    """The inverse of `incidence_text`; ValueError for anything else."""
    t = text.strip()
    if ":" in t:
        a, b, n = (p.strip() for p in t.split(":"))
        return {"from": _num(a), "to": _num(b), "steps": int(n)}
    if "," in t:
        return [_num(p) for p in t.split(",") if p.strip()]
    return _num(t)


def _num(t: str) -> int | float:
    t = t.strip()
    try:
        return int(t)
    except ValueError:
        return float(t)


def to_params(cfg: dict[str, Any]) -> dict[str, Any]:
    """The panel's values of a configuration."""
    c = copy.deepcopy(cfg)
    inc = c.get("incidence", {})
    trunc = c.get("truncation", {})
    period = c.get("period_um", {})
    out: dict[str, Any] = {
        "dimension": c.get("dimension", "1D"),
        "solver": c.get("solver", "rcwa"),
        "nx": int(trunc.get("Nx", 0)),
        "nz": int(trunc.get("Nz", 0)),
        "period_x": float(period.get("x", 0.0)),
        "period_z": float(period.get("z", 0.0)),
        "superstrate": c.get("superstrate", ""),
        "substrate": c.get("substrate", ""),
    }
    for k in INCIDENCE:
        out[k] = incidence_text(inc[k]) if k in inc else ""
    materials = []
    for name, m in c.get("materials", {}).items():
        n = m.get("n") if set(m) == {"n"} else None
        if isinstance(n, int | float) and not isinstance(n, bool):
            materials.append({"name": name, "kind": "n", "n": float(n), "k": 0.0, "json": None})
        elif isinstance(n, list) and len(n) == 2 and all(isinstance(x, int | float) for x in n):
            materials.append({"name": name, "kind": "n", "n": float(n[0]), "k": float(n[1]), "json": None})
        else:
            materials.append({"name": name, "kind": "json", "n": 1.0, "k": 0.0, "json": m})
    out["materials"] = materials
    layers = []
    for layer in c.get("layers", []):
        if layer.get("type") == "lamellar1d" and set(layer) == {"type", "thickness_um", "breakpoints_um", "materials"}:
            bps, mats = layer["breakpoints_um"], layer["materials"]
            layers.append({
                "type": "lamellar1d",
                "thickness": float(layer["thickness_um"]),
                "material": "",
                "lamellae": [{"from": float(x), "material": m} for x, m in zip(bps[:-1], mats, strict=True)],
                "end": float(bps[-1]),
                "json": None,
            })
        elif layer.get("type") == "homogeneous" and set(layer) == {"type", "thickness_um", "material"}:
            layers.append({"type": "homogeneous", "thickness": float(layer["thickness_um"]),
                           "material": layer["material"], "lamellae": [], "end": 0.0, "json": None})
        else:
            layers.append({"type": "json", "thickness": 0.0, "material": "", "lamellae": [], "end": 0.0,
                           "json": layer})
    out["layers"] = layers
    advanced = {k: v for k, v in c.items() if k not in _SHOWN}
    extra_inc = {k: v for k, v in inc.items() if k not in INCIDENCE}
    if extra_inc:
        advanced["incidence"] = extra_inc
    extra_trunc = {k: v for k, v in trunc.items() if k not in ("Nx", "Nz")}
    if extra_trunc:
        advanced["truncation"] = extra_trunc
    out["advanced"] = advanced
    out["_order"] = list(c)  # the key order of the file (written back the same way)
    # keys the file has (a key it lacks is written only when the user sets it off its default)
    out["_present"] = [k for k in ("dimension", "solver") if k in c] + [
        f"truncation.{k}" for k in ("Nx", "Nz") if k in trunc] + [f"period_um.{k}" for k in ("x", "z") if k in period]
    return out


_DEFAULTS = {"dimension": "1D", "solver": "rcwa", "truncation.Nx": 0, "truncation.Nz": 0,
             "period_um.x": 0.0, "period_um.z": 0.0}


def _keep(p: dict[str, Any], key: str, value: Any) -> bool:
    present = p.get("_present")
    return present is None or key in present or value != _DEFAULTS[key]


def to_config(p: dict[str, Any]) -> dict[str, Any]:
    """The configuration of the panel's values."""
    advanced = copy.deepcopy(p.get("advanced") or {})
    cfg: dict[str, Any] = {}
    for key, value in (("dimension", p["dimension"]), ("solver", p["solver"])):
        if _keep(p, key, value):
            cfg[key] = value
    trunc = {k: int(p[f]) for k, f in (("Nx", "nx"), ("Nz", "nz")) if _keep(p, f"truncation.{k}", int(p[f]))}
    trunc.update(advanced.pop("truncation", {}))
    if trunc:
        cfg["truncation"] = trunc
    period = {k: float(p[f]) for k, f in (("x", "period_x"), ("z", "period_z"))
              if _keep(p, f"period_um.{k}", float(p[f]))}
    if period:
        cfg["period_um"] = period
    inc: dict[str, Any] = {}
    for k in INCIDENCE:
        if str(p.get(k, "")).strip():
            inc[k] = incidence_value(str(p[k]))
    inc.update(advanced.pop("incidence", {}))
    cfg["incidence"] = inc
    materials: dict[str, Any] = {}
    for m in p.get("materials", []):
        if m["kind"] == "json":
            materials[m["name"]] = copy.deepcopy(m["json"])
        else:
            materials[m["name"]] = {"n": [float(m["n"]), float(m["k"])] if float(m["k"]) else float(m["n"])}
    cfg["materials"] = materials
    cfg["superstrate"] = p["superstrate"]
    cfg["substrate"] = p["substrate"]
    layers = []
    for layer in p.get("layers", []):
        if layer["type"] == "lamellar1d":
            rows = layer["lamellae"]
            layers.append({"type": "lamellar1d", "thickness_um": float(layer["thickness"]),
                           "breakpoints_um": [float(r["from"]) for r in rows] + [float(layer["end"])],
                           "materials": [r["material"] for r in rows]})
        elif layer["type"] == "homogeneous":
            layers.append({"type": "homogeneous", "thickness_um": float(layer["thickness"]),
                           "material": layer["material"]})
        else:
            layers.append(copy.deepcopy(layer["json"]))
    cfg["layers"] = layers
    cfg.update(advanced)
    order = [k for k in p.get("_order", []) if k in cfg] + [k for k in cfg if k not in p.get("_order", [])]
    return {k: cfg[k] for k in order}
