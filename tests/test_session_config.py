"""session.sh's menu choices, without starting a game.

Balanced is a 1-frame queue (1.5.5; it was 2). Gengis Engine is nds.renderer=superdrastic.
Resume stays on unless the value is 0, and a resume state older than the cartridge save is
dropped. A DraStic stuck in the kernel is state D with SIGKILL pending; a zombie, and a
pending SIGUSR1 (the resume hotkey), are not that.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT

SESSION = ROOT / "dsflip" / "device" / "session.sh"

DECIDE = r"""
. "$SESSION_SH"
CFG="$CFG_PATH"
ROM="$ROM_PATH"
GAME=$(basename "$ROM")
apply_power_profile
apply_renderer
apply_resume
printf '%s\n' --
printf 'QUEUE=%s\n' "${DSFLIP_QUEUE-unset}"
printf 'WAIT=%s\n' "${DSFLIP_QUEUE_WAIT-unset}"
printf 'CMAX=%s\n' "${DSFLIP_CPU_MAX-unset}"
printf 'SOFT=%s\n' "${DSFLIP_CPU_MAX_SOFT-unset}"
printf 'RAST=%s\n' "${DSFLIP_RAST-unset}"
printf 'TEX=%s\n' "${DSFLIP_RAST_TEXFILTER-unset}"
printf 'RFILE=%s\n' "${DSFLIP_RESUME_FILE-unset}"
printf 'RLOAD=%s\n' "${DSFLIP_RESUME_LOAD-unset}"
"""


class SessionConfigTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.cfg = self.root / "system.cfg"
        self.romdir = self.root / "roms"
        self.romdir.mkdir()
        self.states = self.root / "states"
        self.states.mkdir()
        self.kill = self.root / "kill-data"
        self.test_flag = self.root / "testing"
        self.test_resume = self.root / "testing-resume"

    def tearDown(self):
        self.tmp.cleanup()

    def decide(self, text, rom="HeartGold.nds", preset=None):
        self.cfg.write_text(text)
        rom_path = self.romdir / rom
        rom_path.write_bytes(b"")
        env = os.environ.copy()
        for key in list(env):
            if key.startswith("DSFLIP_") or key.startswith("ROCKNIXDS_"):
                del env[key]
        env.update({
            "SESSION_SH": str(SESSION),
            "CFG_PATH": str(self.cfg),
            "ROM_PATH": str(rom_path),
            "ROCKNIXDS_SESSION_FUNCS": "1",
            "ROCKNIXDS_KILL_DATA": str(self.kill),
            "ROCKNIXDS_SAVESTATES_DIR": str(self.states),
            "ROCKNIXDS_TEST_FLAG": str(self.test_flag),
            "ROCKNIXDS_TEST_RESUME": str(self.test_resume),
        })
        if preset:
            env.update(preset)
        out = subprocess.run(["sh", "-c", DECIDE], env=env, capture_output=True, text=True, check=False)
        self.assertEqual(out.returncode, 0, out.stderr)
        log, _, body = out.stdout.partition("\n--\n")
        vals = {}
        for line in body.splitlines():
            k, _, v = line.partition("=")
            vals[k] = v
        vals["log"] = log
        return vals

    def touch(self, path, when):
        path.write_bytes(b"x")
        os.utime(path, (when, when))

    def test_balanced_is_one_frame_and_the_game_line_wins(self):
        game = "Pokemon - White Version 2 (USA).nds"
        blank = self.decide("")
        self.assertEqual(blank["QUEUE"], "1")
        self.assertEqual(blank["WAIT"], "20")
        self.assertEqual(blank["CMAX"], "1416000")
        self.assertEqual(blank["SOFT"], "1")
        self.assertIn("power profile: balanced (queue 1, wait 20 ms, CPU max 1416000)", blank["log"])

        over = self.decide(
            'nds.power_profile=battery\n'
            f'nds["{game}"].power_profile=performance\n'
            'nds["other.nds"].power_profile=battery\n',
            rom=game)
        self.assertEqual(over["QUEUE"], "1")
        self.assertEqual(over["WAIT"], "0")
        self.assertEqual(over["CMAX"], "unset")
        self.assertEqual(over["SOFT"], "unset")
        self.assertIn("CPU max hardware", over["log"])

        last = self.decide("nds.power_profile=performance\nnds.power_profile=battery\n")
        self.assertEqual(last["QUEUE"], "3")
        self.assertEqual(last["CMAX"], "1104000")
        self.assertEqual(last["SOFT"], "1")

        unknown = self.decide("nds.power_profile=turbo\n")
        self.assertEqual(unknown["QUEUE"], "1")
        self.assertEqual(unknown["CMAX"], "1416000")
        self.assertIn("power profile: balanced", unknown["log"])

    def test_a_preset_queue_wins_and_performance_sets_no_cap(self):
        held = self.decide("nds.power_profile=battery\n", preset={
            "DSFLIP_QUEUE": "4", "DSFLIP_CPU_MAX": "1992000", "DSFLIP_CPU_MAX_SOFT": "0"})
        self.assertEqual(held["QUEUE"], "4")
        self.assertEqual(held["WAIT"], "20")
        self.assertEqual(held["CMAX"], "1992000")
        self.assertEqual(held["SOFT"], "0")

        perf = self.decide("nds.power_profile=performance\n", preset={"DSFLIP_CPU_MAX": "816000"})
        self.assertEqual(perf["QUEUE"], "1")
        self.assertEqual(perf["WAIT"], "0")
        self.assertEqual(perf["CMAX"], "816000")
        self.assertEqual(perf["SOFT"], "unset")

    def test_gengis_engine_and_its_texture_filter(self):
        game = "Spirit Tracks.nds"
        off = self.decide('nds.renderer=drastic\nnds.texture_filter=sharp\n', rom=game)
        self.assertEqual(off["RAST"], "unset")
        self.assertEqual(off["TEX"], "2")
        self.assertIn("3D renderer: DraStic", off["log"])

        on = self.decide(
            'nds.renderer=drastic\n'
            f'nds["{game}"].renderer=superdrastic\n'
            'nds.texture_filter=nearest\n'
            f'nds["{game}"].texture_filter=bilinear\n',
            rom=game)
        self.assertEqual(on["RAST"], "1")
        self.assertEqual(on["TEX"], "1")
        self.assertIn("3D renderer: Gengis Engine (texture filter 1)", on["log"])

        # An empty per-game line is "not set": the system line is used. A value already in the
        # environment wins, including RAST=0, which still selects the Gengis log line.
        empty = self.decide(
            'nds["HeartGold.nds"].renderer=\nnds.renderer=superdrastic\nnds.texture_filter=nope\n',
            preset={"DSFLIP_RAST": "0", "DSFLIP_RAST_TEXFILTER": "2"})
        self.assertEqual(empty["RAST"], "0")
        self.assertEqual(empty["TEX"], "2")
        self.assertIn("3D renderer: Gengis Engine (texture filter 2)", empty["log"])

        nearest = self.decide("nds.renderer=superdrastic\nnds.texture_filter=nearest\n")
        self.assertEqual(nearest["RAST"], "1")
        self.assertEqual(nearest["TEX"], "unset")
        self.assertIn("texture filter 0", nearest["log"])

    def test_resume_is_on_unless_zero_and_a_stale_state_is_dropped(self):
        rom = "HeartGold.nds"
        stem = "HeartGold"
        state = self.states / f"{stem}.resume.dss"
        save = self.romdir / f"{stem}.dsv"

        on = self.decide("nds.resume_on_quit=1\n", rom=rom)
        self.assertEqual(on["RLOAD"], "0")
        self.assertEqual(on["RFILE"], str(state))
        self.assertEqual(self.kill.read_text(), "-USR1 drastic\n")
        self.assertNotIn("resuming", on["log"])

        self.touch(state, 200)
        self.touch(save, 100)
        fresh = self.decide("", rom=rom)  # unset means on
        self.assertEqual(fresh["RLOAD"], "1")
        self.assertTrue(state.is_file())
        self.assertIn(f"resuming from {state}", fresh["log"])
        self.assertEqual(self.kill.read_text(), "-USR1 drastic\n")

        self.touch(state, 100)
        self.touch(save, 200)
        stale = self.decide('nds["HeartGold.nds"].resume_on_quit=1\nnds.resume_on_quit=0\n', rom=rom)
        self.assertFalse(state.exists())
        self.assertEqual(stale["RLOAD"], "0")
        self.assertIn("resume state older than the game's save: dropped", stale["log"])
        self.assertNotIn("resuming", stale["log"])
        self.assertEqual(self.kill.read_text(), "-USR1 drastic\n")

        off = self.decide("nds.resume_on_quit=0\n", rom=rom)
        self.assertEqual(off["RFILE"], "unset")
        self.assertEqual(off["RLOAD"], "unset")
        self.assertEqual(self.kill.read_text(), "-9 drastic\n")

        # A test launch leaves the hotkey file alone, unless it opted into resume.
        self.kill.unlink()
        self.test_flag.write_text("1")
        quiet = self.decide("nds.resume_on_quit=1\n", rom=rom)
        self.assertFalse(self.kill.exists())
        self.assertEqual(quiet["RFILE"], "unset")

        self.test_resume.write_text("1")
        still_off = self.decide("nds.resume_on_quit=0\n", rom=rom)
        self.assertFalse(self.kill.exists())
        self.assertEqual(still_off["RFILE"], "unset")

        save.unlink()
        self.touch(state, 50)
        during = self.decide("nds.resume_on_quit=1\n", rom=rom)
        self.assertEqual(during["RLOAD"], "1")
        self.assertTrue(state.is_file())
        self.assertEqual(self.kill.read_text(), "-USR1 drastic\n")

    def test_stuck_means_sigkill_pending_not_a_zombie_or_sigusr1(self):
        proc = self.root / "proc"
        pid = proc / "42"
        pid.mkdir(parents=True)

        def status(text):
            (pid / "status").write_text(text)

        def ask():
            env = os.environ.copy()
            env["ROCKNIXDS_SESSION_FUNCS"] = "1"
            env["ROCKNIXDS_PROC"] = str(proc)
            env["SESSION_SH"] = str(SESSION)
            script = f'. "$SESSION_SH"\nif alive 42; then echo ALIVE; else echo DEAD; fi\nif kill_pending 42; then echo PENDING; else echo CLEAR; fi\n'
            out = subprocess.run(["sh", "-c", script], env=env, capture_output=True, text=True, check=False)
            self.assertEqual(out.returncode, 0, out.stderr)
            return out.stdout.split()

        status("Name:\tdrastic\nState:\tZ (zombie)\nSigPnd:\t0000000000000100\nShdPnd:\t0000000000000000\n")
        self.assertEqual(ask(), ["DEAD", "PENDING"])

        status("Name:\tdrastic\nState:\tD (disk sleep)\nSigPnd:\t0000000000000100\nShdPnd:\t0000000000000000\n")
        self.assertEqual(ask(), ["ALIVE", "PENDING"])

        status("Name:\tdrastic\nState:\tD (disk sleep)\nSigPnd:\t0000000000000000\nShdPnd:\t0000000000000100\n")
        self.assertEqual(ask(), ["ALIVE", "PENDING"])

        # SIGUSR1 is the resume hotkey (bit 9). It is not an undelivered SIGKILL.
        status("Name:\tdrastic\nState:\tS (sleeping)\nSigPnd:\t0000000000000200\nShdPnd:\t0000000000000000\n")
        self.assertEqual(ask(), ["ALIVE", "CLEAR"])

        status("Name:\tdrastic\nState:\tR (running)\nSigPnd:\t0000000000000000\nShdPnd:\t0000000000000000\n")
        self.assertEqual(ask(), ["ALIVE", "CLEAR"])

        # "zombie" in the parenthetical is not state Z, and a high bit is not SIGKILL.
        status("Name:\tdrastic\nState:\tR (zombie)\nSigPnd:\t0000000100000000\nShdPnd:\t0000000000000000\n")
        self.assertEqual(ask(), ["ALIVE", "CLEAR"])

        # The mask is accepted without the kernel's leading zeros.
        status("Name:\tdrastic\nState:\tD (disk sleep)\nSigPnd:\t0\nShdPnd:\t100\n")
        self.assertEqual(ask(), ["ALIVE", "PENDING"])

        (pid / "status").unlink()
        self.assertEqual(ask(), ["ALIVE", "CLEAR"])
        pid.rmdir()
        self.assertEqual(ask(), ["DEAD", "CLEAR"])


if __name__ == "__main__":
    unittest.main()
