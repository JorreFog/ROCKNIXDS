#!/usr/bin/env python3
"""gen_sounds.py <out dir>: ROCKNIXDS Pixel's menu sounds, chiptune style (pulse and triangle waves with short
envelopes), 16-bit mono 44.1 kHz WAV. No dependencies.

  scroll   the shelf moves: a short blip
  select   open a system or a game list: two notes up
  back     back out: two notes down
  fav      favourite on or off: a quick sparkle
  ready    the ready screen: a rising start jingle
  insert   the cartridge clicks into the console: a slide and a two-part click
"""
import math, os, random, struct, sys, wave

SR = 44100


def note(n):                         # MIDI note -> Hz
    return 440.0 * 2 ** ((n - 69) / 12)


def pulse(f, t, duty=0.25):
    return 1.0 if (t * f) % 1.0 < duty else -1.0


def tri(f, t):
    p = (t * f) % 1.0
    return 4 * p - 1 if p < 0.5 else 3 - 4 * p


def tone(buf, start, dur, f, vol, kind="pulse", duty=0.25, decay=None):
    """adds a tone at start (s) for dur (s): a 3 ms attack, then a decay (exponential, default over its length)"""
    s0, n = int(start * SR), int(dur * SR)
    if len(buf) < s0 + n:
        buf.extend([0.0] * (s0 + n - len(buf)))
    d = decay or dur / 3
    for i in range(n):
        t = i / SR
        env = min(1.0, t / 0.003) * math.exp(-t / d) * min(1.0, (dur - t) / 0.004)
        w = pulse(f, t, duty) if kind == "pulse" else tri(f, t)
        buf[s0 + i] += vol * env * w


def write(path, buf):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, x)) * 30000)) for x in buf))


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    s = []; tone(s, 0, 0.035, note(88), 0.22, duty=0.125, decay=0.012); write(os.path.join(out, "scroll.wav"), s)
    s = []; tone(s, 0, 0.05, note(79), 0.3, decay=0.03); tone(s, 0.05, 0.09, note(86), 0.3, decay=0.045)
    write(os.path.join(out, "select.wav"), s)
    s = []; tone(s, 0, 0.05, note(84), 0.28, decay=0.03); tone(s, 0.05, 0.09, note(77), 0.28, decay=0.045)
    write(os.path.join(out, "back.wav"), s)
    s = []
    for i, n in enumerate((84, 88, 91, 96)):
        tone(s, i * 0.035, 0.07, note(n), 0.22, kind="tri", decay=0.03)
    write(os.path.join(out, "fav.wav"), s)
    s = []
    for i, n in enumerate((72, 76, 79)):
        tone(s, i * 0.06, 0.06, note(n), 0.26, duty=0.5, decay=0.05)
    tone(s, 0.18, 0.28, note(84), 0.28, duty=0.5, decay=0.12)
    tone(s, 0.18, 0.28, note(60), 0.18, kind="tri", decay=0.15)
    write(os.path.join(out, "ready.wav"), s)
    # insert: a soft plastic slide, then the latch and the seat (a damped tick and a low thump each)
    random.seed(7)
    s = [0.0] * int(SR * 0.22)
    for i in range(len(s)):
        t = i / SR
        v = 0.0
        if t < 0.07:
            v += (random.random() * 2 - 1) * 0.10 * (t / 0.07) * (1 - t / 0.07)
        for t0, f, a in ((0.075, 2300, 0.55), (0.100, 1500, 0.75)):
            u = t - t0
            if u >= 0:
                e = min(1, u / 0.0004) * math.exp(-u / 0.006)
                v += a * e * (math.sin(2 * math.pi * f * u) * 0.7 + (random.random() * 2 - 1) * 0.3)
        u = t - 0.100
        if u >= 0:
            v += 0.35 * min(1, u / 0.001) * math.exp(-u / 0.02) * math.sin(2 * math.pi * 180 * u)
        s[i] = v
    write(os.path.join(out, "insert.wav"), s)


if __name__ == "__main__":
    main()
