"""Live window lifecycle checks via the public CLI, using simulated tasks only.

Run on the user's Windows desktop (not an isolated sandbox desktop).
"""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import time

from tools.flash_monitor.__main__ import status


def wait_for(directory, predicate, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        snapshot = status(directory)
        if predicate(snapshot):
            return snapshot
        time.sleep(.1)
    raise AssertionError(f"Timed out waiting for window state: {snapshot}")


def command(action, directory):
    subprocess.run([sys.executable, "-m", "tools.flash_monitor", action,
                    "--session", str(directory)], check=True, capture_output=True)


def launch(*extra):
    process = subprocess.Popen([sys.executable, "-m", "tools.flash_monitor", "demo", *extra],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    line = process.stdout.readline()
    if not line.startswith("FLASH_MONITOR "):
        raise AssertionError(f"Missing session handle: {line}")
    return process, Path(json.loads(line[len("FLASH_MONITOR "):])["path"])


def main():
    sessions = []
    try:
        process, directory = launch("--duration", "8")
        sessions.append(directory)
        wait_for(directory, lambda s: s["ui"].get("open") and s["ui"].get("height") == 142)
        command("expand", directory)
        wait_for(directory, lambda s: s["ui"].get("expanded") and s["ui"].get("height") == 446)
        command("collapse", directory)
        wait_for(directory, lambda s: not s["ui"].get("expanded") and s["ui"].get("height") == 142)
        command("close", directory)
        wait_for(directory, lambda s: not s["ui"].get("open") and not s["ui"].get("starting"))
        assert process.poll() is None, "Closing the viewer stopped the running backend"
        assert process.wait(timeout=15) == 0
        assert status(directory)["phase"] == "done"

        failed_process, failed = launch("--fail", "--duration", "1")
        sessions.append(failed)
        assert failed_process.wait(timeout=10) == 1
        wait_for(failed, lambda s: s["phase"] == "error" and s["ui"].get("phase") == "error" and s["ui"].get("open"))
        command("expand", failed)
        wait_for(failed, lambda s: s["ui"].get("height") == 446)
        command("close", failed)
        wait_for(failed, lambda s: not s["ui"].get("open"))

        completed_process, completed = launch("--duration", ".4")
        sessions.append(completed)
        assert completed_process.wait(timeout=10) == 0
        wait_for(completed, lambda s: s["phase"] == "done" and s["ui"].get("open"))
        wait_for(completed, lambda s: not s["ui"].get("open"), timeout=7)
        print(json.dumps({"result": "PASS", "sessions": [s.name for s in sessions],
                          "checks": ["compact", "expand", "collapse", "close preserves backend",
                                     "failure retained", "failure close", "success auto exit"],
                          "hardware_access": False}))
    finally:
        for directory in sessions:
            command("close", directory)


if __name__ == "__main__":
    main()
