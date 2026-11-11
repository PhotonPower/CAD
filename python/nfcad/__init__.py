"""nfcad – Python side of the Nurbforge CAD plugin interface (docs/plugins.md, docs/plugin-api.md).

A plugin is a directory with ``plugin.json`` and a Python module that defines a subclass of
:class:`Plugin`. Nurbforge CAD starts it in its own process (``python -m nfcad.host``) and calls
its actions; the actions talk to CAD through the :class:`Context` they receive.
"""

from .plugin import API_VERSION, Plugin, action
from .context import Cancelled, Context

__all__ = ["API_VERSION", "Plugin", "action", "Context", "Cancelled"]
