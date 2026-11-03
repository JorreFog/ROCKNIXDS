#!/bin/sh
# Host tests for logic that can be wrong without a handheld: which box art a ROM gets,
# which games the menu's background scrape touches, RetroAchievements counts, which
# git branch an update installs, and which performance logs may leave the handheld.
set -e
cd "$(dirname "$0")/.."
sh -n dsflip/device/rocknixds-update
sh -n dsflip/device/es-features.sh
sh -n install.sh
python3 -m unittest discover -s tests -v
