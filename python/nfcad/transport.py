"""JSON lines over a TCP connection to CAD on 127.0.0.1 (the same on all platforms; full duplex,
so the main thread can read while an action sends). The first line authenticates with the token
CAD put into the environment (NFCAD_TOKEN), so other local programs cannot take over the channel."""

from __future__ import annotations

import json
import os
import socket
import threading


class Transport:
    def __init__(self, server: str):
        host, _, port = server.rpartition(":")
        self._sock = socket.create_connection((host or "127.0.0.1", int(port)))
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._reader = self._sock.makefile("rb")
        self._lock = threading.Lock()
        self.send({"type": "auth", "token": os.environ.get("NFCAD_TOKEN", "")})

    def send(self, message: dict) -> None:
        data = (json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8")
        with self._lock:
            self._sock.sendall(data)

    def receive(self) -> dict | None:
        line = self._reader.readline()
        if not line:
            return None
        return json.loads(line.decode("utf-8"))
