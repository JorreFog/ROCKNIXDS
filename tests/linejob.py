#!/usr/bin/env python3
"""Stand-in for rocknixds-store and Döda Kvarter's update.sh.

The C parsers read a command on a pipe. This records each invocation, waits until
the test creates LINEJOB_GO, prints the first prepared line (flushed, so a step
is visible while the command is still running), waits for LINEJOB_GO2, then
prints the rest. Creating both gates before the process reaches them delivers
the whole answer at once.
"""
import os
import sys
import time


def wait(path):
    while not os.path.exists(path):
        time.sleep(0.01)


def main():
    with open(os.environ["LINEJOB_LOG"], "a", encoding="utf-8") as log:
        log.write(" ".join(sys.argv[1:]) + "\n")
    wait(os.environ["LINEJOB_GO"])
    text = open(os.environ["LINEJOB_LINES"], encoding="utf-8").read().split("\n")
    if text and text[-1] == "":
        text.pop()
    if not text:
        return 0
    print(text[0], flush=True)
    wait(os.environ["LINEJOB_GO2"])
    for line in text[1:]:
        print(line, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
