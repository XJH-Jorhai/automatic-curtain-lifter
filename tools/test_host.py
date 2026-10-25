"""Run SDK-independent tests with a host compiler; never access hardware."""
import argparse
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc', help='Host C compiler, not arm-none-eabi-gcc')
    args = parser.parse_args()
    compiler = shutil.which(args.cc)
    if not compiler:
        parser.error(f'Host compiler not found: {args.cc}')
    if 'arm-none-eabi' in compiler:
        parser.error('Use a host compiler to run tests on this computer')
    root = Path(__file__).resolve().parents[1]
    fw = root / 'firmware/g030_mainboard'
    out = root / 'work/host-tests'
    out.mkdir(parents=True, exist_ok=True)
    cases = [
        ('motion', ['App/product/motion.c', 'tests/product_motion_test.c'], ['-DLIFT_CONFIRMED_DIRECTION=0'], [[]]),
        ('eeprom', ['App/eeprom.c', 'tests/product_eeprom_test.c'], [], [['lift'], ['generic'], ['torn']]),
        ('waveform', ['App/product/motion.c', 'tests/trq_capture_motion_test.c'], [], [[]]),
        ('calibration', ['App/product/motion.c', 'App/product/product_button.c', 'tests/manual_calibration_test.c'], [], [[]]),
    ]
    for name, sources, definitions, runs in cases:
        binary = out / (name + ('.exe' if __import__('os').name == 'nt' else ''))
        command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2']
        command += ['-I' + str(fw / p) for p in ('tests/fakes', 'App/product', 'App')]
        command += definitions + [str(fw / p) for p in sources] + ['-o', str(binary)]
        subprocess.run(command, check=True)
        for arguments in runs:
            subprocess.run([str(binary), *arguments], check=True)
    print('PASS: SDK-independent host tests; full Product/SDK regression is separate.')


if __name__ == '__main__':
    main()
