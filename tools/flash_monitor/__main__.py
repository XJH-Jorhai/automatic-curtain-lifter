"""Codex's command interface. All commands except a backend's real flash are UI-only."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import time
import uuid

from .client import FlashTask, SESSION_ROOT
from .model import EventTail, TaskState, atomic_json, process_alive


def resolve_session(value: str) -> Path:
    candidate = Path(value) if Path(value).is_absolute() else SESSION_ROOT / value
    candidate = candidate.resolve()
    root = SESSION_ROOT.resolve()
    if candidate.parent != root or not (candidate / "task.json").is_file():
        raise ValueError("Expected a task ID or session directory inside work/flash-monitor")
    return candidate


def status(directory: Path) -> dict:
    manifest = json.loads((directory / "task.json").read_text(encoding="utf-8"))
    state, tail = TaskState(), EventTail(directory / "events.jsonl")
    while events := tail.read():
        for event in events:
            if event.get("task") == manifest["id"]:
                state.apply(event)
    try:
        ui = json.loads((directory / "ui.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        ui = {"open": False, "starting": True}
    if ui.get("open") and not process_alive(ui.get("pid")):
        ui["open"] = False
    alive = process_alive(manifest["owner_pid"])
    if state.phase not in {"done", "error", "interrupted"} and not alive:
        state.phase, state.message = "interrupted", "烧录进程已退出，未收到完成结果"
    return {"session": manifest["id"], "path": str(directory), "demo": manifest["demo"],
            "backend_alive": alive, **state.snapshot(), "ui": ui}


def main():
    parser = argparse.ArgumentParser(description="Codex task-scoped flash progress window")
    sub = parser.add_subparsers(dest="action", required=True)
    for action in ("ui", "status", "expand", "collapse", "close"):
        sub.add_parser(action).add_argument("--session", required=True)
    demo = sub.add_parser("demo", help="Simulated events only; never imports pyOCD")
    demo.add_argument("--fail", action="store_true")
    demo.add_argument("--hold", action="store_true", help="Keep successful preview visible for 30 seconds")
    demo.add_argument("--no-window", action="store_true")
    demo.add_argument("--duration", type=float, default=10.0)
    args = parser.parse_args()
    if args.action == "demo":
        if not .1 <= args.duration <= 120:
            parser.error("duration must be between 0.1 and 120 seconds")
        with FlashTask(firmware="g030_mainboard.elf", demo=True, hold=args.hold,
                       show_ui=not args.no_window, transport="模拟 SWD · 无硬件访问") as task:
            task.phase("connect", "模拟演示：连接设备")
            time.sleep(args.duration * .1)
            if args.fail:
                task.fail("模拟连接失败：SWD 无应答（没有访问硬件）")
                return 1
            for phase, weight in (("erase", .15), ("program", .4), ("verify", .25)):
                task.phase(phase)
                for n in range(31):
                    task.progress(n / 30, completed=n * 824 if phase == "verify" else None,
                                  total=24720 if phase == "verify" else None)
                    time.sleep(args.duration * weight / 31)
            task.phase("reset", "模拟演示：复位启动")
            time.sleep(args.duration * .1)
            task.succeed("模拟演示完成 · 没有访问硬件")
        return 0
    directory = resolve_session(args.session)
    if args.action == "ui":
        from .ui import run
        run(directory)
    elif args.action == "status":
        print(json.dumps(status(directory), ensure_ascii=True, indent=2))
    else:
        command = {"action": args.action, "time": time.time(), "id": uuid.uuid4().hex}
        atomic_json(directory / "commands" / f"{time.time_ns()}-{command['id']}.json", command)
        print(json.dumps({"session": directory.name, "command": args.action, "queued": True}))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        # pythonw has no terminal; leave a useful UI startup diagnostic for Codex.
        if "ui" in sys.argv and "--session" in sys.argv:
            try:
                directory = resolve_session(sys.argv[sys.argv.index("--session") + 1])
                atomic_json(directory / "ui.json", {"open": False, "pid": os.getpid(), "error": str(exc)})
            except Exception:
                pass
        raise
