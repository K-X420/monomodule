# Logos for the Monomodule MD families, drawn as the Monomachine's are: 1-bit art at the LCD's own resolution (one logo
# pixel = one LCD pixel), each family in a type of its own, every one centred in the same box so the machine block and
# the picker keep their size.
# python make_logos.py --show                 prints them
# python make_logos.py --emit MdLogos.h       writes the header the plugin uses
import math
import sys

BOX_W, BOX_H = 46, 16

def canvas(w, h): return [["."] * w for _ in range(h)]
def paste(cv, art, x, y):
    for r, line in enumerate(art):
        for c, ch in enumerate(line):
            if ch == "#" and 0 <= y + r < len(cv) and 0 <= x + c < len(cv[0]): cv[y + r][x + c] = "#"
def rows(cv): return ["".join(r) for r in cv]
def lit(a, x, y): return 0 <= y < len(a) and 0 <= x < len(a[0]) and a[y][x] == "#"
def outline(art):   # the border pixels of each stroke: hollow letters
    return ["".join("#" if lit(art, x, y) and not all(lit(art, x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)) else "."
                    for x in range(len(art[0]))) for y in range(len(art))]
def trim(rs):
    w = max(len(r) for r in rs)
    rs = [r.ljust(w, ".") for r in rs]
    cols = [x for x in range(w) if any(r[x] == "#" for r in rs)]
    return [r[min(cols):max(cols) + 1] for r in rs]
def word(font, text, gap):
    h = len(next(iter(font.values())))
    out = [""] * h
    for i, ch in enumerate(text):
        for r in range(h): out[r] += font[ch][r] + ("." * gap if i < len(text) - 1 else "")
    return out
def line(cv, ax, ay, bx, by, pen=1):
    n = max(abs(bx - ax), abs(by - ay)) * 3 + 1
    for i in range(n + 1):
        x = round(ax + (bx - ax) * i / n); y = round(ay + (by - ay) * i / n)
        for dx in range(pen):
            if 0 <= y < len(cv) and 0 <= x + dx < len(cv[0]): cv[y][x + dx] = "#"

# a small 3x5 face for the stacked words (as SUPERWAVE's)
TINY = {
'A': [".#.", "#.#", "###", "#.#", "#.#"], 'U': ["#.#", "#.#", "#.#", "#.#", "###"], 'D': ["##.", "#.#", "#.#", "#.#", "##."],
'I': ["###", ".#.", ".#.", ".#.", "###"], 'O': ["###", "#.#", "#.#", "#.#", "###"], 'N': ["#.#", "###", "###", "###", "#.#"],
'P': ["##.", "#.#", "##.", "#..", "#.."], 'T': ["###", ".#.", ".#.", ".#.", ".#."], 'M': ["#.#", "###", "###", "#.#", "#.#"],
'C': [".##", "#..", "#..", "#..", ".##"], 'R': ["##.", "#.#", "##.", "#.#", "#.#"], 'L': ["#..", "#..", "#..", "#..", "###"],
}
# a bold 7x9 face (2 px strokes)
BOLD = {
'R': ["######.", "#######", "##...##", "##...##", "######.", "#####..", "##.###.", "##..###", "##...##"],
'A': ["..###..", ".#####.", "##...##", "##...##", "#######", "#######", "##...##", "##...##", "##...##"],
'M': ["##...##", "###.###", "#######", "##.#.##", "##...##", "##...##", "##...##", "##...##", "##...##"],
'C': [".######", "#######", "##.....", "##.....", "##.....", "##.....", "##.....", "#######", ".######"],
'T': ["#######", "#######", "..###..", "..###..", "..###..", "..###..", "..###..", "..###..", "..###.."],
}
# a heavy 3-stroke face (PHY), 8x11
HEAVY = {
'P': ["#######.", "########", "###..###", "###..###", "###..###", "########", "#######.", "###.....", "###.....", "###.....", "###....."],
'H': ["###..###", "###..###", "###..###", "###..###", "########", "########", "########", "###..###", "###..###", "###..###", "###..###"],
'Y': ["###..###", "###..###", "###..###", "###..###", ".######.", "..####..", "...##...", "...##...", "...##...", "...##...", "...##..."],
}

L = {}

# E12: an 80s LCD clock face: seven segments, 2 px thick, bevelled ends, a pixel apart at the joins (a "1" stands in the
# right-hand segments of its cell, as on the clocks)
SEG = {'a': [(x, y) for y in (0, 1) for x in range(2, 7) if not (y == 1 and x in (2, 6))],
       'g': [(x, y) for y in (6, 7) for x in range(2, 7)],
       'd': [(x, y) for y in (12, 13) for x in range(2, 7) if not (y == 12 and x in (2, 6))],
       'f': [(x, y) for x in (0, 1) for y in range(2, 6)], 'e': [(x, y) for x in (0, 1) for y in range(8, 12)],
       'b': [(x, y) for x in (7, 8) for y in range(2, 6)], 'c': [(x, y) for x in (7, 8) for y in range(8, 12)]}
def digit(segs):
    d = canvas(9, 14)
    for s in segs:
        for (x, y) in SEG[s]: d[y][x] = "#"
    return rows(d)
cv = canvas(40, 16)
for x, segs in [(0, "afged"), (5, "bc"), (16, "abged")]:   # the "1" in its cell's right-hand segments, the cells close
    paste(cv, digit(segs), x, 1)
L["E12"] = rows(cv)

# TRX: bold italic letters, speed lines trailing off the T
ITAL = {'T': BOLD['T'], 'R': BOLD['R'],
        'X': ["##...##", "##...##", ".##.##.", "..###..", "..###..", "..###..", ".##.##.", "##...##", "##...##"]}
w = word(ITAL, "TRX", 2)
cv = canvas(len(w[0]) + 15, 16)
for r, ln in enumerate(w):
    shift = (8 - r) // 2   # leaning forward
    for c, ch in enumerate(ln):
        if ch == "#": cv[r + 3][c + shift + 11] = "#"
for (y, x0, x1) in [(4, 3, 9), (7, 0, 8), (10, 4, 7)]:   # the speed lines
    for x in range(x0, x1 + 1): cv[y][x] = "#"
L["TRX"] = rows(cv)

# EFM: thin tall strokes (as FM+'s), and the M's middle running on as a wave
cv = canvas(46, 16)
def thin_e(x, bar_f=False):
    for y in range(1, 15): cv[y][x] = "#"
    for xx in range(x, x + 7): cv[1][xx] = "#"
    for xx in range(x, x + 6): cv[7][xx] = "#"
    if not bar_f:
        for xx in range(x, x + 7): cv[14][xx] = "#"
thin_e(0); thin_e(10, bar_f=True)
mx = 20
for y in range(1, 15): cv[y][mx] = "#"; cv[y][mx + 10] = "#"
line(cv, mx, 1, mx + 5, 9); line(cv, mx + 5, 9, mx + 10, 1)
paste(cv, [".##........", "#..#.......", "....#....#.", ".....#..#..", "......##..."], mx + 13, 6)   # a sine beside the M
L["EFM"] = rows(cv)

# PHY: heavy block letters with a struck object's thin ripples
w = word(HEAVY, "PHY", 2)
cv = canvas(len(w[0]) + 12, 16)
paste(cv, w, 0, 2)
cx, cy = float(len(w[0]) - 1), 7.5
for r in (3.5, 6.5, 9.5):
    for y in range(16):
        dy = y - cy
        if abs(dy) > r * 0.8: continue
        x = int(round(cx + math.sqrt(r * r - dy * dy)))
        if len(w[0]) + 1 < x < len(cv[0]): cv[y][x] = "#"
L["P-I"] = rows(cv)

# INP: stacked small words (as SUPERWAVE's) and a jack taking the signal in
cv = canvas(40, 16)
paste(cv, word(TINY, "AUDIO", 1), 0, 3)
paste(cv, word(TINY, "INPUT", 1), 0, 9)
jack = ["....#####....", "..##.....##..", ".#.........#.", ".#...###...#.", "#...#...#...#", "#...#...#...#",
        "#...#...#...#", ".#...###...#.", ".#.........#.", "..##.....##..", "....#####...."]
line(cv, 21, 7, 25, 7); line(cv, 21, 8, 25, 8); cv[6][24] = "#"; cv[9][24] = "#"   # the arrow in
paste(cv, jack, 27, 2)
L["INP"] = rows(cv)

# ROM: a DIP chip: the outline with its notch, pins along both edges, the name on the body
cv = canvas(40, 16)
for x in range(0, 40): cv[2][x] = "#"; cv[13][x] = "#"
for y in range(2, 14): cv[y][0] = "#"; cv[y][39] = "#"
for y in (6, 7, 8, 9): cv[y][0] = "."
cv[6][1] = "#"; cv[9][1] = "#"; cv[7][2] = "#"; cv[8][2] = "#"   # the notch
for x in range(3, 39, 4):
    for y in (0, 1, 14, 15): cv[y][x] = "#"; cv[y][x + 1] = "#"
rom = word(BOLD, "RAM", 2)   # placeholder replaced below
ROMF = {'R': BOLD['R'], 'O': [".#####.", "#######", "##...##", "##...##", "##...##", "##...##", "##...##", "#######", ".#####."], 'M': BOLD['M']}
w = word(ROMF, "ROM", 2)
paste(cv, w, (40 - len(w[0])) // 2, 4)
L["ROM"] = rows(cv)

# RAM: the record symbol, then the letters
cv = canvas(42, 16)
ring = ["...#####...", ".##.....##.", ".#.......#.", "#...###...#", "#..#####..#", "#..#####..#", "#..#####..#", "#...###...#", ".#.......#.", ".##.....##.", "...#####..."]
paste(cv, ring, 0, 3)
paste(cv, word(BOLD, "RAM", 2), 14, 4)
L["RAM"] = rows(cv)

# MID: a large 5-pin DIN socket and the word small (as SUPERWAVE's)
cv = canvas(36, 16)
din = ["....#######....", "..##.......##..", ".#...........#.", ".#..#.....#..#.", "#.............#", "#..#.......#..#",
       "#.............#", "#.......#.....#", "#......###....#", ".#...........#.", ".#...........#.", "..##.......##..", "....###.###...."]
din[7] = "#......#.#....#"; din[8] = "#.............#"
din = ["....#######....", "..##.......##..", ".#...........#.", ".#..#.....#..#.", "#.............#", "#..#...#...#..#",
       "#.............#", "#......#......#", "#.............#", ".#...........#.", ".#...........#.", "..##.......##..", "....##...##...."]
paste(cv, din, 0, 2)
paste(cv, word(TINY, "MIDI", 1), 19, 6)
L["MID"] = rows(cv)

# CTR: three faders standing tall, the letters beside them
cv = canvas(44, 16)
for k, knob in enumerate([3, 10, 6]):
    x = 2 + 5 * k
    for y in range(1, 15): cv[y][x] = "#"
    for yy in range(knob, knob + 3):
        for xx in range(x - 2, x + 3): cv[yy][xx] = "#"
    cv[knob + 1][x] = "."   # the knob's grip line
paste(cv, word(BOLD, "CTR", 2), 17, 4)
L["CTR"] = rows(cv)

def boxed(v):   # trimmed, then centred in the common box
    v = trim(v)
    w, h = len(v[0]), len(v)
    assert w <= BOX_W and h <= BOX_H, ("too big", w, h)
    left, top = (BOX_W - w) // 2, (BOX_H - h) // 2
    v = ["." * left + r + "." * (BOX_W - w - left) for r in v]
    return ["." * BOX_W] * top + v + ["." * BOX_W] * (BOX_H - h - top)

for k, v in L.items():
    L[k] = boxed(v)
    if "--show" in sys.argv:
        print(k)
        for r in L[k]: print("   ", r.replace(".", " ").replace("#", "█"))

if "--emit" in sys.argv:
    out = ["// Generated by make_logos.py (python make_logos.py --emit MdLogos.h): the family logos, 1 px = 1 LCD px (bit (63 - x) of a row = pixel x lit)", "#pragma once", "#include <cstdint>", "",
           "namespace mnm::plugin::md::text {", "", "struct LogoArt { const char* family; int w, h; uint64_t rows[16]; };", "inline constexpr LogoArt kLogoArt[] = {"]
    for k, v in L.items():
        vals = []
        for r in v:
            n = 0
            for x, ch in enumerate(r):
                if ch == "#": n |= 1 << (63 - x)
            vals.append("0x%016Xull" % n)
        out.append('    {"%s", %d, %d, {%s}},' % (k, len(v[0]), len(v), ", ".join(vals)))
    out += ["};", "", "} // namespace mnm::plugin::md::text", ""]
    open(sys.argv[sys.argv.index("--emit") + 1], "w", encoding="utf-8").write("\n".join(out))
