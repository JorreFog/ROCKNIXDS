#!/usr/bin/env python3
"""mockdata.py <mockup>/index.html > mockdata.inc: the mockup's systems and games as C++ initialisers for harness.cpp."""
import json, re, sys

src = open(sys.argv[1], encoding='utf-8').read()
block = src[src.index('const systems = ['):src.index('// Ten console icons')]
out = []
for m in re.finditer(r"\{ id:'(\w+)', name:'([^']+)', maker:'([^']+)', accent:'(#\w+)', games:(\d+), played:(\d+), "
                     r"last:'([^']+)', time:'([^']+)', resume:(\d+),\s*list:\[(.*?)\]\}", block, re.S):
    sid, name, maker, accent, games, played, last, time, resume, lst = m.groups()
    games_l = []
    for g in re.finditer(r"g\(((?:'[^']*'|\"[^\"]*\")),'([^']+)','(\d+)',(\d+),'([^']+)','([^']+)',(\d+),(\d+),'(\w+)','(\w+)'\)", lst):
        games_l.append('{%s,"%s","%s",%s,"%s","%s",%s,%s,"%s"}' % (json.dumps(g.group(1)[1:-1]), *g.groups()[1:9]))
    out.append('{"%s","%s","%s",0x%s,%s,%s,"%s",%s,{%s}}' % (sid, name, maker, accent[1:], games, played, time, resume,
                                                            ',\n  '.join(games_l)))
print(',\n'.join(out))
