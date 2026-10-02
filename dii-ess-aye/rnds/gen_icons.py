#!/usr/bin/env python3
"""Generic 32x32 pixel icons for the systems the console icon pack doesn't draw, in the pack's style (a black 1 px
outline, a light top edge, two or three greys and a little colour).

    python3 gen_icons.py <out dir>

Each icon is a character map; '.' is transparent. No dependencies: PNGs are written with zlib.
"""
import os, struct, sys, zlib


def png(path, rows, pal):
    h, w = len(rows), len(rows[0])
    raw = b''
    for r in rows:
        assert len(r) == w, (path, r)
        raw += b'\0' + b''.join(bytes(pal[c]) for c in r)
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)


def rgba(h, a=255):
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


BASE = {'.': (0, 0, 0, 0), 'a': rgba('000000'), 'w': rgba('e1e1e1'), 'l': rgba('c3c3c3'), 'm': rgba('959293'),
        'd': rgba('676767'), 'k': rgba('292929')}

ICONS = {
    # a grey cartridge with a blue label and a gold edge connector (any console without its own icon)
    'cart': ({'b': rgba('3d6fb8'), 'B': rgba('7ea8e6'), 'n': rgba('1f3d6e'), 'g': rgba('d9b44a'), 'G': rgba('8a6a1e')}, [
        '................................',
        '................................',
        '................................',
        '......aaaaaaaaaaaaaaaaaaaa......',
        '.....awwwwwwwwwwwwwwwwwwwwa.....',
        '.....awllllllllllllllllllda.....',
        '.....awlaaaaaaaaaaaaaaaalda.....',
        '.....awlaBBBBBBBBBBBBBBalda.....',
        '.....awlaBbbbbbbbbbbbbnalda.....',
        '.....awlaBbwwwwwwwwwwbnalda.....',
        '.....awlaBbbbbbbbbbbbbnalda.....',
        '.....awlaBbwwwwwwbbbbbnalda.....',
        '.....awlaBbbbbbbbbbbbbnalda.....',
        '.....awlaBbbbbbbbbbbbbnalda.....',
        '.....awlaBnnnnnnnnnnnnnalda.....',
        '.....awlaaaaaaaaaaaaaaaalda.....',
        '.....awllllllllllllllllllda.....',
        '.....awlmmmmmmmmmmmmmmmmlda.....',
        '.....awllllllllllllllllllda.....',
        '.....awlmmmmmmmmmmmmmmmmlda.....',
        '.....awllllllllllllllllllda.....',
        '.....awlmmmmmmmmmmmmmmmmlda.....',
        '.....awllllllllllllllllllda.....',
        '.....admmmmmmmmmmmmmmmmmmda.....',
        '......aaaaaaaaaaaaaaaaaaaa......',
        '.......akkkkkkkkkkkkkkkka.......',
        '.......akgGgGgGgGgGgGgGka.......',
        '.......akgGgGgGgGgGgGgGka.......',
        '.......aaaaaaaaaaaaaaaaaa.......',
        '................................',
        '................................',
        '................................',
    ]),
    # a CD with a rainbow sheen (disc-based consoles)
    'disc': ({'s': rgba('d8dde6'), 'S': rgba('f4f6fa'), 'r': rgba('ee5753'), 'y': rgba('f2c94c'), 'c': rgba('44d4ca'), 'p': rgba('9d7bf4'), 'h': rgba('8a93a3')}, [
        '................................',
        '................................',
        '................................',
        '...........aaaaaaaaaa...........',
        '.........aaSSSSSSSssshaa........',
        '.......aaSSSSSSSSsssssshhaa.....',
        '......aSSSSSSSSsssssssshhhha....',
        '.....aSSSSSSSssssssssshhhhhha...',
        '....aSSSSSSsssssssssshhhhhhhha..',
        '....asSSSSssssssssshhhhhhhhhha..',
        '...asssSSsssssssshhhhhhhhhhhhha.',
        '...assssssssyyyrrhhhhhhhhhhhhha.',
        '..asssssssyyyrraaaarrhhhhhhhhhha',
        '..asssssscccyaaddddaarhhhhhhhhha',
        '..assssssccca.dkkkkd.ahhhhhhhhha',
        '..asssssppcca.dk..kd.ahhhhhhhhha',
        '..assssspppca.dk..kd.ahhhhhhhhha',
        '..asssssppcca.dkkkkd.ahhhhhhhhha',
        '..asssssscccaaddddaacchhhhhhhhha',
        '..ahsssssscccpaaaapcchhhhhhhhhha',
        '...ahsssssssppppcccssshhhhhhhha.',
        '...ahhssssssssssssssssshhhhhhha.',
        '....ahhsssssssssssssssshhhhhha..',
        '....ahhhssssssssssssssshhhhhha..',
        '.....ahhhhssssssssssshhhhhhha...',
        '......ahhhhhhhhhhhhhhhhhhhha....',
        '.......aahhhhhhhhhhhhhhhhaa.....',
        '.........aahhhhhhhhhhhhaa.......',
        '...........aaaaaaaaaaaa.........',
        '................................',
        '................................',
        '................................',
    ]),
    # a landscape handheld with a green screen (portables without their own icon)
    'handheld': ({'g': rgba('b0d459'), 'G': rgba('46514c'), 'r': rgba('ee5753'), 'R': rgba('8a2a2a'), 'b': rgba('4c4d8e')}, [
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '....aaaaaaaaaaaaaaaaaaaaaaaa....',
        '...awwwwwwwwwwwwwwwwwwwwwwwwa...',
        '..awlllllllaaaaaaaaaallllllllda.',
        '..awlllllllaGGGGGGGGallllllllda.',
        '..awllkklllaGGgggggGalllllrlllda',
        '..awlkkkkllaGGgggggGallllrRrllda',
        '..awllkklllaGGgggggGalllllrlllda',
        '..awlllllllaGggggggGallllbllllda',
        '..awlllllllaGggggggGalllbRblllda',
        '..awlllllllaGGGGGGGGallllbllllda',
        '..awlllllllaaaaaaaaaallllllllda.',
        '..admmmmmmmmmmmmmmmmmmmmmmmmmda.',
        '...addddddddddddddddddddddddda..',
        '....aaaaaaaaaaaaaaaaaaaaaaaaa...',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
    ]),
    # a beige home computer: monitor on a keyboard (computers)
    'computer': ({'e': rgba('e9dfc4'), 'E': rgba('cbbf9e'), 'f': rgba('9c9070'), 's': rgba('2b5d8a'), 'S': rgba('7ec8ee'), 'g': rgba('44d4ca')}, [
        '................................',
        '................................',
        '.......aaaaaaaaaaaaaaaaaa.......',
        '......aeeeeeeeeeeeeeeeeeea......',
        '......aeEEEEEEEEEEEEEEEEfa......',
        '......aeEaaaaaaaaaaaaaaEfa......',
        '......aeEasssssssssssSaEfa......',
        '......aeEasgssssssssssaEfa......',
        '......aeEasssssssssssSaEfa......',
        '......aeEasgggssssssssaEfa......',
        '......aeEassssssssssssaEfa......',
        '......aeEassssssssssssaEfa......',
        '......aeEaaaaaaaaaaaaaaEfa......',
        '......aeEEEEEEEEEEEEEEEEfa......',
        '......affffffffffffffffffa......',
        '.......aaaaaaaaaaaaaaaaaa.......',
        '..........aEEEEEEEEEEa..........',
        '...aaaaaaaaaaaaaaaaaaaaaaaaaa...',
        '..aeeeeeeeeeeeeeeeeeeeeeeeeeea..',
        '..aeEaEaEaEaEaEaEaEaEaEaEaEEfa..',
        '..aeEEEEEEEEEEEEEEEEEEEEEEEEfa..',
        '..aeEaEaEaEaEaEaEaEaEaEaEaEEfa..',
        '..aeEEEEEEEEEEEEEEEEEEEEEEEEfa..',
        '..aeEEEaEaaaaaaaaaaaaaaEaEEEfa..',
        '..affffffffffffffffffffffffffa..',
        '...aaaaaaaaaaaaaaaaaaaaaaaaaa...',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
    ]),
    # a classic joystick on a black base (early consoles)
    'joystick': ({'r': rgba('ee5753'), 'R': rgba('a52a2a'), 'h': rgba('ffb3b0'), 'o': rgba('e08a4e')}, [
        '................................',
        '................................',
        '................................',
        '.............aaaaa..............',
        '............ahhrrRa.............',
        '...........ahhrrrrRa............',
        '...........ahrrrrrRa............',
        '...........arrrrrrRa............',
        '............aRRRRRa.............',
        '.............aaaaa..............',
        '..............akda..............',
        '..............akda..............',
        '..............akda..............',
        '..............akda..............',
        '..............akda..............',
        '..............akda..............',
        '.........aaaaaakdaaaaaa.........',
        '.......aawwwwwwwwwwwwwwaa.......',
        '......awllllllllllllllllla......',
        '.....awllaaalllllllllllllda.....',
        '.....awlarRalllllllllllllda.....',
        '.....awlaRRalllllllllllllda.....',
        '.....awllaaalllllllllllllda.....',
        '.....admmmmmmmmmmmmmmmmmmda.....',
        '......adddddddddddddddddda......',
        '.......aaaaaaaaaaaaaaaaaaa......',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
    ]),
    # a wrench (ROCKNIX's tools)
    'tools': ({'s': rgba('c3c9d4'), 'S': rgba('eef1f6'), 'n': rgba('7d8794')}, [
        '................................',
        '................................',
        '................................',
        '.....................aaaa.......',
        '...................aaSSSSa......',
        '..................aSSsssa.......',
        '..................aSssa.........',
        '.................aSsssa...aa....',
        '.................aSsssaaaaSa....',
        '.................aSssssSSSSa....',
        '................aSssssssssna....',
        '...............aSsssssssnna.....',
        '..............aSsssssnnnaa......',
        '.............aSsssnnaaaa........',
        '............aSsssnna............',
        '...........aSsssnna.............',
        '..........aSsssnna..............',
        '.........aSsssnna...............',
        '........aSsssnna................',
        '.......aSsssnna.................',
        '......aSsssnna..................',
        '.....aSsssnna...................',
        '....aSsssnna....................',
        '....asssnna.....................',
        '....aannna......................',
        '.....aaaa.......................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
        '................................',
    ]),
}

# a stack of three cartridges (collections: all games, last played, custom collections)
MINI = [
    'aaaaaaaaaaaaaaaaaaa',
    'awwwwwwwwwwwwwwwwwa',
    'awlaaaaaaaaaaaaalda',
    'awlaXXXXXXXXXXXalda',
    'awlaxxxxxxxxxxxalda',
    'awlaxwwwwwwwwxxalda',
    'awlaxxxxxxxxxxxalda',
    'awlaxwwwwwxxxxxalda',
    'awlaxxxxxxxxxxxalda',
    'awlaaaaaaaaaaaaalda',
    'awllllllllllllllda.',
    'awlmmmmmmmmmmmmlda.',
    'awllllllllllllllda.',
    'admmmmmmmmmmmmmmda.',
    '.aaaaaaaaaaaaaaaa..',
]


def stack():
    grid = [['.'] * 32 for _ in range(32)]
    for (ox, oy, lab, lit) in ((10, 3, 'r', 'R'), (6, 8, 'g', 'G'), (2, 13, 'b', 'B')):
        for y, row in enumerate(MINI):
            for x, c in enumerate(row):
                if c == '.':
                    continue
                grid[oy + y][ox + x] = {'x': lab, 'X': lit}.get(c, c)
    return [''.join(r) for r in grid]


ICONS['collection'] = ({'b': rgba('3d6fb8'), 'B': rgba('7ea8e6'), 'g': rgba('2eae62'), 'G': rgba('8fe0a8'),
                        'r': rgba('e25a5a'), 'R': rgba('f2a0a0')}, stack())


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, (pal, rows) in ICONS.items():
        p = dict(BASE)
        p.update(pal)
        rows = [r[:32].ljust(32, '.') for r in rows]
        png(os.path.join(out, name + '.png'), rows, p)
        print(name)


if __name__ == '__main__':
    main()
