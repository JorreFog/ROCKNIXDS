"""#53, no sound after some cold boots: the boot check sets the RG DS codec's output path once the card is there,
restarts WirePlumber once when the default output is the dummy, unmutes, and leaves a normal boot alone."""
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

from loadmod import ROOT

HOOK = ROOT / "dsflip" / "device" / "autostart-rocknixds-audio"


class AudioCheckTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.mux = self.root / "mux"            # the codec's path: an item index, or absent (no card yet)
        self.sinkf = self.root / "sink"
        self.mutef = self.root / "mute"
        self.calls = self.root / "calls"
        self.cfg = self.root / "system.cfg"
        self.cfg.write_text("audio.device=speakers\n")
        self.cards = self.root / "cards"
        self.cards.write_text(" 0 [rk817ext       ]: simple-card - rk817_ext\n")
        self._stub("amixer", r"""#!/bin/sh
echo "amixer $*" >> "$T/calls"
[ -f "$T/mux" ] || exit 1
case "$*" in
  *cget*) echo "numid=1,iface=MIXER,name='Playback Mux'"
          echo "  ; type=ENUMERATED,access=rw------,values=1,items=3"
          echo "  ; Item #0 'OFF'"; echo "  ; Item #1 'SPK'"; echo "  ; Item #2 'HP'"
          echo "  : values=$(cat "$T/mux")" ;;
  *cset*HP*) echo 2 > "$T/mux" ;;
esac
""")
        self._stub("pactl", r"""#!/bin/sh
echo "pactl $*" >> "$T/calls"
case "$1" in
  info) [ "$PULSE_DOWN" = 1 ] && exit 1; echo ok ;;
  get-default-sink) cat "$T/sink" 2>/dev/null ;;
  get-sink-mute) echo "Mute: $(cat "$T/mute" 2>/dev/null || echo no)" ;;
  set-sink-mute) echo no > "$T/mute" ;;
esac
""")
        self._stub("systemctl", r"""#!/bin/sh
echo "systemctl $*" >> "$T/calls"
echo alsa_output.platform-sound.stereo-fallback > "$T/sink"
""")
        self._stub("volume", 'echo "volume $*" >> "$T/calls"\n')
        self._stub("sleep", "exit 0\n")

    def tearDown(self):
        self.tmp.cleanup()

    def _stub(self, name, text):
        p = self.bin / name
        p.write_text(text)
        p.chmod(p.stat().st_mode | stat.S_IEXEC)

    def run_hook(self, **extra):
        env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"], T=str(self.root),
                   AUDIOCHK_CARDS=str(self.cards), AUDIOCHK_CFG=str(self.cfg), AUDIOCHK_LOG=str(self.root / "log"),
                   AUDIOCHK_STEP="1", ALSA_PRIMARY_CARD="0", DEVICE_PLAYBACK_PATH="Playback Mux",
                   DEVICE_PLAYBACK_PATH_SPK="HP", DEVICE_PLAYBACK_PATH_HP="HP", AUDIO_MANAGEMENT="")
        env.update(extra)
        r = subprocess.run(["sh", str(HOOK)], env=env, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        return self.calls.read_text() if self.calls.exists() else ""

    def log(self):
        return (self.root / "log").read_text().splitlines()[-1]

    def test_a_normal_boot_changes_nothing(self):
        self.mux.write_text("2")
        self.sinkf.write_text("alsa_output.platform-sound.stereo-fallback\n")
        calls = self.run_hook()
        self.assertNotIn("cset", calls)
        self.assertNotIn("systemctl", calls)
        self.assertNotIn("set-sink-mute", calls)
        self.assertIn("path: HP, default: alsa_output", self.log())
        self.assertNotIn("|", self.log())

    def test_a_path_left_off_is_set(self):
        self.mux.write_text("0")
        self.sinkf.write_text("alsa_output.x\n")
        calls = self.run_hook()
        self.assertIn("cset name=Playback Mux HP", calls)
        self.assertEqual(self.mux.read_text().strip(), "2")
        self.assertIn("path 'OFF' -> 'HP'", self.log())

    def test_the_dummy_output_restarts_wireplumber_once(self):
        self.mux.write_text("2")
        self.sinkf.write_text("auto_null\n")
        self.mutef.write_text("yes")
        calls = self.run_hook()
        self.assertEqual(calls.count("systemctl --no-block restart wireplumber.service"), 1)
        self.assertIn("volume restore", calls)
        self.assertIn("set-sink-mute @DEFAULT_SINK@ 0", calls)
        self.assertIn("wireplumber restarted", self.log())
        self.assertIn("unmuted", self.log())

    def test_no_restart_when_pulse_is_down_or_on_ucm(self):
        self.mux.write_text("0")
        self.sinkf.write_text("")
        calls = self.run_hook(PULSE_DOWN="1", AUDIO_MANAGEMENT="UCM")
        self.assertNotIn("systemctl", calls)
        self.assertNotIn("cset", calls)                     # the Plus: UCM sets its paths
        self.assertIn("doesn't answer", self.log())

    def test_a_card_that_never_comes(self):
        self.sinkf.write_text("alsa_output.x\n")
        self.run_hook()
        self.assertIn("no 'Playback Mux' on card 0", self.log())


if __name__ == "__main__":
    unittest.main()
