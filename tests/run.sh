#!/bin/sh
# Host tests for the pieces that can be wrong without a device: gamelist play stats,
# RetroAchievements counting, the performance-log importer, the CPU governor's clock
# decision, the display-interrupt storm check, and the battery LED thresholds.
set -e
cd "$(dirname "$0")/.."
python3 -m unittest discover -s tests -v
gcc -Wall -Wextra -Werror -o /tmp/cpugov_decide_test dsflip/cpugov_decide_test.c
/tmp/cpugov_decide_test
gcc -Wall -Wextra -Werror -o /tmp/vop_irq_test dsflip/vop_irq_test.c
/tmp/vop_irq_test
bash dsflip/device/battery-led-status --selftest
gcc -Wall -Wextra -Werror -c -o /tmp/cpugov.o dsflip/cpugov.c
