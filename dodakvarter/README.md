# Döda Kvarter

**A zombie roguelike for both screens of the Anbernic RG DS, set in a Swedish suburb at night.** The top screen is
the game, seen from above; the bottom screen is your inventory: health and armour, the gun in your hand and its
ammo, perks, the bag, and a map of the town. It plays like Call of Duty Zombies (rounds that never end, kronor for
every hit, barricades to buy your way through, a Mystery Box, perks, the power and the Pack-a-Punch), and every run
is a new town with loot that gets better as the rounds get harder.

<p align="center">
  <img src="docs/img/horde-winter.png" width="320" alt="Winter, round 12: a horde in a miljonprogram yard at night, the torch lighting them; the bottom screen with an epic Ak 5, a Hagelgevär, health, armour, perks, the bag and the town map">
  <img src="docs/img/title.png" width="320" alt="The title: DÖDA KVARTER in blood red over a night skyline of apartment blocks, a church spire, birches, street lamps and the dead walking past in the snow; the menu on the bottom screen">
</p>
<p align="center">
  <img src="docs/img/midsummer.png" width="260" alt="Midsummer night: bright and green">
  <img src="docs/img/moose.png" width="260" alt="The zombie moose, about to charge">
  <img src="docs/img/gameover.png" width="260" alt="YOU SURVIVED 14 ROUNDS">
</p>

It runs natively: a C program that draws every pixel itself and puts both screens straight onto the panels through
DRM/KMS (no compositor, no GL), the way ROCKNIXDS runs DS games, at 60 frames a second. Pixel art at the panels'
full resolution: 320x240 drawn at exactly 2x on the RG DS's 640x480 panels, 341x256 at 3x on the RG DS Plus'
1024x768 ones.

## Playing

On ROCKNIXDS it's in **Ports > Döda Kvarter** (the installer puts it there; `--no-game` leaves it out). Starting it
switches the panels over to the game like a DS game does, and quitting brings the menu back. ROCKNIX's exit hotkey
works too.

| | Classic (the default) | Twin buttons |
|---|---|---|
| Move | D-pad (or the left stick) | D-pad |
| Fire | **A**. Holding it keeps you facing the way you fired: walk backwards and shoot | **X Y A B** fire up, left, right, down; two of them for the diagonals |
| Aim | where you walk, with aim assist; the right stick if there is one | the face buttons |
| Use, buy, take | **B** (hold it to repair a window or search a bin) | **R** |
| Reload | **Y** | **SELECT** (it reloads by itself when the magazine is empty) |
| Switch weapon | **X** | **R2** |
| Knife | **R** | by itself, when you fire at a zombie right next to you |
| Sprint | hold **L** | hold **L** |
| Grenade | **R2** | **L2** |
| Bag | **L2** uses the item, **SELECT** picks the next one; or tap it | tap it on the bottom screen |
| Pause (save and quit, or give up) | **START** | **START** |

The touchscreens work: tap a bag item to pick it (tap again to use it), tap a weapon to switch to it, and touch the
top screen to fire where you touch (*Touch aiming* in the settings). With *Swap A/B* fire and use change buttons.
On a computer: arrows or WASD, J or Z to fire, K or X to use, U reload, I switch, E knife, Q sprint, 1 item,
3 grenade, Tab next item, Enter pause; the mouse aims (left button fires) and clicks the bottom screen.

*Today's town* on the title is the same town (and season) for everyone on the same day; its runs get a star in the
high score list. The list's second page (left or right) adds up everything you've played: runs, zombies, rounds,
the best round, the time, the kronor, boxes, critical hits, downs.

Quitting never loses a run: *Save and quit* in the pause menu, ROCKNIX's exit hotkey, or a shutdown while it runs
keep it in `run.sav`, and the title offers *Continue* (back where you were, paused). The run is also
kept after every round, so even a flat battery costs one round at most. A run that ends (or *Give up*) is gone.

## How it works

Everything in this section follows Black Ops' own rules where it has them; the numbers come from the game's
decompiled scripts and are in [docs/research.md](docs/research.md).

**Rounds.** Round 1 has 6 zombies, round 5 has 24, round 10 33, round 20 60, round 30 105 (Black Ops' solo
formula), at most 24 alive at a time, coming faster every round (2 s apart in round 1, 1.26 s in round 10).
Their health is 150 in round 1, 100 more every round up to 950 in round 9, then 10% more each round (1045, 2701 at
round 20, 7000 at round 30). Each one rolls its speed: walkers first, runners from round 2, sprinters from round 6,
only sprinters from round 10; from round 4 the last zombie of a round always runs. Ten seconds between rounds, the
round in red chalk marks in the corner, and when you fall: *YOU SURVIVED 7 ROUNDS*.

**Kronor.** You start with 500 kr. Every hit that doesn't kill is +10, a kill +60, a critical hit +100 (the
top-down headshot), the knife +130. Every board you nail back on a window is +10 (up to 50 kr per round, 500 at
most). You spend them on:

- **Barricades** between districts (750–2000 kr, more the further from the start): police barriers, a construction
  fence, a wrecked car. Districts are walled off from each other; buying your way through opens the town.
- **Wall buys**, chalk outlines of guns on the walls: Pist 88 500, Revolver 900, Kpist m/45 1000, Ak 5 1200,
  Hagelgevär 1500, Ak 4 1800 kr; ammo for half the price (4500 once upgraded). A gun that has run dry hands
  over to one that hasn't when you pull the trigger. Also the Yxa (axe: a one-hit knife
  for a long time) for 3000 and grenades for 250.
- **Lådan**, the Mystery Box (950 kr): a random weapon you don't have, often a good one, from all twelve, including
  the wonder weapons (Strålpistol, Åskvigg, Snöblåsare). After four pulls a **Dalahäst** may come out instead of a gun:
  your money back, and the box flies to another spot (its beam of light shows where). The odds are Black Ops' bear's.
- **Perks**, at vending machines once the power is on, four at most, lost when you go down:

  | Perk | kr | Does (Black Ops' perk) |
  |---|---|---|
  | Julmust | 2500 | 250 health instead of 100: five hits instead of two (Juggernog) |
  | Snabbkaffe | 3000 | reloads in half the time, repairs faster (Speed Cola) |
  | Salmiak | 2000 | fires a third faster and hits harder (Double Tap 2.0) |
  | Kanelbulle | 500 | gets you back up when you fall; works without power, three times a run (solo Quick Revive) |
  | Blåbärssoppa | 2000 | faster, and twice the sprint (Stamin-Up) |
  | Lingondricka | 2000 | reloading sends out an electric shock, stronger the emptier the magazine (Electric Cherry) |
  | Kaviar | 4000 | carry a third weapon (Mule Kick) |

- **Smedjan**, the Pack-a-Punch (5000 kr, needs the power): twice the damage, half again the magazine and the
  reserve, a little faster and through one more zombie, and a new name (Ak 5 becomes Ragnarök, the Strålpistol
  Norrskenet).
- **The power**: find the switchgear (Elcentral), usually far from the start. Then the perk machines and Smedjan
  work, and the lights come on across the town in a ring spreading from the switch: street lamps, shop windows,
  the windows of the houses.

**Power-ups** drop from the dead (3% a kill, plus one each time your total earnings pass a mark that moves further
away every time; four a round at most): Fullt förråd (Max Ammo), Instadöd (Insta-Kill), Dubbla kronor (Double
Points), Kaboom (every zombie, +400 kr), Snickare (Carpenter: every window boarded up, +200 kr), Rea (Fire Sale:
the box costs 10 kr for 30 s). They last 30 s, and lie on the ground for 15 s before they start to blink.

**The others.** *Vargnatt*: the first wolf night comes in round 5, 6 or 7, then every four or five rounds: a darker
round of fast wolves, the last one bringing a Max Ammo (the hellhound rounds). *Älgen*: from round 9 to 11, and every
four or five rounds after, a zombie moose with 5000 health and more each time, which lowers its antlers and charges
(Black Ops' Panzer Soldat). The doorman from the centrum takes three times the bullets; the bloated one bursts into a
cloud you shouldn't stand in.

**Loot** (the roguelike part). Search bins, cars, mailboxes, recycling bins, containers and sheds (hold B); the dead
drop things too; every new round fills searched places again and leaves things on the ground. Everything has a rarity,
grey, green, blue, purple, gold, and the odds climb with the round: a gold Ak 4 hits almost twice as hard as a grey
one, with a bigger magazine and a faster reload. Armour takes hits for you: a helmet (mössa, cykelhjälm,
hockeyhjälm, kravallhjälm) takes 30% of each hit and a vest or jacket (reflexväst, täckjacka, skinnjacka,
skyddsväst, kravallrustning) 60%, until it breaks. The bag holds six kinds of things: plåster, förbandslåda,
granater, smällare (firecrackers: every zombie comes to listen, then they go off, as the Monkey Bomb), brandbomb
(a fire on the ground), silvertejp (mends armour) and a termos of coffee (fast for 20 s).

Health comes back by itself 2.4 s after the last hit (5 s when you're below a fifth). A zombie's hit is 60.

## The town

A new town every run, with its own name (Björkhagen, Sjövik, Tallsta...), in one of three seasons (autumn rain,
winter snow, or the bright midsummer night), made of districts: usually twelve, four by three, sometimes nine big
ones, fifteen narrow ones or sixteen in four rows; a district bigger than usual gets more of its own things:

| District | What's in it |
|---|---|
| Gården | miljonprogram lamellhus around a yard: coloured balconies, the stairwell doors, a playground, the tvättstuga, recycling, bike racks, birches |
| Torget | the centrum: a row of shops (Livs, Pizzeria, Apotek, Frisör, Konditori, Bibliotek, Vårdcentral...), the tunnelbana entrance with its blue T, a kiosk, Plattan's black and white triangles in the middle, torghandel stalls of fruit under striped awnings, a fountain, a bus stop by the road |
| Villorna | Falu red (and some yellow) houses with white trim, hedges and picket fences, flagpoles with the flag or the long pennant, trampolines, apple trees, the row of mailboxes, a moose sign |
| Gamla stan | narrow cobbled gränder between ochre, rust and yellow plastered houses, a little square with its well |
| Kolonilotterna | allotments: fenced plots, a cottage on each, beds, compost, rain barrels; on the common ground the flag, the notice board, wheelbarrows |
| Parken | a lake with a jetty, lily pads, reeds, a livboj and a red sauna, glacier-smoothed bedrock, birch, pine and spruce; a midsommarstång in summer, a frozen lake in winter |
| Skolan | a brick school, the schoolyard with a hopscotch, an artificial-turf pitch |
| Kyrkan | a white church with a green copper spire, a red bell tower, the graveyard (they come up through the graves) |
| Hamnen | warehouses, shipping containers, fish crates, the quay with its bollards, lifebuoys and boats |
| Stationen | the pendeltåg station on the tracks, the bus terminal |
| Köpcentret | a big-box store and its car park, recycling stations |

Every district is walled off (hedges, fences, stone walls, forest) and joined to its neighbours through gaps with a
barricade in them; there are always loops to lead a horde around. The dead come through boarded windows and
stairwell doors (they pull the boards off; you nail them back for money, but stand right at the gap and they swipe
at you through it; shoot or knife them through the boards), up through manholes, graves and the ground. Each town
has five spots for the box, seven perk machines, Smedjan, the power switch and the wall buys, all placed where you
can reach them, and with the barricades shut no district can be walked out of (tests/test_map.c checks 500 towns
on every run).

## Inside

C, about 10,000 lines, no libraries but libc, libm and libdrm (SDL2 and libasound are loaded only if there).

| | |
|---|---|
| `src/plat_kms.c` | both panels through atomic KMS commits: a pair of dumb buffers per panel, each frame scaled up by whole pixels into the free one and flipped on the next vblank, both panels in one commit; a screen that didn't change isn't sent. Takes the display the way SuperDrastic does (DRM master, a modeset, the display controller's underrun check) |
| `src/input_evdev.c` | the gamepad (d-pad as buttons or a hat, sticks if any), both touchscreens (the bottom one is on `fe5e0000.i2c`), a USB keyboard; nothing grabbed, so ROCKNIX's hotkeys still work |
| `src/audio_alsa.c`, `src/audio.c` | a mixer on its own thread into ALSA's default device (PipeWire); every sound synthesised at start (no sound files), the title music *Vem kan segla förutan vind?* (traditional) as a music box, a music box figure for the Mystery Box and a chorale for the game over (both original), and the night under the play, made as it plays: autumn rain, winter wind, crickets and birds at midsummer |
| `src/plat_sdl.c`, `src/plat_headless.c` | a window for a computer; no display at all for the tests |
| `src/render.c` | the top screen: the painted town, everything standing sorted by its feet, and the night: per-season ambient light, street lamps and windows once the power is on, the torch on your gun (stopped by walls), muzzle flashes; zombie eyes glow after the light |
| `src/mapgen.c`, `src/world.c`, `src/props.c` | the town: districts, buildings with their facades, everything standing in it, painted once into one big bitmap |
| `src/game.c`, `weapons.c`, `zombies.c`, `loot.c`, `inter.c` | the player, guns and what they hit, rounds and the dead (they find you along a flow field), loot and power-ups, buying things |
| `src/hud.c`, `src/menu.c` | the bottom screen, the title, pause, settings, how to play, game over and the high scores |
| `art/*.txt`, `art/build_art.py` | every sprite, drawn as text (one character a pixel); the script writes `src/art_data.c` |

A frame takes about 1 ms on a desktop core (the night's lighting, the heaviest part, runs eight pixels at a time
with NEON on the handheld); `DK_PROFILE=1` writes the time per frame to the log every 10 s, to see it on the
handheld, and `DK_PROFILE=2` every frame over 2 ms. *Effects* in the settings is *Auto* at first: should frames ever
take over 11 ms on the handheld, it changes to *Light* for the rest of the run (the night's light worked out at half
the resolution, under the same dithering, and half the particles; about a quarter less work a frame, and hard to tell
apart). *Full* and *Light* fix it either way. Settings, the high score list (`scores.txt`) and the log (`dodakvarter.log`) are in
`/storage/.config/rocknixds/dodakvarter/data` on the device (`DK_DATA` elsewhere).

**On the RG DS.** `device/` has the Ports entry (`Doda Kvarter.sh`), `launch.sh` (starts the session in a systemd
unit, like ROCKNIXDS's DS launcher, or waits for it in fast-switch mode), `session.sh` (stops ES and sway or switches
the VT, puts the GPU at its lowest clock and the CPU on schedutil, aims ROCKNIX's exit hotkey at the game, runs it,
then hands everything back through ROCKNIXDS's `restore.sh`) and `gamelist.py` (its entry and pictures in the Ports
list). By hand, over ssh: `systemctl stop essway sway; /storage/.config/rocknixds/dodakvarter/dodakvarter; systemctl start sway essway`.

## Building

```sh
sh build.sh                      # this computer: build/dodakvarter (a window, or --backend headless)
sh build.sh aarch64 <sysroot>    # the RG DS: build/dodakvarter-aarch64 (the sysroot recipe is in build.sh)
sh tests/run.sh                  # the tests: 500 towns, the bot playing whole runs under the sanitizers
python3 art/build_art.py         # after changing art/*.txt (--preview sheet.png to look at it)
python3 tools/screenshots.py     # the README's pictures and the Ports entry's, rendered by the game
./build/sounds build/sounds      # every sound, the music and the ambience as WAV files (tests/run.sh builds it)
```

`bin/dodakvarter-aarch64` is the build the installer puts on the device. `./build/dodakvarter --bot` lets the
computer play, `--seed N` replays a run (the town and its season), `--start` skips the title. For testing deep
rounds: `DK_DEBUG_ROUND=N`, `DK_DEBUG_KR=N`, `DK_DEBUG_POWER=1`, `DK_DEBUG_OPEN=1` (every barricade gone),
`DK_DEBUG_GOD=1` (nothing hurts) and `DK_DEBUG_ZLOG=1` (where every zombie is, every 10 s, in the log).

## Credits

Inspired by Call of Duty: Zombies (Treyarch, Activision); this is a fan's homage and isn't affiliated with or endorsed
by them. Names of guns are the Swedish Armed Forces' designations; shop signs are generic. The research behind the
rules and the town is in [docs/research.md](docs/research.md). *Vem kan segla förutan vind?* is a traditional Swedish
folk song. Made for [ROCKNIXDS](../README.md) on [ROCKNIX](https://rocknix.org); MIT licensed like the rest of it.
