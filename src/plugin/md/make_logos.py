# Logos for the Monomodule MD families, drawn on the Monomachine's grid: 1-bit art 9 rows tall (as SID, FM+ and VO are,
# 9 lit rows; DigiPRO 11) and at most 27 wide, so the machine block draws them at the size Monomodule draws its own (the
# lit rows filling an 18-row band). Each family in a type of its own, several akin to the Monomachine's:
#   TRX  in VO's family: heavy strokes, chunky diagonals, cut corners
#   EFM  FM+'s brother: the same thin strokes, the M built as FM+'s
#   E12  an LCD clock face, every digit full height, evenly spaced
# python make_logos.py --show                 prints them
# python make_logos.py --emit MdLogos.h       writes the header the plugin uses
import sys

BOX_W, BOX_H = 27, 9

def canvas(w, h): return [["."] * w for _ in range(h)]
def paste(cv, art, x, y):
    for r, line in enumerate(art):
        for c, ch in enumerate(line):
            if ch == "#" and 0 <= y + r < len(cv) and 0 <= x + c < len(cv[0]): cv[y + r][x + c] = "#"
def rows(cv): return ["".join(r) for r in cv]
def trim(rs):
    w = max(len(r) for r in rs)
    rs = [r.ljust(w, ".") for r in rs]
    cols = [x for x in range(w) if any(r[x] == "#" for r in rs)]
    lines = [y for y in range(len(rs)) if "#" in rs[y]]
    return [r[min(cols):max(cols) + 1] for r in rs[min(lines):max(lines) + 1]]
def hjoin(parts, gap):
    h = max(len(p) for p in parts)
    out = [""] * h
    for i, p in enumerate(parts):
        w = len(p[0])
        for r in range(h): out[r] += (p[r] if r < len(p) else "." * w) + ("." * gap if i < len(parts) - 1 else "")
    return out

L = {}

# TRX, in VO's family (VO: heavy 2 px stems, 3 px diagonals, the round letter split at its middle)
T = ["#######",
     "#######",
     "..###..",
     "..###..",
     "..###..",
     "..###..",
     "..###..",
     "..###..",
     "..###.."]
R = ["#######.",
     "########",
     "##....##",
     "##...###",
     "#######.",
     "######..",
     "##..###.",
     "##...###",
     "##....##"]
X = ["###...###",
     ".###.###.",
     "..#####..",
     "...###...",
     "...###...",
     "..#####..",
     ".###.###.",
     "###...###",
     "##.....##"]
L["TRX"] = hjoin([T, R, X], 1)

# EFM, FM+'s brother: thin strokes, the F as FM+'s, the M as FM+'s
E1 = ["####", "#...", "#...", "#...", "####", "#...", "#...", "#...", "####"]
F1 = ["####", "#...", "#...", "#...", "####", "#...", "#...", "#...", "#..."]
M1 = ["#.......#", "##.....##", "##.....##", "#.#...#.#", "#.#...#.#", "#..#.#..#", "#..#.#..#", "#...#...#", "#...#...#"]
L["EFM"] = hjoin([E1, F1, M1], 1)

# E12, an LCD clock face: segments 1 px tall / 2 px wide with the corners open; the 1 full height like the others
def seg(segs):
    d = canvas(7, 9)
    S = {'a': [(x, 0) for x in range(2, 5)], 'g': [(x, 4) for x in range(2, 5)], 'd': [(x, 8) for x in range(2, 5)],
         'f': [(x, y) for x in (0, 1) for y in range(1, 4)], 'e': [(x, y) for x in (0, 1) for y in range(5, 8)],
         'b': [(x, y) for x in (5, 6) for y in range(1, 4)], 'c': [(x, y) for x in (5, 6) for y in range(5, 8)]}
    for s in segs:
        for (x, y) in S[s]: d[y][x] = "#"
    return trim(rows(d))
ONE = ["##"] * 9   # the right-hand segments as one full-height bar (split at the middle it read as a colon)
L["E12"] = hjoin([seg("afged"), ONE, seg("abged")], 2)

# PHY: heavy letters (as DigiPRO's "Digi"), a struck object's ripples
P = ["#####.", "######", "##..##", "##..##", "######", "#####.", "##....", "##....", "##...."]
H = ["##..##", "##..##", "##..##", "##..##", "######", "######", "##..##", "##..##", "##..##"]
Y = ["##..##", "##..##", "##..##", "######", ".####.", "..##..", "..##..", "..##..", "..##.."]
ripples = [".#..#.", "..#..#", "..#..#", "...#.#", "...#.#", "...#.#", "..#..#", "..#..#", ".#..#."]
L["P-I"] = hjoin([P, H, Y, ripples], 1)

# INP: an arrow running into the letters
I = ["##", "##", "##", "##", "##", "##", "##", "##", "##"]
N = ["##..##", "###.##", "######", "######", "##.###", "##..##", "##..##", "##..##", "##..##"]
arrow = ["......", "......", "...#..", "....#.", "######", "....#.", "...#..", "......", "......"]
L["INP"] = hjoin([arrow, I, N, P], 1)

# ROM: a chip (as DigiPRO's boxed PRO): pins above and below, the name in the body
TINY = {'R': ["##.", "#.#", "##.", "#.#", "#.#"], 'O': ["###", "#.#", "#.#", "#.#", "###"], 'M': ["#...#", "##.##", "#.#.#", "#...#", "#...#"],
        'I': ["###", ".#.", ".#.", ".#.", "###"], 'D': ["##.", "#.#", "#.#", "#.#", "##."]}
word = hjoin([TINY['R'], TINY['O'], TINY['M']], 1)
cw = len(word[0]) + 6
chip = canvas(cw, 9)
for x in range(cw): chip[1][x] = "#"; chip[7][x] = "#"
for y in range(1, 8): chip[y][0] = "#"; chip[y][cw - 1] = "#"
chip[3][0] = "."; chip[4][0] = "."; chip[5][0] = "."; chip[4][1] = "#"   # the notch
for x in range(2, cw - 1, 3): chip[0][x] = "#"; chip[8][x] = "#"
paste(chip, word, 3, 2)
L["ROM"] = rows(chip)

# RAM: the record dot, then the letters
dot = [".......", "..###..", ".#####.", "#######", "#######", "#######", ".#####.", "..###..", "......."]
R5 = ["####.", "#####", "##.##", "#####", "####.", "##.#.", "##.##", "##.##", "##.##"]
A5 = [".###.", "#####", "##.##", "##.##", "#####", "#####", "##.##", "##.##", "##.##"]
M7 = ["##...##", "###.###", "#######", "##.#.##", "##...##", "##...##", "##...##", "##...##", "##...##"]
L["RAM"] = hjoin([dot, R5, A5, M7], 1)

# MID: a 5-pin DIN socket and the word small (as SUPERWAVE's)
din = ["..#####..", ".#.....#.", "#.#...#.#", "#.......#", "#.#.#.#.#", "#.......#", "#...#...#", ".#.....#.", "..##.##.."]
midi = hjoin([TINY['M'], TINY['I'], TINY['D'], TINY['I']], 1)
midi = ["." * len(midi[0])] * 2 + midi + ["." * len(midi[0])] * 2
L["MID"] = hjoin([din, midi], 1)

# CTR: three faders, the letters beside them
fad = canvas(9, 9)
for k, knob in enumerate([1, 5, 3]):
    x = 1 + 3 * k
    for y in range(9): fad[y][x] = "#"
    for yy in (knob, knob + 1):
        for xx in (x - 1, x, x + 1): fad[yy][xx] = "#"
C5 = [".####", "#####", "##...", "##...", "##...", "##...", "##...", "#####", ".####"]
T5 = ["#####", "#####", ".###.", ".###.", ".###.", ".###.", ".###.", ".###.", ".###."]
L["CTR"] = hjoin([rows(fad), C5, T5, R5], 1)

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
        print(k, len(trim(v)[0]), "x", len(trim(v)))
        for r in L[k]: print("   ", r.replace(".", " ").replace("#", "█"))

if "--emit" in sys.argv:
    out = ["// Generated by make_logos.py (python make_logos.py --emit MdLogos.h): the family logos on the Monomachine's grid (bit (63 - x) of a row = pixel x lit)", "#pragma once", "#include <cstdint>", "",
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
