"""Plugin process: ``python -m nfcad.host --server <name> --plugin <dir>`` (started by CAD).

Loads the plugin named in <dir>/plugin.json, says hello, runs actions in a worker thread and
forwards requests / replies; the main thread keeps reading so that Cancel always arrives."""

from __future__ import annotations

import argparse
import importlib
import json
import sys
import threading
import traceback
from pathlib import Path

from .context import Cancelled, Context
from .plugin import API_VERSION, Plugin
from .transport import Transport


class Host:
    def __init__(self, server: str, plugin_dir: Path):
        self.transport = Transport(server)
        manifest = json.loads((plugin_dir / "plugin.json").read_text(encoding="utf-8"))
        sys.path.insert(0, str(plugin_dir))
        module_name, _, class_name = manifest["entry"].partition(":")
        cls = getattr(importlib.import_module(module_name), class_name)
        if not (isinstance(cls, type) and issubclass(cls, Plugin)):
            raise TypeError(f"{manifest['entry']} is not an nfcad.Plugin")
        self.plugin = cls()
        self.manifest = manifest
        self._pending: dict[int, dict] = {}
        self._events: dict[int, threading.Event] = {}
        self._next_rid = 1
        self._lock = threading.Lock()
        self._jobs: dict[int, Context] = {}

    def send(self, message: dict) -> None:
        self.transport.send(message)

    def request(self, method: str, args: dict):
        with self._lock:
            rid = self._next_rid
            self._next_rid += 1
            event = threading.Event()
            self._events[rid] = event
        self.send({"type": "request", "rid": rid, "method": method, "args": args})
        event.wait()
        reply = self._pending.pop(rid)
        if not reply.get("ok", False):
            raise RuntimeError(reply.get("error", f"{method} failed"))
        return reply.get("result")

    def run(self) -> int:
        actions = [{"id": k, **v} for k, v in self.plugin.actions().items()]
        self.send({"type": "hello", "api": API_VERSION, "id": self.manifest["id"],
                   "name": self.manifest.get("name", self.manifest["id"]),
                   "version": self.manifest.get("version", ""), "actions": actions,
                   "params": self.plugin.params_schema()})
        while True:
            msg = self.transport.receive()
            if msg is None or msg.get("type") == "shutdown":
                return 0
            kind = msg.get("type")
            if kind == "reply":
                rid = msg["rid"]
                self._pending[rid] = msg
                self._events.pop(rid).set()
            elif kind == "run":
                params = self.plugin.convert_params(msg.get("params", {}))
                ctx = Context(self, msg["job"], params, msg.get("selection", []))
                self._jobs[ctx.job] = ctx
                threading.Thread(target=self._run_job, args=(ctx, msg["action"]), daemon=True).start()
            elif kind == "cancel":
                ctx = self._jobs.get(msg.get("job"))
                if ctx is not None:
                    ctx._cancel.set()

    def _run_job(self, ctx: Context, action: str) -> None:
        try:
            message = getattr(self.plugin, action)(ctx)
            self.send({"type": "done", "job": ctx.job, "ok": True, "message": message or ""})
        except Cancelled:
            self.send({"type": "done", "job": ctx.job, "ok": False, "cancelled": True, "message": "cancelled"})
        except Exception as e:  # report to CAD instead of dying
            self.send({"type": "log", "job": ctx.job, "text": traceback.format_exc()})
            self.send({"type": "done", "job": ctx.job, "ok": False, "message": f"{type(e).__name__}: {e}"})
        finally:
            self._jobs.pop(ctx.job, None)


def main() -> int:
    parser = argparse.ArgumentParser(description="Raytatouille Fusion plugin host")
    parser.add_argument("--server", required=True)
    parser.add_argument("--plugin", required=True, type=Path)
    args = parser.parse_args()
    return Host(args.server, args.plugin).run()


if __name__ == "__main__":
    sys.exit(main())
