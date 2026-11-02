#!/bin/sh
# Host tests for logic that can be wrong without a handheld: which box art a ROM gets,
# which games the menu's background scrape touches, RetroAchievements counts, which
# git branch an update installs, and the lines the Store and Döda Kvarter read back
# from a job (a step, a version, a failure, and a second job that must not start).
set -e
cd "$(dirname "$0")/.."
sh -n dsflip/device/rocknixds-update
sh -n dsflip/device/es-features.sh
sh -n install.sh
python3 -m py_compile tests/linejob.py
CC=${CC:-cc}
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
echo 'int main(void){return 0;}' > /tmp/rocknixds-san.c
if ! $CC $SAN -o /tmp/rocknixds-san /tmp/rocknixds-san.c 2>/dev/null || ! /tmp/rocknixds-san 2>/dev/null; then
    # cc may be clang without its sanitizer runtime; gcc's is the one the image usually has
    if [ "$CC" != gcc ] && gcc $SAN -o /tmp/rocknixds-san /tmp/rocknixds-san.c 2>/dev/null && /tmp/rocknixds-san 2>/dev/null; then
        CC=gcc
    else
        SAN=
    fi
fi
rm -f /tmp/rocknixds-san /tmp/rocknixds-san.c
# -Wno-format-truncation: the parsers cut a long line with snprintf on purpose (store/build.sh
# uses the same flag). A too-long version or reason is truncated, which the tests check.
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $SAN -Istore/src -Idodakvarter/src -pthread \
    -o /tmp/rocknixds-job-test tests/job_lines_test.c store/src/job.c tests/plat_log_stub.c
$CC -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation $SAN -Idodakvarter/src -pthread \
    -o /tmp/rocknixds-update-test tests/update_lines_test.c dodakvarter/src/update.c tests/plat_log_stub.c
export ROCKNIXDS_ROOT="$PWD" ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1
/tmp/rocknixds-job-test
/tmp/rocknixds-update-test
python3 -m unittest discover -s tests -v
