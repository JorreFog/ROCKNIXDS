#!/usr/bin/env python3
"""ROCKNIXDS Bank's pictures for the menu (device/media): the cover on the top screen and the cartridge's label
(bank-image.png and bank-thumb.png, 640x480) and the title strip (bank-marquee.png, 400x164). Pixel art drawn here,
no dependencies: PNGs are written with zlib. No game's artwork is used.

    python3 gen_media.py <out dir>
"""
import os, struct, sys, zlib


def png(path, w, h, px):
    raw = b''.join(b'\0' + bytes(px[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    data += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)


def rgb(h):
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


class Img:
    def __init__(self, w, h, bg):
        self.w, self.h = w, h
        self.px = bytearray(bytes(bg) * (w * h))

    def rect(self, x0, y0, x1, y1, c):          # inclusive
        x0, y0, x1, y1 = max(0, x0), max(0, y0), min(self.w - 1, x1), min(self.h - 1, y1)
        row = bytes(c) * (x1 - x0 + 1)
        for y in range(y0, y1 + 1):
            self.px[(y * self.w + x0) * 3:(y * self.w + x1 + 1) * 3] = row

    def block(self, rows, pal, ox, oy, k):      # a character map, each cell k x k
        for y, r in enumerate(rows):
            for x, ch in enumerate(r):
                if ch != '.':
                    self.rect(ox + x * k, oy + y * k, ox + x * k + k - 1, oy + y * k + k - 1, pal[ch])


# the bank's front: the menu's 32x32 icon for the bank, as dii-ess-aye/rnds/gen_icons.py draws it (the tile on the
# shelf and these pictures show the same building)
BANK = [
    '................................',
    '................................',
    '..............aaaa..............',
    '............aallllaa............',
    '...........alllllllla...........',
    '.........aalllbbbblllaa.........',
    '........alllbbbyybbbllla........',
    '......aalllbbbyYyybbblllaa......',
    '.....alllbbbbbyyyybbbbbllla.....',
    '...aallbbbbbbbbyybbbbbbbbllaa...',
    '..alllllllllllllllllllllllllla..',
    '..awwwwwwwwwwwwwwwwwwwwwwwwwwa..',
    '..awwwwwwwwwwwwwwwwwwwwwwwwwwa..',
    '..ammmmmmmmmmmmmmmmmmmmmmmmmma..',
    '...aalllBBBlllaaaalllBBBlllaa...',
    '....awwlBBBwwlBBBBwwlBBBwwla....',
    '....awwlBBBwwlnnnnwwlBBBwwla....',
    '....awwlBBBwwlnnnnwwlBBBwwla....',
    '....awwlBBBwwlnnnnwwlBBBwwla....',
    '....awwlBBBwwlnnynwwlBBBwwla....',
    '....awwlBBBwwlnnnnwwlBBBwwla....',
    '....awwlBBBwwlnnnnwwlBBBwwla....',
    '....awwlBBBwwlnnnnwwlBBBwwla....',
    '...aalllBBBlllnnnnlllBBBlllaa...',
    '..awwwwwwwwwwwwwwwwwwwwwwwwwwa..',
    '.aallllllllllllllllllllllllllaa.',
    'alllllllllllllllllllllllllllllla',
    'ammmmmmmmmmmmmmmmmmmmmmmmmmmmmma',
    '.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.',
    '................................',
    '................................',
    '................................',
]
assert all(len(r) == 32 for r in BANK) and len(BANK) == 32
PAL = {'a': rgb('000000'), 'w': rgb('e1e1e1'), 'l': rgb('c3c3c3'), 'm': rgb('959293'), 'b': rgb('3d6fb8'), 'B': rgb('24476f'),
       'n': rgb('1f3d6e'), 'y': rgb('f2c94c'), 'Y': rgb('fff0a8')}

# a 5x7 face for the words on the pictures
FONT = {
    'A': ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'B': ['####.', '#...#', '#...#', '####.', '#...#', '#...#', '####.'],
    'C': ['.####', '#....', '#....', '#....', '#....', '#....', '.####'],
    'D': ['####.', '#...#', '#...#', '#...#', '#...#', '#...#', '####.'],
    'E': ['#####', '#....', '#....', '####.', '#....', '#....', '#####'],
    'I': ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '#####'],
    'K': ['#...#', '#..#.', '#.#..', '##...', '#.#..', '#..#.', '#...#'],
    'N': ['#...#', '##..#', '#.#.#', '#.#.#', '#..##', '#...#', '#...#'],
    'O': ['.###.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    'R': ['####.', '#...#', '#...#', '####.', '#.#..', '#..#.', '#...#'],
    'S': ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
    'T': ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
    'X': ['#...#', '#...#', '.#.#.', '..#..', '.#.#.', '#...#', '#...#'],
    '&': ['.##..', '#..#.', '#..#.', '.##..', '#.#.#', '#..#.', '.##.#'],
    ' ': ['.....'] * 7,
}


def text_w(s, k):
    return (len(s) * 6 - 1) * k


def text(img, s, x, y, k, c, shadow=None):
    for i, ch in enumerate(s):
        g = FONT[ch]
        for gy, row in enumerate(g):
            for gx, bit in enumerate(row):
                if bit == '#':
                    px, py = x + (i * 6 + gx) * k, y + gy * k
                    if shadow:
                        img.rect(px + k, py + k, px + 2 * k - 1, py + 2 * k - 1, shadow)
        for gy, row in enumerate(g):
            for gx, bit in enumerate(row):
                if bit == '#':
                    px, py = x + (i * 6 + gx) * k, y + gy * k
                    img.rect(px, py, px + k - 1, py + k - 1, c)


def backdrop(img):
    # a night-blue sheet, lighter toward the middle rows, with the menu's dither of dots
    top, mid = rgb('0e1522'), rgb('16233a')
    for y in range(img.h):
        t = 1 - abs(y - img.h * 0.45) / (img.h * 0.6)
        t = max(0.0, min(1.0, t))
        c = tuple(int(top[i] + (mid[i] - top[i]) * t) for i in range(3))
        img.rect(0, y, img.w - 1, y, c)
    for y in range(0, img.h, 8):
        for x in range((y // 8 % 2) * 4, img.w, 8):
            img.rect(x, y, x + 1, y + 1, rgb('1d2c47'))


def slots(img, x0, y0, n, k, colours):
    # a row of a storage box: framed cells, a blob of colour in some (nobody's sprite)
    for i in range(n):
        x = x0 + i * (k + 6)
        img.rect(x, y0, x + k - 1, y0 + k - 1, rgb('0b111c'))
        img.rect(x + 2, y0 + 2, x + k - 3, y0 + k - 3, rgb('22344f'))
        c = colours[i % len(colours)]
        if c:
            img.rect(x + k // 2 - 8, y0 + k // 2 - 6, x + k // 2 + 7, y0 + k // 2 + 9, rgb('0b111c'))
            img.rect(x + k // 2 - 6, y0 + k // 2 - 4, x + k // 2 + 5, y0 + k // 2 + 7, rgb(c))
            img.rect(x + k // 2 - 4, y0 + k // 2 - 2, x + k // 2 - 1, y0 + k // 2 + 1, rgb('ffffff'))


def arrows(img, cx, y, k):
    # the trade: one arrow each way, gold, outlined
    R = ['......a......', '......aa.....', 'aaaaaaaya....', 'ayyyyyyyya...', 'ayyyyyyyyya..', 'ayyyyyyyya...', 'aaaaaaaya....',
         '......aa.....', '......a......']
    L = [r[::-1] for r in R]
    pal = {'a': rgb('000000'), 'y': rgb('f2c94c')}
    img.block(R, pal, cx + 150, y, k)
    img.block(L, pal, cx - 150 - 13 * k, y + 5 * k, k)


def cover(path):
    # the top screen's picture: the bank, a trade's two arrows, the name, a row of a box
    img = Img(640, 480, rgb('0e1522'))
    backdrop(img)
    k = 7
    img.block(BANK, PAL, (640 - 32 * k) // 2, 26, k)
    arrows(img, 320, 96, 4)
    small = 'ROCKNIXDS'
    text(img, small, (640 - text_w(small, 4)) // 2, 262, 4, rgb('7ea8e6'), rgb('05080d'))
    big = 'BANK & TRADE'
    text(img, big, (640 - text_w(big, 7)) // 2, 304, 7, rgb('f4f6fa'), rgb('05080d'))
    slots(img, (640 - (8 * 56 - 6)) // 2, 392, 8, 50, ['e25a5a', None, 'f2c94c', '5bb85b', None, '4c8ee0', 'b07ef0', None])
    png(path, 640, 480, img.px)


def label(path):
    # the cartridge's label: the menu shows the middle of this picture about 90 px wide, so the bank is large and the
    # one word under it is in letters that survive that
    img = Img(640, 480, rgb('0e1522'))
    backdrop(img)
    k = 10
    img.block(BANK, PAL, (640 - 32 * k) // 2, 6, k)
    word = 'BANK'
    text(img, word, (640 - text_w(word, 16)) // 2, 322, 16, rgb('f4f6fa'), rgb('05080d'))
    png(path, 640, 480, img.px)


def marquee(path):
    img = Img(400, 164, rgb('0e1522'))
    backdrop(img)
    small = 'ROCKNIXDS'
    text(img, small, (400 - text_w(small, 3)) // 2, 34, 3, rgb('7ea8e6'), rgb('05080d'))
    big = 'BANK & TRADE'
    text(img, big, (400 - text_w(big, 5)) // 2, 76, 5, rgb('f4f6fa'), rgb('05080d'))
    png(path, 400, 164, img.px)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    cover(os.path.join(out, 'bank-image.png'))
    label(os.path.join(out, 'bank-thumb.png'))
    marquee(os.path.join(out, 'bank-marquee.png'))


if __name__ == '__main__':
    main()
