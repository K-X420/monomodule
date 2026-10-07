#include "MdEditor.h"
#include <map>
#include "MdMachineText.h"
#include "MdParamWords.h"
#include <cmath>
#include "ShnolkLogo.h"
#include "ParamDisplay.h"
#include "SharedSettings.h"
#include "Transfer.h"

namespace mnm::plugin::md {

using one::kScale;
using one::LcdCanvas;
using one::KnobPage;
namespace lcd = one::lcd;
using one::drawLcdText;
using one::wrapLcdText;

namespace {

juce::String knobTip(const juce::String& label, int page, const juce::String& family);   // a knob's tooltip (below)

// "TRX-BD" -> "TRX" / "BD"; "P-I-BD" -> "P-I" / "BD"; "GND---" -> "GND" / "---"
juce::String familyOf(int index)
{
    const juce::String n(kMachines[index].name);
    return n.startsWith("P-I-") ? juce::String("P-I") : n.substring(0, 3);
}
juce::String shortOf(int index)
{
    const juce::String n(kMachines[index].name);
    const auto s = n.substring(familyOf(index).length() + 1);
    return s.containsOnly("-") ? juce::String("---") : s;   // GND---: the empty machine
}

void dottedFrame(LcdCanvas& cv, int x, int y, int w, int h)
{
    cv.dotsH(x, x + w - 1, y); cv.dotsH(x, x + w - 1, y + h - 1);
    cv.dotsV(x, y, y + h - 1); cv.dotsV(x + w - 1, y, y + h - 1);
}

constexpr spec::Param numeric(const char* label, int def) { return {label, spec::Display::Numeric, false, uint8_t(def), 127, 128, spec::Icons::None, nullptr}; }
constexpr spec::Param bipolar(const char* label, int def = 64) { return {label, spec::Display::Bipolar, false, uint8_t(def), 127, 128, spec::Icons::None, nullptr}; }
constexpr spec::Param readout(const char* label, const char* const* names, int n, int def = 0)
{
    return {label, spec::Display::Readout, false, uint8_t(def), uint8_t(n - 1), uint8_t(n), spec::Icons::Switch, names};
}
// a two-state switch: a click flips it (the Toggle icon, as Monomodule One's on / off parameters)
constexpr spec::Param toggle(const char* label, const char* const* names, int def = 0)
{
    return {label, spec::Display::Readout, false, uint8_t(def), 1, 2, spec::Icons::Toggle, names};
}
constexpr spec::Param blank() { return {"", spec::Display::Blank, false, 0, 127, 128, spec::Icons::None, nullptr}; }

constexpr const char* kTrackNames[kTracks] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11", "T12", "T13", "T14", "T15", "T16"};
// The LFO shapes (manual: triangle, saw, square, linear decay, exponential decay, random; SHP2 = the inverted ones)
constexpr const char* kShape1Names[6] = {"TRI", "SAW", "SQR", "LIN", "EXP", "RND"};
constexpr const char* kShape2Names[6] = {"ITRI", "ISAW", "ISQR", "ILIN", "IEXP", "IRND"};
constexpr spec::Param withIcons(spec::Param p, spec::Icons i) { p.icons = i; return p; }
constexpr const char* kVelNames[2] = {"VOLUME", "ACCENT"};

const spec::Param kFxParams[8] = {numeric("AMD", 0), numeric("AMF", 0), numeric("EQF", 64), bipolar("EQG"),
                                  numeric("FLTF", 0), numeric("FLTW", 127), numeric("FLTQ", 0), numeric("SRR", 0)};
const spec::Param kRoutingParams[8] = {numeric("DIST", 0), numeric("VOL", 100), bipolar("PAN"), numeric("DEL", 0),
                                       numeric("REV", 0), readout("OUT", kRouteNames, kNumRoutes, kNumRoutes - 1), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
const spec::Param kLfoParams[8] = {readout("TRK", kTrackNames, kTracks), withIcons(readout("PARAM", kLfoParamNames, 24), spec::Icons::MdLfoParam),
                                   withIcons(readout("SHP1", kShape1Names, 6), spec::Icons::MdWave1),
                                   withIcons(readout("SHP2", kShape2Names, 6), spec::Icons::MdWave2), readout("TYPE", kLfoTypes, 3), numeric("SPD", 64), numeric("DEP", 0), numeric("MIX", 0)};
constexpr const char* kSeqNames[2] = {"OFF", "ON"};
constexpr const char* kModeNames[2] = {"PATTERN", "SONG"};
const spec::Param kOutParams[8] = {numeric("VOL", 80), toggle("VEL", kVelNames), numeric("ACNT", 64), blank(),
                                   toggle("SEQ", kSeqNames, 1), readout("PTN", kPatternNames, 128), toggle("MODE", kModeNames), readout("SONG", kSongNames, 32)};
constexpr const char* kMasterTabs[4] = {"REV", "DEL", "EQ", "DYN"};

// The pages of the MID and CTR machines (their parameters 8..23 are not track effects / routing)
const char* const* nameTable(int kind)   // 0 notes C-2..G8, 1 CC number (OFF, then 1 = CC 0), 2 program (OFF, 1..),
{                                        // 3 track T1..T16, 4 parameter (the 24 track parameters)
    static const auto tables = [] {
        std::array<std::array<std::string, 128>, 5> s;
        static const char* const notes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        for (int v = 0; v < 128; ++v) {
            s[0][size_t(v)] = std::string(notes[v % 12]) + std::to_string(v / 12 - 2);
            s[1][size_t(v)] = v == 0 ? "OFF" : std::to_string(v == 1 ? 0 : v);
            s[2][size_t(v)] = v == 0 ? "OFF" : std::to_string(v - 1);
            s[3][size_t(v)] = "T" + std::to_string(std::min(v, 15) + 1);
            s[4][size_t(v)] = kLfoParamNames[std::min(v, 23)];
        }
        return s;
    }();
    static const auto ptrs = [] {
        std::array<std::array<const char*, 128>, 5> p{};
        for (size_t k = 0; k < 5; ++k) for (size_t v = 0; v < 128; ++v) p[k][v] = tables[k][v].c_str();
        return p;
    }();
    return ptrs[size_t(kind)].data();
}
spec::Param named(const char* label, int kind, int def = 0) { return readout(label, nameTable(kind), 128, def); }
const spec::Param kMidFx[8] = {named("CC1D", 1), numeric("CC1V", 0), named("CC2D", 1), numeric("CC2V", 0),
                               named("CC3D", 1), numeric("CC3V", 0), named("CC4D", 1), numeric("CC4V", 0)};
const spec::Param kMidRouting[8] = {named("CC5D", 1), numeric("CC5V", 0), named("CC6D", 1), numeric("CC6V", 0),
                                    named("PCHG", 2), blank(), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
const spec::Param kCtr8pFx[8] = {named("P1T", 3), named("P1P", 4), named("P2T", 3), named("P2P", 4),
                                 named("P3T", 3), named("P3P", 4), named("P4T", 3), named("P4P", 4)};
const spec::Param kCtr8pRouting[8] = {named("P5T", 3), named("P5P", 4), named("P6T", 3), named("P6P", 4), named("P7T", 3), blank(), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
const spec::Param kCtr8pLfo[8] = {blank(), blank(), blank(), blank(), blank(), named("P7P", 4), named("P8T", 3), named("P8P", 4)};
const spec::Param kCtrAllRouting[8] = {numeric("DIST", 0), numeric("VOL", 100), bipolar("PAN"), numeric("DEL", 0),
                                       numeric("REV", 0), blank(), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
const spec::Param kGroupsOnly[8] = {blank(), blank(), blank(), blank(), blank(), blank(), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
const spec::Param kCtrAllLfo[8] = {blank(), blank(), blank(), blank(), blank(), numeric("SPD", 64), numeric("DEP", 0), numeric("MIX", 0)};
const spec::Param kBlankPage[8] = {blank(), blank(), blank(), blank(), blank(), blank(), blank(), blank()};

const spec::Param& masterParam(int fx, int k)
{
    static const auto table = [] {
        std::array<std::array<spec::Param, 8>, 4> t{};
        for (int f = 0; f < 4; ++f)
            for (int i = 0; i < 8; ++i) {
                const bool gain = f == 2 && (i == 1 || i == 3 || i == 5 || i == 7);   // EQ: LG HG PG GAIN are centred
                t[size_t(f)][size_t(i)] = gain ? bipolar(kMasterFxLabels[f][i], kMasterFxDefaults[f][i]) : numeric(kMasterFxLabels[f][i], kMasterFxDefaults[f][i]);
            }
        return t;
    }();
    return table[size_t(fx)][size_t(k)];
}

} // namespace

// ---------------------------------------------------------------------------------------------- machine block

MdMachineBlock::MdMachineBlock() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

void MdMachineBlock::setMachine(int index)
{
    if (index == m_index) return;
    m_index = index;
    if (auto* p = getParentComponent()) p->resized();   // the block hugs its name
    repaint();
}

// The machine block, as Monomodule's: the family's logo (its name for GND), the machine, the picker arrow. The logo is
// drawn in screen pixels, kLogoPx per logo pixel, after the LCD canvas.
namespace {
constexpr int kLogoPx = 2, kLogoX = 4, kLogoGap = 6;   // the picker's screen px per logo pixel (its columns are narrow); LCD px
// The block sizes a logo as Monomodule One / Six do (drawGroupLogo): its lit rows fill an 18-row band, the scale capped
// so the widest stays about as wide as theirs (SUPERWAVE's)
constexpr int kLogoBand = 18 * kScale, kLogoMaxW = 162;   // screen px: a 9-row logo 27 wide at 6 px (as Monomodule's are drawn)
struct LitBox { int x = 0, y = 0, w = 0, h = 0; };
LitBox litBox(const text::LogoArt& a)
{
    int x0 = a.w, x1 = -1, y0 = a.h, y1 = -1;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
            if (text::logoLit(a, x, y)) { x0 = juce::jmin(x0, x); x1 = juce::jmax(x1, x); y0 = juce::jmin(y0, y); y1 = juce::jmax(y1, y); }
    return x1 < 0 ? LitBox{} : LitBox{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}
int blockLogoPx(const text::LogoArt& a)
{
    const auto b = litBox(a);
    if (b.h <= 0) return 1;
    return juce::jmax(1, juce::jmin(int(std::lround(double(kLogoBand) / juce::jmax(9, b.h))), kLogoMaxW / b.w));
}
int logoWidthLcd(const juce::String& fam)
{
    if (const auto* art = text::logoArt(fam.toRawUTF8())) return (litBox(*art).w * blockLogoPx(*art) + kScale - 1) / kScale;
    return LcdCanvas::textWidth(spec::kFontBold8, fam.toRawUTF8());
}
}

// One size for every machine (the widest logo, the longest name), so the header stays put while sounds are stepped
// through: the logo centred in a box as wide as the widest, the name always at the same place.
int maxLogoWidthLcd()
{
    static const int w = [] { int m = 0; for (int i = 0; i < kNumMachines; ++i) m = juce::jmax(m, logoWidthLcd(familyOf(i))); return m; }();
    return w;
}

int MdMachineBlock::preferredWidth() const
{
    static const int nameW = [] { int m = 0; for (int i = 0; i < kNumMachines; ++i) m = juce::jmax(m, LcdCanvas::textWidth(spec::kFontBold8, shortOf(i).toRawUTF8())); return m; }();
    const int w = kLogoX + maxLogoWidthLcd() + kLogoGap + nameW + 4 + 5 + 4;
    return juce::jmax(64, w) * kScale;
}

void MdMachineBlock::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    cv.fillRect(0, 0, w, h, true);
    const auto fam = familyOf(m_index), name = shortOf(m_index);
    const auto* art = text::logoArt(fam.toRawUTF8());
    const int ty = (h - spec::kFontBold8.h) / 2;
    const int logoX = kLogoX + (maxLogoWidthLcd() - logoWidthLcd(fam)) / 2;   // centred in the logo box
    if (!art) cv.text(spec::kFontBold8, fam.toRawUTF8(), logoX, ty, false);
    const int nameX = kLogoX + maxLogoWidthLcd() + kLogoGap;
    cv.text(spec::kFontBold8, name.toRawUTF8(), nameX, ty, false);
    const int ax = nameX + LcdCanvas::textWidth(spec::kFontBold8, name.toRawUTF8()) + 4, ay = h / 2 - 1;
    for (int r = 0; r < 3; ++r) {   // the picker arrow: down when closed, up while open
        const int half = m_open ? r : 2 - r;
        for (int c = 2 - half; c <= 2 + half; ++c) cv.set(ax + c, ay + r, false);
    }
    cv.draw(g, 0, 0);
    if (art) {
        g.setColour(lcd::paper);
        const auto b = litBox(*art);
        const int px = blockLogoPx(*art);
        const int x0 = logoX * kScale, y0 = (getHeight() - b.h * px) / 2;
        for (int y = 0; y < b.h; ++y)
            for (int x = 0; x < b.w; ++x)
                if (text::logoLit(*art, b.x + x, b.y + y)) g.fillRect(x0 + x * px, y0 + y * px, px, px);
    }
}

// ---------------------------------------------------------------------------------------------- track keys

MdTrackKeys::MdTrackKeys()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setOpaque(true);
}

juce::Rectangle<int> MdTrackKeys::keyRect(int t) const
{
    const int w = getWidth() / kScale, gap = 2;
    const int kw = (w - (kTracks - 1) * gap) / kTracks;
    const int extra = w - (kTracks * kw + (kTracks - 1) * gap);   // spread the remainder over the first keys
    const int x = t * (kw + gap) + juce::jmin(t, extra);
    return {x, 0, kw + (t < extra ? 1 : 0), kLcdH};
}

void MdTrackKeys::paintGrid(LcdCanvas& cv)
{
    const auto& gr = m_grid;
    for (int i = 0; i < kTracks; ++i) {
        const auto r = keyRect(i);
        const int s = gr.page * 16 + i;
        if (s >= gr.length) {   // past the pattern's end: only the corners
            for (int c = 0; c < 3; ++c) { cv.set(r.getX() + c, r.getY(), true); cv.set(r.getRight() - 1 - c, r.getY(), true); cv.set(r.getX() + c, r.getBottom() - 1, true); cv.set(r.getRight() - 1 - c, r.getBottom() - 1, true); }
            continue;
        }
        // the trigs, or in an edit window (ACCENT / SLIDE / SWING) that mark, with a small dot where the trigs are
        const uint64_t markMask = gr.mark == 1 ? gr.accent : gr.mark == 2 ? gr.slide : gr.swing;
        const bool hasTrig = (gr.trigs >> s) & 1;
        const bool trig = gr.mark > 0 ? ((markMask >> s) & 1) != 0 : hasTrig;
        if (trig) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        else dottedFrame(cv, r.getX(), r.getY(), r.getWidth(), r.getHeight());
        const bool ink = !trig;
        cv.text(spec::kFontBold8, juce::String(s + 1).toRawUTF8(), r.getX() + 3, r.getY() + 3, ink);
        if (gr.mark > 0 && hasTrig) cv.fillRect(r.getCentreX() - 2, r.getBottom() - 8, 4, 4, ink);
        if (i == m_selected) {   // which track the steps are: its machine on its own key (T3: key 3), the others blank
            const int mi = juce::jlimit(0, kNumMachines - 1, m_machine[size_t(m_selected)]);
            cv.textCentred(spec::kFontTiny3x5, familyOf(mi).toRawUTF8(), r.getX() + 1, r.getWidth() - 1, r.getY() + 13, ink);
            cv.textCentred(spec::kFontTiny3x5, shortOf(mi).toRawUTF8(), r.getX() + 1, r.getWidth() - 1, r.getY() + 19, ink);
        }
        if (((gr.locks >> s) & 1) && s != gr.held) {   // locked: a crit burst in the corner
            static const char* const burst[7] = {"#..#..#", ".#.#.#.", "..###..", "#######", "..###..", ".#.#.#.", "#..#..#"};
            for (int y = 0; y < 7; ++y) for (int x = 0; x < 7; ++x) if (burst[y][x] == '#') cv.set(r.getRight() - 9 + x, r.getY() + 2 + y, ink);
        }
        if (s == gr.held) {   // held for locks: paper, a heavy frame, LOCK
            cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), false);
            cv.fillRect(r.getX(), r.getY(), r.getWidth(), 2, true); cv.fillRect(r.getX(), r.getBottom() - 2, r.getWidth(), 2, true);
            cv.fillRect(r.getX(), r.getY(), 2, r.getHeight(), true); cv.fillRect(r.getRight() - 2, r.getY(), 2, r.getHeight(), true);
            cv.text(spec::kFontBold8, juce::String(s + 1).toRawUTF8(), r.getX() + 4, r.getY() + 4, true);
            cv.textCentred(spec::kFontTiny3x5, "LOCK", r.getX(), r.getWidth(), r.getBottom() - 10, true);
        }
        if (s == gr.play) cv.invertRect(r.getX(), r.getY(), r.getWidth(), r.getHeight());   // the running light
    }
}

void MdTrackKeys::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    if (m_grid.on) { paintGrid(cv); cv.draw(g, 0, 0); return; }
    for (int t = 0; t < kTracks; ++t) {
        const auto r = keyRect(t);
        const bool sel = t == m_selected, ink = !sel;
        if (sel) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        else dottedFrame(cv, r.getX(), r.getY(), r.getWidth(), r.getHeight());
        cv.text(spec::kFontBold8, juce::String(t + 1).toRawUTF8(), r.getX() + 3, r.getY() + 3, ink);
        const int lx = r.getRight() - 7, ly = r.getY() + 4;   // activity LED: solid while the track sounds
        if (m_active[size_t(t)]) cv.fillRect(lx, ly, 4, 4, ink);
        else {
            for (int c = 0; c < 4; ++c) { cv.set(lx + c, ly, ink); cv.set(lx + c, ly + 3, ink); }
            cv.set(lx, ly + 1, ink); cv.set(lx, ly + 2, ink); cv.set(lx + 3, ly + 1, ink); cv.set(lx + 3, ly + 2, ink);
        }
        const int m = m_machine[size_t(t)];
        cv.textCentred(spec::kFontTiny3x5, familyOf(m).toRawUTF8(), r.getX() + 1, r.getWidth() - 1, r.getY() + 13, ink);
        cv.textCentred(spec::kFontTiny3x5, shortOf(m).toRawUTF8(), r.getX() + 1, r.getWidth() - 1, r.getY() + 19, ink);
        if (m_seqLen > 0) {   // the step this key stands for, on the playing page
            const int page = m_seqStep >= 0 ? m_seqStep / 16 : 0, s = page * 16 + t;
            if (s < m_seqLen) {
                const int bx = r.getCentreX() - 3, by = r.getBottom() - 9;
                if ((m_seqTrigs >> s) & 1) cv.fillRect(bx, by, 6, 6, ink);   // unlit: nothing (the L and M keys stay clear)
                if (s == m_seqStep) cv.invertRect(r.getX(), r.getY(), r.getWidth(), r.getHeight());   // the running light
            }
        }
        {   // LOCK and MUTE as round game-pad buttons (L / M), drawn over everything so they read on a lit key too:
            // hollow when off, solid when on, in the colour that stands out from the key under them
            const int page = m_seqStep >= 0 ? m_seqStep / 16 : 0;
            const bool lit = m_seqLen > 0 && page * 16 + t == m_seqStep && m_seqStep < m_seqLen;
            const bool c = !(sel != lit);   // the contrast colour (ink on a paper key)
            static const char* const disc[9] = {"..#####..", ".#######.", "#########", "#########", "#########", "#########", "#########", ".#######.", "..#####.."};
            static const char* const ring[9] = {"..#####..", ".#.....#.", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", ".#.....#.", "..#####.."};
            auto button = [&](juce::Rectangle<int> b, const char* letter, bool on, bool queued = false) {
                for (int y = 0; y < 9; ++y)
                    for (int x = 0; x < 9; ++x) {
                        if (disc[y][x] != '#') continue;
                        const bool fill = queued ? ((x + y) & 1) != 0 : on;   // queued: a checkered button (it flips when Shift is let go)
                        cv.set(b.getX() + x, b.getY() + y, ring[y][x] == '#' ? c : fill ? c : !c);
                    }
                if (queued) cv.fillRect(b.getX() + 2, b.getY() + 1, 5, 7, !c);
                static const char* const glyphL[5] = {".#..", ".#..", ".#..", ".#..", ".###"};   // (3 wide, centred)
                static const char* const glyphM[5] = {"#...#", "##.##", "#.#.#", "#...#", "#...#"};   // 5 wide: a 3-wide M reads as H
                const bool m = letter[0] == 'M';
                for (int y = 0; y < 5; ++y)
                    for (int x = 0; x < (m ? 5 : 4); ++x)
                        if ((m ? glyphM : glyphL)[y][x] == '#') cv.set(b.getX() + 2 + x, b.getY() + 2 + y, on ? !c : c);
            };
            button(lockBox(t), "L", m_locked[size_t(t)]);
            button(muteBox(t), "M", m_muted[size_t(t)], ((m_muteQueue >> t) & 1) != 0);
        }
        if (t == m_dropTarget) {   // a sound from the library would land here: an inner frame
            cv.invertRect(r.getX() + 1, r.getY() + 1, r.getWidth() - 2, 1); cv.invertRect(r.getX() + 1, r.getBottom() - 2, r.getWidth() - 2, 1);
            cv.invertRect(r.getX() + 1, r.getY() + 2, 1, r.getHeight() - 4); cv.invertRect(r.getRight() - 2, r.getY() + 2, 1, r.getHeight() - 4);
        }
    }
    cv.draw(g, 0, 0);
}

void MdTrackKeys::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kScale;
    if (m_grid.on) {
        for (int i = 0; i < kTracks; ++i) {
            if (!keyRect(i).contains(p)) continue;
            const int s = m_grid.page * 16 + i;
            if (m_grid.mark > 0 && !e.mods.isAnyModifierKeyDown() && !e.mods.isPopupMenu()) {   // an edit window: the mark flips,
                if (s >= m_grid.length) return;                                                  // a drag paints it over the keys
                const uint64_t mask = m_grid.mark == 1 ? m_grid.accent : m_grid.mark == 2 ? m_grid.slide : m_grid.swing;
                m_paintOn = !((mask >> s) & 1);
                m_painting = true; m_paintLast = s;
                if (onMarkPaint) onMarkPaint(s, m_grid.mark - 1, m_paintOn, true);
                else if (onFlag) onFlag(s, m_grid.mark - 1);
                return;
            }
            if (e.mods.isPopupMenu()) { if (onStepMenu && s < m_grid.length) onStepMenu(s); }
            else if (e.mods.isAltDown()) { if (onMuteKey) onMuteKey(i); }
            else if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { if (onSelect) onSelect(i); }
            else if (s >= m_grid.length) return;
            else if (e.mods.isShiftDown()) { if (onHold) onHold(s); }
            else if (onPaint) {   // a click sets the trig the other way; a drag from here paints that over the keys
                m_paintOn = !((m_grid.trigs >> s) & 1);
                m_painting = true; m_paintLast = s;
                onPaint(s, m_paintOn, true);
            }
            else if (onStep) onStep(s);
            return;
        }
        return;
    }
    for (int t = 0; t < kTracks; ++t) {
        if (!keyRect(t).contains(p)) continue;
        if (lockBox(t).expanded(1).contains(p)) { if (onLock) onLock(t); }
        else if (muteBox(t).expanded(1).contains(p)) { if (e.mods.isShiftDown() && onMuteQueue) onMuteQueue(t); else if (onMute) onMute(t); }
        else if ((e.mods.isCommandDown() || e.mods.isCtrlDown()) && onSelect) onSelect(t);   // select without a sound
        else if (onPress) onPress(t);
        return;
    }
}

void MdTrackKeys::mouseDrag(const juce::MouseEvent& e)
{
    if (!m_painting || !m_grid.on) return;
    const int key = trackAt(e.getPosition());
    if (key < 0) return;
    const int s = m_grid.page * 16 + key;
    if (s == m_paintLast || s >= m_grid.length) return;
    m_paintLast = s;
    if (m_grid.mark > 0) {   // an edit window: the mark
        const uint64_t mask = m_grid.mark == 1 ? m_grid.accent : m_grid.mark == 2 ? m_grid.slide : m_grid.swing;
        if ((((mask >> s) & 1) != 0) != m_paintOn && onMarkPaint) onMarkPaint(s, m_grid.mark - 1, m_paintOn, false);
        return;
    }
    if (onPaint && (((m_grid.trigs >> s) & 1) != 0) != m_paintOn) onPaint(s, m_paintOn, false);
}

void MdTrackKeys::mouseMove(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kScale;
    juce::String tip;
    if (m_grid.on) {
        const int key = trackAt(e.getPosition());
        const int step = m_grid.page * 16 + juce::jmax(0, key);
        const auto locks = key >= 0 && stepLocks ? stepLocks(step) : juce::String();
        setTooltip("Step " + juce::String(step + 1) + (locks.isNotEmpty() ? ". LOCKS: " + locks : juce::String())
                   + ". Click: trig on/off. Shift+click: hold for locks (turn the track's knobs; double-click one to clear its lock). Ctrl+click: select that track. Alt+click: mute that track. Right-click: step menu. Ctrl+Z: undo.");
        return;
    }
    for (int t = 0; t < kTracks; ++t) {
        if (!keyRect(t).contains(p)) continue;
        if (lockBox(t).expanded(1).contains(p)) tip = "L (LOCK): keep this track's sound when a kit is loaded";
        else if (muteBox(t).expanded(1).contains(p)) tip = "M (MUTE): this track's trigs don't play. Shift+click several: they flip together when you let go of Shift";
        else tip = "Track " + juce::String(t + 1) + ": click to play and select it; Ctrl+click: select it without a sound";
    }
    setTooltip(tip);
}

// ---------------------------------------------------------------------------------------------- machine picker

namespace {
// Geometry in screen pixels, as Monomodule's picker: a column per family (ROM three), a black header with the logo
// and name, the blurb (or the hovered machine's description), then the machines as rows.
constexpr int kPickGap = 4, kPickBorder = 2, kPickHeaderH = 50, kPickPad = 6, kTextScale = 2, kLineH = 12, kBlurbLines = 4;
constexpr int kPickAnimHz = 60; constexpr float kPickAnimSeconds = 0.16f;

void pickDottedH(juce::Graphics& g, int x0, int x1, int y) { for (int x = x0; x < x1; x += 4) g.fillRect(x, y, 2, 2); }
}

MdMachinePicker::MdMachinePicker()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    for (int i = 0; i < kNumMachines; ++i) {   // families are contiguous in kMachines (the picker's order)
        const auto fam = ::mnm::plugin::md::familyOf(i);
        if (m_families.empty() || m_families.back().name != fam) m_families.push_back({fam, {}});
        m_families.back().items.push_back(i);
    }
    int col = 0;
    for (auto& f : m_families) {
        f.firstCol = col; f.cols = (int(f.items.size()) + 15) / 16; col += f.cols;
        for (int sub = 0; sub < f.cols; ++sub)
            m_grid.emplace_back(f.items.begin() + sub * 16, f.items.begin() + std::min(int(f.items.size()), (sub + 1) * 16));
    }
    m_rows.resize(size_t(kNumMachines));
}

void MdMachinePicker::setTargetBounds(juce::Rectangle<int> fullyOpen)
{
    m_target = fullyOpen;
    layout();
    applyAnimation();
}

void MdMachinePicker::open(int current, bool animate)
{
    m_current = current;
    m_hover = current;   // the keyboard cursor starts on the current machine (its description shows)
    m_wantOpen = true;
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    if (!animate) { m_anim = 1.0f; stopTimer(); applyAnimation(); }
    else if (!isTimerRunning()) startTimerHz(kPickAnimHz);
    repaint();
}

void MdMachinePicker::close(bool animate)
{
    const bool was = m_wantOpen;
    m_wantOpen = false;
    m_hover = -1;
    if (!animate) { m_anim = 0.0f; stopTimer(); applyAnimation(); }
    else if (!isTimerRunning()) startTimerHz(kPickAnimHz);
    if (was && onClosed) onClosed();
}

// It unrolls from the top edge: the bounds grow down from the target's top with an ease-out; the layout stays the
// fully open one, so the pages are covered progressively (Monomodule's picker does the same)
void MdMachinePicker::applyAnimation()
{
    const float eased = 1.0f - (1.0f - m_anim) * (1.0f - m_anim) * (1.0f - m_anim);
    setBounds(m_target.withHeight(juce::jmax(0, int(std::lround(m_target.getHeight() * eased)))));
    if (m_anim <= 0.0f && !m_wantOpen) setVisible(false);
}

void MdMachinePicker::timerCallback()
{
    const float step = 1.0f / (kPickAnimSeconds * float(kPickAnimHz));
    m_anim = m_wantOpen ? juce::jmin(1.0f, m_anim + step) : juce::jmax(0.0f, m_anim - step);
    applyAnimation();
    if (m_anim <= 0.0f || m_anim >= 1.0f) stopTimer();
}

void MdMachinePicker::layout()
{
    for (auto& r : m_rows) r = {};
    const int nCols = m_families.empty() ? 1 : m_families.back().firstCol + m_families.back().cols;
    const int W = m_target.getWidth(), H = m_target.getHeight();
    const int colW = (W - (nCols - 1) * kPickGap) / nCols;
    const int rowsTop = kPickBorder + kPickHeaderH + 4 + kBlurbLines * kLineH + 6;
    const int rowH = juce::jlimit(14, 24, (H - rowsTop - kPickBorder - 2) / 16);
    for (auto& f : m_families) {
        const int x = f.firstCol * (colW + kPickGap), w = f.cols * colW + (f.cols - 1) * kPickGap;
        f.bounds = {x, 0, w, H};
        f.header = {x + kPickBorder, kPickBorder, w - 2 * kPickBorder, kPickHeaderH};
        f.blurb = {x + kPickPad, kPickBorder + kPickHeaderH + 4, w - 2 * kPickPad, kBlurbLines * kLineH};
        for (size_t i = 0; i < f.items.size(); ++i) {
            const int sub = int(i) / 16, row = int(i) % 16;
            const int sx = f.firstCol * (colW + kPickGap) + sub * (colW + kPickGap);
            m_rows[size_t(f.items[i])] = {sx + kPickBorder, rowsTop + row * rowH, colW - 2 * kPickBorder, rowH};
        }
    }
}

int MdMachinePicker::itemAt(juce::Point<int> p) const
{
    for (int i = 0; i < kNumMachines; ++i) if (m_rows[size_t(i)].contains(p)) return i;
    return -1;
}

int MdMachinePicker::familyOf(int index) const
{
    for (int k = 0; k < int(m_families.size()); ++k)
        for (int i : m_families[size_t(k)].items) if (i == index) return k;
    return -1;
}

juce::String MdMachinePicker::describe(int index) const
{
    const int id = kMachines[index].id;
    if (isRomMachine(id)) {
        const auto s = sampleName ? sampleName(index) : juce::String();
        return juce::String(kMachines[index].name) + " " + (s.isEmpty() ? juce::String("EMPTY SLOT") : s.toUpperCase());
    }
    if (isMidMachine(id)) return "MIDI OUT ON CH " + juce::String(id - 95);
    if (const char* t = text::machineText(id)) return t;
    return kMachines[index].name;
}

void MdMachinePicker::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper);
    for (const auto& f : m_families) drawFamily(g, f);
}

void MdMachinePicker::drawFamily(juce::Graphics& g, const Family& f) const
{
    g.setColour(lcd::ink);
    g.drawRect(f.bounds, kPickBorder);
    // header: the family's logo alone, paper on ink, as large as the column allows (GND: its name)
    g.fillRect(f.header);
    if (const auto* art = text::logoArt(f.name.toRawUTF8())) {
        int px = 4;   // one scale for every header: the largest the narrowest column takes (the logos share one size)
        for (const auto& o : m_families) px = juce::jmin(px, (o.header.getWidth() - 8) / art->w);
        px = juce::jmax(kLogoPx, px);
        const int lx = f.header.getCentreX() - art->w * px / 2, ly = f.header.getCentreY() - art->h * px / 2;
        g.setColour(lcd::paper);
        for (int y = 0; y < art->h; ++y)
            for (int x = 0; x < art->w; ++x)
                if (text::logoLit(*art, x, y)) g.fillRect(lx + x * px, ly + y * px, px, px);
    } else {
        const int nw = LcdCanvas::textWidth(spec::kFontBold8, f.name.toRawUTF8()) * 3;
        drawLcdText(g, spec::kFontBold8, f.name.toRawUTF8(), f.header.getCentreX() - nw / 2, f.header.getCentreY() - spec::kFontBold8.h * 3 / 2, 3, lcd::paper);
    }
    // the blurb, or what the hovered machine of this family is
    const bool hovered = m_hover >= 0 && familyOf(m_hover) == int(&f - m_families.data());
    const auto words = hovered ? describe(m_hover) : juce::String(text::familyBlurb(f.name.toRawUTF8()));
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(f.blurb);
        int n = 0;
        for (const auto& line : wrapLcdText(spec::kFontSmall4x5, words.toUpperCase(), f.blurb.getWidth() / kTextScale)) {
            if (n >= kBlurbLines) break;
            drawLcdText(g, spec::kFontSmall4x5, line.toRawUTF8(), f.blurb.getX(), f.blurb.getY() + n++ * kLineH, kTextScale, lcd::ink);
        }
    }
    g.setColour(lcd::ink);
    pickDottedH(g, f.bounds.getX() + kPickBorder, f.bounds.getRight() - kPickBorder, f.blurb.getBottom() + 3);
    // the machines
    for (int idx : f.items) {
        const auto& r = m_rows[size_t(idx)];
        const bool cur = idx == m_current;
        if (cur) { g.setColour(lcd::ink); g.fillRect(r); }
        const auto colour = cur ? lcd::paper : lcd::ink;
        const int id = kMachines[idx].id;
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(r);
        const auto label = isRomMachine(id) ? juce::String(kMachines[idx].name).substring(4) : shortOf(idx);
        const int ty = r.getY() + (r.getHeight() - spec::kFontBold8.h * 2) / 2;
        drawLcdText(g, spec::kFontBold8, label.toRawUTF8(), r.getX() + 4, ty, 2, colour);
        if (isRomMachine(id) && sampleName) {   // the slot's sample, as far as it fits
            const auto name = sampleName(idx).toUpperCase();
            if (name.isNotEmpty())
                drawLcdText(g, spec::kFontSmall4x5, name.toRawUTF8(), r.getX() + 4 + LcdCanvas::textWidth(spec::kFontBold8, label.toRawUTF8()) * 2 + 6,
                            r.getY() + (r.getHeight() - spec::kFontSmall4x5.h * 2) / 2, 2, colour);
        }
        if (idx == m_hover && !cur) { g.setColour(lcd::ink); g.drawRect(r, 2); }
    }
}

void MdMachinePicker::mouseMove(const juce::MouseEvent& e)
{
    const int h = itemAt(e.getPosition());
    setMouseCursor(h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != m_hover) { m_hover = h; repaint(); }
}

void MdMachinePicker::mouseDown(const juce::MouseEvent& e)
{
    const int idx = itemAt(e.getPosition());
    if (idx >= 0 && onPick) onPick(idx);
    close();
}

bool MdMachinePicker::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    if (k == juce::KeyPress::returnKey) {
        const int idx = m_hover >= 0 ? m_hover : m_current;
        if (onPick) onPick(idx);
        close();
        return true;
    }
    const bool up = k == juce::KeyPress::upKey, down = k == juce::KeyPress::downKey;
    const bool left = k == juce::KeyPress::leftKey, right = k == juce::KeyPress::rightKey;
    if (!(up || down || left || right) || m_grid.empty()) return false;
    const int at = m_hover >= 0 ? m_hover : m_current;
    int c = 0, r = 0;
    for (int ci = 0; ci < int(m_grid.size()); ++ci)
        for (int ri = 0; ri < int(m_grid[size_t(ci)].size()); ++ri)
            if (m_grid[size_t(ci)][size_t(ri)] == at) { c = ci; r = ri; }
    if (up) r = juce::jmax(0, r - 1);
    if (down) r = juce::jmin(int(m_grid[size_t(c)].size()) - 1, r + 1);
    if (left) c = juce::jmax(0, c - 1);
    if (right) c = juce::jmin(int(m_grid.size()) - 1, c + 1);
    r = juce::jmin(r, int(m_grid[size_t(c)].size()) - 1);
    m_hover = m_grid[size_t(c)][size_t(r)];
    repaint();
    return true;
}

// ---------------------------------------------------------------------------------------------- badge button

int MdBadgeButton::preferredWidth() const { return (LcdCanvas::textWidth(spec::kFontSmall4x5, getButtonText().toRawUTF8()) + 5) * kScale; }

void MdBadgeButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    if (down) cv.fillRect(0, 0, w, h, true);
    cv.text(spec::kFontSmall4x5, getButtonText().toRawUTF8(), 2, (h - spec::kFontSmall4x5.h) / 2, !down);
    cv.draw(g, 0, 0);
    if (highlighted && !down) { g.setColour(lcd::ink.withAlpha(0.15f)); g.fillRect(getLocalBounds()); }
}

// ---------------------------------------------------------------------------------------------- editor

MdEditor::MdEditor(MdProcessor& p)
    : AudioProcessorEditor(p), m_proc((skin::apply(skin::load()), p)),   // the skin first: m_lnf reads it when it is built
      m_footerVersion(spec::kFontSmall4x5, kPluginVersion, 2),
      m_footerBy(spec::kFontSmall4x5, "BY SHNOLK - MD BY KX", 2, false, juce::Justification::centredRight),
      m_status(spec::kFontBold8, "NO MACHINEDRUM OS FILE", kScale),
      m_bpmLabel(spec::kFontBold8, "BPM", kScale, false, juce::Justification::centredRight),
      m_syn(p.apvts, "SYNTHESIS"), m_fx(p.apvts, "EFFECTS"), m_routing(p.apvts, "ROUTING"),
      m_lfo(p.apvts, "LFO"), m_master(p.apvts, "MASTER FX"), m_out(p.apvts, "OUTPUT"),
      m_missingOs([this] { chooseOsFile(); }, [] {
          OsRequirement os;
          os.device = "Machinedrum"; os.osFile = "Elektron_SPS1-1UW_OS1.63.syx"; os.zipFile = "Elektron_SPS1-1UW_OS1.63.zip";
          os.url = "https://www.elektron.se/support-downloads/machinedrum"; os.linkText = "elektron.se  -  Machinedrum support and downloads";
          os.directDownload = false;
          return os;
      }())
{
    setLookAndFeel(&m_lnf);
    m_artPath = loadSharedOsPath();   // the LCD fonts and dials come from the Monomachine OS file, when there is one
    one::loadLcdArt(m_artPath);

    addAndMakeVisible(m_machineBlock);
    m_machineBlock.onOpen = [this] {
        if (m_picker.isOpen()) { m_picker.close(); return; }
        if (m_drop.isVisible()) m_drop.close();
        if (m_panel.isOpen()) m_panel.close(false);
        m_picker.open(m_machineIndex);
        m_machineBlock.setOpen(true);
    };
    addMouseListener(this, true);   // a press anywhere else closes the picker and the list
    m_picker.onPick = [this](int idx) { setMachine(idx); };
    m_picker.onClosed = [this] { m_machineBlock.setOpen(false); };
    m_picker.sampleName = [this](int idx) { const int id = kMachines[idx].id; return isRomMachine(id) ? m_proc.sampleName(romSlotOf(id)) : juce::String(); };
    addChildComponent(m_picker);

    m_menuButton.onClick = [this] { showMenu(); };
    m_osButton.onClick = [this] { chooseOsFile(); };
    for (juce::Component* c : {static_cast<juce::Component*>(&m_menuButton), static_cast<juce::Component*>(&m_footerVersion),
                               static_cast<juce::Component*>(&m_footerBy), static_cast<juce::Component*>(&m_level),
                               static_cast<juce::Component*>(&m_strip)})
        addAndMakeVisible(c);
    m_strip.onPart = [this](MdKitStrip::Part part) {
        if (part == MdKitStrip::KitPrev || part == MdKitStrip::KitNext || part == MdKitStrip::Kit) m_browseKits = true;
        if (part == MdKitStrip::SoundPrev || part == MdKitStrip::SoundNext || part == MdKitStrip::Sound) m_browseKits = false;
        grabKeyboardFocus();
        const bool wasKits = m_drop.isVisible() && m_drop.showingKits(), wasSounds = m_drop.isVisible() && !m_drop.showingKits();
        m_drop.setVisible(false);
        m_strip.setOpen(MdKitStrip::None);
        switch (part) {
            case MdKitStrip::KitPrev:   stepKit(-1); break;
            case MdKitStrip::KitNext:   stepKit(1); break;
            case MdKitStrip::Kit:       if (!wasKits) openKitList(); break;
            case MdKitStrip::KitSave:
                m_saveDialog.onSave = [this](const juce::String& name, bool into, bool asVersion) { return saveKit(name, into, asVersion); };
                m_saveDialog.open("SAVE KIT", m_proc.kitName().isEmpty() ? juce::String("NEW KIT") : m_proc.kitName(), slotText(m_lib->projectSlotOfKit(m_proc.loadedKitKey())),
                                  m_proc.loadedKitKey().isNotEmpty() ? m_proc.kitName() : juce::String());
                break;
            case MdKitStrip::SoundPrev: stepSound(-1); break;
            case MdKitStrip::SoundNext: stepSound(1); break;
            case MdKitStrip::Sound:     if (!wasSounds) openSoundList(); break;
            case MdKitStrip::SoundSave:
                m_saveDialog.onSave = [this](const juce::String& name, bool into, bool asVersion) { return saveSound(name, into, asVersion); };
                m_saveDialog.open("SAVE SOUND T" + juce::String(m_track + 1), soundDisplayName(m_track).isEmpty() || soundDisplayName(m_track) == "-" ? juce::String("NEW SOUND") : soundDisplayName(m_track),
                                  slotText(m_lib->projectSlotOfSound(m_proc.loadedSoundKey(m_track))));
                break;
            case MdKitStrip::Library:   if (m_panel.isOpen()) m_panel.close(); else { m_picker.close(false); m_panel.open(); } break;
            case MdKitStrip::None: break;
        }
    };
    // the library panel over the pages
    m_panel.currentKit = [this] { return m_proc.loadedKitKey(); };
    m_panel.currentSound = [this] { return m_proc.loadedSoundKey(m_track); };
    m_panel.selectedTrack = [this] { return m_track; };
    m_panel.loadKit = [this](const juce::String& key) { loadKitKey(key); };
    m_panel.loadSound = [this](const juce::String& key) { loadSoundKey(m_track, key); };
    m_panel.loadPatternKit = [this](const juce::String& key) {
        if (const auto* p = m_lib->model().mdCatalog().pattern(key.toStdString()); p && !p->kitId.empty()) loadKitKey(juce::String(p->kitId));
        else m_panel.message = "THIS PATTERN'S KIT SLOT IS EMPTY";
    };
    m_panel.audition = [this](const juce::String& key, MdLibraryPanel::Tab kind) { audition(key, int(kind)); };
    m_panel.isPlaying = [this](const juce::String& key) { return m_proc.previewKey() == key; };
    m_panel.looping = [this] { return m_proc.previewLoop(); };
    m_panel.toggleLoop = [this] { m_proc.previewSetLoop(!m_proc.previewLoop()); };
    m_panel.patternMidiFile = [this](const juce::String& key) {
        const auto& cat = m_lib->model().mdCatalog();
        const auto* p = cat.pattern(key.toStdString());
        if (!p) return juce::File();
        const auto* k = cat.kit(p->kitId);
        return mnm::library::writeMdPatternMidiDragFile(k ? &k->kit : nullptr, p->pattern, juce::String(p->name).replace(" ", "-"), -1);
    };
    m_panel.onSoundDragging = [this](juce::Point<int> screen) { m_keys.setDropTarget(m_keys.trackAt(m_keys.getLocalPoint(nullptr, screen))); };
    m_panel.onSoundDropped = [this](const juce::String& key, juce::Point<int> screen) {
        m_keys.setDropTarget(-1);
        const int t = m_keys.trackAt(m_keys.getLocalPoint(nullptr, screen));
        if (t >= 0) { loadSoundKey(t, key); if (t != m_track) selectTrack(t); }
    };
    m_panel.onOpenChanged = [this](bool open) { m_strip.setLibraryOpen(open); };
    m_drop.onLoadKit = [this](const KitEntry& e) { m_browseKits = true; loadKit(e); };
    m_drop.onLoadSound = [this](const SoundEntry& e) { m_browseKits = false; loadSound(e); };
    m_drop.onImport = [this] { importSyx(); };
    m_drop.onAudition = [this](const juce::String& key, bool kit) { audition(key, kit ? int(MdLibraryPanel::Kits) : int(MdLibraryPanel::Sounds)); };
    m_drop.isPlaying = [this](const juce::String& key) { return m_proc.previewKey() == key; };
    m_drop.onClosed = [this] { m_strip.setOpen(MdKitStrip::None); if (isShowing()) grabKeyboardFocus(); };
    m_drop.looping = [this] { return m_proc.previewLoop(); };
    m_drop.toggleLoop = [this] { m_proc.previewSetLoop(!m_proc.previewLoop()); };
    m_drop.onLibrary = [this](bool kits) {
        m_picker.close(false);
        m_panel.open();
        m_panel.setTab(kits ? MdLibraryPanel::Kits : MdLibraryPanel::Sounds);
    };
    addChildComponent(m_status);
    addAndMakeVisible(m_bpmLabel);
    addAndMakeVisible(m_bpm);
    m_bpmAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, bpmId(), m_bpm);
    addAndMakeVisible(m_bpmSync);
    m_bpmSyncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(m_proc.apvts, bpmSyncId(), m_bpmSync);
    m_bpmSync.onStateChange = [this] { m_bpm.setSynced(m_bpmSync.getToggleState()); };
    m_bpm.setSynced(m_bpmSync.getToggleState());
    addChildComponent(m_osButton);

    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_master, &m_out}) addAndMakeVisible(pg);
    m_master.setTabs({kMasterTabs[0], kMasterTabs[1], kMasterTabs[2], kMasterTabs[3]}, 0, [this](int tab) { bindMasterFx(tab); });
    for (int s = 0; s < 128; ++s) { m_ptnNames[size_t(s)] = kPatternNames[s]; m_ptnNamePtrs[size_t(s)] = m_ptnNames[size_t(s)].c_str(); }
    m_out.setTabs({"OUT", "PTN"}, 0, [this](int tab) { bindOutPage(tab); });
    bindOutPage(0);
    m_keys.onStep = [this](int s) {
        const int t = m_track;
        bool removed = false;
        doEdit(editSlot(), "step " + juce::String(s + 1), [&](mnm::mddump::Pattern& p) {
            if ((p.trigs[t] >> s) & 1) { p.trigs[t] &= ~(1ull << s); p.clearStepLocks(t, s); removed = true; }
            else p.trigs[t] |= 1ull << s;
        });
        if (removed && s == m_heldStep) holdStep(-1);
        refreshGrid();
    };
    m_keys.onPaint = [this](int s, bool on, bool first) {   // one stroke = one undo step
        if (first) ++m_paintStroke;
        const int t = m_track;
        doEdit(editSlot(), on ? "paint steps" : "erase steps", [&](mnm::mddump::Pattern& p) {
            if (on) p.trigs[t] |= 1ull << s;
            else { p.trigs[t] &= ~(1ull << s); p.clearStepLocks(t, s); }
        }, 20000 + (m_paintStroke & 0xFFFF));
        if (!on && s == m_heldStep) holdStep(-1);
        refreshGrid();
    };
    m_keys.onHold = [this](int s) { holdStep(s == m_heldStep ? -1 : s); };
    m_keys.onFlag = [this](int s, int f) { flipStepFlag(s, f); };
    m_keys.onMarkPaint = [this](int s, int f, bool on, bool first) {   // one stroke = one undo step
        if (first) ++m_paintStroke;
        static const char* const names[3] = {"accent", "slide", "swing"};
        const int t = m_track;
        doEdit(editSlot(), names[f], [&](mnm::mddump::Pattern& p) {
            uint32_t all[3] = {p.accentEditAll, p.slideEditAll, p.swingEditAll};
            uint64_t* global[3] = {&p.accent, &p.slide, &p.swing};
            uint64_t* own[3] = {&p.accentPerTrack[t], &p.slidePerTrack[t], &p.swingPerTrack[t]};
            uint64_t& m = *(all[f] ? global[f] : own[f]);
            if (on) m |= 1ull << s; else m &= ~(1ull << s);
        }, 20000 + (m_paintStroke & 0xFFFF));
        refreshGrid();
    };
    m_keys.onSelect = [this](int t) { selectTrack(t); };
    m_keys.onMuteKey = [this](int t) { toggleMute(t); };
    m_keys.onMuteQueue = [this](int t) { m_muteQueue ^= uint16_t(1u << t); m_keys.setMuteQueue(m_muteQueue); };
    {   // Alt + turn: the parameter on every track (as the MD's FUNCTION + knob; not MID, RAM-R or CTR tracks)
        auto everyTrack = [this](std::function<juce::String(int, int)> id) {
            return [this, id](int k, int v) {
                for (int u = 0; u < kTracks; ++u) {
                    const int mid = m_proc.machineIdOf(u);
                    if (u == m_track || isMidMachine(mid) || isCtrMachine(mid) || mid == 160 || mid == 161 || mid == 165 || mid == 166) continue;
                    if (auto* p = m_proc.apvts.getParameter(id(u, k))) p->setValueNotifyingHost(p->convertTo0to1(float(v)));
                }
                flash("ALL TRACKS");
            };
        };
        m_syn.onAltTurn = everyTrack([](int u, int k) { return knobId(u, k); });
        m_fx.onAltTurn = everyTrack([](int u, int k) { return fxId(u, k); });
        m_routing.onAltTurn = everyTrack([](int u, int k) {
            static juce::String (* const ids[6])(int) = {distId, volId, panId, delId, revId, routeId};
            return k < 6 ? ids[k](u) : juce::String();
        });
        m_lfo.onAltTurn = everyTrack([](int u, int k) { return lfoId(u, k); });
    }
    m_keys.stepLocks = [this](int step) {   // "PTCH 90, DEC 40": the selected track's locks on that step
        const auto p = m_proc.bankPattern(editSlot());
        juce::String s;
        if (!p || step < 0 || step >= 64) return s;
        for (int q = 0; q < 24; ++q) {
            const int row = p->lockRow(m_track, q);
            if (row < 0 || p->locks[row][step] > 127) continue;
            juce::String name = q < 24 ? juce::String(q) : juce::String();
            if (auto* prm = m_proc.apvts.getParameter(trackParamId(m_track, q))) name = prm->getName(32).fromFirstOccurrenceOf(" ", false, false);
            if (s.isNotEmpty()) s << ", ";
            s << name << " " << int(p->locks[row][step]);
        }
        return s;
    };
    setWantsKeyboardFocus(true);
    m_keys.onStepMenu = [this](int s) { stepMenu(s); };
    addAndMakeVisible(m_seqBar);
    m_seqBar.onPart = [this](MdSeqBar::Part p) {
        switch (p) {
            case MdSeqBar::Play: {
                if (m_proc.hostPlaying()) return;
                const bool on = !m_proc.internalPlay();
                if (on)   // the sequencer has to be on to play
                    if (auto* a = m_proc.apvts.getParameter(seqId()); a && a->getValue() < 0.5f) a->setValueNotifyingHost(1.0f);
                m_proc.setInternalPlay(on);
                break;
            }
            case MdSeqBar::Grid: toggleGrid(); break;
            case MdSeqBar::Mix: toggleMixer(); break;
            case MdSeqBar::Edit: doublePattern(); return;
            case MdSeqBar::DelPg: deletePage(juce::jlimit(0, 3, m_gridPage)); return;
            case MdSeqBar::Rec: m_proc.setRecord(!m_proc.recordArmed()); if (m_proc.recordArmed()) seqOn(); break;
            case MdSeqBar::Mute: toggleMute(m_track); break;
            case MdSeqBar::TrkPrev: selectTrack((m_track + kTracks - 1) % kTracks); break;
            case MdSeqBar::TrkNext: selectTrack((m_track + 1) % kTracks); break;
            case MdSeqBar::PtnPrev: case MdSeqBar::PtnNext:
                if (auto* a = m_proc.apvts.getParameter(patternId())) {
                    const int v = (editSlot() + (p == MdSeqBar::PtnNext ? 1 : 127)) % 128;
                    a->setValueNotifyingHost(a->convertTo0to1(float(v)));
                    holdStep(-1);
                }
                break;
            default: break;
        }
        refreshGrid();
    };
    addChildComponent(m_songEd);
    setTooltipsOn(loadSharedSetting("tooltips", "1") != "0");
    {   // tooltips for the knobs, the header
        int pi = 0;
        for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_master, &m_out}) {
            const int page = pi++;
            pg->tipFor = [this, page, pg](int, const juce::String& label) { return knobTip(label, page == 5 && pg->currentTab() == 1 ? 6 : page, familyOf(juce::jlimit(0, kNumMachines - 1, m_machineIndex))); };
        }
        m_bpm.setTooltip("The tempo (drag or wheel). With SYNC on it follows Ableton");
        m_bpmSync.setTooltip("SYNC: the tempo follows Ableton's; off: the BPM here");
        m_menuButton.setTooltip("Kits, songs, MIDI settings, the mixer, undo, kit tools, tooltips on / off, the OS file");
        m_level.setTooltip("The selected track's LEVEL, with its meter");
        m_machineBlock.setTooltip("The selected track's machine: click to choose another");
        bindOutPage(m_outTab);      // (bound before the tips were set)
        bindMasterFx(m_masterTab);
    }
    m_mixer.setTooltip("Mixer: level, pan, mute (M) and solo (S) of every track");
    m_songEd.setTooltip("Song editor: the song's rows");
    m_midiPanel.setTooltip("MIDI settings");
    addChildComponent(m_mixer);
    m_mixer.setWantsKeyboardFocus(true);
    m_mixer.strip = [this](int t) {
        MdMixer::Strip s;
        const int mi = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(t))->load())));
        s.family = familyOf(mi);
        s.machine = shortOf(mi);
        s.level = int(std::lround(m_proc.apvts.getRawParameterValue(levelId(t))->load()));
        s.pan = int(std::lround(m_proc.apvts.getRawParameterValue(panId(t))->load()));
        s.mute = m_proc.apvts.getRawParameterValue(muteId(t))->load() >= 0.5f;
        s.solo = m_proc.soloed(t);
        s.active = m_proc.trackActivity(t) > 0.012f;
        s.selected = t == m_track;
        s.peak = m_proc.trackPeak(t);
        return s;
    };
    m_mixer.set = [this](int t, MdMixer::What what, int v) {
        if (what == MdMixer::Select) { selectTrack(t); return; }
        if (what == MdMixer::Solo) { m_proc.setSolo(t, v != 0); return; }
        auto* p = m_proc.apvts.getParameter(what == MdMixer::Level ? levelId(t) : what == MdMixer::Pan ? panId(t) : muteId(t));
        if (!p) return;
        const float to = what == MdMixer::Mute ? (v != 0 ? 1.0f : 0.0f) : p->convertTo0to1(float(v));
        if (what == MdMixer::Mute) p->beginChangeGesture();
        p->setValueNotifyingHost(to);
        if (what == MdMixer::Mute) { p->endChangeGesture(); refreshGrid(); }
    };
    m_mixer.gesture = [this](int t, MdMixer::What what, bool begin) {
        if (auto* p = m_proc.apvts.getParameter(what == MdMixer::Level ? levelId(t) : panId(t))) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
    };
    m_mixer.onClose = [this] { grabKeyboardFocus(); };
    addChildComponent(m_midiPanel);
    m_midiPanel.get = [this] {
        const auto s = m_proc.midiSettings();
        MdMidiPanel::Values v;
        v.baseChannel = s.baseChannel; v.programChange = s.programChange; v.midiOut = s.midiOut; v.pcChannel = s.pcChannel;
        v.note.fill(-1);
        for (int n = 0; n < 128; ++n) { const int t = s.noteTrack[size_t(n)]; if (t >= 0 && t < kTracks && v.note[size_t(t)] < 0) v.note[size_t(t)] = n; }
        v.ptnMode = s.patternNoteMode;
        for (int n = 0; n < 128; ++n) {
            const int a = s.noteAction[size_t(n)];
            if (a == MdProcessor::kStartNote && v.startNote < 0) v.startNote = n;
            if (a == MdProcessor::kStopNote && v.stopNote < 0) v.stopNote = n;
        }
        // the pattern notes as FROM / BANK when they are a bank's 16 patterns on consecutive white keys, else CUSTOM
        int from = -1, count = 0;
        for (int n = 0; n < 128; ++n) if (s.noteAction[size_t(n)] >= 0 && s.noteAction[size_t(n)] < 128) { ++count; if (from < 0) from = n; }
        if (count > 0) {
            const int bank = s.noteAction[size_t(from)] / 16;
            bool regular = s.noteAction[size_t(from)] % 16 == 0;
            int n = from, k = 0;
            for (; regular && k < 16 && n < 128; ++n) {
                if (juce::MidiMessage::isMidiNoteBlack(n)) continue;
                regular = s.noteAction[size_t(n)] == bank * 16 + k;
                ++k;
            }
            v.ptnFrom = regular && k == count ? from : -2;
            v.ptnBank = regular ? bank : 0;
        }
        return v;
    };
    m_midiPanel.set = [this](const MdMidiPanel::Values& v) {
        auto s = m_proc.midiSettings();
        s.baseChannel = v.baseChannel; s.programChange = v.programChange; s.midiOut = v.midiOut; s.pcChannel = v.pcChannel;
        s.noteTrack.fill(-1);
        for (int t = 0; t < kTracks; ++t) if (v.note[size_t(t)] >= 0) s.noteTrack[size_t(v.note[size_t(t)])] = int8_t(t);
        s.patternNoteMode = v.ptnMode;
        if (v.ptnFrom != -2) {   // CUSTOM (a project's map) stays as it is until FROM is turned
            for (auto& a : s.noteAction) if (a >= 0 && a < 128) a = -1;
            for (int n = v.ptnFrom, k = 0; v.ptnFrom >= 0 && n < 128 && k < 16; ++n)
                if (!juce::MidiMessage::isMidiNoteBlack(n)) s.noteAction[size_t(n)] = int16_t(v.ptnBank * 16 + k++);
        }
        for (auto& a : s.noteAction) if (a == MdProcessor::kStartNote || a == MdProcessor::kStopNote) a = -1;
        if (v.startNote >= 0) s.noteAction[size_t(v.startNote)] = MdProcessor::kStartNote;
        if (v.stopNote >= 0) s.noteAction[size_t(v.stopNote)] = MdProcessor::kStopNote;
        m_proc.setMidiSettings(s);
    };
    m_midiPanel.onDefault = [this] {
        auto s = MdProcessor::defaultMidiSettings();
        const auto cur = m_proc.midiSettings();
        s.baseChannel = cur.baseChannel; s.programChange = cur.programChange; s.midiOut = cur.midiOut; s.pcChannel = cur.pcChannel;
        m_proc.setMidiSettings(s);
    };
    m_midiPanel.onFromProject = [this] {
        const auto globals = m_proc.bankGlobals();
        juce::PopupMenu m;
        if (globals.empty()) m.addItem(-1, "The pattern bank's project has no globals", false);
        for (size_t i = 0; i < globals.size(); ++i) {
            const auto& g = globals[i];
            juce::String what = "GLOBAL " + juce::String(g.position + 1) + ": channel " + (g.baseChannel < 16 ? juce::String(g.baseChannel + 1) : juce::String("OFF"));
            int mapped = 0; for (int n = 0; n < 128; ++n) if (g.trackOfNote(n) >= 0) ++mapped;
            what << ", " << mapped << " notes mapped";
            m.addItem(int(i) + 1, what);
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_midiPanel), [this, globals](int r) {
            if (r <= 0 || r > int(globals.size())) return;
            m_proc.setMidiSettings(MdProcessor::fromGlobal(globals[size_t(r - 1)], m_proc.midiSettings()));
            m_midiPanel.repaint();
        });
    };
    m_songEd.getSong = [this] { return m_proc.bankSong(m_songEd.slot()); };
    m_songEd.edit = [this](const juce::String& label, const std::function<void(mnm::mddump::Song&)>& fn, int coalesce) { doEditSong(m_songEd.slot(), label, fn, coalesce); };
    m_songEd.playingRow = [this] {
        const bool song = m_proc.apvts.getRawParameterValue(seqModeId())->load() >= 0.5f;
        const int sl = juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load())));
        return song && m_proc.seqPlaying() && sl == m_songEd.slot() ? m_proc.seqSongRow() : -1;
    };
    m_songEd.defaultPattern = [this] { return editSlot(); };
    m_songEd.patternLength = [this](int p) { const auto pat = m_proc.bankPattern(p); return pat ? juce::jlimit(1, 64, int(pat->length)) : 16; };
    m_seqBar.onEditClick = [this](MdSeqBar::Part p, int page, const juce::ModifierKeys& mods) {
        if (p == MdSeqBar::PtnPrev || p == MdSeqBar::PtnNext) {   // Shift + the PTN arrows: the chain grows / shrinks
            if (!mods.isShiftDown()) return;
            auto ch = m_proc.chain();
            if (ch.empty()) ch.push_back(editSlot());
            if (p == MdSeqBar::PtnNext) ch.push_back((ch.back() + 1) % 128);
            else if (!ch.empty()) ch.pop_back();
            if (ch.size() > 16) ch.resize(16);
            seqOn();
            m_proc.setChain(ch);
            flash(ch.size() > 1 ? "CHAIN " + juce::String(int(ch.size())) : juce::String("NO CHAIN"));
            return;
        }
        const int op = mods.isAltDown() ? 2 : (mods.isCommandDown() || mods.isCtrlDown()) ? 1 : 0;   // Alt clear, Ctrl paste, Shift copy
        if (p == MdSeqBar::Pages) pageOp(op, page);
        else if (p == MdSeqBar::Trk) trackOp(op);
        else if (p == MdSeqBar::Ptn) patternOp(op);
    };
    m_seqBar.onPage = [this](int page) { m_gridPage = page; m_pagePinned = m_proc.seqPlaying(); refreshGrid(); };
    bindMasterFx(0);

    m_sample.onClick = [this] { sampleMenu(); };
    addChildComponent(m_sample);

    m_keys.onPress = [this](int t) { if (t != m_track) selectTrack(t); m_proc.auditionTrack(t); };
    m_keys.onMute = [this](int t) {
        if (auto* p = m_proc.apvts.getParameter(muteId(t))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
        timerCallback();
    };
    m_keys.onLock = [this](int t) { m_proc.setTrackLocked(t, !m_proc.trackLocked(t)); timerCallback(); };
    addAndMakeVisible(m_keys);
    addChildComponent(m_panel);
    m_engineStatus.setFont(juce::Font(juce::FontOptions(12.0f)));
    addChildComponent(m_engineStatus);
    addChildComponent(m_about);
    m_skinDialog.onChanged = [this] { skinChanged(); };
    addChildComponent(m_skinDialog);
    addChildComponent(m_missingOs);
    addChildComponent(m_drop);
    addChildComponent(m_saveDialog);

    selectTrack(0);
    const int levW = 19 * kScale, gap = 12;
    setSize(20 + levW + 8 + 3 * KnobPage::kWidth + 2 * gap,
            16 + 16 + 48 + 6 + 36 + 2 * KnobPage::kHeight + gap + gap + MdTrackKeys::kLcdH * kScale);
    timerCallback();
    startTimerHz(20);
}

MdEditor::~MdEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MdEditor::selectTrack(int t)
{
    if (t != m_track) {
        if (m_picker.isOpen()) m_picker.close(false);
        if (m_drop.isVisible()) m_drop.close();
    }
    if (t != m_track) m_heldStep = -1;
    m_track = t;
    m_keys.setSelected(t);
    bindTrackPages();
    refreshGrid();
}

int MdEditor::editSlot() const
{
    return juce::jlimit(0, 127, int(std::lround(m_proc.apvts.getRawParameterValue(patternId())->load())));
}

void MdEditor::holdStep(int step)
{
    if (step == m_heldStep) return;
    m_heldStep = step;
    const juce::String badge = step >= 0 ? "LOCK" + juce::String(step + 1) : juce::String();
    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->setBadge(step >= 0 ? badge.toRawUTF8() : nullptr);
    m_shownMachineId = -2;
    m_pagesMachineId = -2;
    rebuildSynPage();
    refreshGrid();
}

void MdEditor::bindTrackPage(one::KnobPage& page, const spec::Param* params, std::function<juce::String(int)> id, std::function<int(int)> mdParam)
{
    if (m_heldStep < 0) { page.bind(params, id); return; }
    const int t = m_track, step = m_heldStep, slot = editSlot();
    auto paramValue = [this, id](int k) {
        if (auto* a = m_proc.apvts.getParameter(id(k))) return int(std::lround(a->convertFrom0to1(a->getValue())));
        return 0;
    };
    page.bindCustom(params,
        [this, t, step, slot, mdParam, paramValue](int k) {
            const int p = mdParam(k);
            if (p >= 0)
                if (const auto pat = m_proc.bankPattern(slot)) {
                    const int row = pat->lockRow(t, p);
                    if (row >= 0 && pat->locks[row][step] <= 127) return int(pat->locks[row][step]);
                }
            return paramValue(k);
        },
        [this, t, step, slot, id, mdParam](int k, int v) {
            const int p = mdParam(k);
            if (p < 0) {   // not lockable: the parameter itself
                if (auto* a = m_proc.apvts.getParameter(id(k))) a->setValueNotifyingHost(a->convertTo0to1(float(v)));
                return;
            }
            doEdit(slot, "lock", [&](mnm::mddump::Pattern& pat) { pat.trigs[t] |= 1ull << step; pat.setLock(t, p, step, v); }, 1000 + 24 * step + p);
            refreshGrid();
        },
        [this, t, step, slot, mdParam](int k) {   // a double-click: that parameter's lock goes (the knob shows the track's value)
            const int p = mdParam(k);
            if (p >= 0) doEdit(slot, "clear lock", [&](mnm::mddump::Pattern& pat) { pat.clearLock(t, p, step); });
            refreshGrid();
        },
        [this, t, step, slot, mdParam](int k) {   // locked on this step: the value box inverted
            const int p = mdParam(k);
            if (p < 0) return false;
            const auto pat = m_proc.bankPattern(slot);
            if (!pat) return false;
            const int row = pat->lockRow(t, p);
            return row >= 0 && pat->locks[row][step] <= 127;
        });
}

void MdEditor::bindOutPage(int tab)
{
    m_outTab = tab;
    for (int k = 0; k < 8; ++k) m_out.setValuesSource(k, nullptr);
    if (tab == 0) {
        m_out.bind(kOutParams, [](int k) { return k == 0 ? masterId() : k == 1 ? velModeId() : k == 2 ? accentId() : k == 4 ? seqId() : k == 5 ? patternId() : k == 6 ? seqModeId() : k == 7 ? songId() : juce::String(); });
        m_out.setValuesSource(5, [this] { return m_ptnNamePtrs.data(); });
        return;
    }
    static const auto names = [] {
        struct N { std::array<std::string, 64> len, kit; std::array<std::string, 31> swing; std::array<const char*, 64> lenP{}, kitP{}; std::array<const char*, 31> swingP{}; } n;
        for (int i = 0; i < 64; ++i) { n.len[size_t(i)] = std::to_string(i + 1); n.kit[size_t(i)] = (i < 9 ? "0" : "") + std::to_string(i + 1); n.lenP[size_t(i)] = n.len[size_t(i)].c_str(); n.kitP[size_t(i)] = n.kit[size_t(i)].c_str(); }
        for (int i = 0; i < 31; ++i) { n.swing[size_t(i)] = std::to_string(50 + i) + "%"; n.swingP[size_t(i)] = n.swing[size_t(i)].c_str(); }
        return n;
    }();
    static const char* const kSpd[8] = {"1X", "2X", "3/4X", "3/2X", "1/2X", "1/4X", "1/8X", "3X"};   // 4-7: the plugin's extras
    static const char* const kPages[4] = {"1", "2", "3", "4"};
    const spec::Param params[8] = {readout("LEN", names.lenP.data(), 64, 15), readout("SPD", kSpd, 8), readout("SWNG", names.swingP.data(), 31), numeric("ACC", 64),
                                   readout("KIT", names.kitP.data(), 64), toggle("GRID", kSeqNames), readout("PAGE", kPages, 4), readout("PTN", kPatternNames, 128)};
    m_out.bindCustom(params,
        [this](int k) {
            const auto p = m_proc.bankPattern(editSlot());
            switch (k) {
                case 0: return p ? juce::jlimit(0, 63, int(p->length) - 1) : 15;
                case 1: return p ? int(p->doubleTempo & 7) : 0;
                case 2: return p ? juce::jlimit(0, 30, p->swingPercent() - 50) : 0;
                case 3: return p ? int(p->accentAmount) : 64;
                case 4: return p ? juce::jlimit(0, 63, int(p->kit)) : 0;
                case 5: return m_gridOn ? 1 : 0;
                case 6: return m_gridPage;
                default: return editSlot();
            }
        },
        [this](int k, int v) {
            if (k == 5) { m_gridOn = v != 0; if (!m_gridOn) holdStep(-1); refreshGrid(); return; }
            if (k == 6) { m_gridPage = v; refreshGrid(); return; }
            if (k == 7) {
                if (auto* a = m_proc.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(float(v)));
                holdStep(-1);
                refreshGrid();
                return;
            }
            static const char* const what[5] = {"length", "speed", "swing", "accent", "kit"};
            doEdit(editSlot(), what[juce::jlimit(0, 4, k)], [&](mnm::mddump::Pattern& p) {
                switch (k) {
                    case 0: p.length = uint8_t(v + 1); p.scale = uint8_t(v / 16); break;   // SCALE: the pages the length needs
                    case 1: p.doubleTempo = uint8_t(v & 7); break;
                    case 2: p.swingAmount = uint32_t(v * 16384 / 50); break;
                    case 3: p.accentAmount = uint8_t(v); break;
                    case 4: p.kit = uint8_t(v); break;
                    default: break;
                }
            });
            refreshGrid();
        });
    m_out.setValuesSource(7, [this] { return m_ptnNamePtrs.data(); });
}

void MdEditor::stepMenu(int s)
{
    const int t = m_track, slot = editSlot();
    const auto p = m_proc.bankPattern(slot);
    auto maskBit = [&](uint32_t editAll, uint64_t global, uint64_t perTrack) { return (((editAll ? global : perTrack) >> s) & 1) != 0; };
    const bool trig = p && ((p->trigs[t] >> s) & 1);
    bool locks = false;
    if (p) for (int q = 0; q < 24; ++q) { const int row = p->lockRow(t, q); if (row >= 0 && p->locks[row][s] <= 127) locks = true; }
    juce::PopupMenu m;
    m.addSectionHeader("STEP " + juce::String(s + 1) + " - TRACK " + juce::String(t + 1));
    m.addItem(1, "Trig", true, trig);
    m.addItem(2, p && p->accentEditAll ? "Accent (all tracks)" : "Accent", true, p && maskBit(p->accentEditAll, p->accent, p->accentPerTrack[t]));
    m.addItem(3, p && p->slideEditAll ? "Slide (all tracks)" : "Slide", true, p && maskBit(p->slideEditAll, p->slide, p->slidePerTrack[t]));
    m.addItem(4, p && p->swingEditAll ? "Swing (all tracks)" : "Swing", true, p && maskBit(p->swingEditAll, p->swing, p->swingPerTrack[t]));
    m.addSeparator();
    m.addItem(5, m_heldStep == s ? "Release locks hold" : "Hold for locks (turn the knobs)");
    m.addItem(6, "Clear this step's locks", locks);
    m.addSeparator();
    m.addItem(7, "Accent / slide / swing per track", true, p && !p->accentEditAll);
    juce::PopupMenu tracks;
    for (int i = 0; i < kTracks; ++i) tracks.addItem(100 + i, "T" + juce::String(i + 1), true, i == t);
    m.addSubMenu("Select track", tracks);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_keys), [this, s, t, slot](int r) {
        if (r >= 100) { selectTrack(r - 100); return; }
        if (r == 5) { holdStep(m_heldStep == s ? -1 : s); return; }
        if (r <= 0) return;
        static const char* const names[8] = {"", "trig", "accent", "slide", "swing", "", "clear locks", "per-track marks"};
        doEdit(slot, names[juce::jlimit(0, 7, r)], [&](mnm::mddump::Pattern& p) {
            auto flip = [&](uint32_t editAll, uint64_t& global, uint64_t& perTrack) { (editAll ? global : perTrack) ^= 1ull << s; };
            switch (r) {
                case 1: if ((p.trigs[t] >> s) & 1) { p.trigs[t] &= ~(1ull << s); p.clearStepLocks(t, s); } else p.trigs[t] |= 1ull << s; break;
                case 2: flip(p.accentEditAll, p.accent, p.accentPerTrack[t]); break;
                case 3: flip(p.slideEditAll, p.slide, p.slidePerTrack[t]); break;
                case 4: flip(p.swingEditAll, p.swing, p.swingPerTrack[t]); break;
                case 6: p.clearStepLocks(t, s); break;
                case 7: {   // the masks of all tracks <-> each track's own (the current marks carry over)
                    const bool perTrack = p.accentEditAll != 0;
                    if (perTrack) for (int u = 0; u < 16; ++u) { p.accentPerTrack[u] = p.accent; p.slidePerTrack[u] = p.slide; p.swingPerTrack[u] = p.swing; }
                    else { p.accent = p.accentPerTrack[t]; p.slide = p.slidePerTrack[t]; p.swing = p.swingPerTrack[t]; }
                    p.accentEditAll = p.slideEditAll = p.swingEditAll = perTrack ? 0 : 1;
                    break;
                }
                default: break;
            }
        });
        if (r == 1 && s == m_heldStep) holdStep(-1);
        refreshGrid();
    });
}

namespace {
// What each knob does (the manual's words, short), by its label; the machine's own SYNTHESIS knobs get a general line
juce::String knobTip(const juce::String& label, int page, const juce::String& family)
{
    static const std::map<juce::String, const char*> words = {
        {"AMD", "Amplitude modulation depth"}, {"AMF", "Amplitude modulation rate"}, {"EQF", "EQ frequency"}, {"EQG", "EQ gain (centre = flat)"},
        {"FLTF", "Filter base frequency"}, {"FLTW", "Filter width (127 = open)"}, {"FLTQ", "Filter resonance"}, {"SRR", "Sample rate reduction"},
        {"DIST", "Distortion"}, {"VOL", "Track volume"}, {"PAN", "Pan"}, {"DEL", "Send to the master delay"}, {"REV", "Send to the master reverb"},
        {"OUT", "Output: MAIN, or A-F (no master effects there)"}, {"TRGG", "Trig group: a trig here also trigs that track"},
        {"MUTG", "Mute group: a trig here silences that track"}, {"TRK", "The track the LFO moves"}, {"PARAM", "The parameter the LFO moves"},
        {"SHP1", "LFO shape"}, {"SHP2", "Second LFO shape (inverted)"}, {"TYPE", "FREE runs on, TRIG restarts each trig, HOLD holds each trig"},
        {"SPD", "LFO speed (tempo synced)"}, {"DEP", "LFO depth"}, {"MIX", "Blend SHP1 to SHP2"}, {"DVOL", "Delay into the reverb"},
        {"PRED", "Pre-delay"}, {"DEC", "Decay"}, {"DAMP", "Damping"}, {"HP", "High-pass"}, {"LP", "Low-pass"}, {"GATE", "Gate (127 = open)"},
        {"LEV", "Output level"}, {"TIME", "Delay time (tempo synced)"}, {"FB", "Feedback"}, {"MOD", "Modulation depth"}, {"MFRQ", "Modulation rate"},
    };
    static const std::map<juce::String, const char*> out = {
        {"VOL", "Plugin output level"}, {"VEL", "Note velocity: VOLUME or ACCENT"}, {"ACNT", "Accent amount for played notes"},
        {"SEQ", "Sequencer on / off"}, {"PTN", "The pattern"}, {"MODE", "PATTERN or SONG"}, {"SONG", "The song SONG mode plays"},
        {"LEN", "Pattern length"}, {"SPD", "Pattern speed"}, {"SWNG", "Swing amount"}, {"ACC", "Accent amount"}, {"KIT", "The pattern's kit"},
        {"GRID", "Keys as steps (G)"}, {"PAGE", "The page GRID shows"},
    };
    if (page == 0)
        if (const char* w = text::machineWords(family.toRawUTF8(), label.toRawUTF8())) return w;
    if (page >= 5)
        if (const auto it = out.find(label); it != out.end()) return it->second;
    if (const auto it = words.find(label); it != words.end()) return it->second;
    return label;
}
}

void MdEditor::setTooltipsOn(bool on)
{
    if (on && !m_tips) m_tips = std::make_unique<juce::TooltipWindow>(this, 700);
    if (!on) m_tips.reset();
}

void MdEditor::toggleGrid()
{
    m_gridOn = !m_gridOn;
    if (!m_gridOn) holdStep(-1);
    if (m_outTab == 1) m_out.pull();
    refreshGrid();
}

void MdEditor::toggleMixer()
{
    if (m_mixer.isVisible()) { m_mixer.close(); return; }
    m_midiPanel.setVisible(false);
    m_songEd.setVisible(false);
    m_mixer.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
    m_mixer.setVisible(true);
    m_mixer.toFront(true);
    m_mixer.grabKeyboardFocus();
}

void MdEditor::openMidiPanel()
{
    m_mixer.setVisible(false);
    m_midiPanel.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
    m_midiPanel.setVisible(true);
    m_midiPanel.toFront(true);
    m_midiPanel.grabKeyboardFocus();
    m_midiPanel.repaint();
}

void MdEditor::saveBankToLibrary()
{
    const auto bank = m_proc.bankDump();
    const bool fresh = m_proc.bankProjectId().isEmpty();
    const juce::String name = fresh ? "MD PATTERNS " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M") : m_proc.bankName();
    juce::String id;
    juce::StringArray changes;
    const auto r = m_lib->model().saveMdPatterns(m_proc.bankProjectId(), bank, name, "Monomodule MD", &id, &changes);
    if (r.failed()) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save Patterns", r.getErrorMessage()); return; }
    if (fresh) {
        m_proc.setBankProject(id, name);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Save Patterns", "Saved as the new project " + name + " in the library.");
    } else if (changes.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Save Patterns", "Nothing differs from " + name + " in the library: no new version.");
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Save Patterns",
                                               "Saved as a new version of " + name + ":\n" + changes.joinIntoString("\n"));
    }
}

void MdEditor::doEdit(int slot, const juce::String& label, const std::function<void(mnm::mddump::Pattern&)>& fn, int coalesce)
{
    seqOn();
    const auto now = juce::Time::currentTimeMillis();
    const bool merge = coalesce >= 0 && !m_undo.empty() && m_undo.back().slot == slot && m_lastCoalesce == coalesce && now - m_lastEditMs < 1000;
    if (!merge) {
        m_undo.push_back({slot, m_proc.bankPattern(slot), nullptr, label});
        if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_lastCoalesce = coalesce;
    m_lastEditMs = now;
    m_proc.editPattern(slot, fn);
    m_undo.back().after = m_proc.bankPattern(slot);
}

void MdEditor::doEditSong(int slot, const juce::String& label, const std::function<void(mnm::mddump::Song&)>& fn, int coalesce)
{
    const auto now = juce::Time::currentTimeMillis();
    const bool merge = coalesce >= 0 && !m_undo.empty() && m_undo.back().song == slot && m_lastCoalesce == coalesce && now - m_lastEditMs < 1000;
    if (!merge) {
        UndoStep u;
        u.song = slot; u.songBefore = m_proc.bankSong(slot); u.label = label;
        m_undo.push_back(u);
        if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_lastCoalesce = coalesce;
    m_lastEditMs = now;
    m_proc.editSong(slot, fn);
    m_undo.back().songAfter = m_proc.bankSong(slot);
    m_songEd.repaint();
}

void MdEditor::seqOn()
{
    if (auto* a = m_proc.apvts.getParameter(seqId()); a && a->getValue() < 0.5f) {   // sequencing: SEQ on (the host's play then runs it)
        a->beginChangeGesture(); a->setValueNotifyingHost(1.0f); a->endChangeGesture();
    }
}

void MdEditor::undo()
{
    if (m_undo.empty()) return;
    auto s = m_undo.back();
    m_undo.pop_back();
    if (s.song >= 0) { m_proc.setBankSong(s.song, s.songBefore); m_redo.push_back(s); m_lastCoalesce = -1; m_songEd.repaint(); return; }
    m_proc.setBankPattern(s.slot, s.before);
    m_redo.push_back(s);
    m_lastCoalesce = -1;
    refreshGrid();
    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_out}) { pg->pull(); pg->repaint(); }
}

void MdEditor::redo()
{
    if (m_redo.empty()) return;
    auto s = m_redo.back();
    m_redo.pop_back();
    if (s.song >= 0) { m_proc.setBankSong(s.song, s.songAfter); m_undo.push_back(s); m_lastCoalesce = -1; m_songEd.repaint(); return; }
    m_proc.setBankPattern(s.slot, s.after);
    m_undo.push_back(s);
    m_lastCoalesce = -1;
    refreshGrid();
    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_out}) { pg->pull(); pg->repaint(); }
}

bool MdEditor::keyPressed(const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if ((k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey) && !mods.isAnyModifierKeyDown()) {
        // the previous / next kit or sound, as the strip's arrows step (the selector used last)
        if (m_drop.isVisible()) m_drop.close();
        const int dir = k.getKeyCode() == juce::KeyPress::upKey ? -1 : 1;
        if (m_browseKits) stepKit(dir); else stepSound(dir);
        return true;
    }
    if (!mods.isAnyModifierKeyDown() && (k.getKeyCode() == 'G' || k.getKeyCode() == 'g')) { toggleGrid(); return true; }   // GRID on / off
    if (!mods.isAnyModifierKeyDown() && (k.getKeyCode() == 'M' || k.getKeyCode() == 'm')) { toggleMixer(); return true; }   // the mixer
    const int page = juce::jlimit(0, 3, m_gridPage);
    if (m_gridOn && !mods.isAnyModifierKeyDown()) {   // A / S / W: accent / slide / swing marks for all tracks <-> per track
        const int code = k.getKeyCode();
        const int f = code == 'A' || code == 'a' ? 0 : code == 'S' || code == 's' ? 1 : code == 'W' || code == 'w' ? 2 : -1;
        if (f >= 0) {   // the ACCENT / SLIDE / SWING edit window (again: back to the trigs)
            static const char* const names[3] = {"ACCENT EDIT", "SLIDE EDIT", "SWING EDIT"};
            m_markMode = m_markMode == f + 1 ? 0 : f + 1;
            flash(m_markMode ? juce::String(names[f]) : juce::String("TRIGS"));
            refreshGrid();
            return true;
        }
        if (code == juce::KeyPress::escapeKey && m_markMode) { m_markMode = 0; refreshGrid(); return true; }
    }
    if (m_gridOn && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isCommandDown() && !mods.isAltDown()) {   // all tracks <-> this track
        const int code = k.getKeyCode();
        const int f = code == 'A' || code == 'a' ? 0 : code == 'S' || code == 's' ? 1 : code == 'W' || code == 'w' ? 2 : -1;
        if (f >= 0) { togglePerTrack(f); return true; }
    }
    if (m_gridOn && (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey)) {
        const int dir = k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1;
        if (mods.isShiftDown()) { shiftTrack(dir); return true; }   // the MD's FUNCTION + LEFT / RIGHT
        const auto p = m_proc.bankPattern(editSlot());
        const int pages = p ? (juce::jlimit(1, 64, int(p->length)) + 15) / 16 : 1;
        m_gridPage = juce::jlimit(0, pages - 1, m_gridPage + dir);
        m_pagePinned = m_proc.seqPlaying();
        refreshGrid();
        return true;
    }
    if (m_heldStep >= 0 && (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey)) {   // the held step's locks
        const int t = m_track, s = m_heldStep;
        doEdit(editSlot(), "clear note locks", [&](mnm::mddump::Pattern& x) { x.clearStepLocks(t, s); });
        flash("LOCKS CLEARED");
        for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->pull();
        refreshGrid();
        return true;
    }
    if ((mods.isCommandDown() || mods.isCtrlDown()) && mods.isShiftDown()) {   // the selected track's machine
        if (k.getKeyCode() == 'C' || k.getKeyCode() == 'c') { copyMachine(); return true; }
        if (k.getKeyCode() == 'V' || k.getKeyCode() == 'v') { pasteMachine(); return true; }
        if (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) { clearMachine(); return true; }
    }
    if ((mods.isCommandDown() || mods.isCtrlDown()) && mods.isAltDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z')) { undoKit(); return true; }
    if ((mods.isCommandDown() || mods.isCtrlDown()) && !mods.isShiftDown() && (k.getKeyCode() == 'R' || k.getKeyCode() == 'r')) { reloadKit(); return true; }
    if (m_heldStep >= 0 && (mods.isCommandDown() || mods.isCtrlDown()) && (k.getKeyCode() == 'C' || k.getKeyCode() == 'c')) { copyNote(m_heldStep); return true; }
    if (m_heldStep >= 0 && (mods.isCommandDown() || mods.isCtrlDown()) && (k.getKeyCode() == 'V' || k.getKeyCode() == 'v')) { pasteNote(m_heldStep); return true; }
    if ((k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) && m_gridOn) { pageOp(2, page); return true; }
    if (!mods.isCommandDown() && !mods.isCtrlDown()) return false;
    const int code = k.getKeyCode();
    if (code == 'C' || code == 'c') { pageOp(0, page); return true; }
    if (code == 'V' || code == 'v') { pageOp(1, page); return true; }
    if (code == 'D' || code == 'd') { doublePattern(); return true; }
    if ((code == 'Z' || code == 'z') && mods.isShiftDown()) { redo(); return true; }
    if (code == 'Z' || code == 'z') { undo(); return true; }
    if (code == 'Y' || code == 'y') { redo(); return true; }
    return false;
}

// DEL PAGE: the page goes, the pages after it move up and the pattern is 16 steps shorter (one page: its steps cleared)
void MdEditor::deletePage(int page)
{
    const int slot = editSlot();
    const auto cur = m_proc.bankPattern(slot);
    if (!cur) { flash("EMPTY"); return; }
    const int pages = (juce::jlimit(1, 64, int(cur->length)) + 15) / 16;
    if (page >= pages) return;
    doEdit(slot, "delete page", [&](mnm::mddump::Pattern& x) {
        if (pages == 1) { x.clearSteps(0, 16, -1); x.swing = cur->swing; return; }
        const auto src = x;
        for (int k = page; k < pages - 1; ++k) x.copySteps(src, (k + 1) * 16, k * 16, 16, -1, -1);
        x.clearSteps((pages - 1) * 16, 16, -1);
        x.length = uint8_t(juce::jmax(16, int(x.length) - 16));
        x.scale = uint8_t((x.length - 1) / 16);
    });
    m_gridPage = juce::jmin(page, pages - 2 < 0 ? 0 : pages - 2);
    flash(pages == 1 ? "CLEARED P1" : "DEL P" + juce::String(page + 1));
    refreshGrid();
}

void MdEditor::shiftTrack(int dir)
{
    const int slot = editSlot(), t = m_track;
    const auto cur = m_proc.bankPattern(slot);
    if (!cur) { flash("EMPTY"); return; }
    const int len = juce::jlimit(1, 64, int(cur->length));
    doEdit(slot, dir > 0 ? "shift track right" : "shift track left", [&](mnm::mddump::Pattern& x) {
        const auto src = x;
        for (int s = 0; s < len; ++s) x.copySteps(src, s, (s + dir + len) % len, 1, t, t);
    }, 30000 + t);
    flash(dir > 0 ? "T" + juce::String(t + 1) + " >>" : "<< T" + juce::String(t + 1));
    refreshGrid();
}

void MdEditor::copyNote(int step)
{
    const auto p = m_proc.bankPattern(editSlot());
    if (!p || !((p->trigs[m_track] >> step) & 1)) { flash("NO NOTE"); return; }
    m_clip = {4, false, m_track, step, *p};
    flash("COPY NOTE " + juce::String(step + 1));
}

void MdEditor::pasteNote(int step)
{
    if (m_clip.kind != 4) { flash("NO NOTE"); return; }
    const auto clip = m_clip;
    const int t = m_track;
    doEdit(editSlot(), "paste note", [&](mnm::mddump::Pattern& x) { x.copySteps(clip.pat, clip.page, step, 1, clip.track, t); });
    flash("PASTE NOTE " + juce::String(step + 1));
    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->pull();
    refreshGrid();
}

void MdEditor::applyMuteQueue()
{
    for (int t = 0; t < kTracks; ++t) if ((m_muteQueue >> t) & 1) toggleMute(t);
    m_muteQueue = 0;
    m_keys.setMuteQueue(0);
}

void MdEditor::undoKit()
{
    if (!m_kitUndo.valid) { flash("NO KIT UNDO"); return; }
    const KitUndo back = m_kitUndo;
    m_kitUndo = {true, m_proc.captureMdKit(), m_proc.loadedKitKey(), m_proc.kitName()};   // undo again = redo
    m_proc.loadMdKit(back.key, back.kit, back.name);
    flash("KIT UNDONE");
    selectTrack(m_track);
    timerCallback();
}

void MdEditor::reloadKit()
{
    const auto key = m_proc.loadedKitKey();
    mnm::mddump::Kit kit;
    if (key.isEmpty() || !m_lib->loadKit(key, kit)) { flash("NO SAVED KIT"); return; }
    m_kitUndo = {true, m_proc.captureMdKit(), key, m_proc.kitName()};
    m_proc.loadMdKit(key, kit, m_proc.kitName());
    flash("KIT RELOADED");
    selectTrack(m_track);
    timerCallback();
}

void MdEditor::copyMachine()
{
    m_soundClip = m_proc.captureSound(m_track);
    m_soundClipName = m_proc.loadedSoundName(m_track).isNotEmpty() ? m_proc.loadedSoundName(m_track) : "T" + juce::String(m_track + 1) + " COPY";
    flash("COPY T" + juce::String(m_track + 1));
}

void MdEditor::pasteMachine()
{
    if (!m_soundClip) { flash("NOTHING COPIED"); return; }
    if (!m_proc.loadSound(m_track, {}, *m_soundClip, m_soundClipName)) { flash("NO SUCH MACHINE"); return; }
    flash("PASTE T" + juce::String(m_track + 1));
    bindTrackPages();
    timerCallback();
}

void MdEditor::clearMachine()
{
    if (auto* p = m_proc.apvts.getParameter(machineId(m_track))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(float(machineIndexOf(0))));   // GND---: the empty machine
        p->endChangeGesture();
    }
    flash("CLEAR T" + juce::String(m_track + 1));
    timerCallback();
}

void MdEditor::flipStepFlag(int step, int f)
{
    static const char* const names[3] = {"accent", "slide", "swing"};
    const int t = m_track;
    doEdit(editSlot(), names[f], [&](mnm::mddump::Pattern& p) {
        uint32_t all[3] = {p.accentEditAll, p.slideEditAll, p.swingEditAll};
        uint64_t* global[3] = {&p.accent, &p.slide, &p.swing};
        uint64_t* own[3] = {&p.accentPerTrack[t], &p.slidePerTrack[t], &p.swingPerTrack[t]};
        *(all[f] ? global[f] : own[f]) ^= 1ull << step;
    });
    refreshGrid();
}

void MdEditor::togglePerTrack(int f)
{
    static const char* const names[3] = {"ACCENT", "SLIDE", "SWING"};
    bool nowAll = true;
    const int t = m_track;
    doEdit(editSlot(), "per-track marks", [&](mnm::mddump::Pattern& p) {
        uint32_t* all[3] = {&p.accentEditAll, &p.slideEditAll, &p.swingEditAll};
        uint64_t* global[3] = {&p.accent, &p.slide, &p.swing};
        uint64_t* own[3] = {p.accentPerTrack, p.slidePerTrack, p.swingPerTrack};
        if (*all[f]) { for (int u = 0; u < 16; ++u) own[f][u] = *global[f]; *all[f] = 0; nowAll = false; }   // the marks carry over
        else { *global[f] = own[f][t]; *all[f] = 1; nowAll = true; }
    });
    flash(juce::String(names[f]) + (nowAll ? ": ALL" : ": T" + juce::String(t + 1)));
    refreshGrid();
}

void MdEditor::toggleMute(int t)
{
    if (auto* p = m_proc.apvts.getParameter(muteId(t))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
    refreshGrid();
}

void MdEditor::pageOp(int op, int page)
{
    const int slot = editSlot();
    const auto p = m_proc.bankPattern(slot);
    const juce::String pg = "P" + juce::String(page + 1);
    if (op == 0) {
        if (!p) { flash("EMPTY"); return; }
        m_clip = {1, true, m_track, page, *p};
        flash("COPY " + pg);
        return;
    }
    if (op == 1 && m_clip.kind != 1) { flash("NO PAGE"); return; }
    const auto clip = m_clip;
    doEdit(slot, op == 1 ? "paste page" : "clear page", [&](mnm::mddump::Pattern& x) {
        if (op == 1) {
            x.copySteps(clip.pat, clip.page * 16, page * 16, 16, clip.all ? -1 : clip.track, m_track);
            if (x.length < (page + 1) * 16) { x.length = uint8_t((page + 1) * 16); x.scale = uint8_t(page); }
        } else {
            x.clearSteps(page * 16, 16, -1);
        }
    });
    if (op == 1) m_gridPage = page;
    holdStep(-1);
    flash(op == 1 ? "PASTE " + pg : "CLEAR " + pg);
}

void MdEditor::trackOp(int op)
{
    const int slot = editSlot(), t = m_track;
    const auto p = m_proc.bankPattern(slot);
    const juce::String tr = "T" + juce::String(t + 1);
    if (op == 0) {
        if (!p) { flash("EMPTY"); return; }
        m_clip = {2, false, t, 0, *p};
        flash("COPY " + tr);
        return;
    }
    if (op == 1 && m_clip.kind != 2) { flash("NO TRACK"); return; }
    const auto clip = m_clip;
    doEdit(slot, op == 1 ? "paste track" : "clear track", [&](mnm::mddump::Pattern& x) {
        if (op == 1) x.copySteps(clip.pat, 0, 0, 64, clip.track, t);
        else x.clearSteps(0, 64, t);
    });
    holdStep(-1);
    flash(op == 1 ? "PASTE " + tr : "CLEAR " + tr);
}

void MdEditor::patternOp(int op)
{
    const int slot = editSlot();
    const auto p = m_proc.bankPattern(slot);
    const juce::String pn = kPatternNames[slot];
    if (op == 0) {
        if (!p) { flash("EMPTY"); return; }
        m_clip = {3, true, 0, 0, *p};
        flash("COPY " + pn);
        return;
    }
    if (op == 1 && m_clip.kind != 3) { flash("NO PTN"); return; }
    const auto clip = m_clip;
    doEdit(slot, op == 1 ? "paste pattern" : "clear pattern", [&](mnm::mddump::Pattern& x) {
        if (op == 1) { const int pos = x.position; x = clip.pat; x.position = pos; }
        else x.clearSteps(0, 64, -1);
    });
    holdStep(-1);
    flash(op == 1 ? "PASTE " + pn : "CLEAR " + pn);
}

void MdEditor::doublePattern()
{
    const int slot = editSlot();
    const auto p = m_proc.bankPattern(slot);
    const int len = p ? juce::jlimit(1, 64, int(p->length)) : 16;
    if (!p) { flash("EMPTY"); return; }
    if (len > 32) { flash("64 = FULL"); return; }
    doEdit(slot, "double", [&](mnm::mddump::Pattern& x) {
        const auto copy = x;
        x.copySteps(copy, 0, len, len, -1, 0);
        x.length = uint8_t(juce::jmin(64, 2 * len));
        x.scale = uint8_t((x.length - 1) / 16);
    });
    flash("X2 = " + juce::String(2 * len));
}

void MdEditor::openSongEditor()
{
    m_mixer.setVisible(false);
    m_songEd.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
    m_songEd.open(juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load()))));
    m_songEd.grabKeyboardFocus();
}

void MdEditor::editMenu()
{
    const int slot = editSlot(), t = m_track;
    const auto p = m_proc.bankPattern(slot);
    const int len = p ? juce::jlimit(1, 64, int(p->length)) : 16;
    const int page = juce::jlimit(0, 3, m_gridPage);
    const juce::String pg = "page " + juce::String(page + 1), tr = "T" + juce::String(t + 1), pn = kPatternNames[slot];
    juce::String what;
    if (m_clip.kind == 1) what = " (page " + juce::String(m_clip.page + 1) + (m_clip.all ? ", all tracks)" : ", T" + juce::String(m_clip.track + 1) + ")");
    juce::PopupMenu m;
    m.addItem(20, m_undo.empty() ? juce::String("Undo") : "Undo " + m_undo.back().label + "  (Ctrl+Z)", !m_undo.empty());
    m.addItem(21, m_redo.empty() ? juce::String("Redo") : "Redo " + m_redo.back().label + "  (Ctrl+Shift+Z)", !m_redo.empty());
    m.addSeparator();
    m.addSectionHeader(pn + "  -  " + pg.toUpperCase() + "  -  " + tr);
    m.addItem(1, "Copy " + pg + " (all tracks)", p != nullptr);
    m.addItem(2, "Copy " + pg + " of " + tr, p != nullptr);
    m.addItem(3, "Paste onto " + pg + what, m_clip.kind == 1);
    m.addItem(4, "Clear " + pg + " (all tracks)", p != nullptr);
    m.addItem(5, "Clear " + pg + " of " + tr, p != nullptr);
    m.addSeparator();
    m.addItem(6, "Copy track " + tr, p != nullptr);
    m.addItem(7, "Paste track onto " + tr + (m_clip.kind == 2 ? " (T" + juce::String(m_clip.track + 1) + ")" : juce::String()), m_clip.kind == 2);
    m.addItem(8, "Clear track " + tr, p != nullptr);
    m.addSeparator();
    m.addItem(9, "Copy pattern " + pn, p != nullptr);
    m.addItem(10, "Paste pattern onto " + pn, m_clip.kind == 3);
    m.addItem(11, "Clear pattern " + pn + " (its steps; the settings stay)", p != nullptr);
    m.addSeparator();
    {
        const int sl = juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load())));
        m.addItem(31, "Edit song " + juce::String(sl + 1).paddedLeft('0', 2) + " (the SONG knob's)...");
    }
    m.addItem(30, m_proc.bankProjectId().isNotEmpty() ? "Save patterns and songs to the library (" + m_proc.bankName() + ": a new version)"
                                                      : juce::String("Save patterns and songs to the library (a new project)"),
              m_proc.bankHasPattern(slot) || m_proc.bankName().isNotEmpty());
    m.addSeparator();
    m.addItem(12, "Double: steps 1-" + juce::String(len) + " again after themselves (length " + juce::String(juce::jmin(64, 2 * len)) + ")", p != nullptr && len <= 32);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_seqBar), [this, slot, t, page, p, len](int r) {
        if (r <= 0) return;
        if (r == 20) { undo(); return; }
        if (r == 30) { saveBankToLibrary(); return; }
        if (r == 31) {
            m_songEd.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
            m_songEd.open(juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load()))));
            m_songEd.grabKeyboardFocus();
            return;
        }
        if (r == 21) { redo(); return; }
        if (r == 1 || r == 2) { m_clip = {1, r == 1, t, page, *p}; return; }
        if (r == 6) { m_clip = {2, false, t, 0, *p}; return; }
        if (r == 9) { m_clip = {3, true, 0, 0, *p}; return; }
        const auto clip = m_clip;
        static const char* const names[13] = {"", "", "", "paste page", "clear page", "clear page", "", "paste track", "clear track", "", "paste pattern", "clear pattern", "double"};
        doEdit(slot, names[juce::jlimit(0, 12, r)], [&](mnm::mddump::Pattern& x) {
            switch (r) {
                case 3:   // the copied page onto this one (the pattern grows to reach it)
                    x.copySteps(clip.pat, clip.page * 16, page * 16, 16, clip.all ? -1 : clip.track, t);
                    if (x.length < (page + 1) * 16) { x.length = uint8_t((page + 1) * 16); x.scale = uint8_t(page); }
                    break;
                case 4: x.clearSteps(page * 16, 16, -1); break;
                case 5: x.clearSteps(page * 16, 16, t); break;
                case 7: x.copySteps(clip.pat, 0, 0, 64, clip.track, t); break;
                case 8: x.clearSteps(0, 64, t); break;
                case 10: { const int pos = x.position; x = clip.pat; x.position = pos; break; }
                case 11: x.clearSteps(0, 64, -1); break;
                case 12: {
                    const auto copy = x;
                    x.copySteps(copy, 0, len, len, -1, 0);
                    x.length = uint8_t(juce::jmin(64, 2 * len));
                    x.scale = uint8_t((x.length - 1) / 16);
                    break;
                }
                default: break;
            }
        });
        if (r == 3) m_gridPage = page;
        holdStep(-1);
        refreshGrid();
    });
}

void MdEditor::refreshGrid()
{
    {   // GRID follows the playing page of the pattern it shows (unless a page was picked while playing)
        const bool playingHere = m_proc.seqPlaying() && m_proc.seqPattern() == editSlot();
        if (!m_proc.seqPlaying()) m_pagePinned = false;
        else if (playingHere && !m_pagePinned && m_heldStep < 0 && m_proc.seqStep() >= 0) m_gridPage = m_proc.seqStep() / 16;
    }
    {   // the bar
        MdSeqBar::State s;
        const int slot = editSlot();
        const auto p = m_proc.bankPattern(slot);
        s.playing = m_proc.seqPlaying() || m_proc.internalPlay();
        s.hostPlaying = m_proc.hostPlaying();
        s.grid = m_gridOn;
        s.mark = m_gridOn ? m_markMode : 0;
        s.beat = m_proc.beatPhase() < 0.5f;
        {   // the chain, as the PTN box shows it
            const auto ch = m_proc.chain();
            juce::String c;
            for (size_t i = 0; i < ch.size(); ++i) c << (i ? ">" : "") << kPatternNames[juce::jlimit(0, 127, ch[i])];
            s.chain = c;
        }
        s.mix = m_mixer.isVisible();
        s.track = m_track;
        s.muted = m_proc.apvts.getRawParameterValue(muteId(m_track))->load() >= 0.5f;
        s.rec = m_proc.recordArmed();
        s.recording = m_proc.recording();
        const int mi = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(m_track))->load())));
        s.machine = shortOf(mi) == "---" ? familyOf(mi) : shortOf(mi);
        s.pattern = slot;
        s.empty = p == nullptr;
        s.length = p ? juce::jlimit(1, 64, int(p->length)) : 16;
        s.page = juce::jlimit(0, (s.length - 1) / 16, m_gridPage);
        s.step = m_proc.seqPlaying() && m_proc.seqPattern() == slot ? m_proc.seqStep() : -1;
        s.row = m_proc.seqPlaying() ? m_proc.seqSongRow() : -1;
        s.seqOff = m_proc.apvts.getRawParameterValue(seqId())->load() < 0.5f;
        if (m_flash.isNotEmpty() && juce::Time::getMillisecondCounter() > m_flashUntil) m_flash.clear();
        s.flash = m_flash;
        m_seqBar.setState(s);
    }
    MdTrackKeys::Grid g;
    g.on = m_gridOn;
    if (!m_gridOn) m_markMode = 0;
    g.mark = m_markMode;
    if (m_gridOn) {
        const int slot = editSlot(), t = m_track;
        const auto p = m_proc.bankPattern(slot);
        g.length = p ? juce::jlimit(1, 64, int(p->length)) : 16;
        m_gridPage = juce::jlimit(0, (g.length - 1) / 16, m_gridPage);
        g.page = m_gridPage;
        g.held = m_heldStep;
        if (p) {
            g.trigs = p->trigs[t];
            g.accent = p->accentEditAll ? p->accent : p->accentPerTrack[t];
            g.slide = p->slideEditAll ? p->slide : p->slidePerTrack[t];
            g.swing = p->swingEditAll ? p->swing : p->swingPerTrack[t];
            for (int q = 0; q < 24; ++q) {
                const int row = p->lockRow(t, q);
                if (row < 0) continue;
                for (int s = 0; s < 64; ++s) if (p->locks[row][s] <= 127) g.locks |= 1ull << s;
            }
        }
        if (m_proc.seqPattern() == slot) g.play = m_proc.seqStep();
    }
    m_keys.setGrid(g);
}

// Binds the LEV column and the track pages to the selected track
void MdEditor::bindTrackPages()
{
    const int t = m_track;
    m_levelAttach.reset();
    m_levelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, levelId(t), m_level);
    m_level.setDoubleClickReturnValue(true, 100.0);
    m_shownMachineId = -2; m_pagesMachineId = -2;
    rebuildSynPage();
}

// The SYNTHESIS page follows the machine: its labels and defaults from the OS file's descriptor
void MdEditor::rebuildSynPage()
{
    const int id = m_proc.machineIdOf(m_track);
    int shown = id;   // CTR-8P: its knob labels also follow the TRK / PAR assignments
    if (id == kCtr8p)
        for (int p = 8; p < 24; ++p) shown = (shown * 131 + int(std::lround(m_proc.apvts.getRawParameterValue(trackParamId(m_track, p))->load()))) & 0x7FFFFF;
    if (shown == m_shownMachineId) return;
    m_shownMachineId = shown;
    const auto* m = m_proc.machineInfo(id);
    for (int k = 0; k < 8; ++k) {
        m_synLabels[size_t(k)] = m ? m->labels[size_t(k)] : std::string();
        auto& p = m_synParams[size_t(k)];
        p = m_synLabels[size_t(k)].empty() ? blank() : numeric("", m ? m->defaults[size_t(k)] : 0);
        p.label = m_synLabels[size_t(k)].c_str();
    }
    if (isMidMachine(id)) {   // NOTE as a note, N2 N3 PB centred
        m_synParams[0] = named(m_synParams[0].label, 0, 64);
        for (int k : {1, 2, 5}) { const auto* l = m_synParams[size_t(k)].label; m_synParams[size_t(k)] = bipolar(l); }
    }
    if (id == kCtr8p)   // P1..P8 show the parameter they turn
        for (int k = 0; k < 8; ++k) {
            const int tt = juce::jmin(15, int(std::lround(m_proc.apvts.getRawParameterValue(trackParamId(m_track, 8 + 2 * k))->load())));
            const int tp = juce::jmin(23, int(std::lround(m_proc.apvts.getRawParameterValue(trackParamId(m_track, 9 + 2 * k))->load())));
            static const char* const code[24] = {"S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "AMD", "AMF", "EQF", "EQG",
                                                 "FF", "FW", "FQ", "SRR", "DIS", "VOL", "PAN", "DEL", "REV", "LS", "LD", "LM"};
            m_synLabels[size_t(k)] = (juce::String(tt + 1) + code[tp]).toStdString();   // "5S1" = track 5 SYN1, "16VOL"
            m_synParams[size_t(k)].label = m_synLabels[size_t(k)].c_str();
        }
    const int t = m_track;
    bindTrackPage(m_syn, m_synParams.data(), [t](int k) { return knobId(t, k); }, [](int k) { return k; });
    // the other pages follow the machine family (rebound on a machine change only: a turn of an 8P TRK / PAR is not one)
    if (id == m_pagesMachineId) return;
    m_pagesMachineId = id;
    const bool mid = isMidMachine(id), master = ctrMasterFx(id) >= 0;
    bindTrackPage(m_fx, mid ? kMidFx : id == kCtr8p ? kCtr8pFx : master ? kBlankPage : kFxParams, [t](int k) { return fxId(t, k); }, [](int k) { return 8 + k; });
    bindTrackPage(m_routing, mid ? kMidRouting : id == kCtr8p ? kCtr8pRouting : master ? kGroupsOnly : id == kCtrAll ? kCtrAllRouting : kRoutingParams, [t](int k) {
        static juce::String (* const ids[8])(int) = {distId, volId, panId, delId, revId, routeId, trigGroupId, muteGroupId};
        return ids[k](t);
    }, [](int k) { return k < 5 ? 16 + k : -1; });
    bindTrackPage(m_lfo, id == kCtr8p ? kCtr8pLfo : id == kCtrAll ? kCtrAllLfo : kLfoParams, [t](int k) { return lfoId(t, k); }, [](int k) { return k >= 5 ? 21 + (k - 5) : -1; });
}

void MdEditor::bindMasterFx(int fx)
{
    m_masterTab = fx;
    std::array<spec::Param, 8> params{};
    for (int k = 0; k < 8; ++k) params[size_t(k)] = masterParam(fx, k);
    m_master.bind(params.data(), [fx](int k) { return masterFxId(fx, k); });
}

void MdEditor::setMachine(int index)
{
    if (auto* p = m_proc.apvts.getParameter(machineId(m_track))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(float(index)));
        p->endChangeGesture();
    }
    timerCallback();
}

void MdEditor::showMenu()
{
    juce::PopupMenu skins;
    const auto current = skin::current().preset;
    for (int i = 0; i < skin::kNumPresets; ++i)
        skins.addItem(100 + i, i == int(skin::Preset::Custom) ? juce::String("CUSTOM...") : juce::String(skin::kPresetNames[i]), true, int(current) == i);
    juce::PopupMenu m;
    m.addItem(5, "INIT KIT");
    m.addItem(1, "SELECT MACHINEDRUM OS FILE...");
    m.addItem(6, "CLEAR OS FILE SELECTION", m_proc.firmwarePath().isNotEmpty());
    m.addSeparator();
    m.addItem(2, "IMPORT MACHINEDRUM .SYX...");
    m.addItem(9, "LIBRARY", true, m_panel.isOpen());
    m.addSeparator();
    juce::PopupMenu outputs;   // as Monomodule Six's Plugin Outputs
    const int outMode = int(std::lround(m_proc.apvts.getRawParameterValue(outputModeId())->load()));
    outputs.addItem(40, "HARDWARE (MAIN A/B, OUT C/D, OUT E/F)", true, outMode == int(OutputMode::Hardware));
    outputs.addItem(41, "PER TRACK (TRACK 1-16, WHERE ENABLED)", true, outMode == int(OutputMode::Tracks));
    m.addSubMenu("PLUGIN OUTPUTS", outputs);
    m.addItem(16, "MIXER  (M)", true, m_mixer.isVisible());
    m.addItem(17, "TOOLTIPS", true, m_tips != nullptr);
    m.addSeparator();
    m.addItem(18, "UNDO KIT CHANGE  (CTRL+ALT+Z)", m_kitUndo.valid);
    m.addItem(19, "RELOAD KIT  (CTRL+R)", m_proc.loadedKitKey().isNotEmpty());
    m.addItem(20, "COPY T" + juce::String(m_track + 1) + " MACHINE  (CTRL+SHIFT+C)");
    m.addItem(21, "PASTE MACHINE ONTO T" + juce::String(m_track + 1) + "  (CTRL+SHIFT+V)", m_soundClip.has_value());
    m.addItem(22, "CLEAR T" + juce::String(m_track + 1) + " MACHINE  (CTRL+SHIFT+DEL)");
    m.addItem(11, "MIDI SETTINGS...");
    m.addSeparator();
    {
        const int sl = juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load())));
        m.addItem(12, "EDIT SONG " + juce::String(sl + 1).paddedLeft('0', 2) + "...");
    }
    m.addItem(13, m_proc.bankProjectId().isNotEmpty() ? "SAVE PATTERNS + SONGS TO LIBRARY (" + m_proc.bankName().toUpperCase() + ")"
                                                      : juce::String("SAVE PATTERNS + SONGS TO LIBRARY (NEW PROJECT)"));
    m.addItem(14, m_undo.empty() ? juce::String("UNDO") : "UNDO " + m_undo.back().label.toUpperCase() + "  (CTRL+Z)", !m_undo.empty());
    m.addItem(15, m_redo.empty() ? juce::String("REDO") : "REDO " + m_redo.back().label.toUpperCase() + "  (CTRL+SHIFT+Z)", !m_redo.empty());
    m.addItem(10, "SYNC BPM TO HOST", true, m_bpmSync.getToggleState());
    m.addSubMenu("SKIN", skins);
    m.addItem(7, "SHOW ENGINE STATUS", true, m_showStatus);
    m.addItem(4, juce::String("SAMPLE MEMORY USED ") + juce::String(int(std::lround(m_proc.sampleMemoryUsed() * 100.0))) + "%", false);
    m.addSeparator();
    m.addItem(8, "PLUGIN INFO...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_menuButton), [this](int r) {
        if (r == 11) { openMidiPanel(); return; }
        if (r == 16) { toggleMixer(); return; }
        if (r == 18) { undoKit(); return; }
        if (r == 19) { reloadKit(); return; }
        if (r == 20) { copyMachine(); return; }
        if (r == 21) { pasteMachine(); return; }
        if (r == 22) { clearMachine(); return; }
        if (r == 17) { setTooltipsOn(m_tips == nullptr); saveSharedSetting("tooltips", m_tips ? "1" : "0"); return; }
        if (r == 12) { openSongEditor(); return; }
        if (r == 13) { saveBankToLibrary(); return; }
        if (r == 14) { undo(); return; }
        if (r == 15) { redo(); return; }
        if (r == 1) chooseOsFile();
        else if (r == 2) importSyx();
        else if (r == 5) { m_proc.initKit(); selectTrack(m_track); timerCallback(); }
        else if (r == 6) { m_proc.clearFirmware(); timerCallback(); }
        else if (r == 7) { m_showStatus = !m_showStatus; resized(); timerCallback(); }
        else if (r == 8) { m_about.setVisible(true); m_about.toFront(false); }
        else if (r == 9) { if (m_panel.isOpen()) m_panel.close(); else m_panel.open(); }
        else if (r == 10) m_bpmSync.setToggleState(!m_bpmSync.getToggleState(), juce::sendNotificationSync);
        else if (r == 40 || r == 41) { if (auto* p = m_proc.apvts.getParameter(outputModeId())) p->setValueNotifyingHost(p->convertTo0to1(float(r - 40))); }
        else if (r == 100 + int(skin::Preset::Custom)) { m_skinDialog.setBounds(getLocalBounds()); m_skinDialog.open(); }
        else if (r >= 100) applySkin(skin::presetSkin(skin::Preset(r - 100)));
    });
}

void MdEditor::skinChanged()
{
    m_lnf.applySkin();
    sendLookAndFeelChange();
    repaint();
}

void MdEditor::applySkin(const skin::Skin& s)
{
    skin::apply(s);
    skin::save(s);
    m_lnf.applySkin();
    sendLookAndFeelChange();
    repaint();
}

void MdEditor::chooseOsFile()
{
    const juce::File current(m_proc.firmwarePath());
    m_chooser = std::make_unique<juce::FileChooser>("Select the Machinedrum OS .syx (Elektron_SPS1-1UW_OS1.63.syx)",
        current.existsAsFile() ? current.getParentDirectory() : juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) { m_proc.setFirmwarePath(f.getFullPathName()); m_shownMachineId = -2; timerCallback(); }
    });
}

// Machinedrum sysex files (kits or whole dumps) into the kit library, then the list to pick from
void MdEditor::importSyx()
{
    m_chooser = std::make_unique<juce::FileChooser>("Import Machinedrum kits (.syx kit or dump)",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& fc) { importFiles(fc.getResults()); });
}

void MdEditor::importFiles(const juce::Array<juce::File>& files)
{
    juce::StringArray problems;
    juce::String lastId;
    int imported = 0;
    for (const auto& f : files) {
        if (!f.existsAsFile()) continue;
        juce::String id;
        const auto r = m_lib->importSyx(f, &id);
        if (r.failed()) problems.add(r.getErrorMessage());
        else { lastId = id; ++imported; }
    }
    if (!problems.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import", problems.joinIntoString("\n"));
    if (imported == 0) return;
    // a file of a single kit is what the user wants to hear: load it; a dump opens the list
    std::vector<KitEntry> fromLast;
    for (const auto& k : m_lib->kits()) if (k.sourceId == lastId) fromLast.push_back(k);
    if (imported == 1 && fromLast.size() == 1) loadKit(fromLast.front());
    else openKitList();
}

bool MdEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files) if (f.endsWithIgnoreCase(".syx") || mnm::library::isMdTransferFile(f) || MdProcessor::isAudioFile(f)) return true;
    return false;
}

juce::Rectangle<int> MdEditor::dropFrame(const juce::StringArray& files, int x, int y) const
{
    const auto pages = juce::Rectangle<int>(m_syn.getX(), m_syn.getY() + m_syn.overhangPx(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY() - m_syn.overhangPx());
    bool sound = false;
    for (const auto& f : files) sound = sound || f.endsWithIgnoreCase(".mdsound") || MdProcessor::isAudioFile(f);
    if (sound) {   // a sound or a sample: the track key it lands on (the one under the pointer, else the selected track's)
        const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
        const auto key = m_keys.keyBounds(over >= 0 ? over : m_track);
        return (key * kScale + m_keys.getPosition()).expanded(kScale);
    }
    return pages;
}

void MdEditor::fileDragEnter(const juce::StringArray& files, int x, int y) { m_dragOver = true; m_dropRect = dropFrame(files, x, y); repaint(); }
void MdEditor::fileDragMove(const juce::StringArray& files, int x, int y) { const auto r = dropFrame(files, x, y); if (r != m_dropRect) { m_dropRect = r; repaint(); } }
void MdEditor::fileDragExit(const juce::StringArray&) { m_dragOver = false; repaint(); }

void MdEditor::paintOverChildren(juce::Graphics& g)
{
    if (!m_dragOver || m_dropRect.isEmpty()) return;
    g.setColour(lcd::ink);
    const auto r = m_dropRect;
    for (int x = r.getX(); x < r.getRight(); x += 2 * kScale) {
        g.fillRect(x, r.getY(), kScale, 2 * kScale);
        g.fillRect(x, r.getBottom() - 2 * kScale, kScale, 2 * kScale);
    }
    for (int y = r.getY(); y < r.getBottom(); y += 2 * kScale) {
        g.fillRect(r.getX(), y, 2 * kScale, kScale);
        g.fillRect(r.getRight() - 2 * kScale, y, 2 * kScale, kScale);
    }
}

void MdEditor::mouseDown(const juce::MouseEvent& e)
{
    auto* c = e.eventComponent;
    if (m_drop.isVisible() && c != &m_drop && !m_drop.isParentOf(c) && c != &m_strip) m_drop.close();
    if (m_picker.isOpen() && c != &m_picker && !m_picker.isParentOf(c) && c != &m_machineBlock) m_picker.close();
}

void MdEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    m_dragOver = false;
    repaint();
    juce::Array<juce::File> syx, audio;
    juce::StringArray errors;
    for (const auto& path : files) {
        const juce::File f(path);
        if (path.endsWithIgnoreCase(".syx")) { syx.add(f); continue; }
        if (MdProcessor::isAudioFile(path)) { audio.add(f); continue; }
        mnm::library::MdTransferPayload p;
        if (!mnm::library::readMdTransferFile(f, p)) { errors.add(f.getFileName() + " is not a Machinedrum sound or kit file."); continue; }
        if (p.isKit) {
            const int emptied = m_proc.loadMdKit(juce::String(mnm::mdcatalog::Catalog::kitHash(p.kit)), p.kit, p.name.isNotEmpty() ? p.name.toUpperCase() : juce::String(p.kit.name));
            if (emptied > 0) errors.add(juce::String(emptied) + " track(s) of " + f.getFileName() + " use machines Monomodule MD does not have; they were left empty.");
            selectTrack(m_track);
        } else {
            const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
            const int t = over >= 0 ? over : m_track;
            if (!m_proc.loadSound(t, juce::String(mnm::mdcatalog::Catalog::soundHash(p.sound)), p.sound, p.name.toUpperCase()))
                errors.add(f.getFileName() + " uses a machine Monomodule MD does not have.");
            if (t != m_track) selectTrack(t); else bindTrackPages();
        }
    }
    if (!syx.isEmpty()) importFiles(syx);
    // samples: the first onto the track it was dropped on (its ROM slot, or the first empty ROM slot with the track
    // switched to that ROM machine); any more into the next empty ROM slots
    for (int n = 0; n < audio.size(); ++n) {
        const auto& f = audio.getReference(n);
        int slot = -1;
        if (n == 0) {
            const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
            const int t = over >= 0 ? over : m_track;
            const int id = m_proc.machineIdOf(t);
            slot = isRomMachine(id) ? romSlotOf(id) : m_proc.firstEmptyRomSlot();
            if (slot < 0) { errors.add("Every ROM slot holds a sample: clear one (or drop onto a ROM track to replace its sample)."); break; }
            const auto err = m_proc.loadSample(slot, f);
            if (err.isNotEmpty()) { errors.add(f.getFileName() + ": " + err); continue; }
            if (!isRomMachine(id))
                if (auto* p = m_proc.apvts.getParameter(machineId(t))) p->setValueNotifyingHost(p->convertTo0to1(float(machineIndexOf(slot + 128))));
            if (t != m_track) selectTrack(t);
        } else {
            slot = m_proc.firstEmptyRomSlot();
            if (slot < 0) { errors.add("No empty ROM slot left for " + f.getFileName() + " and the rest."); break; }
            const auto err = m_proc.loadSample(slot, f);
            if (err.isNotEmpty()) errors.add(f.getFileName() + ": " + err);
        }
    }
    if (!errors.isEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Monomodule MD", errors.joinIntoString("\n"));
    timerCallback();
}

void MdEditor::openKitList()
{
    const int maxH = m_out.getBottom() - m_strip.getBottom() - 2;
    const auto part = m_strip.partBounds(MdKitStrip::Kit) + m_strip.getPosition();
    m_drop.openKits(m_proc.loadedKitKey(), {juce::jmax(0, part.getX()), m_strip.getBottom() + 2}, maxH);
    m_strip.setOpen(MdKitStrip::Kit);
}

void MdEditor::openSoundList()
{
    const int maxH = m_out.getBottom() - m_strip.getBottom() - 2;
    const auto part = m_strip.partBounds(MdKitStrip::Sound) + m_strip.getPosition();
    const int x = juce::jmin(part.getX(), getWidth() - MdLibraryDrop::kLcdW * MdLibraryDrop::kS - 10);
    m_drop.openSounds(m_proc.machineIdOf(m_track), m_track, m_proc.loadedSoundKey(m_track), {x, m_strip.getBottom() + 2}, maxH);
    m_strip.setOpen(MdKitStrip::Sound);
}

void MdEditor::loadKit(const KitEntry& e)
{
    mnm::mddump::Kit kit;
    if (!m_lib->loadKit(e.key, kit)) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Kit", "The kit " + e.name + " is no longer in the library.");
        return;
    }
    m_kitUndo = {true, m_proc.captureMdKit(), m_proc.loadedKitKey(), m_proc.kitName()};
    const int emptied = m_proc.loadMdKit(e.key, kit, e.name);
    // a project's kit: its patterns become the pattern bank (SEQ / PTN)
    if (e.sourceId.isNotEmpty() && e.sourceId != "saved")
        if (const auto* dump = m_lib->model().mdState(e.sourceId)) m_proc.setPatternBank(e.sourceId, e.source, *dump, e.position);
    if (emptied > 0)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Kit",
            juce::String(emptied) + " track(s) use machines Monomodule MD does not have; they were left empty.");
    selectTrack(m_track);
    timerCallback();
}

// previous / next kit in the list order, wrapping
void MdEditor::stepKit(int dir)
{
    const auto kits = m_lib->kits();
    if (kits.empty()) { openKitList(); return; }
    int i = -1;
    for (int k = 0; k < int(kits.size()); ++k) if (kits[size_t(k)].key == m_proc.loadedKitKey()) i = k;
    i = i < 0 ? (dir > 0 ? 0 : int(kits.size()) - 1) : (i + dir + int(kits.size())) % int(kits.size());
    loadKit(kits[size_t(i)]);
}

juce::String MdEditor::slotText(const mnm::library::LibraryModel::Slot& s)
{
    if (!s.valid() || s.kit < 0) return {};
    return "PROJECT " + s.projectName + ", KIT " + juce::String(s.kit + 1).paddedLeft('0', 2) + (s.track >= 0 ? " T" + juce::String(s.track + 1) : juce::String());
}

juce::String MdEditor::saveKit(const juce::String& name, bool intoProject, bool asVersion)
{
    juce::String key;
    const auto into = intoProject ? m_lib->projectSlotOfKit(m_proc.loadedKitKey()) : mnm::library::LibraryModel::Slot{};
    const auto r = m_lib->saveKit(name, m_proc.captureMdKit(), asVersion ? m_proc.loadedKitKey() : juce::String(), &key, into);
    if (r.failed()) return r.getErrorMessage();
    m_proc.setLoadedKit(key, name.toUpperCase());
    timerCallback();
    return {};
}

void MdEditor::loadSound(const SoundEntry& e)
{
    mnm::mdcatalog::Sound s;
    if (!m_lib->loadSound(e.key, s)) return;
    if (!m_proc.loadSound(m_track, e.key, s, e.name))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Sound", "This sound uses a machine Monomodule MD does not have yet.");
    bindTrackPages();
    timerCallback();
}

// previous / next sound of the selected track's machine, wrapping
void MdEditor::stepSound(int dir)
{
    const auto sounds = m_lib->sounds(m_proc.machineIdOf(m_track));
    if (sounds.empty()) { openSoundList(); return; }
    int i = -1;
    for (int k = 0; k < int(sounds.size()); ++k) if (sounds[size_t(k)].key == m_proc.loadedSoundKey(m_track)) i = k;
    i = i < 0 ? (dir > 0 ? 0 : int(sounds.size()) - 1) : (i + dir + int(sounds.size())) % int(sounds.size());
    loadSound(sounds[size_t(i)]);
}

juce::String MdEditor::saveSound(const juce::String& name, bool intoProject, bool asVersion)
{
    juce::String key;
    const auto into = intoProject ? m_lib->projectSlotOfSound(m_proc.loadedSoundKey(m_track)) : mnm::library::LibraryModel::Slot{};
    const auto r = m_lib->saveSound(name, m_proc.captureSound(m_track), asVersion ? m_proc.loadedSoundKey(m_track) : juce::String(), &key, into);
    if (r.failed()) return r.getErrorMessage();
    m_proc.setLoadedSound(m_track, key, name.toUpperCase());
    timerCallback();
    return {};
}

// A kit (its best pattern, else a demo), a sound (one trig) or a pattern (on its kit), heard before loading it; a second
// click stops
void MdEditor::audition(const juce::String& key, int kind)
{
    if (m_proc.previewKey() == key) { m_proc.previewStop(); m_drop.repaint(); m_panel.repaint(); return; }
    const auto& cat = m_lib->model().mdCatalog();
    mnm::mdpreview::Options opt;
    if (kind == MdLibraryPanel::Kits) {
        const auto* k = cat.kit(key.toStdString());
        if (!k) return;
        const auto kitData = k->kit;
        const auto best = mnm::mdpreview::choosePreviewPattern(cat, *k);
        if (const auto* p = best.empty() ? nullptr : cat.pattern(best)) { const auto pat = p->pattern; m_proc.previewPlay(key, [kitData, pat, opt] { return mnm::mdpreview::patternPreview(kitData, pat, opt); }); }
        else m_proc.previewPlay(key, [kitData, opt] { return mnm::mdpreview::patternPreview(kitData, mnm::mdpreview::demoPattern(kitData), opt); });
    } else if (kind == MdLibraryPanel::Patterns) {
        const auto* p = cat.pattern(key.toStdString());
        const auto* k = p ? cat.kit(p->kitId) : nullptr;
        if (!p || !k) { m_panel.message = "THIS PATTERN'S KIT SLOT IS EMPTY"; m_panel.repaint(); return; }
        const auto pat = p->pattern; const auto kitData = k->kit;
        m_proc.previewPlay(key, [kitData, pat, opt] { return mnm::mdpreview::patternPreview(kitData, pat, opt); });
    } else {
        const auto* s = cat.sound(key.toStdString());
        if (!s) return;
        const auto snd = s->sound;
        m_proc.previewPlay(key, [snd, opt] { return mnm::mdpreview::soundPreview(snd, opt); });
    }
    if (const auto st = m_proc.previewStatus(); st.isNotEmpty()) { m_panel.message = st.toUpperCase(); juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Audition", st); }
    m_drop.repaint();
    m_panel.repaint();
}

void MdEditor::loadKitKey(const juce::String& key)
{
    for (const auto& k : m_lib->kits()) if (k.key == key) { loadKit(k); return; }
}

void MdEditor::loadSoundKey(int track, const juce::String& key)
{
    mnm::mdcatalog::Sound s;
    if (!m_lib->loadSound(key, s)) return;
    const auto* item = m_lib->model().mdCatalog().sound(key.toStdString());
    const auto name = item ? juce::String(item->name).toUpperCase() : juce::String();
    if (!m_proc.loadSound(track, key, s, name))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Sound", "This sound uses a machine Monomodule MD does not have yet.");
    bindTrackPages();
    timerCallback();
}

// The selected track's sound: the name it was loaded or saved under, else its catalog name, else "-"
juce::String MdEditor::soundDisplayName(int t)
{
    if (m_proc.machineIdOf(t) == 0) return "-";
    if (const auto n = m_proc.loadedSoundName(t); n.isNotEmpty()) return n;
    if (const auto key = m_proc.loadedSoundKey(t); key.isNotEmpty())
        if (const auto* s = m_lib->model().mdCatalog().sound(key.toStdString())) return juce::String(s->name).toUpperCase();
    return "-";
}

void MdEditor::sampleMenu()
{
    const int id = m_proc.machineIdOf(m_track);
    if (!isRomMachine(id)) return;
    const int slot = romSlotOf(id);
    juce::PopupMenu m;
    m.addItem(1, "LOAD SAMPLE...");
    m.addItem(2, "CLEAR SAMPLE", m_proc.sampleName(slot).isNotEmpty());
    m.addSeparator();
    if (m_proc.sampleName(slot).isNotEmpty())
        m.addItem(3, m_proc.sampleName(slot).toUpperCase() + "  " + juce::String(m_proc.sampleSeconds(slot), 2) + " S", false);
    m.addItem(4, juce::String("SAMPLE MEMORY USED ") + juce::String(int(std::lround(m_proc.sampleMemoryUsed() * 100.0))) + "%", false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_sample), [this, slot](int r) {
        if (r == 2) { m_proc.clearSample(slot); return; }
        if (r != 1) return;
        m_chooser = std::make_unique<juce::FileChooser>("Load a sample into " + juce::String(kMachines[machineIndexOf(slot + 128)].name),
            juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
        m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            const auto err = m_proc.loadSample(slot, f);
            if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Sample", err);
        });
    });
}

void MdEditor::timerCallback()
{
    if (m_muteQueue != 0 && !juce::ModifierKeys::currentModifiers.isShiftDown()) applyMuteQueue();   // Shift let go: the queued mutes flip together
    // the LCD artwork follows the Monomachine OS file the other Monomodule plugins use
    if (const auto path = loadSharedOsPath(); path != m_artPath) {
        m_artPath = path;
        if (one::loadLcdArt(path)) { resized(); repaint(); }
    }
    m_bpm.setHostBpm(float(m_proc.hostBpm()));
    if (--m_osPoll <= 0) { m_osPoll = 40; m_proc.refreshSharedOsPath(); }   // an OS file picked in another instance (every ~2 s)
    const bool ready = m_proc.engineReady();
    if (ready != m_ready) m_shownMachineId = -2;   // the labels come from the OS file
    m_ready = ready;
    m_status.setVisible(false);
    m_osButton.setVisible(false);
    if (m_missingOs.isVisible() == ready) { m_missingOs.setVisible(!ready); if (!ready) m_missingOs.toFront(false); }
    if (!ready) m_missingOs.setStatusMessage(m_proc.firmwarePath().isNotEmpty() ? m_proc.statusText() : juce::String());
    m_engineStatus.setVisible(m_showStatus);
    if (m_showStatus) m_engineStatus.setText(m_proc.statusText() + (m_proc.firmwarePath().isNotEmpty() ? "   " + m_proc.firmwarePath() : juce::String()), juce::dontSendNotification);
    if (--m_skinPoll <= 0) {   // a skin chosen in another Monomodule window
        m_skinPoll = 40;
        if (!m_skinDialog.isVisible()) if (const auto s = skin::load(); s != skin::current()) { skin::apply(s); skinChanged(); }
    }
    // machines: the block, the keys, and the SYNTHESIS page when the selected track's machine changed
    for (int t = 0; t < kTracks; ++t) {
        const int idx = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(t))->load())));
        m_keys.setMachine(t, idx);
        m_keys.setActive(t, m_proc.trackActivity(t) > 0.012f);
        if (t == 0) {
            const bool seq = m_proc.apvts.getRawParameterValue(seqId())->load() >= 0.5f && m_proc.seqPattern() >= 0;   // an empty pattern runs too
            m_keys.setSeq(m_proc.seqStep(), seq ? m_proc.seqLength() : 0, m_proc.seqTrigs(m_track));
            refreshGrid();
            {   // a finished recording run: one undo step
                MdProcessor::RecordedEdit re;
                while (m_proc.takeRecordedEdit(re)) { m_undo.push_back({re.slot, re.before, re.after, "recording"}); m_redo.clear(); m_lastCoalesce = -1; }
                if (m_proc.recording()) m_seqBar.repaint();   // the blinking dot
                if (m_songEd.isVisible()) m_songEd.repaint();
                if (m_mixer.isVisible()) m_mixer.repaint();   // the meters
            }
            if (m_outTab == 1) m_out.pull();
            if (m_heldStep >= 0) for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->pull();
            if (--m_ptnPoll <= 0) {   // the bank: its name on the panel, its empty slots marked
                m_ptnPoll = 10;
                const auto bn = m_proc.bankName();
                const auto badge = bn.isEmpty() ? juce::String("BANK: --") : bn == "PLUGIN" ? juce::String("BANK: NEW") : "BANK: " + bn.toUpperCase().removeCharacters(" ").substring(0, 7);
                if (badge != m_bankBadge) { m_bankBadge = badge; m_out.setBadge(m_bankBadge.toRawUTF8()); }
                bool changed = false;
                for (int s = 0; s < 128; ++s) {
                    const std::string want = std::string(kPatternNames[s]) + (m_proc.bankHasPattern(s) ? "" : " --");
                    if (want != m_ptnNames[size_t(s)]) { m_ptnNames[size_t(s)] = want; m_ptnNamePtrs[size_t(s)] = m_ptnNames[size_t(s)].c_str(); changed = true; }
                }
                if (changed) m_out.repaint();
            }
        }
        m_keys.setFlags(t, m_proc.apvts.getRawParameterValue(muteId(t))->load() >= 0.5f, m_proc.trackLocked(t));
        if (t == m_track && idx != m_machineIndex) { m_machineIndex = idx; m_machineBlock.setMachine(idx); }
    }
    rebuildSynPage();
    m_level.setMeter(m_proc.trackPeak(m_track));
    // ROM machines: their sample slot in the SYNTHESIS title bar
    const int id = m_proc.machineIdOf(m_track);
    const bool rom = isRomMachine(id);
    if (rom) {
        // as much of the sample's name as fits beside the page title
        const int room = KnobPage::kLcdW - LcdCanvas::textWidth(spec::kFontBold8, "SYNTHESIS") - 2 - 6 - 5;
        juce::String text = m_proc.sampleName(romSlotOf(id)).toUpperCase();
        if (text.isEmpty()) text = "LOAD SAMPLE";
        while (text.length() > 1 && LcdCanvas::textWidth(spec::kFontSmall4x5, text.toRawUTF8()) > room) text = text.dropLastCharacters(1);
        if (m_sample.getButtonText() != text) { m_sample.setButtonText(text); resized(); }
    }
    if (m_sample.isVisible() != rom) { m_sample.setVisible(rom); resized(); }
    if (m_proc.previewPoll()) { m_drop.repaint(); m_panel.repaint(); }   // an audition ended
    m_panel.refreshIfChanged();
    if ((m_tick++ % 5) == 0) {   // the modified marks: a few times a second
        m_strip.setKit(m_proc.kitName(), m_proc.kitModified());
        m_strip.setSound(m_track, soundDisplayName(m_track), m_proc.soundModified(m_track));
    }
    m_strip.setVisible(ready);
}

void MdEditor::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper);
    drawShnolkLogo(g, m_logoBounds.toFloat(), lcd::ink);
}

void MdEditor::resized()
{
    auto r = getLocalBounds().reduced(10, 8);
    auto footer = r.removeFromBottom(16);
    m_footerVersion.setBounds(footer.removeFromLeft(200));
    m_footerBy.setBounds(footer.removeFromRight(240));

    const int levW = 19 * kScale, top = r.getY(), gap = 12;
    auto header = r.removeFromTop(48);
    m_logoBounds = header.removeFromLeft(levW).withY(top).withHeight(18 * kScale);
    header.removeFromLeft(8);
    m_machineBlock.setBounds(header.getX(), top, m_machineBlock.preferredWidth(), MdMachineBlock::kLcdH * kScale);
    auto headerTop = header.removeFromTop(30);
    m_menuButton.setBounds(headerTop.removeFromRight(32 * kScale));
    headerTop.removeFromRight(10);
    m_bpmSync.setBounds(headerTop.removeFromRight(28 * kScale));
    headerTop.removeFromRight(8);
    m_bpm.setBounds(headerTop.removeFromRight(62 * kScale));   // "120.0" in the tall digit face is 59 px
    m_bpmLabel.setBounds(headerTop.removeFromRight(22 * kScale));
    {   // the kit strip: centred between the machine block and MENU, as wide as that leaves
        const int x0 = m_machineBlock.getRight() + 12, x1 = m_bpmLabel.getX() - 4;
        const int w = m_strip.preferredWidth(juce::jmax(0, x1 - x0));
        m_strip.setBounds(x0 + (x1 - x0 - w) / 2, top, w, MdKitStrip::kLcdH * MdKitStrip::kS);
    }
    {   // without an OS file: the notice and its button beside the machine block
        auto s = headerTop.withLeft(m_machineBlock.getRight() + 16);
        m_status.setBounds(s.removeFromLeft(LcdCanvas::textWidth(spec::kFontBold8, "NO MACHINEDRUM OS FILE") * kScale + 6));
        s.removeFromLeft(10);
        m_osButton.setBounds(s.removeFromLeft((LcdCanvas::textWidth(spec::kFontBold8, "SELECT OS FILE") + 8) * kScale));
    }
    {   // the sequencer bar: under the kit strip, from the machine block to the right edge
        const int x0 = m_machineBlock.getRight() + 12;
        m_seqBar.setBounds(x0, m_strip.getBottom() + 6, (r.getRight() - x0) / MdSeqBar::kS * MdSeqBar::kS, MdSeqBar::kLcdH * MdSeqBar::kS);
        auto col = [&](MdKitStrip::Part p, bool right) { const auto b = m_strip.partBounds(p) + m_strip.getPosition(); return ((right ? b.getRight() : b.getX()) - x0) / MdSeqBar::kS; };
        m_seqBar.setAnchors({(m_bpmSync.getX() - x0) / MdSeqBar::kS, m_bpmSync.getWidth() / MdSeqBar::kS,
                             (m_menuButton.getX() - x0) / MdSeqBar::kS, (m_menuButton.getRight() - x0) / MdSeqBar::kS - (m_menuButton.getX() - x0) / MdSeqBar::kS,
                             col(MdKitStrip::KitPrev, false), col(MdKitStrip::KitSave, true), col(MdKitStrip::SoundPrev, false), col(MdKitStrip::Library, true)});
    }
    r.removeFromTop(6);

    auto body = r;
    auto lev = body.removeFromLeft(levW);
    body.removeFromLeft(8);
    body.removeFromTop(36);   // the machine block's lower part and the gap under it
    auto keys = body.removeFromBottom(MdTrackKeys::kLcdH * kScale);
    body.removeFromBottom(gap);
    // the track keys run from the LEV column's left edge to the pages' right edge (the LEV column ends above them)
    {   // ... at a whole number of LCD pixels (the keys paint in LCD pixels; a remainder would stay unpainted)
        const int right = keys.getX() + 3 * KnobPage::kWidth + 2 * gap;
        const int w = (right - lev.getX()) / kScale * kScale;
        m_keys.setBounds(keys.withLeft(right - w).withRight(right));
    }
    lev = lev.withTop(body.getY() - 11 * kScale).withBottom(body.getY() + 2 * KnobPage::kHeight + gap);
    m_level.setBounds(lev.withHeight((lev.getHeight() / kScale) * kScale));

    auto place = [](juce::Rectangle<int> b, KnobPage& page) { page.setBounds(b.withTop(b.getY() - page.overhangPx())); };   // tabs stand above the page
    auto placeRow = [&](juce::Rectangle<int> row, KnobPage& a, KnobPage& b, KnobPage& c) {
        place(row.removeFromLeft(KnobPage::kWidth), a); row.removeFromLeft(gap);
        place(row.removeFromLeft(KnobPage::kWidth), b); row.removeFromLeft(gap);
        place(row.removeFromLeft(KnobPage::kWidth), c);
    };
    placeRow(body.removeFromTop(KnobPage::kHeight), m_syn, m_fx, m_routing);
    body.removeFromTop(gap);
    placeRow(body.removeFromTop(KnobPage::kHeight), m_lfo, m_master, m_out);

    // the sample slot stands in the SYNTHESIS title bar, right-aligned like a page badge
    const int bw = m_sample.preferredWidth();
    m_sample.setBounds(m_syn.getRight() - bw - kScale, m_syn.getY() + m_syn.overhangPx() + kScale, bw, (KnobPage::kTitleH - 2) * kScale);
    m_saveDialog.setBounds(getLocalBounds());
    m_missingOs.setBounds(getLocalBounds());
    m_about.setBounds(getLocalBounds());
    m_skinDialog.setBounds(getLocalBounds());
    m_engineStatus.setBounds(m_machineBlock.getRight() + 12, m_strip.getBottom() + 1, getWidth() - m_machineBlock.getRight() - 22, 16);
    m_picker.setTargetBounds(juce::Rectangle<int>(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY()));
    m_panel.setTargetBounds(juce::Rectangle<int>(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY()));
}

} // namespace mnm::plugin::md
