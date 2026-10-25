"""Wireless recovery: bounded memory transfers with RAM upload verification."""
import argparse
import hashlib
import sys
import time
from pathlib import Path

# The monitor is an on-demand task helper, shared with Codex's CLI controls.
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.flash_monitor import FlashTask
from tools.flash_monitor.client import bind_pyocd_progress

p = argparse.ArgumentParser()
p.add_argument("--elf", type=Path, required=True)
p.add_argument("--probe", required=True, help="Your CMSIS-DAP probe UID; list with python -m pyocd list")
p.add_argument("--words", type=int, choices=[1, 4, 8, 16], default=16)
p.add_argument("--frequency", type=int, default=50000)
p.add_argument("--prefer-v1", action="store_true", help="Prefer CMSIS-DAP HID transport")
p.add_argument("--verify-only", action="store_true",
               help="No programming or pre-reset; compare Flash then reset to run.")
p.add_argument("--no-window", action="store_true", help="Explicit headless mode; task events are still recorded")


def flash(args, task):
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.file_programmer import FileProgrammer
    from pyocd.core.session import Session

    if not args.elf.is_file():
        raise FileNotFoundError(args.elf)
    # Check the readback image before touching the probe, rather than after programming.
    expected = args.elf.with_suffix(".bin").read_bytes()
    if not expected:
        raise ValueError("Readback binary is empty")
    task.phase("connect", "正在连接 CMSIS-DAP 与目标芯片")
    # Interface selection happens in choose_probe, before the real session exists.
    discovery_session = Session.get_current()
    discovery_session.options.update({"cmsis_dap.prefer_v1": args.prefer_v1,
                                      "cmsis_dap.limit_packets": True})
    s = ConnectHelper.session_with_chosen_probe(
        unique_id=args.probe, blocking=False, options={
            "no_config": True, "target_override": "stm32g030c8tx",
            "frequency": args.frequency, "auto_unlock": False,
            "cmsis_dap.limit_packets": True, "cmsis_dap.deferred_transfers": False,
            "cmsis_dap.prefer_v1": args.prefer_v1,
            "pack.debug_sequences.enable": True,
            "pack.debug_sequences.disabled_sequences":
                ["DebugCoreStart", "DebugCoreStop", "DebugDeviceUnlock"],
            "reset_type": "sysresetreq", "load.pre_reset": "off",
            "load.post_reset": "off", "resume_on_disconnect": False,
        })
    if s is None:
        raise RuntimeError("No probe")
    with s:
        transport = type(s.probe._link._interface).__name__
        print("USB transport", transport, flush=True)
        task.log(f"USB transport: {transport}")
        t = s.target
        r32, w32 = t.read_memory_block32, t.write_memory_block32

        def read_words(addr, count):
            if args.words == 1:
                return [t.read32(addr + 4 * i) for i in range(count)]
            out = []
            for i in range(0, count, args.words):
                out.extend(r32(addr + 4 * i, min(args.words, count - i)))
            return out

        def write_words(addr, words):
            for i in range(0, len(words), args.words):
                part = list(words[i:i + args.words])
                if args.words == 1:
                    t.write32(addr + 4 * i, part[0])
                else:
                    w32(addr + 4 * i, part)
                t.flush()
                if read_words(addr + 4 * i, len(part)) != part:
                    raise RuntimeError(f"RAM upload mismatch at {addr + 4 * i:#x}")
                time.sleep(0.002)

        def read_bytes(addr, size):
            start = addr & ~3
            words = read_words(start, (addr - start + size + 3) // 4)
            raw = b"".join(int(w).to_bytes(4, "little") for w in words)
            return list(raw[addr - start:addr - start + size])

        def write_bytes(addr, data):
            # Flash loader buffers are word aligned. Refuse unexpected alignment.
            if addr & 3 or len(data) & 3:
                raise RuntimeError("Unaligned flash RAM buffer")
            write_words(addr, [int.from_bytes(bytes(data[i:i + 4]), "little")
                               for i in range(0, len(data), 4)])

        t.read_memory_block32 = read_words
        t.write_memory_block32 = write_words
        t.read_memory_block8 = read_bytes
        t.write_memory_block8 = write_bytes
        if not args.verify_only:
            t.reset_and_halt()
            t.write_core_register("primask", 1)
            t.write32(0xE000E010, 0)  # SysTick off while executing the RAM flash algorithm.
            t.write32(0xE000E180, 0xffffffff)  # NVIC disable.
            t.write32(0xE000E280, 0xffffffff)  # NVIC clear pending.
            t.write32(0xE000ED04, (1 << 25) | (1 << 27))
            t.flush()
            print("RESET/HALT", t.get_state(),
                  "IPSR", t.read_core_register("ipsr"), flush=True)
            progress = bind_pyocd_progress(task, s)
            FileProgrammer(s, chip_erase="sector", progress=progress).program(str(args.elf))
        else:
            t.halt()
            print("VERIFY ONLY: no erase/program or pre-reset", flush=True)
        task.phase("verify", "逐段读取 Flash，与 BIN 镜像比较")
        actual = bytearray()
        for offset in range(0, len(expected), 64):
            actual.extend(read_bytes(0x08000000 + offset, min(64, len(expected) - offset)))
            task.progress(len(actual) / len(expected), completed=len(actual), total=len(expected))
        if actual != expected:
            raise RuntimeError("Flash mismatch")
        if t.read32(0x40022020) != 0xDFFFE1AA:
            raise RuntimeError("Option bytes changed")
        digest = hashlib.sha256(actual).hexdigest()
        print("PASS FLASH", len(actual), digest, flush=True)
        task.log(f"PASS FLASH {len(actual)} bytes · SHA256 {digest}")
        task.phase("reset", "读回与选项字节检查通过，正在发送启动复位")
        t.reset()
        print("Application reset to run programmed firmware", flush=True)
    # Only report success after session cleanup also completed without error.
    task.succeed()


if __name__ == "__main__":
    args = p.parse_args()
    with FlashTask(firmware=str(args.elf), probe=args.probe,
                   transport=f"SWD · {args.frequency / 1000:g} kHz", show_ui=not args.no_window) as task:
        flash(args, task)
