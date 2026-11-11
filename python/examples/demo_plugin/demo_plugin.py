"""Demo plugin (docs/plugin-api.md): no kernel library needed, only the nfcad package."""

import math
import random
import time

from nfcad import Param, Plugin, action
from nfcad import geometry as g


class DemoPlugin(Plugin):
    params = {
        "rays": Param.integer(300, min=1, max=100_000, label="Number of rays", group="Rays"),
        "length": Param.number(20.0, min=0.1, unit="mm", label="Length", group="Rays"),
        "origin": Param.point((0, 0, 0), label="Origin", group="Rays",
                              description="Start point of the fan (Pick: click in a view)"),
        "colormap": Param.choice(["viridis", "inferno", "plasma", "coolwarm", "grey"], "viridis",
                                 label="Colour map", group="Rays"),
        "wavelengths": Param.numbers([400, 700], unit="nm", label="Wavelength range", group="Rays"),
        "target": Param.entity(kinds=["surface", "trimmed_surface", "shell"], label="Field on",
                               group="Field", description="Surfaces that get the demo field"),
    }

    @action("Ray fan from the origin")
    def rays(self, ctx):
        p = ctx.params
        n = p["rays"]
        ox, oy, oz = p["origin"]
        lo, hi = (p["wavelengths"] + [400, 700])[:2]
        points, starts, values = [], [], []
        for k in range(n):
            starts.append(len(points))
            a = 2 * math.pi * k / n
            for t in (0.0, p["length"] / 2, p["length"]):
                points.append((ox + t * math.cos(a), oy + t * math.sin(a),
                               oz + 0.25 * p["length"] * math.sin(3 * a) * t / p["length"]))
                values.append(lo + (hi - lo) * k / n)
            ctx.progress(k / n, "tracing")
            ctx.check_cancel()
        starts.append(len(points))
        run = ctx.result_set("Demo rays")
        ctx.lines(run, "Rays", points, starts, values, unit="nm", colormap=p["colormap"])
        return f"{n} rays"

    @action("Field on the selected surfaces")
    def field(self, ctx):
        run = ctx.result_set("Demo field")
        target = ctx.params["target"]
        for e in ctx.entities(ids=[target]) if target is not None else ctx.entities(selection=True):
            w = h = 64
            values = [math.sin(6 * i / w) * math.cos(6 * j / h) for j in range(h) for i in range(w)]
            ctx.field(run, f"Pattern on {e['name'] or e['id']}", e["id"], values, w, h, colormap="coolwarm")
        return "done"

    @action("Live curve (until cancelled)")
    def live(self, ctx):
        run = ctx.result_set("Demo optimisation")
        curve = ctx.curve(run, "Merit", x_label="iteration", y_label="merit")
        k = 0
        while not ctx.cancelled() and k < 200:
            ctx.append(curve, k, 1.0 / (1 + 0.1 * k) + 0.02 * random.random())
            ctx.progress(k / 200, f"iteration {k}")
            time.sleep(0.05)
            k += 1
        return f"{k} iterations"

    @action("Add a helix-like polyline")
    def add(self, ctx):
        pts = [(5 * math.cos(t / 5), 5 * math.sin(t / 5), t / 10) for t in range(100)]
        eid = ctx.add(g.polyline(pts), name="demo polyline", attributes={"demo.created": True})
        return f"entity {eid}"
