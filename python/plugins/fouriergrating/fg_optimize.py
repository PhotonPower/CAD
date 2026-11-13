"""Optimisation of a grating with FourierGrating's exact gradients (adjoint, M19): objective
L = Σ w · efficiency(side, n, m), parameters by JSON pointer with bounds, L-BFGS-B (scipy).
No CAD here: `optimize` reports each step to a callback and asks `stop` between steps."""

from __future__ import annotations

import copy
import math
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any


def pointer_get(cfg: dict[str, Any], pointer: str) -> Any:
    """The value at a JSON pointer (RFC 6901) of the configuration; KeyError with the pointer."""
    if not pointer.startswith("/"):
        raise KeyError(f"{pointer!r} is no JSON pointer (it starts with /, e.g. /layers/0/thickness_um)")
    node: Any = cfg
    for raw in pointer[1:].split("/"):
        part = raw.replace("~1", "/").replace("~0", "~")
        try:
            node = node[int(part)] if isinstance(node, list) else node[part]
        except (KeyError, IndexError, ValueError, TypeError):
            raise KeyError(f"{pointer} is not in the configuration") from None
    return node


def pointer_set(cfg: dict[str, Any], pointer: str, value: float) -> None:
    *path, last = (p.replace("~1", "/").replace("~0", "~") for p in pointer[1:].split("/"))
    node: Any = cfg
    for part in path:
        node = node[int(part)] if isinstance(node, list) else node[part]
    if isinstance(node, list):
        node[int(last)] = value
    else:
        node[last] = value


@dataclass
class Parameter:
    pointer: str
    lower: float
    upper: float


@dataclass
class Outcome:
    values: list[float]  # the optimum, per parameter
    start: list[float]
    objective: float  # L at the optimum
    start_objective: float
    steps: int  # evaluations of L and its gradient
    message: str
    stopped: bool  # by the user

    def config(self, cfg: dict[str, Any], parameters: list[Parameter]) -> dict[str, Any]:
        """The configuration with the optimum."""
        out = copy.deepcopy(cfg)
        for p, v in zip(parameters, self.values, strict=True):
            pointer_set(out, p.pointer, float(v))
        return out


def objective_of(terms: list[dict[str, Any]]) -> dict[tuple[str, int, int], float]:
    """{(side, n, m): weight} of the panel's rows; ValueError for an empty or unknown term."""
    out: dict[tuple[str, int, int], float] = {}
    for t in terms:
        side = str(t["side"])
        if side not in ("R", "T"):
            raise ValueError(f"side {side!r}: R or T")
        key = (side, int(t["n"]), int(t.get("m", 0)))
        out[key] = out.get(key, 0.0) + float(t["weight"])
    if not out:
        raise ValueError("no objective terms – add a row (side, n, m, weight)")
    return out


def parameters_of(rows: list[dict[str, Any]], cfg: dict[str, Any]) -> list[Parameter]:
    """The panel's parameter rows, checked against the configuration (a number there, within its
    bounds); ValueError / KeyError with the reason."""
    out = []
    for r in rows:
        p = Parameter(str(r["pointer"]).strip(), float(r["lower"]), float(r["upper"]))
        v = pointer_get(cfg, p.pointer)
        if isinstance(v, bool) or not isinstance(v, int | float):
            raise ValueError(f"{p.pointer} is {v!r}, not a number")
        if not p.lower <= float(v) <= p.upper:
            raise ValueError(f"{p.pointer} = {v} lies outside its bounds [{p.lower}, {p.upper}]")
        out.append(p)
    if not out:
        raise ValueError("no parameters – add a row (JSON pointer, lower, upper)")
    return out


def optimize(
    value_and_grad: Callable[[list[float]], tuple[float, list[float]]],
    start: list[float],
    parameters: list[Parameter],
    *,
    maximize: bool = True,
    iterations: int = 50,
    step: Callable[[int, float, list[float]], None] | None = None,
    stop: Callable[[], bool] | None = None,
) -> Outcome:
    """L-BFGS-B over the parameters within their bounds. `value_and_grad(x)` gives L and dL/dx;
    `step(k, L, x)` is told every evaluation; `stop()` ends the search after the current one (the
    best point so far is the outcome)."""
    import numpy as np
    from scipy.optimize import minimize

    sign = -1.0 if maximize else 1.0
    best: dict[str, Any] = {"x": list(start), "L": math.nan, "k": 0}
    first: dict[str, float] = {}

    class _Stop(Exception):
        pass

    def fun(x: Any) -> tuple[float, Any]:
        if stop is not None and stop():
            raise _Stop
        L, g = value_and_grad([float(v) for v in x])
        best["k"] += 1
        if "L" not in first:
            first["L"] = L
        if math.isnan(best["L"]) or sign * L < sign * best["L"]:
            best["x"], best["L"] = [float(v) for v in x], float(L)
        if step is not None:
            step(best["k"], float(L), [float(v) for v in x])
        return sign * float(L), sign * np.asarray(g, dtype=float)

    stopped = False
    message = ""
    try:
        res = minimize(fun, np.asarray(start, float), jac=True, method="L-BFGS-B",
                       bounds=[(p.lower, p.upper) for p in parameters], options={"maxiter": int(iterations)})
        message = str(res.message)
    except _Stop:
        stopped, message = True, "stopped"
    return Outcome(best["x"], list(start), best["L"], first.get("L", math.nan), best["k"], message, stopped)
