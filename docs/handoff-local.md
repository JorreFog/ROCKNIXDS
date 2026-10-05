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

In progress: the investigation's plan is being applied (an ES patch that keeps the DS system loaded with no games,
an empty state the Pixel engine draws, guards for a library with no games). This section is filled in when it lands.

## 3. The real microphone

In progress: the investigation's plan is being applied (per-block logging behind `DSFLIP_MIC_DEBUG`, tunables for the
echo gate, the most likely fix switchable). This section is filled in when it lands.

## 4. Wi-Fi online play (Nintendo WFC)

**Nothing in this section has run on a handheld.** DraStic has no Wi-Fi emulation: its wifi register handlers are
stubs. The SuperDrastic package this branch ships (`0.4.0-beta.2-rocknixds.2`, source in
`dsflip/superdrastic-0.4.0-beta.2-rocknixds.2.patch`, SuperDrastic branch `rocknixds-wfc`) carries the port of
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
