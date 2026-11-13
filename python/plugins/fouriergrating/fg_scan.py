"""Scan results of FourierGrating (``solve_scan(...).columns``: one row per order and point) as
curves: efficiency of every order over the varying quantity, one curve per order and per
combination of the other varying quantities, plus ΣR and ΣT. numpy only, no FourierGrating."""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

#: the quantities a scan can vary, with axis labels
AXES = {
    "lambda_um": "λ / µm",
    "phi_deg": "φ / deg",
    "psi_deg": "ψ / deg",
    "delta_deg": "δ / deg",
    "sweep_value": "sweep value",
}
_SHORT = {"lambda_um": "λ", "phi_deg": "φ", "psi_deg": "ψ", "delta_deg": "δ", "sweep_value": "sweep"}


@dataclass
class Curve:
    name: str
    x: list[float]
    y: list[float]


def varying(columns: dict[str, Any]) -> list[str]:
    """The quantities with more than one value, in the order of AXES."""
    return [k for k in AXES if k in columns and len(np.unique(np.asarray(columns[k]))) > 1]


def x_axis(columns: dict[str, Any], choice: str = "auto") -> str:
    """The quantity on the x axis: `choice` if it varies, else the one with the most values
    (λ first on a tie); ValueError if nothing varies (a single point)."""
    var = varying(columns)
    if not var:
        raise ValueError("the scan has a single point – give a list or a range for λ, an angle or a sweep")
    if choice != "auto":
        if choice not in var:
            raise ValueError(f"{_SHORT.get(choice, choice)} does not vary in this scan (varying: "
                             + ", ".join(_SHORT[k] for k in var) + ")")
        return choice
    return max(var, key=lambda k: len(np.unique(np.asarray(columns[k]))))


def curves(columns: dict[str, Any], x_key: str, min_efficiency: float = 0.01,
           sweep_label: str = "sweep") -> list[Curve]:
    """ΣR, ΣT and every order whose efficiency reaches `min_efficiency` somewhere, over `x_key`;
    one curve per combination of the other varying quantities (named after them)."""
    cols = {k: np.asarray(v) for k, v in columns.items()}
    others = [k for k in varying(columns) if k != x_key]
    side, n, m = cols["side"].astype(str), cols["n"].astype(int), cols["m"].astype(int)
    eff = cols["efficiency"].astype(float)
    two_d = bool(np.any(m != 0))
    keys = np.stack([cols[k].astype(float) for k in others], axis=1) if others else np.zeros((len(eff), 0))
    out: list[Curve] = []
    for combo in (np.unique(keys, axis=0) if others else [np.zeros(0)]):
        here = np.all(keys == combo, axis=1) if others else np.ones(len(eff), bool)
        suffix = ""
        if others:
            parts = [f"{(sweep_label if k == 'sweep_value' else _SHORT[k])} = {v:g}" for k, v in zip(others, combo, strict=True)]
            suffix = " (" + ", ".join(parts) + ")"
        xs = np.unique(cols[x_key][here].astype(float))
        at = {float(x): i for i, x in enumerate(xs)}
        for s in ("R", "T"):
            total = np.zeros(len(xs))
            rows = here & (side == s)
            for x, e in zip(cols[x_key][rows].astype(float), eff[rows], strict=True):
                total[at[float(x)]] += e
            out.append(Curve(f"Σ{s}{suffix}", xs.tolist(), total.tolist()))
        for s, nn, mm in sorted({(a, int(b), int(c)) for a, b, c in zip(side[here], n[here], m[here], strict=True)}):
            rows = here & (side == s) & (n == nn) & (m == mm)
            if eff[rows].max(initial=0.0) < min_efficiency:
                continue
            y = np.full(len(xs), np.nan)  # an order that is evanescent at some points has no value there
            for x, e in zip(cols[x_key][rows].astype(float), eff[rows], strict=True):
                y[at[float(x)]] = e
            order = f"{nn},{mm}" if two_d else f"{nn}"
            ok = np.isfinite(y)  # NaN does not travel as JSON: the points are left out
            out.append(Curve(f"{s} {order}{suffix}", xs[ok].tolist(), y[ok].tolist()))
    return out


def write_csv(columns: dict[str, Any], path: str | Path) -> int:
    """All rows as CSV (as FourierGrating's efficiencies.csv; complex amplitudes as re/im);
    returns the number of rows."""
    names = list(columns)
    cols = [np.asarray(columns[k]) for k in names]
    head: list[str] = []
    for k, c in zip(names, cols, strict=True):
        head += [f"{k}_re", f"{k}_im"] if np.iscomplexobj(c) else [k]
    count = len(cols[0]) if cols else 0
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(head)
        for i in range(count):
            row: list[Any] = []
            for c in cols:
                v = c[i]
                row += [repr(float(v.real)), repr(float(v.imag))] if np.iscomplexobj(c) else [
                    repr(float(v)) if np.issubdtype(c.dtype, np.floating) else str(v)]
            w.writerow(row)
    return count
