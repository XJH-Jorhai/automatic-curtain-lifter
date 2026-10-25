"""Small task API: filesystem events, no daemon, socket, or GUI on the SWD thread."""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import uuid

from .model import NUMERIC_PHASES, PHASES, TERMINAL, atomic_json

WORKSPACE = Path(__file__).resolve().parents[2]
SESSION_ROOT = WORKSPACE / "work" / "flash-monitor"


class FlashTask:
    """One flashing attempt. Closing its viewer never stops or restarts hardware work."""
    def __init__(self, *, firmware: str, target: str = "STM32G030C8T6",
                 probe: str = "CMSIS-DAP", transport: str = "SWD",
                 show_ui: bool = True, demo: bool = False, hold: bool = False,
                 session_root: Path | None = None):
        self.id = uuid.uuid4().hex
        self.directory = (session_root or SESSION_ROOT) / self.id
        self.manifest = {
            "schema": 1, "id": self.id, "firmware": str(firmware), "target": target,
            "probe": probe, "transport": transport, "demo": demo,
            "owner_pid": os.getpid(), "created_at": time.time(),
            "success_seconds": 30.0 if hold else 4.0, "failure_seconds": 120.0,
        }
        self.show_ui, self.phase_name = show_ui, "prepare"
        self.seq, self.started, self.finished = 0, 0.0, False
        self.ui_process = None
        self._lock = threading.Lock()
        self._last_progress = (-1.0, 0.0)
        self._io_warning = False

    def __enter__(self):
        self.directory.mkdir(parents=True, exist_ok=False)
        self.started = time.monotonic()
        atomic_json(self.directory / "task.json", self.manifest)
        (self.directory / "events.jsonl").touch()
        self.directory.joinpath("commands").mkdir()
        self.phase("prepare", "模拟演示 · 不连接硬件" if self.manifest["demo"] else "烧录任务已由 Codex 启动")
        if self.show_ui:
            self.open_ui()
        print("FLASH_MONITOR " + json.dumps({"session": self.id, "path": str(self.directory),
                                             "ui_pid": self.ui_process.pid if self.ui_process else None},
                                            ensure_ascii=True), flush=True)
        return self

    def open_ui(self):
        interpreter = Path(sys.executable)
        if os.name == "nt" and interpreter.with_name("pythonw.exe").is_file():
            interpreter = interpreter.with_name("pythonw.exe")
        command = [str(interpreter), "-m", "tools.flash_monitor", "ui", "--session", str(self.directory)]
        options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        try:
            self.ui_process = subprocess.Popen(command, cwd=WORKSPACE, stdin=subprocess.DEVNULL,
                                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **options)
        except OSError as exc:
            self.log(f"窗口启动失败（烧录继续）：{exc}")

    def _emit(self, kind: str, **fields):
        with self._lock:
            if self.finished:
                return
            self.seq += 1
            event = {"schema": 1, "task": self.id, "seq": self.seq, "kind": kind,
                     "elapsed": round(time.monotonic() - self.started, 4), **fields}
            try:
                with (self.directory / "events.jsonl").open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(event, ensure_ascii=False, allow_nan=False) + "\n")
            except OSError as exc:
                # A viewer/disk failure must not interrupt an erase/program operation.
                if not self._io_warning:
                    self._io_warning = True
                    print(f"Flash monitor event write failed: {exc}", file=sys.stderr, flush=True)

    def phase(self, name: str, message: str | None = None):
        if name not in PHASES or name in TERMINAL:
            raise ValueError(f"Not a running phase: {name}")
        self.phase_name = name
        self._last_progress = (-1.0, 0.0)
        self._emit("phase", phase=name, message=message or PHASES[name][0])

    def progress(self, amount: float, *, completed: int | None = None, total: int | None = None):
        if self.phase_name not in NUMERIC_PHASES:
            return
        if not math.isfinite(amount) or not 0 <= amount <= 1:
            raise ValueError("Progress must be in [0, 1]")
        last_value, last_time = self._last_progress
        now = time.monotonic()
        if amount not in (0, 1) and (now - last_time < .05 or abs(amount - last_value) < .005):
            return
        self._last_progress = (amount, now)
        self._emit("progress", phase=self.phase_name, progress=amount, completed=completed, total=total)

    def log(self, message: str):
        self._emit("log", message=str(message)[:8192])

    def succeed(self, message: str = "读回校验通过 · 已发送启动复位"):
        self._emit("terminal", phase="done", message=message)
        self.finished = True

    def fail(self, message: str, *, interrupted: bool = False):
        self._emit("terminal", phase="interrupted" if interrupted else "error", message=str(message)[:8192])
        self.finished = True

    def __exit__(self, exc_type, exc, traceback):
        if not self.finished:
            if exc_type is not None:
                self.fail(f"{exc_type.__name__}: {exc}", interrupted=issubclass(exc_type, KeyboardInterrupt))
            else:
                self.fail("任务已结束，但没有收到成功结果", interrupted=True)
        return False


def bind_pyocd_progress(task: FlashTask, session):
    """Use public pyOCD stage notifications; its callback resets to zero after erase."""
    from pyocd.core.target import Target
    events = {Target.Event.PRE_FLASH_ERASE: "erase", Target.Event.PRE_FLASH_PROGRAM: "program"}

    def on_stage(notification):
        task.phase(events[notification.event])

    def on_progress(amount):
        # pyOCD sums per-page weights; its last update can be 1 + a few ULPs.
        # Display normalization must never abort an in-flight flash operation.
        if math.isfinite(amount):
            task.progress(min(1.0, max(0.0, amount)))

    session.subscribe(on_stage, list(events))
    return on_progress
