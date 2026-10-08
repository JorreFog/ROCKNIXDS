#!/bin/sh
# Host tests for logic that can be wrong without a handheld: which box art a ROM gets,
# which games the menu's background scrape touches, RetroAchievements counts, which
# git branch an update installs, the base ROCKNIX's update and re-apply, where DS saves go,
# the recommended settings and the boot's audio check.
set -e
cd "$(dirname "$0")/.."
sh -n dsflip/device/rocknixds-update
sh -n dsflip/device/es-features.sh
sh -n dsflip/device/save-dirs.sh
sh -n dsflip/device/nds-settings.sh
sh -n dsflip/device/drastic-wrapper.sh
sh -n dsflip/device/session.sh
sh -n dsflip/device/autostart-rocknixds-os
sh -n dsflip/device/autostart-rocknixds-audio
sh -n install.sh
python3 -m unittest discover -s tests -v
