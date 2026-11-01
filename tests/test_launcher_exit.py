"""A quit DS game was recorded as a failed launch, so play count, time and last played never moved.

start_drastic.sh ends with `kill -9 $(pidof gptokeyb)`, and that status is what runemu.sh hands to
EmulationStation. ES writes those stats only when the launch exits 0. session.sh kills gptokeyb as the
game starts, so the trailing kill failed and a normal quit looked like a failed launch.
"""
import os
import subprocess
import unittest

from loadmod import ROOT

WRAPPER = ROOT / "dsflip" / "device" / "drastic-wrapper.sh"


def pidof_gptokeyb():
    return subprocess.call(["pidof", "gptokeyb"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class LauncherExitTest(unittest.TestCase):
    def setUp(self):
        if pidof_gptokeyb() == 0:
            self.skipTest("a gptokeyb process is already running")

    def tearDown(self):
        subprocess.run(
            "kill -9 $(pidof gptokeyb) 2>/dev/null",
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        subprocess.run(
            "rm -rf /tmp/rocknixds-gptokeyb-hold.*",
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def test_hold_makes_start_drastic_trailing_kill_succeed(self):
        env = os.environ.copy()
        env["ROCKNIXDS_WRAPPER_HOLD_TEST"] = "1"
        started = subprocess.run(["sh", str(WRAPPER)], env=env)
        self.assertEqual(started.returncode, 0)
        # start_drastic.sh's last line. Its status is the launch's status.
        kill = subprocess.run(
            "kill -9 $(pidof gptokeyb)",
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.assertEqual(kill.returncode, 0)
        self.assertNotEqual(pidof_gptokeyb(), 0)

    def test_without_a_process_that_kill_fails(self):
        # the status ES was getting: gptokeyb already gone, so the launch is not 0
        kill = subprocess.run(
            "kill -9 $(pidof gptokeyb)",
            shell=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        self.assertNotEqual(kill.returncode, 0)
