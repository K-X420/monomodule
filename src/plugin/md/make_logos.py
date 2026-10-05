# Wordmark logos for the Monomodule MD families: a bold 8x10 letter set (2 px strokes), then a style per family.
# python make_logos.py --show                 prints them
# python make_logos.py --emit MdLogos.h       writes the header the plugin uses
import sys

F = {
'T': ["########","########","...##...","...##...","...##...","...##...","...##...","...##...","...##...","...##..."],
'R': ["#######.","########","##....##","##....##","########","#######.","##..##..","##...##.","##....##","##....##"],
'X': ["##....##","###..###",".######.","..####..","...##...","...##...","..####..",".######.","###..###","##....##"],
'E': ["########","########","##......","##......","######..","######..","##......","##......","########","########"],
'F': ["########","########","##......","##......","######..","######..","##......","##......","##......","##......"],
'M': ["##....##","###..###","########","##.##.##","##.##.##","##....##","##....##","##....##","##....##","##....##"],
'1': ["...##...","..###...",".####...","...##...","...##...","...##...","...##...","...##...",".######.",".######."],
'2': [".######.","########","##....##","......##","....####","..####..",".###....","##......","########","########"],
'P': ["#######.","########","##....##","##....##","########","#######.","##......","##......","##......","##......"],
'I': ["######","######","..##..","..##..","..##..","..##..","..##..","..##..","######","######"],
'N': ["##....##","###...##","####..##","##.##.##","##.##.##","##..####","##...###","##....##","##....##","##....##"],
'O': [".######.","########","##....##","##....##","##....##","##....##","##....##","##....##","########",".######."],
'A': ["..####..",".######.","###..###","##....##","##....##","########","########","##....##","##....##","##....##"],
'D': ["#######.","########","##....##","##....##","##....##","##....##","##....##","##....##","########","#######."],
'H': ["##....##","##....##","##....##","##....##","########","########","##....##","##....##","##....##","##....##"],
'Y': ["##....##","##....##","###..###",".######.","..####..","...##...","...##...","...##...","...##...","...##..."],
'C': [".#######","########","##......","##......","##......","##......","##......","##......","########",".#######"],
}

def word(text, gap=2):
    rows = [""] * 10
    for i, ch in enumerate(text):
        g = F[ch]
        for r in range(10):
            rows[r] += g[r] + ("." * gap if i < len(text) - 1 else "")
    return rows

def canvas(w, h): return [["."] * w for _ in range(h)]
def paste(cv, art, x, y):
    for r, line in enumerate(art):
        for c, ch in enumerate(line):
            if ch == "#" and 0 <= y + r < len(cv) and 0 <= x + c < len(cv[0]): cv[y + r][x + c] = "#"
def rows(cv): return ["".join(r) for r in cv]
def lit(a, x, y): return 0 <= y < len(a) and 0 <= x < len(a[0]) and a[y][x] == "#"
def outline(art):   # keep the border pixels of each stroke: hollow, double-line letters
    out = []
    for y in range(len(art)):
        line = ""
        for x in range(len(art[0])):
            inner = lit(art, x, y) and all(lit(art, x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1))
            line += "#" if lit(art, x, y) and not inner else "."
        out.append(line)
    return out
def trim(rs):
    w = max(len(r) for r in rs)
    rs = [r.ljust(w, ".") for r in rs]
    cols = [x for x in range(w) if any(r[x] == "#" for r in rs)]
    return [r[min(cols):max(cols) + 1] for r in rs]

# A condensed set (6 wide, 2 px strokes; I 4) for the wordmarks that share the box with a symbol
C = {
'C': [".#####","######","##....","##....","##....","##....","##....","##....","######",".#####"],
'T': ["######","######","..##..","..##..","..##..","..##..","..##..","..##..","..##..","..##.."],
'R': ["#####.","######","##..##","##..##","######","#####.","##.##.","##..##","##..##","##..##"],
'I': ["####","####",".##.",".##.",".##.",".##.",".##.",".##.","####","####"],
'N': ["##..##","###.##","###.##","######","######","##.###","##.###","##..##","##..##","##..##"],
'P': ["#####.","######","##..##","##..##","######","#####.","##....","##....","##....","##...."],
'A': [".####.","######","##..##","##..##","##..##","######","######","##..##","##..##","##..##"],
'D': ["#####.","######","##..##","##..##","##..##","##..##","##..##","##..##","######","#####."],
'H': ["##..##","##..##","##..##","##..##","######","######","##..##","##..##","##..##","##..##"],
'Y': ["##..##","##..##","##..##","######",".####.","..##..","..##..","..##..","..##..","..##.."],
'E': ["######","######","##....","##....","#####.","#####.","##....","##....","######","######"],
'F': ["######","######","##....","##....","#####.","#####.","##....","##....","##....","##...."],
'O': [".####.","######","##..##","##..##","##..##","##..##","##..##","##..##","######",".####."],
'M': ["##....##","###..###","########","##.##.##","##.##.##","##....##","##....##","##....##","##....##","##....##"],
}
def cword(text, gap=2):
    rows = [""] * 10
    for i, ch in enumerate(text):
        g = C[ch]
        for r in range(10): rows[r] += g[r] + ("." * gap if i < len(text) - 1 else "")
    return rows

BOX_W, BOX_H = 32, 12   # every logo the same size: the machine block and the picker stay put
L = {}
import math
# TRX: bold italic, leaning forward, a racing stripe cut through the letters
w = word("TRX")
cv = canvas(len(w[0]) + 4, 12)
for r, line in enumerate(w):
    if r == 5: continue                      # the stripe
    shift = (9 - r) // 3
    for c, ch in enumerate(line):
        if ch == "#": cv[r + 1][c + shift] = "#"
L["TRX"] = rows(cv)
# EFM: E and F, then the M as a frequency-modulated wave (its cycles closing up)
ef = cword("EF")
cv = canvas(32, 12)
paste(cv, ef, 0, 1)
x0 = len(ef[0]) + 2
# a zigzag whose strokes get shorter (the frequency rising), read as an M: 2 px pen
pts = [(0, 9), (5, 0), (9, 9), (12, 0), (14, 9)]
for (ax, ay), (bx, by) in zip(pts, pts[1:]):
    n = max(abs(bx - ax), abs(by - ay)) * 4
    for i in range(n + 1):
        x = ax + (bx - ax) * i / n
        y = ay + (by - ay) * i / n
        for dx in (0, 1):
            xx, yy = x0 + int(round(x)) + dx, 1 + int(round(y))
            if 0 <= xx < 32: cv[yy][xx] = "#"
L["EFM"] = rows(cv)
# E12: a sampler's display: segment digits (corners open), slanted
SEG = {'a': [(x, y) for x in range(1, 7) for y in (0, 1)], 'g': [(x, y) for x in range(1, 7) for y in (5, 6)], 'd': [(x, y) for x in range(1, 7) for y in (10, 11)],
       'f': [(x, y) for x in (0, 1) for y in range(1, 6) if not (x == 0 and y in (1, 5))], 'e': [(x, y) for x in (0, 1) for y in range(6, 11) if not (x == 0 and y in (6, 10))],
       'b': [(x, y) for x in (6, 7) for y in range(1, 6) if not (x == 7 and y in (1, 5))], 'c': [(x, y) for x in (6, 7) for y in range(6, 11) if not (x == 7 and y in (6, 10))]}
def digit(segs, w=8):
    d = [["."] * w for _ in range(12)]
    for s in segs:
        for (x, y) in SEG[s]:
            if x < w: d[y][x] = "#"
    return ["".join(r) for r in d]
one = ["".join("#" if (x, y) in SEG['b'] + SEG['c'] else "." for x in (6, 7)) for y in range(12)]
glyphs = [digit("afged"), one, digit("abged")]
cv = canvas(32, 12)
x = 3
for gl in glyphs:
    for r in range(12):
        shift = (11 - r) // 4
        for c, ch in enumerate(gl[r]):
            if ch == "#" and 0 <= x + c + shift < 32: cv[r][x + c + shift] = "#"
    x += len(gl[0]) + 3
L["E12"] = rows(cv)
# P-I ("physically informed") as PHY, with a struck object's vibration rippling off it
w = cword("PHY")
cv = canvas(len(w[0]) + 9, 12)
paste(cv, w, 0, 1)
cx, cy = float(len(w[0]) - 2), 5.5
for r in (2.6, 4.9, 7.2):
    for y in range(12):
        dy = y - cy
        if abs(dy) > r * 0.86: continue
        x = int(round(cx + math.sqrt(r * r - dy * dy)))
        if len(w[0]) < x < len(cv[0]): cv[y][x] = "#"
L["P-I"] = rows(cv)
# INP: an arrow running into the letters
w = cword("INP")
cv = canvas(9 + len(w[0]), 12)
paste(cv, ["....#...", ".....#..", "########", "########", ".....#..", "....#..."], 0, 3)
paste(cv, w, 9, 1)
L["INP"] = rows(cv)
# ROM: a chip: outlined letters with pins above and below (narrower letters, so it reads smaller)
w = outline(cword("ROM"))
cv = canvas(len(w[0]) + 4, 12)
paste(cv, w, 2, 1)
for x in range(2, len(w[0]) + 2, 4): cv[0][x] = "#"; cv[11][x] = "#"
L["ROM"] = rows(cv)
# RAM: the record dot, then the letters
w = cword("RAM", gap=1)
ring = ["..####..", ".#....#.", "#......#", "#......#", "#......#", "#......#", ".#....#.", "..####.."]
cv = canvas(10 + len(w[0]), 12)
paste(cv, ring, 0, 2); paste(cv, [".##.", "####", "####", ".##."], 2, 4); paste(cv, w, 10, 1)
L["RAM"] = rows(cv)
# MID: the letters and a DIN socket (a symmetric 10x10 circle, its pins on an arc)
w = cword("MID", gap=1)
cv = canvas(len(w[0]) + 11, 12)
paste(cv, w, 0, 1)
din = ["...####...", ".##....##.", ".#..##..#.", "#..#..#..#", "#........#", "#.#....#.#", "#........#", ".#..##..#.", ".##....##.", "...####..."]
paste(cv, din, len(w[0]) + 1, 1)
L["MID"] = rows(cv)
# CTR: the letters beside three fader slots
w = cword("CTR")
cv = canvas(len(w[0]) + 10, 12)
paste(cv, w, 0, 1)
x0 = len(w[0]) + 1
for k in range(3):
    x = x0 + 3 * k + 1
    for y in range(0, 12): cv[y][x] = "#"
    knob = [3, 8, 5][k]
    for yy in range(knob, knob + 2):
        for xx in range(x - 1, x + 2): cv[yy][xx] = "#"
L["CTR"] = rows(cv)

def boxed(v):   # trimmed, then centred in the common box
    v = trim(v)
    w = len(v[0])
    assert w <= BOX_W, ("too wide", w)
    left = (BOX_W - w) // 2
    return ["." * left + r + "." * (BOX_W - w - left) for r in v]

for k, v in L.items():
    v = boxed(v)
    L[k] = v
    if "--show" in sys.argv:
        print(k, len(v[0]), "x", len(v))
        for r in v: print("   ", r.replace(".", " ").replace("#", "█"))

if "--emit" in sys.argv:
    out = ["// Generated by make_logos.py (python make_logos.py --emit MdLogos.h): the family wordmarks (bit (63 - x) of a row = pixel x lit)", "#pragma once", "#include <cstdint>", "",
           "namespace mnm::plugin::md::text {", "", "struct LogoArt { const char* family; int w, h; uint64_t rows[12]; };", "inline constexpr LogoArt kLogoArt[] = {"]
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
