# Logos for the Monomodule MD families, drawn on the Monomachine's grid: 1-bit art 9 rows tall (as SID, FM+ and VO are,
# 9 lit rows; DigiPRO 11) and at most 27 wide, so the machine block draws them at the size Monomodule draws its own (the
# lit rows filling an 18-row band). GND, RAM and CTR have none: their names are printed, as GND's is on the Monomachine.
#   TRX  hollow, double-line letters (as the SID logo's)
#   EFM  FM+'s family: thin strokes; the E's bars run on into the F
#   E12  a digital display (Digital-7 style): slanted segments, the digits in equal cells
#   P-I  as SUPERWAVE: small stacked words and a simple wave
#   INP, ROM, MID  a symbol and the name
# python make_logos.py --show                 prints them
# python make_logos.py --emit MdLogos.h       writes the header the plugin uses
# python make_logos.py --preview out.txt      every logo (and the P-I variants) as text rows for a preview image
import sys

BOX_W, BOX_H = 27, 9

def canvas(w, h): return [["."] * w for _ in range(h)]
def paste(cv, art, x, y):
    for r, line in enumerate(art):
        for c, ch in enumerate(line):
            if ch == "#" and 0 <= y + r < len(cv) and 0 <= x + c < len(cv[0]): cv[y + r][x + c] = "#"
def rows(cv): return ["".join(r) for r in cv]
def lit(a, x, y): return 0 <= y < len(a) and 0 <= x < len(a[0]) and a[y][x] == "#"
def outline(art):   # the border pixels of each stroke: hollow, double-line letters
    return ["".join("#" if lit(art, x, y) and not all(lit(art, x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)) else "."
                    for x in range(len(art[0]))) for y in range(len(art))]
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
        pad = (h - len(p)) // 2
        for r in range(h):
            rr = r - pad
            out[r] += (p[rr] if 0 <= rr < len(p) else "." * w) + ("." * gap if i < len(parts) - 1 else "")
    return out
def vjoin(parts, gap):
    w = max(len(p[0]) for p in parts)
    out = []
    for i, p in enumerate(parts):
        out += [r.ljust(w, ".") for r in p]
        if i < len(parts) - 1: out += ["." * w] * gap
    return out

# a small 3x5 face (SUPERWAVE's words are in the OS's 4x5; this is its narrow cousin)
S = {
'A': [".#.", "#.#", "###", "#.#", "#.#"], 'C': [".##", "#..", "#..", "#..", ".##"], 'D': ["##.", "#.#", "#.#", "#.#", "##."],
'E': ["###", "#..", "##.", "#..", "###"], 'F': ["###", "#..", "##.", "#..", "#.."], 'H': ["#.#", "#.#", "###", "#.#", "#.#"],
'I': ["###", ".#.", ".#.", ".#.", "###"], 'L': ["#..", "#..", "#..", "#..", "###"], 'M': ["#...#", "##.##", "#.#.#", "#...#", "#...#"],
'N': ["#..#", "##.#", "#.##", "#..#", "#..#"], 'O': ["###", "#.#", "#.#", "#.#", "###"], 'P': ["##.", "#.#", "##.", "#..", "#.."],
'R': ["##.", "#.#", "##.", "#.#", "#.#"], 'S': [".##", "#..", ".#.", "..#", "##."], 'Y': ["#.#", "#.#", ".#.", ".#.", ".#."],
'-': ["...", "...", "###", "...", "..."], '.': [".", ".", ".", ".", "#"],
}
def small(text, gap=1): return hjoin([S[c] for c in text], gap)

L = {}

# TRX, as the SID logo: hollow letters, their strokes drawn as two lines
FAT = {
'T': ["#######", "#######", "#######", "..###..", "..###..", "..###..", "..###..", "..###..", "..###.."],
'R': ["#######.", "########", "###..###", "########", "#######.", "###.###.", "###..###", "###..###", "###..###"],
'X': ["###...###", "###...###", ".###.###.", "..#####..", "...###...", "..#####..", ".###.###.", "###...###", "###...###"],
}
L["TRX"] = outline(hjoin([FAT['T'], FAT['R'], FAT['X']], 1))

# EFM, FM+'s family: thin strokes; the E's top and middle bars run on into the F (a ligature)
ef = ["#########", "#...#....", "#...#....", "#...#....", "#########", "#...#....", "#...#....", "#...#....", "#####...."]
M1 = ["#.......#", "##.....##", "##.....##", "#.#...#.#", "#.#...#.#", "#..#.#..#", "#..#.#..#", "#...#...#", "#...#...#"]
L["EFM"] = hjoin([ef, M1], 1)

# E12, a digital display (Digital-7 style): the verticals own the corners (4 rows each, split at the middle), the bars
# 3 px between them, the top half leaning a pixel right
def digit(segs):
    d = canvas(7, 9)
    SEG = {'a': [(x, 0) for x in range(2, 5)], 'g': [(x, 4) for x in range(2, 5)], 'd': [(x, 8) for x in range(2, 5)],
           'f': [(x, y) for x in (0, 1) for y in range(0, 4)], 'e': [(x, y) for x in (0, 1) for y in range(5, 9)],
           'b': [(x, y) for x in (5, 6) for y in range(0, 4)], 'c': [(x, y) for x in (5, 6) for y in range(5, 9)]}
    for s in segs:
        for (x, y) in SEG[s]: d[y][x] = "#"
    return rows(d)
def display(cells, pitch):   # digits in cells `pitch` apart, slanted
    cv = canvas(pitch * (len(cells) - 1) + 8, 9)
    for i, (segs, x0) in enumerate(cells):
        dg = digit(segs)
        for r in range(9):
            shift = 1 if r < 4 else 0
            for c, ch in enumerate(dg[r]):
                if ch == "#": cv[r][x0 + c + shift] = "#"
    return rows(cv)
E12_VARIANTS = {
    "E12 a: display cells (the 1 at its cell's right)": display([("afged", 0), ("bc", 9), ("abged", 18)], 9),
    "E12 b: even spacing (the 1 between)": display([("afged", 0), ("bc", 4), ("abged", 13)], 9),
}
L["E12"] = E12_VARIANTS["E12 b: even spacing (the 1 between)"]

# P-I (Elektron's "physically informed" models), as SUPERWAVE: small stacked words and a simple wave
def wave():   # a struck object's wave: a sine that dies away, as SUPERWAVE's is drawn (1 px, 8 rows)
    return ["#.......",
            "#.#.....",
            "#.#.#...",
            "#.#.#.#.",
            ".#.#.#.#",
            ".#.#.#..",
            ".#.#....",
            ".#......"]
S4 = {   # a 3x4 face for two stacked words in 9 rows
'P': ["##.", "#.#", "##.", "#.."], 'H': ["#.#", "###", "#.#", "#.#"], 'Y': ["#.#", ".#.", ".#.", ".#."], 'S': [".##", "##.", "..#", "##."],
'I': ["###", ".#.", ".#.", "###"], 'N': ["#..#", "##.#", "#.##", "#..#"], 'F': ["###", "#..", "##.", "#.."], 'O': ["###", "#.#", "#.#", "###"],
'-': ["...", "...", "###", "..."], 'M': ["#...#", "##.##", "#.#.#", "#...#"], 'D': ["##.", "#.#", "#.#", "##."], 'L': ["#..", "#..", "#..", "###"],
}
def small4(text): return hjoin([S4[c] for c in text], 1)
PI_VARIANTS = {
    "A: PHYS / INFO stacked": hjoin([vjoin([small4("PHYS"), small4("INFO")], 1), wave()], 2),
    "B: P-I big": hjoin([hjoin([["##.", "#.#", "#.#", "##.", "#..", "#..", "#.."], [".....", ".....", ".....", "#####", ".....", ".....", "....."], ["###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"]], 1), wave()], 2),
    "C: P-I / PHYS stacked": hjoin([vjoin([small4("P-I"), small4("PHYS")], 1), wave()], 2),
}
L["P-I"] = PI_VARIANTS["A: PHYS / INFO stacked"]

# INP: an arrow running into the letters
I2 = ["##", "##", "##", "##", "##", "##", "##", "##", "##"]
N = ["##..##", "###.##", "######", "######", "##.###", "##..##", "##..##", "##..##", "##..##"]
P = ["#####.", "######", "##..##", "##..##", "######", "#####.", "##....", "##....", "##...."]
arrow = ["......", "......", "...#..", "....#.", "######", "....#.", "...#..", "......", "......"]
L["INP"] = hjoin([arrow, I2, N, P], 1)

# ROM: a chip: the body's outline, pins above and below, the name on it
word = small("ROM")
cw = len(word[0]) + 6
chip = canvas(cw, 9)
for x in range(cw): chip[1][x] = "#"; chip[7][x] = "#"
for y in range(1, 8): chip[y][0] = "#"; chip[y][cw - 1] = "#"
for x in range(2, cw - 1, 3): chip[0][x] = "#"; chip[8][x] = "#"
paste(chip, word, 3, 2)
L["ROM"] = rows(chip)

# MID: a 5-pin DIN socket and MIDI, 7 rows tall
din = ["..#####..", ".#.....#.", "#.#...#.#", "#.......#", "#.#.#.#.#", "#.......#", "#...#...#", ".#.....#.", "..##.##.."]
M7 = ["#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#"]
I7 = ["#"] * 7
D7 = ["###.", "#..#", "#..#", "#..#", "#..#", "#..#", "###."]
L["MID"] = hjoin([din, hjoin([M7, I7, D7, I7], 1)], 2)

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

if "--preview" in sys.argv:
    with open(sys.argv[sys.argv.index("--preview") + 1], "w", encoding="utf-8") as f:
        for k, v in list(L.items()) + [(n, boxed(a)) for n, a in E12_VARIANTS.items()] + [("P-I " + n, boxed(a)) for n, a in PI_VARIANTS.items()]:
            f.write(k + "\n" + "\n".join(v) + "\n\n")

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
