"""Validated events shared by the flashing process, UI, and CLI. No GUI imports."""
from __future__ import annotations

import json
import math
import os
from dataclasses import asdict, dataclass, field
from pathlib import Path
import tempfile

PHASES = {
    "prepare": ("准备烧录", "准备阶段"),
    "connect": ("正在连接设备", "连接阶段"),
    "erase": ("正在擦除 Flash", "擦除阶段"),
    "program": ("正在写入 Flash", "写入阶段"),
    "verify": ("正在读回校验", "校验阶段"),
    "reset": ("正在复位启动", "启动阶段"),
    "done": ("烧录完成", "全部阶段完成"),
    "error": ("烧录失败", "任务失败"),
    "interrupted": ("烧录任务已中断", "未收到完成结果"),
}
TERMINAL = {"done", "error", "interrupted"}
NUMERIC_PHASES = {"erase", "program", "verify"}


def atomic_json(path: Path, value: dict) -> None:
    """Publish snapshots without readers seeing a half-written JSON document."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(value, stream, ensure_ascii=False, allow_nan=False)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def process_alive(pid: int | None) -> bool:
    if not pid:
        return False
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED_INFORMATION
        if not handle:
            # Access denied is different from a known terminated process.
            return ctypes.get_last_error() == 5
        try:
            code = wintypes.DWORD()
            return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


@dataclass
class TaskState:
    phase: str = "prepare"
    progress: float | None = None
    elapsed: float = 0.0
    message: str = "等待烧录任务"
    last_seq: int = 0
    completed: int | None = None
    total: int | None = None
    logs: list[str] = field(default_factory=list)

    def apply(self, event: dict) -> bool:
        """Ignore malformed, duplicate, stale, and post-terminal progress events."""
        seq = event.get("seq")
        if not isinstance(seq, int) or seq <= self.last_seq or self.phase in TERMINAL:
            return False
        kind = event.get("kind")
        elapsed = event.get("elapsed", self.elapsed)
        if not isinstance(elapsed, (float, int)) or not math.isfinite(elapsed) or elapsed < 0:
            return False
        if kind == "phase":
            phase = event.get("phase")
            if phase not in PHASES or phase in TERMINAL:
                return False
            self.phase, self.progress = phase, None
            self.completed = self.total = None
            self.message = str(event.get("message", PHASES[phase][0]))[:8192]
        elif kind == "progress":
            value = event.get("progress")
            if event.get("phase") != self.phase or self.phase not in NUMERIC_PHASES:
                return False
            if not isinstance(value, (float, int)) or not math.isfinite(value) or not 0 <= value <= 1:
                return False
            self.progress = value
            self.completed, self.total = event.get("completed"), event.get("total")
        elif kind == "terminal":
            phase = event.get("phase")
            if phase not in TERMINAL:
                return False
            self.phase = phase
            self.progress = 1.0 if phase == "done" else None
            self.message = str(event.get("message", PHASES[phase][0]))[:8192]
        elif kind == "log":
            pass
        else:
            return False
        self.last_seq, self.elapsed = seq, float(elapsed)
        if kind in {"phase", "terminal", "log"}:
            message = str(event.get("message", self.message)).replace("\r", " ")[:8192]
            self.logs = (self.logs + [f"{self.elapsed:6.1f}s  {message}"])[-120:]
        return True

    def snapshot(self) -> dict:
        return asdict(self)


class EventTail:
    """Follow complete UTF-8 JSONL records; preserve partial writes for the next poll."""
    def __init__(self, path: Path):
        self.path, self.position, self.pending = path, 0, b""

    def read(self) -> list[dict]:
        try:
            with self.path.open("rb") as stream:
                stream.seek(self.position)
                data = stream.read(256 * 1024)
                self.position = stream.tell()
        except FileNotFoundError:
            return []
        lines = (self.pending + data).split(b"\n")
        self.pending = lines.pop()
        events = []
        for line in lines:
            try:
                event = json.loads(line)
                if isinstance(event, dict):
                    events.append(event)
            except (ValueError, UnicodeError):
                continue
        return events
