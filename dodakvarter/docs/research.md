DESIGN BRIEF: CoD Zombies mechanics + Swedish urban districts

Method: Part A numbers come mainly from decompiled WaW/BO1/BO2/BO3 game scripts on GitHub (primary source); web search fills gaps.

A. CALL OF DUTY ZOMBIES

A1. Rounds
- Zombie HP: R1 150, +100/round to R9 (950), then x1.1 per round from R10 (integer). R10 1045, R15 1679, R20 2701, R30 7000, R50 47,073. BO1/BO2 overflow near R163 ("insta-kill rounds").
- Zombies/round (BO1-BO3): max=24, m=max(1, R/5); if R>=10, m*=0.15R. Solo: max+int(3m); co-op: max+int((players-1)*6m). Then R1 x0.25, R2 x0.3, R3 x0.5, R4 x0.7, R5 x0.9.
  Solo R1-10: 6, 8, 13, 18, 24, 27, 28, 28, 29, 33; R20 60, R30 105, R50 249. 4 players: R10 78, R20 240. WaW: no solo bonus, R1-4 x0.2/0.4/0.6/0.8 (4, 9, 14, 19).
- Max 24 alive at once.
- Spawn delay: 2.0 s, x0.95 per round, floor 0.08 s (R10 1.26 s, R20 0.75 s). WaW base 3.0 s. BO3: 2.0/1.5/0.89/0.67 s for 1-4 players, floor 0.1 s.
- Speed: each zombie rolls [S, S+35) with S=8*(R-1): 35 or less walks, 70 or less runs, higher sprints. Walk/run/sprint %: R1 100/0/0, R3 57/43/0, R5 11/89/0, R6 0/89/11, R8 0/43/57, R10+ all sprint. BO3 uses 4*(R-1): first sprinters R11, all sprint R19. From R4 the last zombie of a round is forced to sprint.
- Flow: 10 s between rounds. Chalk tallies R1-5, two groups R6-10, numerals R11+. Game over: "You Survived N Rounds".

A2. Points
- Start 500. Non-lethal hit +10. Kill 50, plus torso +10, neck +20, head +50 (100), knife +80 (130).
- Boards +10 each; cap per player per round 50*R, max 500.
- Penalties: going down -5% of points; teammate bleeding out -10%.
- Doors/debris: default 1000; typically 750-1500.
- Wall-buys (BO1): Olympia/M14 500, MP40/MP5K 1000, AK74u/M16 1200, Stakeout 1500, Claymore 1000, frags 250. Ammo = half the gun price; ammo for a Pack-a-Punched gun 4500.
- Mystery Box 950; weapon shown ~12 s. Teddy bear (counted per location): pulls 1-4 safe, 5-8 15%, first location forced on pull 9; later locations 30% on pulls 9-13, 50% from 14. The bear refunds 950 and the box relocates.
- Fire Sale: box costs 10 at every location for 30 s; only drops after the box has moved once.

A3. Perks (limit 4 in BO1-BO3; all lost when downed)
- Juggernog 2500: HP 100 -> 250 (WaW 160; BO3 +100 = 200).
- Speed Cola 3000: half reload time, faster board repair.
- Double Tap 2000: +33% fire rate (2.0 in BO2+ also doubles bullet damage).
- Quick Revive: co-op 1500, revives 3 s -> 1.5 s. Solo 500: self-revive after 10 s; machine disappears after 3 buys.
- Stamin-Up 2000: +7% speed, double sprint time.
- Mule Kick 4000: third weapon.
- Deadshot 1500: aim snaps to heads, +35% hip-fire accuracy, less recoil.
- PhD Flopper 2000: immune to explosive and fall damage; dive-to-prone explosion.
- Electric Cherry 2000: shock burst on reload, stronger with an emptier magazine.
- Widow's Wine 4000: when hit, webs hold nearby zombies 20 s; grenades become sticky web grenades; knife slows zombies.
- Cold War: no limit; first perk 2500, +500 per perk owned (10th 7000); perk tiers (Jugg +50 HP, Tier III +100).

A4. Player health
- 100 HP; zombie swipe 60 (dog 40), so the 2nd hit downs you. Jugg 250: the 5th hit downs you.
- Regen (BO3 code): full refill 2.4 s after the last hit; below 20% HP, 5 s.
- Downed: 45 s bleed-out, pistol only; teammate revive 3 s.

A5. Power-ups
- Max Ammo refills all reserves and grenades. Nuke kills every zombie, +400 to each player. Carpenter rebuilds all barricades, +200 each (BO1: only drops if 5+ windows are broken).
- Timed, 30 s each: Insta-Kill, Double Points, Fire Sale, Death Machine, Bonfire Sale (Pack-a-Punch 1000).
- Drop rules: each kill has a 3% random chance. A points trigger also fires once team points earned exceed players*500 + 2000; each such drop multiplies the increment by 1.14 and sets the next target to current points + increment. Max 4 drops per round, dealt from a shuffled deck. A drop lasts 15 s, then blinks for 11.5 s.

A6. Special rounds and bosses
- Hellhounds: first dog round on a random R5-7, then every 4-5 rounds. 6 dogs per player (first two dog rounds), then 8. Dog HP 400/900/1300/1600 cap. The last dog drops a Max Ammo.
- Panzer Soldat (Origins): first on R8, then every 3 rounds co-op (4-5 solo); co-op waves of 1, then 2, then 3 (solo always 1). HP (5000+1000n) x player modifier, cap 22,500.
- Brutus (Mob of the Dead): every 4-6 rounds and after heavy box use. HP 1000n, cap 5000 (x player modifier); helmet worth 250 points.

A7. Pack-a-Punch: 5000; 15 s to take the gun. New name (M1911 -> "Mustang & Sally", Ray Gun -> "Porter's X2"), more damage, bigger magazine and reserve, sometimes special ammo. Repack: BO2 2000 (attachment), BO3 2500 (alternate ammo); BO3 Bonfire Sale 1000/500. Cold War tiers 5000/15000/30000.

A8. Barricades: 6 boards per window, 10 points each (capped as in A2).

A9. Loop: spawn room -> doors -> one-time power switch (powers perks, Pack-a-Punch, traps, electric doors; solo Quick Revive works without power) -> perks/box -> Pack-a-Punch. Cold War adds Exfil at R10 and every 5 rounds after.

B. SWEDISH URBAN ENVIRONMENTS

B1. Miljonprogrammet (1965-74, ~1M homes)
- Lamellhus: 3 storeys, no lift; the most common type. Set at angles around a courtyard or in parallel rows.
- Skivhus: 6-9 storeys (often 8-9), lifts; ~25% of homes.
- Punkthus: 3-16 storeys (usually 6-8), central stair core, 4-6 flats per floor.
- Loftgångshus: gallery access, 2-8 storeys, rare.
- Materials: prefab concrete, raw or exposed-pebble (pale yellow/red); brick; light grey "mexitegel"; eternit panels. Balcony fronts: concrete, eternit or corrugated sheet.
- Colours: muted 1960s, with green/blue sheet metal; 1970s strong yellow/blue/orange/brown balconies.
- Gård (courtyard): playground and sandbox watched from kitchens; tvättstuga (laundry) with a cylinder-lock booking board, slots 07-22; miljöhus/soprum (waste house); bike racks at entrances.
- SCAFT traffic separation: car-free yards, parking at the perimeter, pedestrian bridges and tunnels.

B2. Förort centrum
- ABC model (Arbete-Bostad-Centrum = work-housing-centre); Vällingby (1954) was the first.
- Pedestrian torg over or beside the tunnelbana, often marked by a high-rise; goods delivered underground.
- Services: grocery, pharmacy, kiosk, pizzeria, hairdresser, café/bakery, library, health centre, bank/post, liquor store, youth centre.
- Tunnelbana entrance: white translucent lantern with concave sides and a blue "T" (since 1958).
- Bus stops: green top sign = city bus, yellow = regional (Malmö practice); dashed yellow curb line, yellow zigzag on the road; busskur (shelter) and bench.

B3. Villaområde
- Falu red: iron-oxide pigment from Falun copper-mine slag, mixed with water, linseed oil and rye flour. White corners, windows and doors.
- Yard: flagpole with the flag on flag days and a blue-yellow vimpel (pennant) otherwise; picket fence, hedge, apple tree, trampoline, shed, gravel path; mailboxes in a row on a shared stand (2023 rules: up to 200 m from the house).

B4. Gamla stan
- Gränder (alleys) with cobbles and stairs; Mårten Trotzigs gränd is 90 cm at its narrowest.
- Tall, narrow 17th-18th-century gabled houses in ochre, rust, red, muted yellow and green.
- Stortorget square with a 1778 well; 82 beheaded there in the 1520 Bloodbath.
- Roofs mostly red/orange tile; spires green copper (Tyska kyrkan, 96 m).

B5. Other elements
- Kolonilotter (allotments): plots 200-500 m², cottages 10-40 m² in red/white/yellow/green; since 1895; Stockholm has 78 areas.
- Berg i dagen: glacier-polished granite/gneiss outcrops in parks. Trees by volume: ~40% spruce, 39% pine, 13% birch.
- Lakes: T-shaped wooden bryggor (jetties); red-and-white summer cottages.
- Återvinningsstation (recycling) colour code: newspapers blue, paper packaging light brown, plastic purple, metal grey, clear glass white, coloured glass green, general waste black.
- Street: lamps hung on cables across streets; white permanent road markings, yellow temporary (roadworks); white-bar zebra crossings; yellow warning triangles with red borders (A19 moose sign).
- Winter: crushed-stone grit on sidewalks; plough banks piled on pavements.
- Cars: boxy 1970s-80s estates, e.g. Volvo 240 (1974-93, 2.8M built).
- Suggested palettes (my hex approximations):
  - Miljonprogram: concrete #9C9A92, pebble concrete #B9AE94, mexitegel #D8D4C8, brick #8A4B38; accents #D9782D, #D4A62A, #3E6FA3, #4E8A5C.
  - Centrum: paving #A7A39A, glass #BFCAD0, T-blue #1F5FA8.
  - Villa: Falu red #8C2B1E, trim #F2F0EA, lawn #5E8C3A, gravel #B4A88F; flag #006AA7/#FECC02.
  - Gamla stan: ochre #C99A3B, rust #A44A2C, yellow #E3C26B, roof tile #B5532E, copper #6FA58C, cobble #6E6A63.
  - Winter: snow #EEF2F5, shadow #B8C6D6, grit #7D7468, dusk #2E3B55, sodium lamp #F2B65A.

B6. Flavour/loot
- reflexväst: hi-vis vest worn by kids and joggers in the dark.
- Dalahäst: carved red horse, handmade in Nusnäs since 1928.
- julmust: Christmas soda; ~50M litres a year, outsells cola in December.
- kanelbulle: cinnamon bun; its own day is 4 October.
- blåbärssoppa: warm blueberry soup, served at Vasaloppet ski-race stations since 1958.
- snabbkaffe: instant coffee, the fika fallback.
- surströmming: fermented herring, premiere on the third Thursday of August; stink-bomb item.
- älgskylt: moose sign, stolen by the hundreds each year.
- More: semla, knäckebröd, caviar tube, lösgodis, termos, pulka/spark, snus tin, glögg, falukorv.

SOURCES
Game scripts:
github.com/SyndiShanX/COD-GSC-Source (WAW-GSC/maps/_zombiemode*.gsc; BO1-GSC/maps/_zombiemode*.gsc; BO2-GSC/maps/mp/zombies/_zm*.gsc incl. _zm_ai_mechz, _zm_ai_brutus; BO3-GSC/zm/_zm*.gsc; BO3-GSC/shared/ai/zombie_utility.gsc)
github.com/JTAG7371/T5-RawFile-Dump/blob/master/mp/zombiemode.csv
github.com/JezuzLizard/T6-Data-Archive/blob/main/ZM/Script/Tables/zombiemode.csv
github.com/very-inky/Cod-Zombie-Health-Calculator
CoD web:
callofduty.fandom.com/wiki/Quick_Revive
callofduty.fandom.com/wiki/Perk-a-Cola
callofduty.fandom.com/wiki/Points_(Zombies)
callofduty.fandom.com/wiki/Pack-a-Punch_Machine
nazizombies.fandom.com/wiki/Speed_Cola
nazizombies.fandom.com/wiki/Stamin-Up
nazizombies.fandom.com/wiki/Widow's_Wine
nerdschalk.com/perks-in-cold-war-zombies/
charlieintel.com/news/all-black-ops-cold-war-zombies-perks-and-upgrades-65478/
gamesradar.com/black-ops-cold-war-zombies-guide/
gamezo.gg/black-ops-cold-war-zombies-pack-a-punch-guide/
Sweden:
sv.wikipedia.org/wiki/Miljonprogrammet
en.wikipedia.org/wiki/Million_Programme
karlstad.se/kommun-och-politik/sa-arbetar-vi-med/kulturmiljo/arkitektur/arkitekturtyper/miljonprogrammet---flerbostadshus
sv.wikipedia.org/wiki/Flerbostadshus
stockholmslansmuseum.se/byggnadswebben/arkitekturstilar/byggnadsguide/flerfamiljshus/1960-1980-tal/
stadsmuseet.stockholm.se/utforska/byggnader-och-miljoer/varda-ert-hus-historia/husets-alla-delar/fasader/hus-fran-rekordaren/
tandfonline.com/doi/full/10.1080/01426397.2020.1858248
arkitekten.se/debatt/inspireras-av-miljonprogrammet-for-barnens-skull/
sv.wikipedia.org/wiki/Bokningstavla
stockholmskallan.stockholm.se/post/35514
en.wikipedia.org/wiki/Vällingby
goteborg.se/wps/wcm/connect/62591226-e880-4424-b8f6-fcbd42167c52/OPAFrolunda.pdf
sv.wikipedia.org/wiki/T-symbolen
ilyabirman.net/meanwhile/all/stockholm-metro/
malmo.se/Teknisk-handbok/Gatubyggnad/Hallplatser.html
falurodfarg.com/en/about/the-story-of-the-red-cottage/
en.wikipedia.org/wiki/Falun_red
hejsweden.com/en/swedish-summer-house-cottage/
hbg.nu/flagga-eller-vimpel-flaggregler/
villaagarna.se/debatt/samfallighetsfragor/samfallighetsfragor/beslut-om-nya-postregler/
stockholmmuseum.com/stockholm-unveiled/areas/stortorget.htm
storyhunt.io/en/articles/stortorget
aviewoncities.com/stockholm/tyska-kyrkan
ne.se/uppslagsverk/encyklopedi/lång/koloniträdgård
stockholmsfria.se/artikel/119068
thenordroom.com/a-red-wooden-cottage-on-a-swedish-allotment/
americageography.github.io/sweden-geography/stockholms-geography/
en.wikipedia.org/wiki/Forests_of_Sweden
hem.se/atervinn-sortera/fargsortera
glasatervinning.se/insamlingsbehallare/
en.wikipedia.org/wiki/Street_lighting_in_Stockholm
learndrivingtheory.com/en/sweden/curriculum/driving-theory-se-b/unit/u01-road-signs-and-signals/lesson/l05-road-markings-pavement-symbols
drivingtheorysverige.com/en/article/swedish-traffic-signs-shape-colour-groups
norrkoping.se/boende-trafik-och-miljo/drift-och-underhall/vintervaghallning
en.wikipedia.org/wiki/Volvo_200_Series
thelocal.se/20181205/swedishchristmas-julmust-the-festive-drink-that-outsells-coca-cola-every-winter/
temadags.se/kanelbullens-dag/
isof.se/utforska/kunskapsbanker/lar-dig-mer-om-arets-namn-och-handelser/handelser/surstrommingspremiar
vasaloppet.se/om-oss/historia/blabarssoppan/
en.wikipedia.org/wiki/Dala_horse
thelocal.se/20060623/4156
