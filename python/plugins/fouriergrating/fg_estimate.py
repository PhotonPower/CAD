"""Rough size and memory of a computation – for display only, no physics. Taken over from
GUI-FourierGrating (`fg_gui/estimate.py`, MIT): numbers read from the configuration (orders,
lengths of the scan lists) times a rule-of-thumb factor. They can be far off (RK solver, many slices,
GPU); they serve as a warning."""

from __future__ import annotations

import os
from typing import Any

#: complex n x n matrices in memory at once per solve (rule of thumb: eigenproblem, S-matrices)
MATRICES_PER_SOLVE = 12
BYTES_COMPLEX = 16
#: share of the RAM above which the plugin warns
WARN_FRACTION = 0.7


def _count(v: Any) -> int:
    """Number of values of an incidence entry (number, list, range)."""
    if isinstance(v, list):
        return max(len(v), 1)
    if isinstance(v, dict) and isinstance(v.get("steps"), int):
        return max(v["steps"], 1)
    return 1


def scan_points(cfg: dict) -> int:
    """Scan points per sweep value: product of the lengths of λ, φ, ψ, δ."""
    inc = cfg.get("incidence") if isinstance(cfg.get("incidence"), dict) else {}
    n = 1
    for k in ("lambda_um", "phi_deg", "psi_deg", "delta_deg"):
        n *= _count(inc.get(k, 0))
    return n


def sweep_values(cfg: dict) -> int:
    sw = cfg.get("sweep")
    return _count(sw.get("values")) if isinstance(sw, dict) else 1


def fourier_terms(cfg: dict) -> int:
    """n_f = (2 Nx + 1)(2 Nz + 1) in 2D, 2 Nx + 1 in 1D (FourierGrating config_format.md)."""
    t = cfg.get("truncation") if isinstance(cfg.get("truncation"), dict) else {}
    nx = int(t.get("Nx", 0) or 0)
    nz = int(t.get("Nz", 0) or 0)
    return (2 * nx + 1) * ((2 * nz + 1) if cfg.get("dimension") == "2D" else 1)


def matrix_size(cfg: dict) -> int:
    """n = 4 n_f (size of the eigenvalue matrix, FourierGrating user_guide.md)."""
    return 4 * fourier_terms(cfg)


def memory_bytes(cfg: dict, cores: int | None = None, points: int | None = None) -> int:
    """Rule of thumb: parallel scan points x matrices per solve x n² x 16 bytes; `points`: the
    points run at once (default: the scan's, at most the cores)."""
    cores = cores or os.cpu_count() or 1
    parallel = min(scan_points(cfg) if points is None else points, cores)
    n = matrix_size(cfg)
    return parallel * MATRICES_PER_SOLVE * n * n * BYTES_COMPLEX


def total_ram_bytes() -> int | None:
    try:
        return os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
    except (ValueError, OSError, AttributeError):
        return None


def format_bytes(n: float) -> str:
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if abs(n) < 1024 or unit == "TiB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} TiB"


def summary(cfg: dict, cores: int | None = None, points: int | None = None,
            ram: int | None = None) -> dict[str, Any]:
    mem = memory_bytes(cfg, cores, points)
    ram = total_ram_bytes() if ram is None else ram
    return {
        "scan_points": scan_points(cfg),
        "sweep_values": sweep_values(cfg),
        "matrix_size": matrix_size(cfg),
        "memory_bytes": mem,
        "memory_text": format_bytes(mem),
        "ram_fraction": (mem / ram) if ram else None,
    }


def text(e: dict[str, Any]) -> str:
    return (f"{e['scan_points'] * e['sweep_values']} solve(s) · matrix size n = {e['matrix_size']} · "
            f"memory roughly {e['memory_text']} (rule of thumb)")


def warning(e: dict[str, Any]) -> str:
    """A warning if the estimate exceeds WARN_FRACTION of the RAM, else ""."""
    f = e["ram_fraction"]
    if f is None or f <= WARN_FRACTION:
        return ""
    return (f"the estimated memory is {f:.0%} of the RAM – fewer orders (Nx, Nz) or fewer parallel scan points "
            "(threads.scan under Advanced)")
