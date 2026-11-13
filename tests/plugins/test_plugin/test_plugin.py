import os
import time

from nfcad import Param, Plugin, UserError, action
from nfcad import geometry as g


class TestPlugin(Plugin):
    params = {
        "count": Param.integer(3, min=1),
        "scale": Param.number(1.5, unit="mm"),
        "where": Param.point((1, 2, 3)),
        "target": Param.entity(kinds=["curve"]),
        "list": Param.numbers([1, 2]),
    }

    @action("Echo parameters")
    def echo(self, ctx):
        p = ctx.params
        return f"{p['count']}|{p['scale']}|{p['where']}|{p['target']}|{p['list']}"

    @action("Model and results")
    def model(self, ctx):
        sel = ctx.entities(selection=True)
        assert len(sel) == 1, sel
        target = sel[0]
        assert target["geometry"]["type"] == "curve"
        ctx.replace(target["id"], g.line((0, 0, 0), (0, 0, 7)))
        ctx.set_attribute(target["id"], "rtt.role", "mirror")
        new = ctx.add(g.line((1, 1, 1), (2, 2, 2)), name="from plugin", attributes={"x": 1})
        run = ctx.result_set("test run")
        ctx.lines(run, "rays", [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 1, 1)], [0, 2, 4], [1, 2, 3, 4], unit="nm")
        ctx.field(run, "field", target["id"], [0.0, 1.0, 2.0, 3.0, 4.0, 5.0], 3, 2)
        c = ctx.curve(run, "merit", [0], [1.0])
        ctx.append(c, 1, 0.5)
        ctx.image(run, "camera", [0.0, 1.0, 2.0, 3.0, 4.0, 5.0], 3, 2, extent=(-1.5, 1.5, -1.0, 1.0),
                  unit="W/mm²", description="6 px")
        ctx.progress(1.0, "done")
        ctx.log("hello from the plugin")
        return f"new {new}"

    @action("Model tree")
    def tree(self, ctx):
        system = ctx.add_group("system")
        lens = ctx.add_group("lens", system)
        ctx.add(g.line((0, 0, 0), (0, 0, 1)), name="in lens", group=lens)
        old = next(e for e in ctx.entities() if e["name"] == "from plugin")
        ctx.replace(old["id"], g.line((5, 5, 5), (6, 6, 6)))  # changed, then removed
        ctx.remove(old["id"])
        temp = ctx.add(g.point((0, 0, 0)), name="temp")  # added and removed: no trace
        ctx.remove(temp)
        ctx.remove_group(ctx.add_group("temp"))
        return ",".join(f"{x['name']}<{x['parent']}" for x in ctx.groups())

    @action("Loop until cancelled")
    def loop(self, ctx):
        while True:
            ctx.check_cancel()
            time.sleep(0.01)

    @action("On a block", block_types=("demo.block", "demo.other"))
    def on_block(self, ctx):
        return f"{ctx.target.get('block', '')}|{'/'.join(ctx.target.get('scope', []))}"

    @action("Set parameters")
    def set_params(self, ctx):
        ctx.set_params({"count": 9})
        try:
            ctx.set_params({"nonsense": 1})
        except RuntimeError as e:
            return f"{ctx.params['count']}|refused: {e}"
        return "not refused"

    @action("Tables")
    def tables(self, ctx):
        run = ctx.result_set("orders")
        ctx.table(run, "orders", ["n", "efficiency"], [[-1, 0.25], [0, 0.5]])
        ctx.bars(run, "R", ["-1", "0"], [0.25, 0.5], y_label="efficiency")
        try:
            ctx.table(run, "broken", ["a", "b"], [[1]])
        except RuntimeError as e:
            return f"refused: {e}"
        return "not refused"

    @action("Export STEP")
    def export_step(self, ctx):
        import os
        import tempfile

        bodies = [e["id"] for e in ctx.entities() if e["name"].startswith("body")]
        path = os.path.join(tempfile.mkdtemp(), "bodies.step")
        try:
            ctx.export_step(bodies + [e["id"] for e in ctx.entities() if e["name"] == "target"], path)
        except RuntimeError as e:
            refused = str(e)
        return f"{ctx.export_step(bodies, path)}|{path}|{refused}"

    @action("Needs input")
    def needs_input(self, ctx):
        raise UserError("Choose a file first.")

    @action("Raise")
    def fail(self, ctx):
        raise ValueError("deliberate")

    @action("Crash")
    def crash(self, ctx):
        os._exit(3)
