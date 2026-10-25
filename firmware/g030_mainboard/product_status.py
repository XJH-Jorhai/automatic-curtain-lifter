"""Read product startup state; no reset, halt, EEPROM writes or motion commands."""
import argparse
import time
from pathlib import Path

from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper
from pyocd.core.target import Target


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--probe", required=True)
    parser.add_argument("--port", help="Optional console, e.g. COM3; sends M status only")
    parser.add_argument("--confirmed-direction", action="store_true", help="Expect confirmed-direction Product startup mode1")
    args = parser.parse_args()
    with args.elf.open("rb") as stream:
        table = ELFFile(stream).get_section_by_name(".symtab")
        for name in ("Product_Request", "Product_TickISR", "ProductMotor_TimerISR", "Lift_Tick",
                     "Product_PlayStartup", "Music_PlayStored", "Motor_PlayText",
                     "MotorTune_Parse", "MotorTune_OscEdge", "Eeprom_UpdateLiftRecord"):
            assert table.get_symbol_by_name(name), f"Missing product entry {name}"
        for name in ("app_test_mode", "Motor_SelfTest", "Motor_IOTest", "Motor_PlayPhrase",
                     "Motor_StartHeld", "Motor_StartTimed", "MotorTune_SelfTest",
                     "Music_SaveScore", "Eeprom_WriteScoreSlot",
                     "BoardTest_Command", "SerialBringup_Poll", "TuyaBridge_Run"):
            assert not table.get_symbol_by_name(name), f"Bench entry linked: {name}"
        names = ("uwTick", "wifi_work_state", "bridge_rx_frames", "bridge_rx_overflows",
                 "bridge_uart_errors", "bridge_tx_errors", "storage_loads", "storage_saves", "storage_errors",
                 "startup_music_result", "reads", "read_errors", "attempts", "writes", "up_key", "saved_valid")
        symbols = {name: table.get_symbol_by_name(name)[0] for name in names}
        addresses = {name: (symbol["st_value"], symbol["st_size"]) for name, symbol in symbols.items()}
        music_address = table.get_symbol_by_name("music_feedback")[0]["st_value"]
        score_address = table.get_symbol_by_name("cached")[0]["st_value"]
    print("PASS ELF product and startup music present; bench, score writes and diagnostic injection absent", flush=True)
    session = ConnectHelper.session_with_chosen_probe(
        unique_id=args.probe, blocking=False, options={
            "no_config": True, "target_override": "stm32g030c8tx", "frequency": 50000,
            "connect_mode": "attach", "auto_unlock": False, "resume_on_disconnect": False,
            "cmsis_dap.limit_packets": True, "cmsis_dap.deferred_transfers": False,
            "cache.enable_memory": False, "cache.enable_register": False,
            "pack.debug_sequences.enable": True,
            "pack.debug_sequences.disabled_sequences": ["DebugCoreStart", "DebugCoreStop", "DebugDeviceUnlock"],
        })
    if session is None:
        raise SystemExit("Probe not found")
    with session:
        target = session.target
        assert target.get_state() == Target.State.RUNNING, "CPU is not running"
        assert target.read32(0x40022020) == 0xDFFFE1AA, "Option bytes differ"
        deadline = time.monotonic() + 2
        while target.read32(music_address) == 1 and time.monotonic() < deadline:
            time.sleep(0.05)
        music = target.read_memory_block32(music_address, 3)
        print("startup music state/pulses/offset", *music, flush=True)
        assert music[0] == 2 and music[1] > 0 and music[2] == 0, "Startup melody did not complete balanced"
        score = bytearray()
        for offset in range(0, 192, 16):
            for word in target.read_memory_block32(score_address + offset, 4):
                score.extend(int(word).to_bytes(4, "little"))
            time.sleep(0.002)
        score = bytes(score).split(b"\0", 1)[0].decode("ascii")
        print("stored startup score", score, flush=True)
        assert score == "T232 C4/16 E4/8 F#4/16 R/16 Ab4/8 G4/16 R/16 B4/16 C5/4", "Stored cue differs from approved B1"
        for name, base, pins in (("GPIOA", 0x50000000, (4, 6, 7)),
                                 ("GPIOB", 0x50000400, (0, 1))):
            moder, odr = target.read32(base), target.read32(base + 0x14)
            for pin in pins:
                assert (moder >> (2 * pin)) & 3 == 1, f"{name}.{pin} not output"
                assert not odr & (1 << pin), f"{name}.{pin} not low"
        for name, base, baud in (("module", 0x40013800, 9600), ("console", 0x40004400, 115200)):
            brr = target.read32(base + 0x0C)
            assert brr == (16000000 + baud // 2) // baud, f"{name} baud mismatch: {brr}"
            print(name, "BRR", brr, flush=True)
        first_tick = target.read32(addresses["uwTick"][0])
        time.sleep(0.05)
        assert target.read32(addresses["uwTick"][0]) != first_tick, "SysTick is not advancing"
        first = {}
        for name, (address, size) in addresses.items():
            value = target.read8(address) if size == 1 else target.read32(address)
            first[name] = value
            print(name, value, flush=True)
        assert first["storage_loads"] == 2 and first["storage_saves"] == 0
        assert first["storage_errors"] == 0
        assert first["startup_music_result"] == 1
        assert first["reads"] == 4 and first["read_errors"] == 0
        assert first["attempts"] == 0 and first["writes"] == 0
        assert first["up_key"] == 0 and first["saved_valid"] == 0, "New schema must wait for first local UP toggle"
        for name in ("bridge_rx_overflows", "bridge_uart_errors", "bridge_tx_errors"):
            assert first[name] == 0, f"Transport error: {name}"
        time.sleep(2)
        for name in ("storage_loads", "storage_saves", "reads", "attempts", "writes"):
            assert target.read32(addresses[name][0]) == first[name], "Background EEPROM access"
        assert target.read_memory_block32(music_address, 3) == music, "Music replayed during idle observation"
        print("PASS CPU running; melody completed once; outputs low; UART; options; demand-only EEPROM with zero writes", flush=True)
    if args.port:
        import serial
        port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
        port.dtr = port.rts = False
        port.port = args.port
        with port:
            time.sleep(0.5)
            port.reset_input_buffer()
            port.write(b"M")
            port.flush()
            received = bytearray()
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline and b"\r\n" not in received:
                received.extend(port.read(512))
        message = received.decode("ascii", errors="replace")
        print(message.strip(), flush=True)
        assert "PRODUCT phase=0 busy=0" in message, "No fresh idle product response"
        assert "percent=50 " in message and "count=0 " in message
        assert "EN=0 SLEEP=0" in message and "ee=2/0/0" in message
        assert f"boot=1 music=2/{music[1]}/0" in message
        expected_mode = 1 if args.confirmed_direction else 0
        assert f"map=0 mode={expected_mode} ends=0/0 saved=0" in message
        assert "capture=0" in message
        print("PASS fresh console product status, startup 50%, balanced melody, no lift steps or EEPROM writes", flush=True)


if __name__ == "__main__":
    main()

