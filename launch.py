#!/usr/bin/env python3
"""Optional convenience wrapper; the C executable owns the full lifecycle."""
import argparse
import os
from pathlib import Path
import re
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', action='append', required=True)
    parser.add_argument('--target', help='accept connections only from this host MAC')
    parser.add_argument('--adapter', help='local adapter, for example hci0')
    parser.add_argument('--mode', choices=['keyboard', 'mouse', 'combo'], default='combo')
    parser.add_argument('--name', help='SDP service name')
    parser.add_argument('--no-grab', action='store_true')
    parser.add_argument('--swap-ctrl-meta', action='store_true')
    parser.add_argument('--exit-key', default='F12')
    args = parser.parse_args()
    if args.target and not re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}', args.target):
        parser.error('--target must be a Bluetooth MAC address')
    for path in args.device:
        if not Path(path).exists():
            parser.error(f'Input device does not exist: {path}')
    executable = Path(__file__).resolve().with_name('hidclient')
    command = [str(executable), '--mode', args.mode, '--exit-key', args.exit_key]
    for path in args.device:
        command.extend(['--device', path])
    if not args.no_grab:
        command.append('--grab')
    if args.swap_ctrl_meta:
        command.append('--swap-ctrl-meta')
    if args.adapter:
        command.extend(['--adapter', args.adapter])
    if args.name:
        command.extend(['--name', args.name])
    if args.target:
        command.extend(['--host', args.target])
    try:
        os.execv(str(executable), command)
    except OSError as error:
        print(f'hidclient: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
