#!/bin/sh
# Poketool (formerly ROCKNIXDS Bank & Trade): a Pokémon bank, save editor, legality checks and trading over Wi-Fi.
# The app is in /storage/.config/rocknixds/bank (install-bank.sh puts it there); this is its entry in the menu: on its
# own tile where ROCKNIXDS lists the bank as a system (1.6 on), else in Ports.
APP=/storage/.config/rocknixds/bank/rocknixds-bank.sh
if [ ! -x "$APP" ]; then
    echo "Poketool isn't installed: run install-bank.sh (see the ROCKNIXDS README)." >&2
    exit 1
fi
exec "$APP" "$@"
