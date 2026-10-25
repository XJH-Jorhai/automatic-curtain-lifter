"""Read back this board's application Flash and motor-disable GPIO state."""
import argparse
import hashlib
from pathlib import Path
from pyocd.core.helpers import ConnectHelper


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--baud", type=int, choices=(9600,115200), default=115200)
    parser.add_argument("--bridge-elf", type=Path,
                        help="Also check live bridge configuration and counters without halting")
    args = parser.parse_args()
    expected = args.binary.read_bytes()
    if not 8 <= len(expected) <= 64 * 1024:
        raise SystemExit("Unexpected binary size for STM32G030C8")
    session = ConnectHelper.session_with_chosen_probe(
        unique_id=args.probe, blocking=False, options={
            "no_config": True,
            "target_override": "stm32g030c8tx",
            "frequency": 50000,
            "connect_mode": "attach",
            "auto_unlock": False,
            "resume_on_disconnect": False,
            "cmsis_dap.limit_packets": True,
            "cmsis_dap.deferred_transfers": False,
            "pack.debug_sequences.enable": True,
            "pack.debug_sequences.disabled_sequences": [
                "DebugCoreStart", "DebugCoreStop", "DebugDeviceUnlock"
            ],
        })
    if session is None:
        raise SystemExit("Probe not found")
    with session:
        target = session.target
        # Bound wireless transfers; one large read failed in DAP_TRANSFER_BLOCK.
        actual = bytearray()
        for offset in range(0, len(expected), 64):
            actual.extend(target.read_memory_block8(
                0x08000000 + offset, min(64, len(expected) - offset)))
        if actual != expected:
            raise SystemExit("FAIL: Flash readback differs from binary")
        print(f"PASS Flash: {len(expected)} bytes, SHA256 {hashlib.sha256(actual).hexdigest()}")
        values = {name: target.read32(address) for name, address in {
            "GPIOA_MODER": 0x50000000, "GPIOA_ODR": 0x50000014,
            "GPIOB_MODER": 0x50000400, "GPIOB_ODR": 0x50000414,
            "FLASH_OPTR": 0x40022020,
        }.items()}
        for name, value in values.items():
            print(f"{name}=0x{value:08X}")
        for bank, pins in [("GPIOA", [4, 6, 7]), ("GPIOB", [0, 1])]:
            for pin in pins:
                if (values[bank + "_MODER"] >> (2 * pin)) & 3 != 1:
                    raise SystemExit(f"FAIL: {bank} pin {pin} is not an output")
                if values[bank + "_ODR"] & (1 << pin):
                    raise SystemExit(f"FAIL: {bank} pin {pin} is high")
        print("PASS: ENABLE/nSLEEP/STEP/DIR/current-select outputs are low")
        if values["FLASH_OPTR"] != 0xDFFFE1AA:
            raise SystemExit("FAIL: option register differs from recorded board baseline")
        print("PASS: option register unchanged from pre-flash baseline")
        if args.bridge_elf:
            from elftools.elf.elffile import ELFFile
            with args.bridge_elf.open("rb") as stream:
                symbols = ELFFile(stream).get_section_by_name(".symtab")
                addresses = {name: symbols.get_symbol_by_name(name)[0]["st_value"]
                             for name in ("tuya_bridge_fault", "tuya_bridge_to_module_bytes",
                                          "tuya_bridge_to_host_bytes", "tuya_bridge_rx_errors",
                                          "tuya_bridge_queue_drops", "tuya_bridge_last_error")}
                stats_address = symbols.get_symbol_by_name("bridge_stats")[0]["st_value"]
                mode_address = symbols.get_symbol_by_name("bridge_test_mode")[0]["st_value"]
            state = target.get_state()
            print(f"CPU state: {state}")
            from pyocd.core.target import Target
            if state != Target.State.RUNNING:
                raise SystemExit("FAIL: CPU is not running")
            for pin in (2, 3, 9, 10):
                if (values["GPIOA_MODER"] >> (2 * pin)) & 3 != 2:
                    raise SystemExit(f"FAIL: PA{pin} is not alternate function")
                afr = target.read32(0x50000020 + (pin // 8) * 4)
                if (afr >> ((pin % 8) * 4)) & 15 != 1:
                    raise SystemExit(f"FAIL: PA{pin} is not AF1")
            for name, base in (("USART1", 0x40013800), ("USART2", 0x40004400)):
                cr1, cr2, cr3 = (target.read32(base + offset) for offset in (0, 4, 8))
                brr = target.read32(base + 12)
                print(f"{name}: CR1={cr1:#x} CR2={cr2:#x} CR3={cr3:#x} BRR={brr}")
                expected_brr = (16000000 + args.baud // 2) // args.baud
                if cr1 != 0xD or cr2 != 0 or cr3 != 0 or brr != expected_brr:
                    raise SystemExit(f"FAIL: {name} differs from HSI16 / {args.baud} 8N1 polling setup")
            for name, address in addresses.items():
                value = target.read32(address)
                print(f"{name}={value}")
                if name == "tuya_bridge_fault" and value:
                    raise SystemExit("FAIL: bridge has latched a transport fault")
            mode = target.read32(mode_address)
            if mode:
                raise SystemExit(f"FAIL: diagnostics still active: {mode}")
            fields = ('rx', 'tx', 'framing', 'noise', 'overrun', 'parity', 'dropped', 'high_water')
            counters = target.read_memory_block32(stats_address, 16)
            for side, name in enumerate(('PC->module', 'module->PC')):
                print(name, dict(zip(fields, counters[side*8:side*8+8])))
            if values["GPIOB_ODR"] & (1 << 12):
                raise SystemExit("FAIL: bridge ready LED is off")
            print("PASS: live bridge running, UART pin/configuration checks pass, no latched fault")


if __name__ == "__main__":
    main()
