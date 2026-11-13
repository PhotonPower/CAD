"""A plugin with a block for the node workflow (ADR 0010 test): RTT-Fusion's thin lens as
"lensprov.thin_lens", evaluated in this plugin's process."""

import dataclasses
import os

from nfcad import Plugin
from rtt_fusion.graph import builtin_registry, optics
from rtt_fusion.graph.remote import BlockProvider

_registry = builtin_registry()
_evaluators = optics.register(_registry)
_lens = _evaluators["optics.thin_lens"]


def _crashing_lens(ctx, node, socket, inputs):
    if node.params.get("crash"):
        os._exit(4)  # the process dies while evaluating
    return _lens(ctx, node, socket, inputs)


_crashing_lens.representations = getattr(_lens, "representations", frozenset("R"))


class LensProvider(Plugin):
    def __init__(self):
        lens = dataclasses.replace(_registry.get("optics.thin_lens"), type_id="lensprov.thin_lens", port_frames=None)
        self.provider = BlockProvider([lens], {"lensprov.thin_lens": _crashing_lens})

    def blocks(self):
        return self.provider.catalogue()

    def evaluate_block(self, request):
        return self.provider.evaluate(request)
