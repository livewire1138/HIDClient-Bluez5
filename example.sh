#!/bin/sh
# Usage: sudo ./example.sh /dev/input/by-id/YOUR-KEYBOARD-event-kbd [HOST_MAC]
set -eu
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 INPUT_DEVICE [PAIRED_HOST_MAC]" >&2
    exit 2
fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -eq 2 ]; then
    exec python3 "$script_dir/launch.py" --device "$1" --target "$2"
fi
exec python3 "$script_dir/launch.py" --device "$1"
