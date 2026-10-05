#!/bin/sh
# ROCKNIXDS Bank & Trade: a Pokémon bank, legality checks and trading over Wi-Fi.
# The app is in /storage/.config/rocknixds/bank (install-bank.sh puts it there); this is its entry in Ports.
APP=/storage/.config/rocknixds/bank/rocknixds-bank.sh
if [ ! -x "$APP" ]; then
    echo "ROCKNIXDS Bank isn't installed: run install-bank.sh (see the ROCKNIXDS README)." >&2
    exit 1
fi
exec "$APP" "$@"
