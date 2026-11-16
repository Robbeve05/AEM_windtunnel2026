"""Compile input/LED task variants with a frame budget; excludes callees.
Author: R.E. van Eekelen. Requires the firmware compile database, never flashes.
"""
import argparse
import json
from pathlib import Path
import subprocess

from test_io_configuration import command_arguments


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    options = parser.parse_args()
    records = json.loads(options.database.read_text())
    output = options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = [
        ('input', 'InputHandlerTask.c', []),
        ('control', 'ControlTask.c', []),
        ('recorder', 'FaultRecorder.c', []),
        ('fault_diagnostics', 'FaultDiagnosticsTask.c', []),
        ('heartbeat', 'HeartbeatTask.c', []),
        ('heartbeat_idle', 'HeartbeatTask.c', ['-DHEARTBEAT_DIAGNOSTIC_MODE=1']),
        ('heartbeat_telemetry', 'HeartbeatTask.c',
         ['-DHEARTBEAT_DIAGNOSTIC_MODE=2', '-DHEARTBEAT_DIAGNOSTICS=1']),
        ('heartbeat_large', 'HeartbeatTask.c',
         ['-DHEARTBEAT_STACK_BYTES=6144', '-DHEARTBEAT_DIAGNOSTICS=1']),
        ('creation_large', 'ApplicationTasks.c', ['-DHEARTBEAT_STACK_BYTES=6144']),
    ]
    for name, source, flags in cases:
        record = next(r for r in records if r['file'].replace('\\', '/').endswith('/' + source))
        args = command_arguments(record)
        args[args.index('-o') + 1] = str(output / (name + '.o'))
        for flag in ('-MF', '-MT', '-MQ'):
            while flag in args:
                index = args.index(flag)
                del args[index:index + 2]
        args = [arg for arg in args if arg not in ('-MD', '-MMD', '-MP')]
        args[1:1] = ['-Werror=frame-larger-than=512', '-fstack-usage'] + flags
        result = subprocess.run(args, cwd=record['directory'], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / (name + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(name + ' exceeded frame budget or failed compilation:\n' + result.stdout)
        print('PASS ' + name + ': frame <=512 bytes (callee use requires hardware measurement)')


if __name__ == '__main__':
    main()
