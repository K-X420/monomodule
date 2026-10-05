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

L = {}
# TRX: bold letters leaning forward (rows shifted right toward the top)
w = word("TRX")
cv = canvas(len(w[0]) + 6, 12)
for r, line in enumerate(w):
    shift = (9 - r) // 2
    for c, ch in enumerate(line):
        if ch == "#": cv[r + 1][c + shift] = "#"
L["TRX"] = rows(cv)
# EFM: thin single-line letters (the bold strokes' left / top edges), like a line drawing
def thin(art):
    return ["".join("#" if lit(art, x, y) and (not lit(art, x - 1, y) or not lit(art, x, y - 1)) else "." for x in range(len(art[0]))) for y in range(len(art))]
THIN = {"E": ["#######","#......","#......","#......","#####..","#......","#......","#......","#......","#######"], "F": ["#######","#......","#......","#......","#####..","#......","#......","#......","#......","#......"], "M": ["#.....#","##...##","#.#.#.#","#..#..#","#.....#","#.....#","#.....#","#.....#","#.....#","#.....#"]}
w = [THIN["E"][r] + "...." + THIN["F"][r] + "...." + THIN["M"][r] for r in range(10)]
cv = canvas(len(w[0]), 12); paste(cv, w, 0, 1); L["EFM"] = rows(cv)
# E12: scanlines (every other row), as a sampled signal's steps
w = word("E12")
cv = canvas(len(w[0]), 12)
for r, line in enumerate(w):
    if r % 2 == 0 or r == 9:
        for c, ch in enumerate(line):
            if ch == "#": cv[r + 1][c] = "#"
L["E12"] = rows(cv)
# P-I: heavy rounded letters with a dot between
p, i = F["P"], F["I"]
cv = canvas(8 + 6 + 6, 12)
paste(cv, p, 0, 1); paste(cv, ["##", "##"], 10, 5); paste(cv, i, 14, 1)
for (x, y) in [(0, 1), (7, 1), (7, 6), (14, 1), (19, 1), (14, 10), (19, 10)]: cv[y][x] = "."
L["P-I"] = rows(cv)
# INP: an arrow running into the letters
w = word("INP")
cv = canvas(9 + len(w[0]), 12)
paste(cv, ["......#..", ".......#.", "#########", "#########", ".......#.", "......#.."], 0, 3)
paste(cv, w, 9, 1)
L["INP"] = rows(cv)
# ROM: a chip: outlined letters with pins above and below
w = outline(word("ROM"))
cv = canvas(len(w[0]) + 4, 12)
paste(cv, w, 2, 1)
for x in range(2, len(w[0]) + 2, 4): cv[0][x] = "#"; cv[11][x] = "#"
L["ROM"] = rows(cv)
# RAM: the record dot, then the letters
w = word("RAM")
cv = canvas(10 + len(w[0]), 12)
dot = [".##.", "####", "####", ".##."]
ring = ["..####..", ".#....#.", "#......#", "#......#", "#......#", "#......#", ".#....#.", "..####.."]
paste(cv, ring, 0, 2); paste(cv, dot, 2, 4); paste(cv, w, 10, 1)
L["RAM"] = rows(cv)
# MID: outlined letters with a DIN socket's pins in an arc
w = word("MID")
cv = canvas(len(w[0]) + 12, 12)
paste(cv, w, 0, 1)
din = ["...#####...", "..#.....#..", ".#...#...#.", "#..#...#..#", "#.........#", "#.#.....#.#", "#.........#", ".#.......#.", "..##...##..", "....###...."]
paste(cv, din, len(w[0]) + 1, 1)
L["MID"] = rows(cv)
# CTR: the letters standing on three fader slots
w = word("CTR")
cv = canvas(len(w[0]) + 14, 12)
paste(cv, w, 0, 1)
x0 = len(w[0]) + 1
for k in range(3):
    x = x0 + 3 * k + 1
    for y in range(0, 12): cv[y][x] = "#"
    knob = [3, 8, 5][k]
    for yy in range(knob, knob + 2):
        for xx in range(x - 1, x + 2): cv[yy][xx] = "#"
L["CTR"] = rows(cv)

for k, v in L.items():
    v = trim(v)
    L[k] = v
    if "--show" in sys.argv:
        print(k, len(v[0]), "x", len(v))
        for r in v: print("   ", r.replace(".", " ").replace("#", "â–ˆ"))

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
