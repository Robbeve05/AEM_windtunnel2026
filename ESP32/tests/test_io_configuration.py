"""Compile production output code with valid/invalid configuration overlays.
Author: R.E. van Eekelen. Run after a PlatformIO firmware build; no link or flash.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess


def command_arguments(record):
    if 'arguments' in record:
        return record['arguments'][:]
    if os.name != 'nt':
        return shlex.split(record['command'])
    count = ctypes.c_int()
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    pointer = parse(record['command'], ctypes.byref(count))
    if not pointer:
        raise OSError('Cannot parse compiler command')
    result = [pointer[i] for i in range(count.value)]
    ctypes.windll.kernel32.LocalFree(pointer)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    options = parser.parse_args()
    records = json.loads(options.database.read_text())
    record = next(r for r in records if r['file'].replace('\\', '/').endswith('/DigitalOutputs.c'))
    base = command_arguments(record)
    project = Path(record['file']).resolve().parent.parent
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = [
        ('enabled', ['-DEXTERNAL_STATUS_LEDS_ENABLED=1'], None, True),
        ('active_low', ['-DEXTERNAL_STATUS_LEDS_ENABLED=1', '-DSTATUS_LED_ACTIVE_LEVEL=0', '-DVFD_ENABLE_ACTIVE_LEVEL=0'], None, True),
        ('invalid_lamp_polarity', ['-DSTATUS_LED_ACTIVE_LEVEL=2'], None, False),
        ('invalid_k2_polarity', ['-DVFD_ENABLE_ACTIVE_LEVEL=2'], None, False),
        ('invalid_enable', ['-DEXTERNAL_STATUS_LEDS_ENABLED=2'], None, False),
        ('duplicate_bit', [], ('#define GREEN_LED_DO_BIT 1', '#define GREEN_LED_DO_BIT 0'), False),
        ('out_of_range', [], ('#define RED_LED_DO_BIT 3', '#define RED_LED_DO_BIT 8'), False),
    ]
    for name, flags, replacement, should_compile in cases:
        overlay = output / name
        overlay.mkdir(exist_ok=True)
        for header in ('AppConfig.h', 'BoardConfig.h', 'MachinePins.h'):
            shutil.copy2(project / 'include' / header, overlay / header)
        if replacement:
            pins = overlay / 'MachinePins.h'
            text = pins.read_text()
            assert replacement[0] in text
            pins.write_text(text.replace(*replacement))
        args = base[:]
        args[args.index('-o') + 1] = str(overlay / 'outputs.o')
        # Do not modify dependencies belonging to the real firmware build.
        for flag in ('-MF', '-MT', '-MQ'):
            while flag in args:
                index = args.index(flag)
                del args[index:index+2]
        args = [a for a in args if a not in ('-MD', '-MMD', '-MP')]
        args[1:1] = ['-I' + str(overlay)] + flags
        result = subprocess.run(args, cwd=record['directory'], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (overlay / 'compiler.log').write_text(result.stdout)
        if (result.returncode == 0) != should_compile:
            raise RuntimeError(name + ' unexpected compiler result: ' + result.stdout)
        if not should_compile and 'static assertion failed' not in result.stdout.lower():
            raise RuntimeError(name + ' failed for an unrelated reason: ' + result.stdout)
        print('PASS ' + name + (' compiles' if should_compile else ' rejected by configuration assertions'))


if __name__ == '__main__':
    main()
