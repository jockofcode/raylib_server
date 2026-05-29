"""
raylib_client.py — Python client library for raylib_server.

Mirrors the Ruby RaylibClient API.

Example:
    from raylib_client import RaylibClient

    with RaylibClient.connect() as rls:
        with rls.display_list("main"):
            rls.clear_background(color="RAYWHITE")
            rls.draw_circle_v(center=[400, 300], radius=50, color="RED")

        info = rls.get_server_info()
        print(f"Server v{info['version']} on port {info['port']}")
"""

from __future__ import annotations

import json
import os
import re
import secrets
import socket
from base64 import b64encode
from contextlib import contextmanager
from typing import Any

__all__ = ["RaylibClient", "RaylibError"]


class RaylibError(Exception):
    pass


# Commands that require a synchronous response from the server.
_SYNC_CMDS: frozenset[str] = frozenset({
    "LoadTexture", "LoadRenderTexture", "LoadFont", "LoadFontEx",
    "LoadSound", "LoadMusicStream", "LoadShader",
    "UnloadTexture", "UnloadRenderTexture", "UnloadFont",
    "UnloadSound", "UnloadMusicStream", "UnloadShader",
    "UploadTexture", "UploadTextureRaw", "UploadFont", "UploadSound",
    "UploadMusic", "UploadShader",
    "BeginUpload", "UploadChunk", "CommitUpload", "AbortUpload", "ListUploads",
    "MeasureText", "MeasureTextEx",
    "GetScreenWidth", "GetScreenHeight", "GetRenderWidth", "GetRenderHeight",
    "GetWindowPosition", "GetWindowScaleDPI",
    "IsWindowReady", "IsWindowFullscreen", "IsWindowHidden", "IsWindowMinimized",
    "IsWindowMaximized", "IsWindowFocused", "IsWindowResized",
    "GetFPS", "GetFrameTime", "GetTime",
    "GetMonitorCount", "GetCurrentMonitor",
    "GetMonitorWidth", "GetMonitorHeight", "GetMonitorName",
    "IsKeyPressed", "IsKeyDown", "IsKeyReleased", "IsKeyUp",
    "GetKeyPressed", "GetCharPressed",
    "IsMouseButtonPressed", "IsMouseButtonDown", "IsMouseButtonReleased",
    "GetMousePosition", "GetMouseDelta", "GetMouseWheelMove", "GetMouseWheelMoveV",
    "IsGamepadAvailable", "IsGamepadButtonPressed", "IsGamepadButtonDown",
    "GetGamepadAxisMovement",
    "GetTouchPointCount", "GetTouchPosition", "GetGestureDetected",
    "ListHandles", "GetTextureInfo", "GetFontInfo",
    "GetDisplayLists", "GetDisplayListCommands", "GetServerInfo",
    "Subscribe", "Unsubscribe",
    "LoadModel",
    "TimerCreate", "TimerOnce", "ListTimers",
})

# Post-processing rules for snake_case → CamelCase conversion.
_CMD_FIXES: list[tuple[str, str]] = [
    ("Fps",    "FPS"),
    ("Dpi",    "DPI"),
    ("Mode2d", "Mode2D"),
    ("Mode3d", "Mode3D"),
    ("Npatch", "NPatch"),
]

DEFAULT_CHUNK_SIZE = 48 * 1024  # 48 KiB raw → ~64 KiB base64


def _snake_to_cmd(name: str) -> str:
    """Convert a snake_case method name to the CamelCase wire protocol command."""
    result = "".join(part.capitalize() for part in name.split("_"))
    for from_, to in _CMD_FIXES:
        result = result.replace(from_, to)
    return result


class RaylibClient:
    """TCP client for raylib_server.

    Prefer the context-manager form to ensure the socket is closed:

        with RaylibClient.connect() as rls:
            rls.draw_text(text="hi", posX=10, posY=10, fontSize=20, color="WHITE")
    """

    @classmethod
    def connect(cls, host: str = "localhost", port: int = 7878) -> "RaylibClient":
        """Open a connection and return a new client."""
        sock = socket.create_connection((host, port))
        return cls(sock)

    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._file = sock.makefile("rwb", buffering=0)
        self._batch: list[str] | None = None

    # ------------------------------------------------------------------
    # Context manager
    # ------------------------------------------------------------------

    def __enter__(self) -> "RaylibClient":
        return self

    def __exit__(self, *_: Any) -> None:
        self.close()

    # ------------------------------------------------------------------
    # Low-level command API
    # ------------------------------------------------------------------

    def cmd(self, name: str, args: dict | None = None) -> None:
        """Send a fire-and-forget command (no response expected)."""
        msg: dict[str, Any] = {"cmd": name}
        if args:
            msg["args"] = args
        line = json.dumps(msg, separators=(",", ":")) + "\n"
        if self._batch is not None:
            self._batch.append(line)
        else:
            self._file.write(line.encode())

    def sync(self, name: str, args: dict | None = None) -> dict | None:
        """Send a synchronous command and return the result dict.

        Raises RaylibError on server error or disconnection.
        """
        req_id = "p" + secrets.token_hex(3)
        msg: dict[str, Any] = {"id": req_id, "cmd": name}
        if args:
            msg["args"] = args
        line = json.dumps(msg, separators=(",", ":")) + "\n"
        self._file.write(line.encode())
        raw = self._file.readline()
        if not raw:
            raise RaylibError("server disconnected")
        resp = json.loads(raw)
        if not resp.get("ok"):
            raise RaylibError(f"{name} failed: {resp.get('error')}")
        return resp.get("result")

    # ------------------------------------------------------------------
    # Batch mode
    # ------------------------------------------------------------------

    @contextmanager
    def batch(self):
        """Buffer all fire-and-forget commands and flush in a single write."""
        self._batch = []
        try:
            yield self
        finally:
            data = self._batch
            self._batch = None
            if data:
                self._file.write("".join(data).encode())

    # ------------------------------------------------------------------
    # Display list DSL
    # ------------------------------------------------------------------

    @contextmanager
    def display_list(self, name: str):
        """Record draw commands into a named display list."""
        self.cmd("DisplayListBegin", {"name": name})
        try:
            yield self
        finally:
            self.cmd("DisplayListEnd")

    # ------------------------------------------------------------------
    # Event streaming
    # ------------------------------------------------------------------

    def subscribe(self, *events: str) -> dict | None:
        flat = [e for ev in events for e in (ev if isinstance(ev, list) else [ev])]
        return self.sync("Subscribe", {"events": flat})

    def unsubscribe(self, *events: str) -> dict | None:
        flat = [e for ev in events for e in (ev if isinstance(ev, list) else [ev])]
        return self.sync("Unsubscribe", {"events": flat})

    def drain_events(self) -> list[dict]:
        """Non-blocking: return all immediately available server-push events."""
        events: list[dict] = []
        self._sock.setblocking(False)
        try:
            while True:
                raw = self._file.readline()
                if not raw:
                    break
                try:
                    events.append(json.loads(raw))
                except json.JSONDecodeError:
                    pass
        except (BlockingIOError, OSError):
            pass
        finally:
            self._sock.setblocking(True)
        return events

    # ------------------------------------------------------------------
    # Upload helpers
    # ------------------------------------------------------------------

    def chunked_upload(
        self,
        path: str,
        *,
        file_type: str,
        resource_type: str,
        chunk_size: int = DEFAULT_CHUNK_SIZE,
    ) -> int:
        """Upload a file using the chunked upload protocol. Returns the handle."""
        with open(path, "rb") as f:
            data = f.read()
        return self.upload_data(
            data,
            file_type=file_type,
            resource_type=resource_type,
            name=os.path.basename(path),
            chunk_size=chunk_size,
        )

    def upload_data(
        self,
        data: bytes,
        *,
        file_type: str,
        resource_type: str,
        name: str = "upload",
        chunk_size: int = DEFAULT_CHUNK_SIZE,
    ) -> int:
        """Upload raw bytes using the chunked upload protocol. Returns the handle."""
        total = len(data)
        result = self.sync("BeginUpload", {"name": name, "fileType": file_type, "totalBytes": total})
        upload_id = result["uploadId"]

        seq = 0
        pos = 0
        while pos < total:
            chunk = data[pos: pos + chunk_size]
            self.sync("UploadChunk", {
                "uploadId": upload_id,
                "seq": seq,
                "data": b64encode(chunk).decode(),
            })
            pos += len(chunk)
            seq += 1

        result = self.sync("CommitUpload", {"uploadId": upload_id, "type": resource_type})
        return result["handle"]

    # ------------------------------------------------------------------
    # Connection management
    # ------------------------------------------------------------------

    def close(self) -> None:
        try:
            self._file.close()
        except OSError:
            pass
        try:
            self._sock.close()
        except OSError:
            pass

    @property
    def connected(self) -> bool:
        return not self._sock._closed  # type: ignore[attr-defined]

    # ------------------------------------------------------------------
    # __getattr__: snake_case → CamelCase dispatch
    #
    # Any attribute access that doesn't resolve to a real method is
    # interpreted as a command call.  Sync commands return the result
    # dict; fire-and-forget commands return None.
    #
    # Usage:
    #   rls.draw_circle(centerX=400, centerY=300, radius=50, color="RED")
    #   width = rls.get_screen_width()["width"]
    # ------------------------------------------------------------------

    def __getattr__(self, name: str):
        cmd_name = _snake_to_cmd(name)

        def _dispatch(**kwargs: Any) -> dict | None:
            args = kwargs or None
            if cmd_name in _SYNC_CMDS:
                return self.sync(cmd_name, args)
            else:
                self.cmd(cmd_name, args)
                return None

        return _dispatch

    # ------------------------------------------------------------------
    # Utility
    # ------------------------------------------------------------------

    @staticmethod
    def snake_to_cmd(name: str) -> str:
        """Convert snake_case to CamelCase raylib command name."""
        return _snake_to_cmd(name)
