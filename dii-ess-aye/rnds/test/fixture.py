#!/usr/bin/env python3
"""Builds a ROCKNIX-like /storage for testing the rnds theme in a desktop ES: the mockup's systems and games.
    fixture.py <scratchpad> <theme name> [root=/storage]
Creates or overwrites its own files only."""
import html, os, re, shutil, sys, time

SP, THEME = sys.argv[1], sys.argv[2]
ROOT = sys.argv[3] if len(sys.argv) > 3 else '/storage'
mock = f'{SP}/mockup-pack/mockup'
src = open(f'{mock}/index.html').read()
block = src[src.index('const systems = ['):src.index('// Ten console icons')]
ES = f'{ROOT}/.config/emulationstation'
os.makedirs(f'{ES}/themes', exist_ok=True)
names = {'md': 'megadrive'}
exts = {'nds': '.nds', 'gba': '.gba', 'gbc': '.gbc', 'gb': '.gb', 'snes': '.sfc', 'nes': '.nes', 'n64': '.z64',
        'psx': '.chd', 'megadrive': '.md', 'arcade': '.zip', 'ports': '.sh'}
now = time.time()


def stamp(rel):
    """an ES lastplayed stamp that the engine shows as the mockup's relative day"""
    fixed = {'Today': 0, 'Yesterday': 1, 'Last week': 9, 'Never': None}
    wd = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday']
    if rel in fixed:
        d = fixed[rel]
    elif rel in wd:
        today = time.localtime(now).tm_wday
        d = (today - wd.index(rel)) % 7
        if d < 2:
            d += 7 if d == 0 else 0
    else:
        try:
            d = max(15, int((now - time.mktime(time.strptime(rel + ' 2026', '%b %d %Y'))) / 86400))
        except ValueError:
            d = 30
    if d is None:
        return ''
    return time.strftime('%Y%m%dT%H%M%S', time.localtime(now - d * 86400))


systems = []
favs = {'hg', 'sm', 'ctds', 'mf'}
for m in re.finditer(r"\{ id:'(\w+)', name:'([^']+)', maker:'([^']+)', accent:'(#\w+)'.*?list:\[(.*?)\]\}", block, re.S):
    sid, name, maker, accent, lst = m.groups()
    if sid == 'fav':
        continue
    es = names.get(sid, sid)
    systems.append((es, name, maker))
    d = f'{ROOT}/roms/{es}'
    os.makedirs(f'{d}/media', exist_ok=True)
    out = ['<?xml version="1.0"?>', '<gameList>']
    for g in re.finditer(r"g\(((?:'[^']*'|\"[^\"]*\")),'([^']+)','(\d+)',(\d+),'([^']+)','([^']+)',(\d+),(\d+),'(\w+)','(\w+)'\)", lst):
        title, genre, year, plays, last, tm, a, b, slug, _ = g.groups()
        title = title[1:-1]
        rom = f'{slug}{exts[es]}'
        open(f'{d}/{rom}', 'w').write('x')
        for k in ('box', 'snap'):
            shutil.copy(f'{mock}/art/{slug}-{k}.jpg', f'{d}/media/{slug}-{k}.jpg')
        h, mi = re.match(r'(?:(\d+)h )?(\d+)m', tm).groups()
        secs = int(h or 0) * 3600 + int(mi) * 60
        out.append('  <game>')
        out.append(f'    <path>./{rom}</path><name>{html.escape(title)}</name><genre>{genre}</genre><releasedate>{year}0101T000000</releasedate>')
        out.append(f'    <image>./media/{slug}-snap.jpg</image><thumbnail>./media/{slug}-box.jpg</thumbnail>')
        if int(plays):
            out.append(f'    <playcount>{plays}</playcount><gametime>{secs}</gametime>')
            lp = stamp(last)
            if lp:
                out.append(f'    <lastplayed>{lp}</lastplayed>')
        if slug in favs:
            out.append('    <favorite>true</favorite>')
        out.append('  </game>')
    out.append('</gameList>')
    open(f'{d}/gamelist.xml', 'w').write('\n'.join(out))

cfg = ['<?xml version="1.0"?>', '<systemList>']
for es, name, maker in systems:
    cfg.append(f'  <system><name>{es}</name><fullname>{name}</fullname><manufacturer>{maker}</manufacturer><path>{ROOT}/roms/{es}</path>'
               f'<extension>{exts[es]}</extension><command>/bin/true %ROM%</command><platform>{es}</platform><theme>{es}</theme></system>')
cfg.append('</systemList>')
open(f'{ES}/es_systems.cfg', 'w').write('\n'.join(cfg))
open(f'{ES}/es_settings.cfg', 'w').write('''<?xml version="1.0"?>
<config>
	<string name="ThemeSet" value="%s" />
	<string name="SortSystems" value="" />
	<string name="CollectionSystemsAuto" value="favorites" />
	<bool name="ShowHelpPrompts" value="false" />
	<bool name="ClockMode12" value="true" />
	<string name="PowerSaverMode" value="enhanced" />
	<bool name="DrawClock" value="false" />
</config>
''' % THEME)
print('systems', len(systems))
