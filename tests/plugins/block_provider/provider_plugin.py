"""A plugin with one block type for the router test (ADR 0010): doubles a number."""

import os

from nfcad import Plugin


class Provider(Plugin):
    def blocks(self):
        return [{"type": "provider.double", "category": "domain", "sockets": []}]

    def evaluate_block(self, request):
        if request.get("crash"):
            os._exit(3)  # the process dies while evaluating
        return {"ok": True, "value": 2 * request["x"]}
