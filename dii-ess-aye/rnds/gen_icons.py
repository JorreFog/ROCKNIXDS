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


# ---- drawn icons: shapes on a 32x32 grid, then the pack's outline and light edge added by outline() ----------------
class Canvas:
    def __init__(self):
        self.g = [['.'] * 32 for _ in range(32)]

    def px(self, x, y, c):
        if 0 <= x < 32 and 0 <= y < 32:
            self.g[y][x] = c

    def rect(self, x0, y0, x1, y1, c):          # inclusive
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.px(x, y, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(32):
            for x in range(32):
                if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1:
                    self.px(x, y, c)

    def ring(self, cx, cy, r0, r1, c, keep=lambda x, y: True):
        for y in range(32):
            for x in range(32):
                d = ((x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2) ** 0.5
                if r0 <= d <= r1 and keep(x, y):
                    self.px(x, y, c)

    def poly(self, pts, c):
        n = len(pts)
        for y in range(32):
            for x in range(32):
                inside = False
                px_, py_ = x + 0.5, y + 0.5
                for i in range(n):
                    (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
                    if (y0 > py_) != (y1 > py_) and px_ < x0 + (py_ - y0) * (x1 - x0) / (y1 - y0):
                        inside = not inside
                if inside:
                    self.px(x, y, c)

    def rows(self):
        return [''.join(r) for r in outline(self.g)]


def outline(g):
    """The pack's look: a black 1 px outline around every shape (4-neighbours of transparent pixels)."""
    out = [r[:] for r in g]
    for y in range(32):
        for x in range(32):
            if g[y][x] != '.':
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < 32 and 0 <= ny < 32 and g[ny][nx] not in '.a':
                    out[y][x] = 'a'
                    break
    return out


def music():
    c = Canvas()
    c.poly([(11, 7), (27, 4), (27, 9), (11, 12)], 'p')          # the beam
    c.rect(11, 9, 12, 22, 'P'); c.rect(25, 6, 26, 19, 'P')     # stems
    c.ellipse(9, 23.5, 4.5, 3.5, 'p'); c.ellipse(23, 20.5, 4.5, 3.5, 'p')
    c.px(7, 22, 'w'); c.px(8, 21, 'w'); c.px(21, 19, 'w'); c.px(22, 18, 'w')
    c.rect(12, 7, 26, 7, 'q')
    return c.rows()


def video():
    c = Canvas()
    c.rect(4, 13, 27, 27, 'k')                                    # the slate
    c.rect(5, 14, 26, 14, 'd')
    c.rect(6, 17, 25, 17, 'm'); c.rect(6, 20, 20, 20, 'm'); c.rect(6, 23, 23, 23, 'm')
    c.poly([(3, 6), (26, 2), (27, 7), (4, 11)], 'k')              # the clapper, open
    for i in range(4):                                            # its stripes
        x = 6 + i * 5
        c.poly([(x, 9 - i * 0.9), (x + 3, 8.4 - i * 0.9), (x + 5, 3.5 - i * 0.9 + 0.6), (x + 2, 4.1 - i * 0.9 + 0.6)], 'w')
    c.rect(4, 12, 27, 12, 'w')
    for i in range(4):
        c.rect(5 + i * 6, 12, 7 + i * 6, 12, 'k')
    return c.rows()


def picture():
    c = Canvas()
    c.rect(3, 6, 28, 26, 'o')                                     # the frame
    c.rect(3, 6, 28, 6, 'O'); c.rect(3, 6, 3, 26, 'O')
    c.rect(6, 9, 25, 23, 's')                                     # sky
    c.ellipse(21, 13, 2.6, 2.6, 'y')                              # sun
    c.poly([(6, 23), (6, 18), (11, 13), (17, 19), (20, 16), (25.9, 21), (25.9, 23)], 'g')   # hills
    c.poly([(11, 13), (9, 15), (13, 15)], 'w')                    # snow cap
    c.rect(6, 22, 25, 23, 'G')
    return c.rows()


def moon():
    c = Canvas()
    c.ellipse(15, 16, 11, 11, 'y')
    c.ellipse(21, 12, 9.5, 9.5, '.')                              # bite: a crescent
    c.ellipse(9, 20, 2, 1.6, 'Y'); c.ellipse(13, 24.5, 1.5, 1.2, 'Y')   # craters
    for x, y in ((24, 21), (27, 15), (22, 26)):                   # stars
        c.px(x, y, 'w'); c.px(x - 1, y, 'l'); c.px(x + 1, y, 'l'); c.px(x, y - 1, 'l'); c.px(x, y + 1, 'l')
    return c.rows()


def fantasy():
    c = Canvas()
    c.rect(5, 5, 26, 27, 'r')                                     # a little console, PICO-8 red
    c.rect(5, 5, 26, 5, 'R'); c.rect(26, 5, 26, 27, 'n')
    c.rect(8, 8, 23, 18, 'k')                                     # its screen
    c.rect(10, 10, 12, 16, 'y'); c.rect(13, 10, 15, 16, 'g'); c.rect(16, 10, 18, 16, 'b'); c.rect(19, 10, 21, 16, 'p')
    c.rect(9, 21, 13, 22, 'k'); c.rect(10, 20, 12, 23, 'k')       # d-pad
    c.ellipse(19.5, 22.5, 1.6, 1.6, 'w'); c.ellipse(23, 21, 1.6, 1.6, 'w')
    return c.rows()


def phone():
    c = Canvas()
    c.rect(9, 3, 22, 29, 'k')                                     # a candy-bar phone
    c.rect(9, 3, 22, 3, 'm'); c.rect(9, 3, 9, 29, 'd')
    c.rect(11, 6, 20, 14, 'g'); c.rect(12, 7, 19, 7, 'G'); c.rect(12, 9, 16, 9, 'G')
    c.rect(14, 16, 17, 17, 'm')
    for row in range(4):
        for col in range(3):
            c.rect(11 + col * 4, 19 + row * 2 + row // 2 * 0, 12 + col * 4, 19 + row * 2, 'l')
    c.rect(19, 1, 20, 3, 'k')                                     # antenna
    return c.rows()


def zombie():
    """Döda Kvarter (ROCKNIXDS's zombie roguelike): one of the dead, in the pack's style."""
    c = Canvas()
    c.ellipse(16, 14.5, 10, 10, 'g')                              # the head
    c.rect(8, 15, 23, 23, 'g'); c.rect(10, 24, 21, 26, 'g')       # jaw and chin
    c.rect(7, 16, 7, 20, 'g'); c.rect(24, 16, 24, 20, 'g')        # ears
    c.rect(22, 12, 23, 23, 'h'); c.rect(20, 24, 21, 26, 'h'); c.rect(24, 17, 24, 20, 'h')   # the side in shadow
    c.rect(11, 25, 19, 26, 'h')
    c.poly([(6.2, 12), (7, 7), (10, 4), (16, 3), (22, 4), (25, 7), (25.8, 12), (23, 9), (20, 10.5), (17, 8), (14, 10.5),
            (11, 8.5), (8.5, 10.5)], 'k')                        # hair, in tufts
    c.rect(10, 5, 14, 5, 'd'); c.rect(8, 7, 9, 7, 'd')            # its light edge
    c.rect(9, 12, 12, 12, 'G'); c.rect(8, 13, 8, 15, 'G')         # the brow's light
    c.ellipse(12, 16.5, 3.2, 3.2, 'w')                            # the open eye
    c.rect(12, 16, 13, 17, 'r'); c.px(12, 16, 'R')
    c.rect(18, 15, 22, 15, 'k'); c.rect(19, 16, 21, 17, 'k'); c.px(20, 16, 'y')   # the other one, sunk
    c.rect(15, 18, 16, 20, 'h')                                   # nose
    c.rect(11, 22, 21, 23, 'k')                                   # mouth
    for x in (12, 15, 18):                                        # teeth
        c.px(x, 22, 'w'); c.px(x + 1, 22, 'w')
    c.px(13, 23, 'w'); c.px(19, 23, 'w')
    c.rect(18, 9, 18, 12, 'h'); c.px(17, 10, 'h'); c.px(19, 10, 'h'); c.px(17, 12, 'h'); c.px(19, 12, 'h')   # a stitched cut
    c.rect(13, 27, 18, 29, 'p'); c.rect(11, 29, 20, 30, 'p'); c.rect(13, 27, 18, 27, 'h')   # neck and collar
    return c.rows()


def bank():
    """ROCKNIXDS Bank & Trade: a bank's front, a coin in its gable."""
    c = Canvas()
    c.poly([(16, 2.2), (29.5, 11), (2.5, 11)], 'l')               # the gable
    c.poly([(16, 4.4), (25.5, 10), (6.5, 10)], 'b')
    c.ellipse(16, 8, 2.1, 2.1, 'y'); c.px(15, 7, 'Y')             # the coin
    c.rect(3, 11, 28, 12, 'w'); c.rect(3, 13, 28, 13, 'm')        # the beam
    for x in (5, 11, 18, 24):                                     # four columns
        c.rect(x, 14, x + 2, 23, 'w'); c.rect(x + 2, 14, x + 2, 23, 'l'); c.rect(x, 14, x + 2, 14, 'l'); c.rect(x, 23, x + 2, 23, 'l')
    c.rect(8, 14, 10, 23, 'B'); c.rect(21, 14, 23, 23, 'B')       # the dark between them
    c.rect(14, 15, 17, 23, 'n'); c.rect(14, 15, 17, 15, 'B'); c.px(16, 19, 'y')   # the door
    c.rect(3, 24, 28, 25, 'w'); c.rect(3, 25, 28, 25, 'l')        # steps
    c.rect(1, 26, 30, 27, 'l'); c.rect(1, 27, 30, 27, 'm')
    return c.rows()


def store():
    """The ROCKNIXDS Store: a shopping bag, a down arrow on it (something new to install)."""
    c = Canvas()
    c.ring(16, 9.5, 4.2, 6, 'm', keep=lambda x, y: y < 10)       # the handle
    c.poly([(5, 10), (27, 10), (29, 29), (3, 29)], 'p')           # the bag
    c.poly([(5, 10), (27, 10), (27.3, 13), (4.7, 13)], 'P')       # its fold
    c.rect(5, 10, 26, 10, 'q')
    c.poly([(23, 13), (27.3, 13), (29, 29), (24, 29)], 'P')       # shade on its right side
    c.rect(14, 15, 17, 21, 'w')                                   # the arrow
    c.poly([(10, 21), (22, 21), (16, 27)], 'w')
    c.rect(17, 15, 17, 21, 'l'); c.px(19, 22, 'l'); c.px(18, 23, 'l'); c.px(17, 24, 'l')
    return c.rows()


def gear():
    c = Canvas()
    import math
    for i in range(8):                                            # teeth
        a = i * math.pi / 4
        cx, cy = 16 + 10.5 * math.cos(a), 16 + 10.5 * math.sin(a)
        c.ellipse(cx, cy, 2.6, 2.6, 'l')
    c.ellipse(16, 16, 10, 10, 'l')
    c.ring(16, 16, 7, 10, 'm', keep=lambda x, y: x + y > 31)      # shade on the lower right
    c.ellipse(16, 16, 4, 4, '.')                                  # the hole
    return c.rows()


def apps():
    c = Canvas()
    cols = ['r', 'y', 'g', 'b', 'p', 'o', 'c', 'r', 'y']
    for i in range(9):
        x, y = 5 + (i % 3) * 8, 5 + (i // 3) * 8
        c.rect(x, y, x + 5, y + 5, cols[i])
        c.rect(x, y, x + 5, y, 'w')
    return c.rows()


def console():
    c = Canvas()
    c.rect(3, 9, 28, 20, 'k')                                     # a home console: black box, cartridge slot
    c.rect(3, 9, 28, 9, 'd')
    c.rect(4, 14, 27, 14, 'o'); c.rect(4, 15, 27, 15, 'O')        # the wood trim
    c.rect(8, 11, 19, 12, 'a')                                    # slot
    c.rect(22, 11, 23, 12, 'r'); c.rect(25, 11, 26, 12, 'm')      # switches
    c.rect(6, 23, 13, 28, 'l'); c.rect(6, 23, 13, 23, 'w')        # controller
    c.rect(8, 25, 9, 26, 'k'); c.rect(11, 25, 11, 25, 'r')
    c.rect(9, 21, 9, 22, 'k'); c.rect(10, 20, 15, 20, 'k')        # its cord
    return c.rows()


def gamepad():
    c = Canvas()
    c.poly([(4, 13), (8, 9), (24, 9), (28, 13), (29, 24), (26, 27), (22, 25), (19, 21), (13, 21), (10, 25), (6, 27), (3, 24)], 'l')
    c.rect(8, 9, 24, 10, 'w')
    c.rect(8, 15, 12, 15, 'k'); c.rect(10, 13, 10, 17, 'k')       # d-pad
    c.ellipse(21.5, 13.5, 1.4, 1.4, 'g'); c.ellipse(24.5, 16, 1.4, 1.4, 'r')
    c.ellipse(18.5, 16, 1.4, 1.4, 'b'); c.ellipse(21.5, 18.5, 1.4, 1.4, 'y')
    c.rect(14, 13, 15, 13, 'm'); c.rect(17, 13, 18, 13, 'm')
    return c.rows()


def book():
    c = Canvas()
    c.poly([(2, 8), (15, 10), (15, 28), (2, 26)], 'w')            # pages, open
    c.poly([(17, 10), (30, 8), (30, 26), (17, 28)], 'w')
    c.rect(15, 10, 16, 28, 'm')
    for i in range(5):
        y = 13 + i * 3
        c.rect(4, y, 13, y, 'l'); c.rect(19, y, 28, y, 'l')
    c.poly([(1, 25), (15, 28), (17, 28), (31, 25), (31, 28), (17, 30), (15, 30), (1, 28)], 'b')   # the cover
    return c.rows()


def sword():
    c = Canvas()
    c.poly([(24, 3), (28, 3), (28, 7), (13, 22), (9, 18)], 's')   # blade
    c.poly([(25, 4), (27, 4), (12, 19), (11, 18)], 'S')
    c.poly([(6, 17), (8, 15), (16, 23), (14, 25)], 'y')           # guard
    c.poly([(9, 21), (11, 23), (6, 28), (4, 26)], 'o')            # grip
    c.ellipse(4.5, 27.5, 1.8, 1.8, 'y')                           # pommel
    return c.rows()


DRAWN = {
    'music': ({'p': rgba('b07ef0'), 'P': rgba('7c4fc4'), 'q': rgba('d9c2ff')}, music),
    'video': ({}, video),
    'picture': ({'o': rgba('b0703a'), 'O': rgba('d9a066'), 's': rgba('7ec8ee'), 'y': rgba('f2c94c'), 'g': rgba('5bb85b'),
                 'G': rgba('2f7f3f')}, picture),
    'moon': ({'y': rgba('f2d16b'), 'Y': rgba('c9a640')}, moon),
    'fantasy': ({'r': rgba('e25a5a'), 'R': rgba('f2a0a0'), 'n': rgba('9c3434'), 'y': rgba('f2c94c'), 'g': rgba('5bb85b'),
                 'b': rgba('4c8ee0'), 'p': rgba('b07ef0')}, fantasy),
    'phone': ({'g': rgba('b0d459'), 'G': rgba('46514c')}, phone),
    'gear': ({}, gear),
    'dodakvarter': ({'g': rgba('7fae4e'), 'G': rgba('a8d477'), 'h': rgba('4f7f3a'), 'r': rgba('d8322f'), 'R': rgba('ff8a7a'),
                     'y': rgba('e8d44a'), 'p': rgba('5a4a78')}, zombie),
    'bank': ({'b': rgba('3d6fb8'), 'B': rgba('24476f'), 'n': rgba('1f3d6e'), 'y': rgba('f2c94c'), 'Y': rgba('fff0a8')}, bank),
    'store': ({'p': rgba('2eae62'), 'P': rgba('1e7a44'), 'q': rgba('7fe0a4')}, store),
    'apps': ({'r': rgba('e25a5a'), 'y': rgba('f2c94c'), 'g': rgba('5bb85b'), 'b': rgba('4c8ee0'), 'p': rgba('b07ef0'),
              'o': rgba('e08a4e'), 'c': rgba('44d4ca')}, apps),
    'console': ({'o': rgba('b0703a'), 'O': rgba('7a4a24'), 'r': rgba('e25a5a')}, console),
    'gamepad': ({'g': rgba('5bb85b'), 'r': rgba('e25a5a'), 'b': rgba('4c8ee0'), 'y': rgba('f2c94c')}, gamepad),
    'book': ({'b': rgba('3d6fb8')}, book),
    'sword': ({'s': rgba('c3c9d4'), 'S': rgba('f4f6fa'), 'y': rgba('f2c94c'), 'o': rgba('8a5a2e')}, sword),
}
for _name, (_pal, _fn) in DRAWN.items():
    ICONS[_name] = (_pal, _fn())


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
