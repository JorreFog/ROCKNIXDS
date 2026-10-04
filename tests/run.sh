#!/bin/sh
# Host tests for logic that can be wrong without a handheld: which box art a ROM gets,
# which games the menu's background scrape touches, RetroAchievements counts, and which
# git branch an update installs.
set -e
cd "$(dirname "$0")/.."
sh -n dsflip/device/rocknixds-update
sh -n install.sh
sh -n dsflip/device/es-features.sh
sh -n dsflip/device/session.sh
cc -O2 -Wall -Wextra -Werror -Idsflip/device -o /tmp/drastic-preload-test tests/drastic_preload_test.c
/tmp/drastic-preload-test
python3 -m unittest discover -s tests -v
