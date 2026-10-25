"""Regression checks: event transport, stage boundaries, and a fully fake SWD backend."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

from tools.flash_monitor import FlashTask
from tools.flash_monitor.client import bind_pyocd_progress
from tools.flash_monitor.model import EventTail, TaskState, process_alive

TEST_ROOT = Path(__file__).resolve().parents[3] / "work" / "flash-monitor-tests"
TEST_ROOT.mkdir(parents=True, exist_ok=True)


class EventTests(unittest.TestCase):
    def test_partial_utf8_and_bad_records(self):
        with tempfile.TemporaryDirectory(dir=TEST_ROOT) as directory:
            path = Path(directory) / "events.jsonl"
            data = json.dumps({"message": "烧录"}, ensure_ascii=False).encode("utf-8")
            path.write_bytes(b"bad json\n" + data[:-2])
            tail = EventTail(path)
            self.assertEqual(tail.read(), [])
            with path.open("ab") as stream:
                stream.write(data[-2:] + b"\n")
            self.assertEqual(tail.read(), [{"message": "烧录"}])
            self.assertEqual(tail.read(), [])

    def test_stage_100_percent_is_not_success(self):
        state = TaskState()
        self.assertTrue(state.apply({"seq": 1, "kind": "phase", "phase": "erase"}))
        state.apply({"seq": 2, "kind": "progress", "phase": "erase", "progress": 1.0})
        self.assertEqual(state.phase, "erase")
        state.apply({"seq": 3, "kind": "phase", "phase": "program"})
        self.assertIsNone(state.progress)
        self.assertFalse(state.apply({"seq": 4, "kind": "progress", "phase": "erase", "progress": 1.0}))
        self.assertFalse(state.apply({"seq": 4, "kind": "progress", "phase": "program", "progress": float("nan")}))
        state.apply({"seq": 5, "kind": "terminal", "phase": "error", "message": "校验失败"})
        self.assertFalse(state.apply({"seq": 6, "kind": "terminal", "phase": "done"}))
        self.assertEqual(state.message, "校验失败")

    def test_exit_without_explicit_success_is_interrupted(self):
        with tempfile.TemporaryDirectory(dir=TEST_ROOT) as directory, contextlib.redirect_stdout(io.StringIO()):
            with FlashTask(firmware="fake.elf", show_ui=False, session_root=Path(directory)) as task:
                task.phase("program")
                task.progress(1.0)
            state = TaskState()
            for event in EventTail(task.directory / "events.jsonl").read():
                state.apply(event)
            self.assertEqual(state.phase, "interrupted")

    def test_process_identity(self):
        import os
        self.assertTrue(process_alive(os.getpid()))
        self.assertFalse(process_alive(None))


class FakeTarget:
    def __init__(self, binary):
        self.memory = bytearray(binary)
        self.option = 0xDFFFE1AA
        self.resets, self.pre_resets, self.halts, self.max_read = 0, 0, 0, 0
        self.writes = []

    def read_memory_block32(self, address, count):
        self.max_read = max(count, self.max_read)
        offset = address - 0x08000000
        return [int.from_bytes(self.memory[n:n + 4].ljust(4, b"\x00"), "little")
                for n in range(offset, offset + count * 4, 4)]

    def write_memory_block32(self, address, words):
        self.writes.append((address, list(words)))

    def read32(self, address):
        return self.option if address == 0x40022020 else self.read_memory_block32(address, 1)[0]

    def write32(self, address, value):
        self.writes.append((address, value))

    def write_core_register(self, *args):
        pass

    def read_core_register(self, *args):
        return 0

    def reset_and_halt(self):
        self.pre_resets += 1

    def halt(self):
        self.halts += 1

    def reset(self):
        self.resets += 1

    def flush(self):
        pass

    def get_state(self):
        return "HALTED"


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=TEST_ROOT)
        self.directory = Path(self.temporary.name)
        self.binary = bytes(range(256)) * 3 + b"END"  # exercises final unaligned byte count
        self.elf = self.directory / "firmware.elf"
        self.elf.write_bytes(b"fake ELF; never given to real pyOCD")
        self.elf.with_suffix(".bin").write_bytes(self.binary)
        self.target = FakeTarget(self.binary)
        self.stage_callbacks, self.programmed = [], 0
        self.session = types.SimpleNamespace(
            target=self.target,
            probe=types.SimpleNamespace(_link=types.SimpleNamespace(_interface=types.SimpleNamespace())),
            subscribe=lambda callback, events: self.stage_callbacks.append(callback))
        session_context = self

        class FakeSession:
            def __getattr__(self, name):
                return getattr(session_context.session, name)

            def __enter__(self):
                return session_context.session

            def __exit__(self, *args):
                return False

        class FakeProgrammer:
            def __init__(self, session, progress, chip_erase):
                self.progress = progress
                if chip_erase != "sector":
                    raise AssertionError("erase policy changed")

            def program(self, path):
                session_context.programmed += 1
                for event in ("erase", "program"):
                    for callback in session_context.stage_callbacks:
                        callback(types.SimpleNamespace(event=event))
                    self.progress(0.0)
                    self.progress(1.0)

        self.modules = {
            "pyocd.core.helpers": types.SimpleNamespace(ConnectHelper=types.SimpleNamespace(
                session_with_chosen_probe=lambda **kwargs: FakeSession())),
            "pyocd.core.session": types.SimpleNamespace(Session=types.SimpleNamespace(
                get_current=lambda: types.SimpleNamespace(options={}))),
            "pyocd.core.target": types.SimpleNamespace(Target=types.SimpleNamespace(Event=types.SimpleNamespace(
                PRE_FLASH_ERASE="erase", PRE_FLASH_PROGRAM="program"))),
            "pyocd.flash.file_programmer": types.SimpleNamespace(FileProgrammer=FakeProgrammer),
        }
        source = Path(__file__).resolve().parents[3] / "firmware/g030_mainboard/flash_bounded.py"
        spec = importlib.util.spec_from_file_location("bounded_flash_under_test", source)
        self.backend = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.backend)
        self.args = types.SimpleNamespace(elf=self.elf, probe="fake", words=16,
                                          frequency=50000, prefer_v1=False, verify_only=False)

    def tearDown(self):
        self.temporary.cleanup()

    def run_task(self):
        with patch.dict("sys.modules", self.modules), contextlib.redirect_stdout(io.StringIO()):
            with FlashTask(firmware=str(self.elf), show_ui=False, session_root=self.directory) as task:
                self.task = task
                self.backend.flash(self.args, task)
        return EventTail(self.task.directory / "events.jsonl").read()

    def test_real_entry_uses_api_stages_and_bounded_readback(self):
        events = self.run_task()
        phases = [e["phase"] for e in events if e["kind"] in ("phase", "terminal")]
        self.assertEqual(phases, ["prepare", "connect", "erase", "program", "verify", "reset", "done"])
        self.assertEqual(self.programmed, 1)
        self.assertEqual(self.target.resets, 1)
        self.assertLessEqual(self.target.max_read, 16)
        verification = [e for e in events if e["kind"] == "progress" and e["phase"] == "verify"]
        self.assertEqual(verification[-1]["completed"], len(self.binary))
        self.assertEqual(verification[-1]["progress"], 1)

    def test_verify_only_never_erases_programs_or_pre_resets(self):
        self.args.verify_only = True
        self.run_task()
        self.assertEqual((self.programmed, self.target.pre_resets, self.target.halts), (0, 0, 1))
        self.assertEqual(self.target.resets, 1)

    def test_readback_mismatch_never_reports_success_or_resets(self):
        self.target.memory[30] ^= 1
        with self.assertRaisesRegex(RuntimeError, "Flash mismatch"):
            self.run_task()
        events = EventTail(self.task.directory / "events.jsonl").read()
        self.assertEqual(events[-1]["phase"], "error")
        self.assertEqual(self.target.resets, 0)

    def test_option_byte_mismatch_never_reports_success_or_resets(self):
        self.target.option = 0
        with self.assertRaisesRegex(RuntimeError, "Option bytes changed"):
            self.run_task()
        self.assertEqual(self.target.resets, 0)
        self.assertEqual(EventTail(self.task.directory / "events.jsonl").read()[-1]["phase"], "error")

    def test_missing_binary_stops_before_probe_connection(self):
        self.elf.with_suffix(".bin").unlink()
        self.modules["pyocd.core.helpers"].ConnectHelper.session_with_chosen_probe = lambda **kwargs: self.fail("probe touched")
        with self.assertRaises(FileNotFoundError):
            self.run_task()


if __name__ == "__main__":
    unittest.main()
