# Hand-off: finishing 1.5.13 on the handheld

What was prepared without a handheld, and exactly what a person (or an AI) with an RG DS or RG DS Plus on the desk
runs to finish each item. ssh in as `root` (password `rocknix`); the device paths below are the installed ones.
Each item ends with its acceptance test. Update this file as items close.

Install the branch under test on the device first:

```sh
curl -fsSL https://raw.githubusercontent.com/JorreFog/ROCKNIXDS/main/install.sh | RGDS_BRANCH=claude/tender-volta-a9nkpk sh
```

(or `RGDS_SRC=/path/to/checkout sh install.sh` from a copy on the device). Logs that matter:
`/storage/.config/drastic/dsflip/dsflip.log` (the engine's log of the last game), `last-session.log` (the launcher's),
`/var/log/es_log.txt` (EmulationStation).

## 1. The recommended settings are the defaults

**Done on the host.** `dsflip/device/session.sh` runs Gengis Engine when *3D renderer* is *Auto* (unset) or
*Gengis Engine*, DraStic's renderer only when the player chose *DraStic*; `install.sh` sets `nds.threaded_3d=1` once
where it was never set (uninstall removes it); README step 6 and the release notes describe the defaults.

**On the device:**

1. Fresh state: `sed -i '/^nds\.renderer=/d; /^nds\.threaded_3d=/d' /storage/.config/system/configs/system.cfg`, then
   run the installer. Check `grep '^nds\.' /storage/.config/system/configs/system.cfg` shows `nds.hires_3d=1` and
   `nds.threaded_3d=1` and no `nds.renderer=` line.
2. Start a 3D game (Pokémon HeartGold) with every Nintendo DS setting on *Auto*. `grep '3D renderer'
   /storage/.config/drastic/dsflip/last-session.log` must say `Gengis Engine (Auto; scale 2, texture filter 0)`, and
   `grep rast /storage/.config/drastic/dsflip/dsflip.log` must show the rasterizer starting.
3. Set *3D renderer* to *DraStic* for that game (X on it > advanced game options), start it again: the log says
   `3D renderer: DraStic`.
4. Play 5 minutes of HeartGold walking at *Auto*: `tools/rgds-monitor.py <ip>` on a PC should show the same or fewer
   dropped frames than with *DraStic* (1.5.9 measured 53.1 vs 50.6 fps at 816 MHz on a Plus).
5. `sh install.sh --uninstall` removes `nds.threaded_3d=1` again (`grep threaded_3d system.cfg` empty).

**Acceptance:** steps 2, 3 and 5 as described; no visible difference in any 3D game between *Auto* and *DraStic*
(Gengis Engine is pixel-identical by design; a difference is a bug to report with the game and a screenshot).

## 2. Empty `roms/nds` breaks the menu

**What it was.** EmulationStation drops a game system with no files, so with `roms/nds` empty the DS did not exist:
the `LastSystem=nds` the installer seeds resolved to nothing, the menu opened on the first system left (Music Player,
Tools, Favorites, Last played), and the two empty collections' library showed ES's `<No Entries Found>` placeholder
drawn as a cartridge. A tap on that cartridge or on START entered the ready screen and stayed on "Starting..." for
good (the gamepad's A was filtered out, the touch path was not). ES also overwrote `LastSystem` with the fallback
system on exit, so even after adding a game the menu did not open on the DS. Found by reading the code and running
the Pixel engine on the host with empty data (no crash, no bad layout: the engine itself survives 0 games and 0
systems, under the address sanitizer).

**Done on the host.** `dii-ess-aye/es-rgds-emptylibrary.patch` (in `tools/build-es.sh`'s list): the systems in
`RGDS_KEEP_SYSTEMS` (default `nds`) are loaded, themed and visible with zero games (five gates in `SystemData.cpp`).
The Pixel engine (its files live in `es-rgds-rnds.patch`; after editing them in the patched tree,
`tools/regen-rnds-patch.sh <es src> dii-ess-aye/es-rgds-rnds.patch` rewrites their sections) counts ES's placeholder
as no game and draws an empty state: the bubble **No games yet / Copy .nds files to roms/nds** over an empty reel (no
cartridge, no plate, no START), the rail `00 / 00`, the faces *B Back* and a muted *No games*; the top screen the DS
icon in the cover frame with the same two lines. Collections say **Nothing here yet** with their own hint (Favorites:
*Y on a game adds it here*; Last played: *Games you play show up here*). The engine never asks the host for a game
past the count, never opens a library with no systems and never favourites nothing (`RndsUI.cpp`, `RndsEs.cpp`).
The host harness (`dii-ess-aye/rnds/test`, built with `-DMOCKDATA='"empty.inc"'`) renders that state under the
address sanitizer, and with games present 24 frames (home, library, ready screen, back; dark and light) are
byte-identical to the previous engine. `emulationstation-rgds` rebuilt; `install.sh` sends `LastSystem` back to
`nds` when it holds one of the fallback systems. README and release notes updated.

**On the device** (a handheld with DS games; simulate the fresh card):

1. `mv /storage/roms/nds /storage/roms/nds.bak && mkdir /storage/roms/nds && systemctl restart essway.service`
   (ES reads `roms/nds` at start; the directory must exist, as ROCKNIX creates it at boot).
2. The shelf shows **Nintendo DS** (Games 0) and the menu opens on it. Open it: the bottom screen shows the bubble
   "No games yet / Copy .nds files to roms/nds" over an empty reel (no cartridge, no START), the rail `00 / 00`,
   *B Back* and a muted *No games*; the top screen the DS icon in the frame with the same two lines. **A, X, Y, up,
   down, L, R**, a tap on the reel, on START's old spot and on the *No games* face do nothing harmful (X opens ES's
   own game options on the placeholder, as it always did: close it; before the fix a tap on START hung the ready
   screen on "Starting..."); **B** goes back; X on the shelf (resume) does nothing. Right to Favorites, A: "Nothing
   here yet / Y on a game adds it here"; Last played: "Games you play show up here".
   `grep -c 'System "nds" has no games' /var/log/es_log.txt` is `0` (ES's log may instead be
   `/storage/.config/emulationstation/es_log.txt`). Grab both panels for the record: `export
   XDG_RUNTIME_DIR=/var/run/0-runtime-dir SWAYSOCK=$(ls /var/run/0-runtime-dir/sway-ipc.*.sock | head -n1); grim -o
   DSI-2 /storage/roms/screenshots/empty-top.png; grim -o DSI-1 /storage/roms/screenshots/empty-bottom.png`.
3. Still with the empty folder: Music Player / Tools / the collections look as before; nothing else gained a shelf
   entry (`LoadEmptySystems` stays off).
4. Add a game live: copy one `.nds` into the empty `/storage/roms/nds`, then `curl -s
   http://127.0.0.1:1234/reloadgames` (or restart essway): the library shows its cartridge, the shelf "Games 1", it
   launches, and the menu is back on the DS after the quit.
5. `rm -rf /storage/roms/nds && mv /storage/roms/nds.bak /storage/roms/nds && systemctl restart essway.service`: the
   games are back, play stats intact (they live in gamelist.xml inside the folder, which moved with it); the shelf,
   the library, the ready screen and a subfolder look exactly as before.
6. The real thing, if a spare card is at hand: flash the image, boot with no games: step 2's menu on the first boot,
   then copy one game over the network and restart: it opens on the DS with the game.
7. A card that ran 1.5.10-1.5.12 without games holds `LastSystem` = music/tools/favorites/recent in
   `/storage/.config/emulationstation/es_settings.cfg`; after the update it says `nds` and the menu opens on the DS.

**Acceptance:** steps 2, 4 and 5; with games present nothing looks different; `grep -c "nds" /var/log/es_log.txt`
shows the system loaded ("Loading system nds" or its gamelist line) on the empty run.

## 3. The real microphone

**What is known.** Issue 26: on an RG DS Plus (ROCKNIX 20261001) blowing into the mic does nothing at *microphone
sensitivity* high, while a button bound to DraStic's *Fake Microphone* in DraStic's own menu works. So DraStic's
fake-microphone path (it plays `config/microphone/microphone.wav` into the DS mic while the control is held) is fine;
what fails is between libdsflip's capture and that control. The path: ES's setting → `DSHOOK_MIC_THRESH` (high 0.03,
medium 0.15, low 0.3; *Auto* is off) → libdsflip's mic thread (`src/audio.c`: ALSA `default` capture, 44.1 kHz mono,
RMS per 1024-sample block, a noise floor learned over the first 1.4 s, then `level > floor + threshold`, and an
**echo gate** that also demands `level > 3 × learned speaker leak × speaker level + threshold`) → an SDL key event
for Scroll Lock, which DraStic reads as control code 327 → `controls_a[CONTROL_INDEX_FAKE_MICROPHONE]` in
`config/drastic.cfg`. Four hypotheses came out of the code, in order of likelihood, each now visible in the log:

- **H2, the echo gate swallows real blows.** The leak factor is learned from every block that is not a blow,
  including quiet passages where the room noise, not the speaker, sets the ratio, and it is capped at 4: with music
  at speaker level 0.15 the bar can sit at 0.5-1.8 RMS, above any blow (0.1-0.4). *Low* (0.3) is unreachable with
  any music. The gate was tuned on an RG DS at one volume (0 false presses); the Plus's louder speakers next to its
  mic make it worse.
- **H1, the control is unbound.** ROCKNIX's `drastic.cfg.rgds` has bound 327 in both control sets since 2026-02-04,
  but a `/storage/.config/drastic` created by an older nightly keeps `65535` (unbound) for ever, and rebinding a
  button in DraStic's menu overwrites whichever set DraStic edits.
- **H3, the Plus's capture is silent.** The Plus's mic sits on the `rk817_hp` card (the speakers are `aw88166`), its
  UCM declares capture gains it never sets; a muted or zero-gain source reads as all zeros, a missing source as a
  capture that never returns frames.
- **H5, the key flickers.** The key follows every 23 ms block; a blow hovering around the bar presses and releases
  several times inside one DraStic frame, which a game's blow detector never sees as held.

**What the uploaded logs say** (branch `device-logs`, `docs/data/device/<device>/`; the handheld uploads one record a
second with `dsflip.log`'s new lines). Two handhelds have `[mic]` lines. `48acc59e82bc43ed`, Phantom Hourglass on
2026-10-03 (the issue's day and game): `noise floor 0.037-0.060, peak 0.21-0.29, speaker leak x0.30-0.65`, 1 press
in the first 10 s and 0 in the next 70. `36ae0e2f8fd74c2a` (Mario & Luigi, Mario Kart, HeartGold; nobody blowing):
`floor 0.003-0.09, peak 0.22-0.35, leak x0.39-0.58, 0 presses`: the game's own sound reaches that mic at 0.22-0.35
RMS, as loud as a blow (0.2-0.4). So on these two the capture works and the units are right (H3 and H4 out), the gate
held the bar at 3 × leak × output, around 0.5-0.7, which no blow reaches (H2 is the mechanism), and lowering the
factor alone cannot be the whole fix where the leak is as loud as a blow: at ×1 the music would press the key.
That is what the low-frequency share below is for. Also in the logs: one session with `floor 0.0000, peak 0.0000`
for its whole 2.5 minutes, and two where the peak sat at `0.0000` for 100 s mid-game and came back: the capture
delivers silence at times (a suspended PipeWire source? the pause menu?), which the `capture is silent` line now
names at start and which step 2 can catch mid-game.

**Done on the host** (SuperDrastic `0.4.0-beta.2-rocknixds.3`, commit *Microphone: tunables and logs for the
handheld* in `dsflip/superdrastic-0.4.0-beta.2-rocknixds.3.patch`; nothing needs a rebuild to test):

- `dsflip.log` says at start how the control is bound: `[mic] fake microphone: drastic.cfg controls_a 327 (Scroll
  Lock), controls_b N: pressing the key`, or `... pressing joystick button N` when the keyboard set is unbound but
  the joystick set has a button (the thread then sends that button, as DraStic's own binding would), or `...
  pressing Scroll Lock (327), which this config does NOT map` (H1 confirmed from the log alone).
- `session.sh` repairs H1 before the game starts: with the mic on and `controls_a[...FAKE_MICROPHONE] = 65535` it
  writes 327 there and says so in `last-session.log` (`microphone: bound DraStic's fake microphone ...`). DraStic
  saves the file on exit, so a later rebinding by the player stays.
- `[mic] capture is silent (peak 0.00000 in 5 s): is a microphone source behind ALSA's default? (wpctl status)`
  after 5 s of exact zeros (H3). A capture that never returns frames shows `[mic] listening` and then never the
  `[mic] 10 s:` line.
- Switches, read once at the game's start, set with `systemctl set-environment NAME=value` over ssh (the game unit
  inherits them; `systemctl unset-environment NAME` removes them; `last-session.log` does not list them):
  - `DSFLIP_MIC_DEBUG=1`: a line every 8 blocks (~190 ms) `[mic] level L floor F out O coup C bleed B bar X down D`
    and one on every `PRESS` / `release`.
  - `DSFLIP_MIC_GATE=k`: the gate's factor (3 is the shipped value; `0` turns the gate off, which is libdrastouch's
    behaviour: floor + threshold only).
  - `DSFLIP_MIC_COUPLING_MAX=r`: the most leak the gate may learn (4 shipped; 1 keeps the bar reachable at any
    volume while music plays).
  - `DSFLIP_MIC_HOLD_MS=ms`: the key stays down at least this long after a press (0 shipped).
  - `DSFLIP_MIC_KEY=auto|key|button`: what presses the control (auto as described above).
  - `DSFLIP_MIC_LF=r`: a press also needs the block's low-frequency share above r: the RMS below ~250 Hz over the
    whole RMS. A blow into the mic is wind, mostly below that; game sound is mostly above (synthetic check on the
    host: filtered wind 0.7-0.8, tones 0.5-0.6, white noise 0.14; the real numbers come from the device). 0 shipped
    (logged only): every `[mic] 10 s:` line ends with `lf lo-hi above floor`, the share's range over the blocks
    above floor + threshold, and the debug lines carry `lf`.
  - The `[mic] listening, threshold T, echo gate on (x3.0, leak cap 4.0), hold 0 ms, low-frequency share logged only
    (DSFLIP_MIC_LF=0)` line echoes what was read.

**On the device** (an RG DS Plus, the reporter's model; an RG DS too if one is at hand). Phantom Hourglass's first
candle (Mercay Island, the two candles in Oshus's house, blow to put them out) or Nintendogs are the games; the
*Blow* entry of SuperDrastic 0.5.0's in-game menu (the plus-beta line) holds the control for 3 s *without* the mic
thread, so it separates DraStic's side from ours when both lines are on the desk.

1. **Baseline.** In ES set *Nintendo DS > microphone sensitivity* to *high*. `systemctl set-environment
   DSFLIP_MIC_DEBUG=1`. Start the game, reach the candle, blow 3-4 times over ~15 s with the music on, quit.
   `grep -n '\[mic\]' /storage/.config/drastic/dsflip/dsflip.log` and `grep -n microphone
   /storage/.config/drastic/dsflip/last-session.log`. Read it like this:
   - no `[mic] listening` but `[mic] off`: the setting never reached the game (`grep -i micro
     /storage/.config/system/configs/system.cfg`; ROCKNIX's `start_drastic.sh` exports `DSHOOK_MIC_THRESH`).
   - `[mic] no capture device` or `capture is silent`, or `listening` with no `10 s:` line ever: **H3**, step 2.
   - `fake microphone: ... does NOT map`: **H1**; the launcher should have repaired it (its `microphone:` line);
     if `controls_a` holds a button code instead of 65535, `DSFLIP_MIC_KEY=button` or rebind in DraStic's menu.
   - debug lines during a blow with `level` well above `floor + 0.03` but below `bar`, and no `PRESS`: **H2**, step 3.
   - `PRESS`/`release` pairs many times per blow (10+ in one `10 s:` line): **H5**, step 4.
   - `PRESS` once or twice per blow, held through it, and the candle still burns: the key reached DraStic and was
     ignored: run DraStic's menu, *Fake Microphone*, and check the keyboard set shows Scroll Lock; try
     `DSFLIP_MIC_KEY=button` after binding a button there.
2. **H3, capture sanity** (ES menu, no game running): `cat /proc/asound/cards` (expect `rk817_hp` and `aw88166`);
   `XDG_RUNTIME_DIR=/var/run/0-runtime-dir wpctl status | sed -n '/Sources:/,/Filters:/p'` (an rk817 mic source,
   not muted); `XDG_RUNTIME_DIR=/var/run/0-runtime-dir arecord -q -D default -d 3 -f S16_LE -r 44100 -c 1
   /tmp/blow.wav` while blowing, then its RMS with `python3 -c "import wave,struct,math;w=wave.open('/tmp/blow.wav');
   d=w.readframes(w.getnframes());s=struct.unpack('<%dh'%(len(d)//2),d);print(math.sqrt(sum(x*x for x in s)/len(s))/32768)"`
   (a blow: > 0.05; < 0.002: silent). Silent: `amixer -c <rk817 index> contents | grep -A4 -i capture`, raise *Mic
   Capture Gain* / *Master Capture Volume* with `amixer cset`, and then the fix is a `wpctl set-volume` / amixer line
   in `session.sh`'s audio block (next to the 48 kHz PipeWire setting) plus ROCKNIX's UCM, to report upstream.
3. **H2, the gate.** Repeat step 1 with `DSFLIP_MIC_GATE=0`: if the candle now goes out, the gate was the cause
   (expected from the logs); then find the setting that keeps it out and gives 0 false presses in 2 minutes of the
   game's music at the handheld's full volume: try `DSFLIP_MIC_GATE=1.5`, then `DSFLIP_MIC_COUPLING_MAX=1` with the
   gate at 3. Note the debug lines' `coup` and `bleed` during music for the record. The fix is the found values as
   the defaults in `src/audio.c` (`envf("DSFLIP_MIC_GATE", ...)`, `envf("DSFLIP_MIC_COUPLING_MAX", ...)`).
   **The leak as loud as a blow.** When every factor either misses blows or presses on music (the logged leak says
   it will), read `lf` from the `10 s:` lines of two runs, one blowing with the game paused or its volume down and
   one music only: a blow should sit at 0.7-0.95, music lower. Pick r between the two and run `DSFLIP_MIC_GATE=1
   DSFLIP_MIC_LF=<r>` (or `DSFLIP_MIC_GATE=0 DSFLIP_MIC_LF=<r>`) through the candle and the 2 minutes of music; the
   fix is those two as the defaults. If the two ranges overlap, the share needs another corner frequency (`lpa` in
   `src/audio.c`, 0.035 = 250 Hz, 0.014 = 100 Hz) or real echo cancellation, and that is the finding to report.
4. **H5, flicker.** `DSFLIP_MIC_HOLD_MS=150` (then 300): the `PRESS` count per blow drops to 1-2 and the candle goes
   out. The fix is that value as `envf("DSFLIP_MIC_HOLD_MS", ...)`'s default.
5. **Rebuild** once the defaults are known: in a SuperDrastic checkout on branch `rocknixds-wfc` (or
   `git am dsflip/superdrastic-0.4.0-beta.2-rocknixds.3.patch` on tag `v0.4.0-beta.2`), bump `VERSION` to
   `0.4.0-beta.2-rocknixds.4`, `sh build.sh <arm64 sysroot>` and `sh package.sh` (the `SUPERDRASTIC` file's comment
   names the toolchain), ship the tarball in `dsflip/`, regenerate the patch (`git format-patch --stdout
   v0.4.0-beta.2..rocknixds-wfc`), pin version and sha256 in `SUPERDRASTIC`, and test the package with
   `RGDS_SUPERDRASTIC=<tarball> sh install.sh`. Then `systemctl unset-environment DSFLIP_MIC_DEBUG DSFLIP_MIC_GATE
   DSFLIP_MIC_COUPLING_MAX DSFLIP_MIC_HOLD_MS DSFLIP_MIC_KEY DSFLIP_MIC_LF` and run the acceptance with no switches
   set.
6. **Medium and low.** Repeat the candle at *medium*; note whether *low* can work at all with music (if not, say so
   in the README's microphone paragraph: *high* or *medium*).

**Acceptance:** with no switches set and the shipped defaults, *microphone sensitivity* at *high* and at *medium*, a
real blow puts out the candle in Phantom Hourglass on the first or second try, and 2 minutes of the game's music at
full volume with nobody blowing give `0 presses` in every `[mic] 10 s:` line. Then close issue 26 with the log
excerpt, and add the found defaults to the release notes' microphone section.

## 4. Wi-Fi online play (Nintendo WFC)

**Nothing in this section has run on a handheld.** DraStic has no Wi-Fi emulation: its wifi register handlers are
stubs. The SuperDrastic package this branch ships (`0.4.0-beta.2-rocknixds.3`, source in
`dsflip/superdrastic-0.4.0-beta.2-rocknixds.3.patch`, SuperDrastic branch `rocknixds-wfc`) carries the port of
ROCKNIXDS's unmerged `origin/cursor/drastic-wfc-dns-24ad` (b1a4564): `src/wifi.c` replaces DraStic r2.5.2.2's wifi
load/store handler tables (at fixed offsets, for build id `7a5e0e5fc6e52e6e8f5499c3d4d667ef51db0748` only), answers
the game as an open access point named `rocknixds`, hands it the chosen DNS server over DHCP and carries the game's
traffic over the handheld's own sockets (`src/wfcnet.c`: DHCP, ARP, ICMP, UDP NAT, client-only TCP NAT). The DNS
server is what makes it online play: the game resolves `*.nintendowifi.net` through it and lands on a community
replacement for Nintendo's servers (shut down in 2014). No patched ROM and no account are needed on any of them.

**Done on the host.** The port cross-builds warning-free (`sh build.sh <arm64 sysroot>`) and its host test passes
(`sh build.sh wfc_test`: CRC, beacon/probe/auth/assoc frames, the RX ring, DHCP with the DNS option, ARP, ICMP, UDP
and TCP NAT over loopback). Three things a review against melonDS found are already fixed in the shipped package: the
gateway answered ARP probes for the DS's own address (a DHCP client would decline its lease), RX records left out the
FCS and ignored `W_RXLEN_CROP`, and the hook assumed DraStic stores `W_IF` as written (the layout line now reports
`IF store plain` or `write-1-to-clear` and adapts). ROCKNIXDS side: the ES option *wfc dns* (`nds.wfc_dns`: off /
Kaeru WFC / WiiLink DNS / AltWFC), `session.sh` exporting `DSFLIP_WFC` (`wifi: ...` in last-session.log), the README
paragraph, the es-features test, the package pinned in `SUPERDRASTIC`. The library also reads `nds.wfc_dns` and
`nds["<game>.nds"].wfc_dns` from `system.cfg` itself, so `DSFLIP_WFC=kaeru` in the environment (`systemctl
set-environment`) or the ES option both work. With the option off nothing is hooked: the package is safe to ship.

**Not verifiable on the host** (each has a step below): DraStic's build id and the six table pointers (no DraStic
binary here); the ARM7 IRQ path (`STATE_AT`, `ARM7_PEND/HALT/ALERT` are guesses that only the device can confirm);
whether Nintendo's Wi-Fi library accepts the hook's register semantics (a review against melonDS and dswifi found
two likely mismatches still open: TX fired only on `W_TXREQ_SET` writes, `W_POWERSTATE` bit 9 forced set, and
the beacon/compare timers gated on `W_RXCNT`); whether DraStic
keeps the game's saved WFC connection across launches; DraStic's DS MAC address (a MAC shared by every unit is
already banned on Wiimmfi); and whether any server is alive, because this host's resolver intercepts UDP/53 (all
four servers "answered" Nintendo's dead AWS hosts) and every server site is proxy-blocked. Known gap on top: the
UDP NAT is symmetric, so logins, lobbies and the GTS can work while races and battles between players (GameSpy
NAT negotiation, error 86420) will not until it is made full-cone.

### Servers

Status is from 2025-2026 web sources (gbatemp's "List of every known Nintendo WFC DNS", Delta 1.7's server picker,
the melonDS docs, the dwc_network_server_emulator wiki); none was probed. All are DNS-only: the game's own
*Auto-obtain DNS* gets the address from libdsflip's DHCP.

| Server | DNS | Status | DS games | Notes |
|---|---|---|---|---|
| **Kaeru WFC** (the one to try first) | `178.62.43.212` | reported alive | Wiimmfi's 300+ DS titles: Mario Kart DS "works flawlessly", Pokémon Gen IV/V GTS and battles via Poké Classic Network on the same DNS | hackless DNS + SSL offload in front of Wiimmfi, so Wiimmfi's rules apply (cheat detection, bans, MAC-hopping detection over 42 days) — https://kaeru.world/projects/wfc |
| WiiLink DNS → Wiimmfi | `167.235.229.36` | reported alive since July 2024 | same backend as Kaeru | the branch's "WiiLink" choice; it is a DNS operator in front of Wiimmfi, not WiiLink WFC; moved twice in 2024 — https://wiilink.ca/guide/dns/ |
| WiiLink WFC | `5.161.56.11` | reported alive | Mario Kart DS, Animal Crossing WW, Pokémon D/P/HG/SS pages; its own player pool | open-source wfc-server; not in the menu yet: `DSFLIP_WFC=5.161.56.11` — https://wfc.wiilink24.com |
| AltWFC / WFZwei | `172.104.88.237` | "mostly abandoned" (2025) | DS + Wii, unpatched DS | keep last or drop — https://github.com/barronwaffles/dwc_network_server_emulator/wiki/List-of-Servers |
| Wiimmfi direct | `95.217.77.181` | alive, Wii-oriented | 300+ DS games | DS players are sent to Kaeru/WiiLink DNS; raw address only |
| NewWFC | `89.117.58.143` | alive | Mario Kart DS only | cheats allowed; not offered — https://newwfc.xyz/ |
| dead: `164.132.44.106` (old RC24, in old DS guides), `167.86.108.126`, `185.82.22.28`, `185.59.132.99`, `185.82.21.64`, Twilit `34.66.49.81`, BenFi `24.218.177.103` | | do not use | | https://gbatemp.net/threads/list-of-every-known-nintendo-wfc-dns.661049/ |

Default: the option ships **off** (stock DraStic). When on, Kaeru WFC first.

### On the device

Logs: `/storage/.config/drastic/dsflip/dsflip.log` (every line of the hook starts with `[wfc]`),
`last-session.log` (`wifi: ...`), `drastic.out`. Each step says what to try first when it fails; stop and write
down what you saw where it says stop.

0. **The build.** Installing this branch (the command at the top) puts the `.2` package in place, with the ES option.
   Check: `grep -a -c "online via %s" /storage/.config/drastic/dsflip/libdsflip.so` prints `1` (`0` = an older
   library). To try a library built by hand instead: back it up (`cp libdsflip.so libdsflip.so.bak`) and copy the new
   `libsuperdrastic.so` over `libdsflip.so`. If a game then fails to start at all: put the `.bak` back and report
   dsflip.log's first lines.
1. **The DraStic binary.** (If `drastic.real` is missing, the stock launcher is still in place: start a DS game once,
   then `sh /storage/.config/drastic/dsflip/install.sh`.)
   ```sh
   python3 - <<'PY'
   d=open('/storage/.config/drastic/drastic.real','rb').read()
   i=d.find(b'\x04\x00\x00\x00\x14\x00\x00\x00\x03\x00\x00\x00GNU\x00')
   print('build id', d[i+16:i+36].hex() if i>=0 else 'not found', '| type', int.from_bytes(d[16:18],'little'), '(3 = PIE)')
   PY
   ```
   Expected: `build id 7a5e0e5fc6e52e6e8f5499c3d4d667ef51db0748 | type 3 (3 = PIE)`. A different id: **stop**,
   nothing else can work (the table offsets are for that build only); send the id, `ls -l` and `md5sum` of
   `drastic.real`. Type `2`: report it (the load-bias code assumes a PIE).
2. **The servers, from the handheld's network** (ROCKNIX on Wi-Fi; `ping -c1 1.1.1.1` works):
   ```sh
   for s in 178.62.43.212 5.161.56.11 167.235.229.36 172.104.88.237; do python3 - $s conntest.nintendowifi.net nas.nintendowifi.net gpcm.gs.nintendowifi.net <<'PY'
   import socket,struct,sys
   s=sys.argv[1]
   for name in sys.argv[2:]:
       q=b'\x124\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00'+b''.join(bytes([len(p)])+p.encode() for p in name.split('.'))+b'\x00\x00\x01\x00\x01'
       k=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);k.settimeout(3)
       try: k.sendto(q,(s,53));d=k.recv(512)
       except Exception as e: print(s,name,'NO ANSWER',e);continue
       n=struct.unpack('>H',d[6:8])[0];i=12
       while d[i]: i+=d[i]+1
       i+=5;a=[]
       for _ in range(n):
           if d[i]&0xc0==0xc0: i+=2
           else:
               while d[i]: i+=d[i]+1
               i+=1
           t,c,ttl,l=struct.unpack('>HHIH',d[i:i+10]);i+=10
           if t==1: a.append('.'.join(map(str,d[i:i+4])))
           i+=l
       print(s,name,'->',' '.join(a) or 'no A record')
   PY
   done
   ```
   Expected: every line answers, and **not** with `35.160.180.49`, `44.229.101.156`, `54.201.103.197` or
   `44.245.86.170` (Nintendo's dead AWS hosts: that answer means the query was not redirected, which is what this
   sandbox saw for all four). Then
   `curl -s -o /dev/null -w '%{http_code}\n' -H 'Host: conntest.nintendowifi.net' http://<conntest ip from the Kaeru line>/`
   must print `200` (what the DS's connection test needs). All `NO ANSWER`: the router blocks UDP/53 to outside
   resolvers or the handheld is offline; try the loop from a PC on the same LAN. One server fails while the others
   answer: it is down or gone; remove its choice from `es-features.sh` before release. Record the answers here.
3. **First launch.** `systemctl set-environment DSFLIP_WFC=kaeru DSFLIP_WFC_DEBUG=1` over ssh (the game unit
   inherits it; the ES option *wfc dns* = Kaeru WFC does the same without the debug log). Start Mario Kart DS or
   Pokémon HeartGold from ES, then `grep '\[wfc\]' /storage/.config/drastic/dsflip/dsflip.log`. Expected:
   `[wfc] online via Kaeru WFC (178.62.43.212)`; last-session.log says `wifi: kaeru`.
   `online left off: DraStic build id is not r2.5.2.2`: step 1 again. `online left off: wifi handler table was not
   where r2.5.2.2 keeps it`: the build matches but the pointers at base+0x15d3a0/0x15d3b8 are not the expected
   ones: **stop**, copy `drastic.real` to the host and look at those offsets (`llvm-objdump -s
   --start-address=0x15d3a0 --stop-address=0x15d3d0 drastic.real`) before touching `wifi.c:40-53`. No `[wfc]`
   line at all: `grep wifi: last-session.log` (the setting never reached the environment), step 0's grep (wrong
   library), or the game died within 300 ms (`drastic.out`).
4. **Scan for the access point.** In the game: Nintendo WFC Setup (Nintendo Wi-Fi Connection Settings) > Connection
   1 > Search for an Access Point. Expected in the log: `[wfc] layout ok (reg ok, mac ok, IF store plain)` (or
   `write-1-to-clear`: fine too, the hook adapts; note which) and, with debug, several
   `[wfc] tx fc 0040 len N` lines (probe requests). On screen: `rocknixds` with an open (no key) lock.
   - `[wfc] layout mismatch (reg no, ...)` or `(..., mac no)`: the register/RAM offsets are wrong for this binary:
     **stop**, report the line.
   - no `[wfc] layout` line at all: the game never touched the wifi registers through the hook. First suspect:
     the power-up handshake (`W_POWERSTATE` bit 9 forced set, `wifi.c:384`; melonDS treats bit 9 as "powered
     off"): apply that fix, then check whether DraStic dispatches wifi accesses elsewhere.
   - `layout ok` but no `tx fc` line: the firmware arms `W_TXREQ_SET` once and only writes `W_TXBUF_LOCn`: apply
     the TXREQ-shadow fix (`wifi.c:430-433`).
   - `tx fc 0040` lines but an empty list: the probe response does not reach the game. `[wfc] no ARM7 state at
     ...` or `[wfc] ARM7 io ..., expected mem+0x23070` in the log = the IRQ offsets are wrong: **stop**, report.
     Otherwise the probe response is in the ring but the stack rejects it: log `W_RXLEN_CROP` (add a debug line
     for register 0x0DA reads/writes) and compare the RX header with melonDS's `Wifi.cpp` FinishRX.
   - `rocknixds` listed with a closed lock: a saved connection holds a WEP key: Options > Erase Nintendo WFC
     Configuration, rescan.
5. **Join and test.** Select `rocknixds`, keep *Auto-obtain IP Address* = Yes and *Auto-obtain DNS* = Yes
   (Advanced Setup), save, Test Connection. Expected log, in order: `[wfc] associated to rocknixds, dns
   178.62.43.212` (plus a 4 s toast "Wi-Fi / Kaeru WFC" on the panel), `[wfc] dhcp offer, dns 178.62.43.212`,
   `[wfc] dhcp ack, dns 178.62.43.212`, with debug `[wfc] dns query N bytes` / `[wfc] dns reply N bytes`, then
   `[wfc] tcp <ip>:80 from :<port>`. On screen: "Connection test successful".
   - 52000-52003 (no IP) with `associated` but no `dhcp offer`: the DHCP DISCOVER never arrived as a data frame:
     look for `tx fc 0208` lines; none = the data path dies before the hook (RX/TX fixes above); `fc` with bit
     `0x40` = WEP frames, dropped on purpose (erase the WFC configuration).
   - `dhcp offer` but no `dhcp ack`, 52000: the DS declined the lease. The gateway no longer answers ARP probes for
     10.13.37.20; with debug on, look for the DECLINE (a `dhcp` line) and what the DS sent before it.
   - `dhcp ack` but no `dns query`: the saved connection has a manual DNS: set Auto-obtain DNS = Yes or erase
     the configuration.
   - `dns query` but no `dns reply`, 52100-52103: this network cannot reach UDP/53 on the server (step 2 again
     from here), or the reply holds Nintendo's AWS addresses (wrong server).
   - `tcp <ip>:80` logged but the test fails: the conntest page did not return 200: try `DSFLIP_WFC=167.235.229.36`
     or `5.161.56.11` and compare.
6. **Persistence and the MAC.** Quit, start the game again: Connection 1 should still show `rocknixds` and Test
   Connection pass without a new scan. If Connection 1 is empty, DraStic does not keep the firmware's WFC area:
   note it (setup every launch; the friend code changes too) and send `ls -la /storage/.config/drastic` so the
   firmware file can be found. Then Options > System Information: write down the MAC address, and do the same on
   a second handheld. `00:09:BF:11:22:33`, or the same MAC on both units: **do not enable any server by default**
   (a shared identity is banned on Wiimmfi/Kaeru: 20102/23914/23915/20104); the per-device MAC becomes a blocker.
7. **A real login.** HeartGold/SoulSilver: Pokémon Center upstairs > GTS; Mario Kart DS: Nintendo WFC > Worldwide.
   Expected log: `tcp <ip>:80` (NAS login), `tcp <ip>:29900` (GameSpy gpcm), `tcp <ip>:28910` (server browser).
   On screen: the GTS loads / the lobby searches for opponents. 20100 or 20110: the game reached a wrong or dead
   server (manual DNS in the saved connection, or the server is down: step 2). 20102/23914/23915: banned (step 6's
   MAC). 20104: identifier already in use (another unit with the same MAC is online). Other 23xxx = NAS HTTP status
   + 23000 (23502 game server offline, 23800 game unsupported there). 60100: stale profile data (erase the game's
   WFC settings). 61020/61070: profile server unreachable (server side).
8. **Player-to-player (expected to fail on this build).** Start a Worldwide race or a GTS trade/battle. Opponents
   found but the race never starts, or 86420: the symmetric UDP NAT, the known gap, not a device problem. Log it.
   If it works, say so: the gap is smaller than assumed.
9. **Regression with the option off.** `systemctl unset-environment DSFLIP_WFC DSFLIP_WFC_DEBUG`, *wfc dns* off or
   Auto, start the same game: no `[wfc]` lines, `wifi: off` in last-session.log, the game's Wi-Fi menu behaves as
   on stock DraStic (no AP found). Play 5 minutes of a non-Wi-Fi game on the WFC build too. Restore the pinned
   library if step 0 used the quick swap.
10. **Collect:** dsflip.log (and .1-.3), last-session.log, drastic.out, `dmesg | tail -n 50`, the outputs of steps 1
    and 2, the MAC from step 6, a photo of each error code, and which game/server combination reached which step.

### Error codes (DS, replacement servers)

20100 AP joined but WFC servers unreachable (DNS not redirected, server down) · 20102 banned · 20103/20104 console
identifier broken / already in use · 20110 "service discontinued" (reached Nintendo's shutdown page: DNS not
redirected) · 23xxx NAS login HTTP status + 23000 (23302 captive portal, 23502 game server offline, 23800 game
unsupported, 23913/23914/23917 banned, 23915 too many MAC changes) · 31020 download server failed · 51300-51399
cannot connect to the AP · 52000-52003 no IP (DHCP failed: libdsflip's NAT did not answer) · 52100-52103 IP but no
internet (DNS/conntest failed) · 52200-52203 too many attempts, reboot · 60100 profile error · 61020/61070 profile
server unreachable · 86420 peer-to-peer connection failed (the NAT gap) · 91010 server maintenance or kicked.

**Acceptance:** step 1 prints the expected build id and type 3; a Kaeru launch logs `online via` then `layout ok
(reg ok, mac ok)` with no ARM7 error line; the scan lists `rocknixds` and Test Connection succeeds with
`associated`, `dhcp offer`, `dhcp ack` and `tcp <ip>:80` in the log, and the saved connection survives a restart (or
this file records that it does not); step 2 from the handheld's network answers for every server kept in the menu
with non-Nintendo addresses and a 200 conntest, and failing servers are removed; one real login works (GTS loads or
the MKDS lobby searches, `tcp :29900`) with no 2xxxx error, and the P2P result is recorded either way; the MAC is
not `00:09:BF:11:22:33` and differs between two units, otherwise no server ships enabled by default; with the
option off there are no `[wfc]` lines and no regression; `sh tests/run.sh` and `sh build.sh wfc_test` pass on the
host.
