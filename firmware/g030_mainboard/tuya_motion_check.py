"""Bench motor validation. Requires explicit --allow-motion; never run unattended."""
import argparse
import re
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--allow-motion', action='store_true', help='Enable real motor runs of up to 2 seconds')
    parser.add_argument('--output', type=Path, default=Path('work/tuya-motion-validation.txt'))
    args = parser.parse_args()
    if not args.allow_motion:
        parser.error('Physical motion requires --allow-motion; no serial port opened')
    import serial

    output = []

    def log(message):
        print(message, flush=True)
        output.append(message)

    with serial.Serial(args.port, 115200, timeout=0.03) as port:
        def command(value, wait=0.3):
            port.write(value.encode('ascii'))
            deadline = time.monotonic() + wait
            data = bytearray()
            while time.monotonic() < deadline:
                data.extend(port.read(4096))
            text = data.decode(errors='replace')
            log(value + ': ' + text.strip())
            return text

        def status():
            text = command('M')
            rows = re.findall(r'MOTOR (.*)', text)
            assert rows, text
            return {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', rows[-1])}

        def stopped(state):
            assert state['EN'] == 0 and state['phase'] in (0, 4), state

        try:
            port.reset_input_buffer()
            command('X')
            assert 'SAFETY PASS' in command('T', 0.6)
            stopped(status())

            command('F', 0.15)
            state = status()
            assert state['EN'] == 1 and state['count'] > 0, state
            command('X')
            before = status()
            stopped(before)
            time.sleep(0.2)
            after = status()
            assert before['count'] == after['count']
            log('PASS explicit stop freezes STEP count')

            command('B', 0.15)
            state = status()
            assert state['dir'] == 1 and state['EN'] == 1, state
            command('F')
            stopped(status())
            log('PASS opposite direction stops without reversing')

            command('X')
            command('F', 0.15)
            command('H', 0.4)
            state = status()
            assert state['reason'] == 5 and state['EN'] == 0, state
            log('PASS 250ms main-loop watchdog')

            command('X')
            start = time.monotonic()
            command('B', 0.2)
            command('B', 0.2)
            time.sleep(max(0, 2.15 - (time.monotonic() - start)))
            before = status()
            assert before['reason'] == 6 and before['EN'] == 0 and before['count'] > 0, before
            command('B')
            after = status()
            assert after['count'] == before['count'] and after['EN'] == 0
            log('PASS absolute 2s limit and duplicate cannot restart/extend')
        finally:
            # Persist evidence even if the cleanup command or readback fails.
            try:
                command('X')
                stopped(status())
            finally:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text('\n'.join(output), encoding='utf-8')


if __name__ == '__main__':
    main()
