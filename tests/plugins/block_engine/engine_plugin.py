"""A plugin that asks for the other plugins' blocks and has one evaluated (router test, ADR 0010)."""

import json

from nfcad import Param, Plugin, action


class Engine(Plugin):
    params = {"x": Param.integer(21), "crash": Param.boolean(False)}

    @action("Ask")
    def ask(self, ctx):
        catalogues = ctx.block_catalogues()
        try:
            answer = ctx.evaluate_block("provider", {"x": ctx.params["x"], "crash": ctx.params["crash"]})
        except RuntimeError as e:
            answer = {"failed": str(e)}
        return json.dumps({"catalogues": catalogues, "answer": answer}, sort_keys=True)
