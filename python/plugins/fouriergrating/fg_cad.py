"""CAD bodies as a FourierGrating `cad` layer (docs/cad.md of FourierGrating): the layer entry for
bodies written as STEP, the material of each body, and the slicer's report as table rows. No
FourierGrating, no CAD here."""

from __future__ import annotations

from typing import Any


def material_of(entity: dict[str, Any], materials: list[str]) -> str:
    """The material of a body: its attribute "material", else its name if that names a material;
    ValueError otherwise."""
    attr = (entity.get("attributes") or {}).get("material")
    if attr:
        if str(attr) not in materials:
            raise ValueError(f"{entity.get('name') or entity['id']}: material {attr!r} is not in the panel's "
                             f"materials ({', '.join(materials)})")
        return str(attr)
    name = str(entity.get("name", ""))
    if name in materials:
        return name
    raise ValueError(f"body {name or entity['id']!r} has no material: give it the attribute \"material\" or name "
                     f"it after one of {', '.join(materials)}")


def layer(step_file: str, materials: list[str], *, unit: str = "um", background: str = "superstrate",
          rcwa_slices: int = 30) -> dict[str, Any]:
    """The `cad` layer: bodies by their position in the STEP file (the order they were written in),
    CAD +Z the stacking direction (FourierGrating's y), CAD +X its x."""
    return {
        "type": "cad",
        "file": step_file,
        "unit": unit,
        "axes": {"y": "+Z", "x": "+X"},
        "background": background,
        "bodies": [{"index": i, "material": m} for i, m in enumerate(materials)],
        "slicing": {"rcwa_slices": int(rcwa_slices)},
    }


def report_rows(name: str, report: dict[str, Any]) -> tuple[list[list[Any]], list[list[Any]]]:
    """Findings (layer, severity, code, message, where) and bands (layer, y from, y to, kind,
    materials) of `fouriergrating.cad.check`."""
    findings = []
    for severity in ("errors", "warnings"):
        for f in report.get(severity) or []:
            where = f.get("where_um")
            findings.append([name, severity[:-1], f.get("code", ""), f.get("message", ""),
                             "" if where is None else ", ".join(f"{float(v):.4g}" for v in where)])
    bands = []
    for b in report.get("bands") or []:
        fractions = ", ".join(f"{m} {v:.3f}" for m, v in sorted((b.get("area_fractions") or {}).items()))
        bands.append([name, float(b["y0_um"]), float(b["y1_um"]), b.get("kind", ""), fractions])
    return findings, bands
