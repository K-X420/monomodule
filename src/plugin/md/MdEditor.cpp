#include "MdEditor.h"
#include "MdMachineText.h"
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
constexpr spec::Param blank() { return {"", spec::Display::Blank, false, 0, 127, 128, spec::Icons::None, nullptr}; }

constexpr const char* kTrackNames[kTracks] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11", "T12", "T13", "T14", "T15", "T16"};
constexpr const char* kShapeNames[8] = {"0", "1", "2", "3", "4", "5", "6", "7"};
constexpr const char* kVelNames[2] = {"VOLUME", "ACCENT"};

const spec::Param kFxParams[8] = {numeric("AMD", 0), numeric("AMF", 0), numeric("EQF", 64), bipolar("EQG"),
                                  numeric("FLTF", 0), numeric("FLTW", 127), numeric("FLTQ", 0), numeric("SRR", 0)};
const spec::Param kRoutingParams[8] = {numeric("DIST", 0), numeric("VOL", 100), bipolar("PAN"), numeric("DEL", 0),
                                       numeric("REV", 0), readout("OUT", kRouteNames, kNumRoutes, kNumRoutes - 1), blank(), blank()};
const spec::Param kLfoParams[8] = {readout("TRK", kTrackNames, kTracks), readout("PARAM", kLfoParamNames, 24), readout("SHP1", kShapeNames, 8),
                                   readout("SHP2", kShapeNames, 8), readout("TYPE", kLfoTypes, 3), numeric("SPD", 64), numeric("DEP", 0), numeric("MIX", 0)};
constexpr const char* kSeqNames[2] = {"OFF", "ON"};
constexpr const char* kModeNames[2] = {"PATTERN", "SONG"};
const spec::Param kOutParams[8] = {numeric("VOL", 80), readout("VEL", kVelNames, 2), numeric("ACNT", 64), blank(),
                                   readout("SEQ", kSeqNames, 2), readout("PTN", kPatternNames, 128), readout("MODE", kModeNames, 2), readout("SONG", kSongNames, 32)};
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
                                    named("PCHG", 2), blank(), blank(), blank()};
const spec::Param kCtr8pFx[8] = {named("P1T", 3), named("P1P", 4), named("P2T", 3), named("P2P", 4),
                                 named("P3T", 3), named("P3P", 4), named("P4T", 3), named("P4P", 4)};
const spec::Param kCtr8pRouting[8] = {named("P5T", 3), named("P5P", 4), named("P6T", 3), named("P6P", 4), named("P7T", 3), blank(), blank(), blank()};
const spec::Param kCtr8pLfo[8] = {blank(), blank(), blank(), blank(), blank(), named("P7P", 4), named("P8T", 3), named("P8P", 4)};
const spec::Param kCtrAllRouting[8] = {numeric("DIST", 0), numeric("VOL", 100), bipolar("PAN"), numeric("DEL", 0),
                                       numeric("REV", 0), blank(), blank(), blank()};
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
constexpr int kLogoPx = 4, kLogoX = 4, kLogoGap = 6;   // screen px per logo pixel; LCD px
int logoWidthLcd(const juce::String& fam)
{
    if (const auto* art = text::logoArt(fam.toRawUTF8())) return (art->w * kLogoPx + kScale - 1) / kScale;
    return LcdCanvas::textWidth(spec::kFontBold8, fam.toRawUTF8());
}
}

int MdMachineBlock::preferredWidth() const
{
    const int w = kLogoX + logoWidthLcd(familyOf(m_index)) + kLogoGap
                + LcdCanvas::textWidth(spec::kFontBold8, shortOf(m_index).toRawUTF8()) + 4 + 5 + 4;
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
    if (!art) cv.text(spec::kFontBold8, fam.toRawUTF8(), kLogoX, ty, false);
    const int nameX = kLogoX + logoWidthLcd(fam) + kLogoGap;
    cv.text(spec::kFontBold8, name.toRawUTF8(), nameX, ty, false);
    const int ax = nameX + LcdCanvas::textWidth(spec::kFontBold8, name.toRawUTF8()) + 4, ay = h / 2 - 1;
    for (int r = 0; r < 3; ++r) {   // the picker arrow: down when closed, up while open
        const int half = m_open ? r : 2 - r;
        for (int c = 2 - half; c <= 2 + half; ++c) cv.set(ax + c, ay + r, false);
    }
    cv.draw(g, 0, 0);
    if (art) {
        g.setColour(lcd::paper);
        const int x0 = kLogoX * kScale, y0 = (getHeight() - art->h * kLogoPx) / 2;
        for (int y = 0; y < art->h; ++y)
            for (int x = 0; x < art->w; ++x)
                if (text::logoLit(*art, x, y)) g.fillRect(x0 + x * kLogoPx, y0 + y * kLogoPx, kLogoPx, kLogoPx);
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
        const bool trig = (gr.trigs >> s) & 1;
        if (trig) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        else dottedFrame(cv, r.getX(), r.getY(), r.getWidth(), r.getHeight());
        const bool ink = !trig;
        cv.text(spec::kFontBold8, juce::String(s + 1).toRawUTF8(), r.getX() + 3, r.getY() + 3, ink);
        int fx = r.getX() + 3;   // the step's flags along the bottom
        auto flag = [&](const char* c, bool on) { if (on) cv.text(spec::kFontTiny3x5, c, fx, r.getBottom() - 8, ink); fx += 6; };
        flag("A", (gr.accent >> s) & 1);
        flag("S", (gr.slide >> s) & 1);
        flag("W", (gr.swing >> s) & 1);
        flag("L", (gr.locks >> s) & 1);
        if (s == gr.held) {   // held for locks: an inner frame
            cv.invertRect(r.getX() + 2, r.getY() + 2, r.getWidth() - 4, 1); cv.invertRect(r.getX() + 2, r.getBottom() - 3, r.getWidth() - 4, 1);
            cv.invertRect(r.getX() + 2, r.getY() + 3, 1, r.getHeight() - 6); cv.invertRect(r.getRight() - 3, r.getY() + 3, 1, r.getHeight() - 6);
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
        // LOCK and MUTE keys along the bottom: solid when on, dotted otherwise
        auto key = [&](juce::Rectangle<int> b, const char* letter, bool on) {
            if (on) cv.fillRect(b.getX(), b.getY(), b.getWidth(), b.getHeight(), ink);
            else dottedFrame(cv, b.getX(), b.getY(), b.getWidth(), b.getHeight());
            cv.textCentred(spec::kFontTiny3x5, letter, b.getX(), b.getWidth(), b.getY() + 2, on ? !ink : ink);
        };
        key(lockBox(t), "L", m_locked[size_t(t)]);
        key(muteBox(t), "M", m_muted[size_t(t)]);
        if (m_seqLen > 0) {   // the step this key stands for, on the playing page
            const int page = m_seqStep >= 0 ? m_seqStep / 16 : 0, s = page * 16 + t;
            if (s < m_seqLen) {
                const int bx = r.getCentreX() - 3, by = r.getBottom() - 9;
                if ((m_seqTrigs >> s) & 1) cv.fillRect(bx, by, 6, 6, ink);   // unlit: nothing (the L and M keys stay clear)
                if (s == m_seqStep) cv.invertRect(r.getX(), r.getY(), r.getWidth(), r.getHeight());   // the running light
            }
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
            if (e.mods.isPopupMenu()) { if (onStepMenu && s < m_grid.length) onStepMenu(s); }
            else if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { if (onSelect) onSelect(i); }
            else if (s >= m_grid.length) return;
            else if (e.mods.isShiftDown()) { if (onHold) onHold(s); }
            else if (onStep) onStep(s);
            return;
        }
        return;
    }
    for (int t = 0; t < kTracks; ++t) {
        if (!keyRect(t).contains(p)) continue;
        if (lockBox(t).expanded(1).contains(p)) { if (onLock) onLock(t); }
        else if (muteBox(t).expanded(1).contains(p)) { if (onMute) onMute(t); }
        else if (onPress) onPress(t);
        return;
    }
}

void MdTrackKeys::mouseMove(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kScale;
    juce::String tip;
    if (m_grid.on) {
        setTooltip("Step " + juce::String(m_grid.page * 16 + 1) + "-" + juce::String(m_grid.page * 16 + 16)
                   + ". Click: trig on/off. Shift+click: hold for locks (turn the track's knobs). Ctrl+click: select that track. Right-click: step menu.");
        return;
    }
    for (int t = 0; t < kTracks; ++t)
        if (lockBox(t).expanded(1).contains(p)) tip = "Lock: keep this track's sound when a kit is loaded";
        else if (muteBox(t).expanded(1).contains(p)) tip = "Mute: ignore this track's trigs";
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
        const int px = juce::jlimit(1, 3, juce::jmin((f.header.getWidth() - 12) / art->w, (f.header.getHeight() - 12) / art->h));
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
    m_drop.onLoadKit = [this](const KitEntry& e) { loadKit(e); };
    m_drop.onLoadSound = [this](const SoundEntry& e) { loadSound(e); };
    m_drop.onImport = [this] { importSyx(); };
    m_drop.onAudition = [this](const juce::String& key, bool kit) { audition(key, kit ? int(MdLibraryPanel::Kits) : int(MdLibraryPanel::Sounds)); };
    m_drop.isPlaying = [this](const juce::String& key) { return m_proc.previewKey() == key; };
    m_drop.onClosed = [this] { m_strip.setOpen(MdKitStrip::None); };
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
    m_out.setTabs({"OUT", "PATTERN"}, 0, [this](int tab) { bindOutPage(tab); });
    bindOutPage(0);
    m_keys.onStep = [this](int s) {
        const int t = m_track;
        bool removed = false;
        m_proc.editPattern(editSlot(), [&](mnm::mddump::Pattern& p) {
            if ((p.trigs[t] >> s) & 1) { p.trigs[t] &= ~(1ull << s); p.clearStepLocks(t, s); removed = true; }
            else p.trigs[t] |= 1ull << s;
        });
        if (removed && s == m_heldStep) holdStep(-1);
        refreshGrid();
    };
    m_keys.onHold = [this](int s) { holdStep(s == m_heldStep ? -1 : s); };
    m_keys.onSelect = [this](int t) { selectTrack(t); };
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
            case MdSeqBar::Grid:
                m_gridOn = !m_gridOn;
                if (!m_gridOn) holdStep(-1);
                if (m_outTab == 1) m_out.pull();
                break;
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
    m_seqBar.onPage = [this](int page) { m_gridPage = page; refreshGrid(); };
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
            m_proc.editPattern(slot, [&](mnm::mddump::Pattern& pat) { pat.trigs[t] |= 1ull << step; pat.setLock(t, p, step, v); });
            refreshGrid();
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
    static const char* const kSpd[4] = {"1X", "2X", "3/4X", "3/2X"};
    static const char* const kPages[4] = {"1", "2", "3", "4"};
    const spec::Param params[8] = {readout("LEN", names.lenP.data(), 64, 15), readout("SPD", kSpd, 4), readout("SWNG", names.swingP.data(), 31), numeric("ACC", 64),
                                   readout("KIT", names.kitP.data(), 64), readout("GRID", kSeqNames, 2), readout("PAGE", kPages, 4), readout("PTN", kPatternNames, 128)};
    m_out.bindCustom(params,
        [this](int k) {
            const auto p = m_proc.bankPattern(editSlot());
            switch (k) {
                case 0: return p ? juce::jlimit(0, 63, int(p->length) - 1) : 15;
                case 1: return p ? int(p->doubleTempo & 3) : 0;
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
            m_proc.editPattern(editSlot(), [&](mnm::mddump::Pattern& p) {
                switch (k) {
                    case 0: p.length = uint8_t(v + 1); p.scale = uint8_t(v / 16); break;   // SCALE: the pages the length needs
                    case 1: p.doubleTempo = uint8_t(v & 3); break;
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
        m_proc.editPattern(slot, [&](mnm::mddump::Pattern& p) {
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

void MdEditor::refreshGrid()
{
    {   // the bar
        MdSeqBar::State s;
        const int slot = editSlot();
        const auto p = m_proc.bankPattern(slot);
        s.playing = m_proc.seqPlaying() || m_proc.internalPlay();
        s.hostPlaying = m_proc.hostPlaying();
        s.grid = m_gridOn;
        s.track = m_track;
        const int mi = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(m_track))->load())));
        s.machine = shortOf(mi) == "---" ? familyOf(mi) : shortOf(mi);
        s.pattern = slot;
        s.empty = p == nullptr;
        s.length = p ? juce::jlimit(1, 64, int(p->length)) : 16;
        s.page = juce::jlimit(0, (s.length - 1) / 16, m_gridPage);
        s.step = m_proc.seqPlaying() && m_proc.seqPattern() == slot ? m_proc.seqStep() : -1;
        s.row = m_proc.seqPlaying() ? m_proc.seqSongRow() : -1;
        m_seqBar.setState(s);
    }
    MdTrackKeys::Grid g;
    g.on = m_gridOn;
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
    bindTrackPage(m_routing, mid ? kMidRouting : id == kCtr8p ? kCtr8pRouting : master ? kBlankPage : id == kCtrAll ? kCtrAllRouting : kRoutingParams, [t](int k) {
        static juce::String (* const ids[6])(int) = {distId, volId, panId, delId, revId, routeId};
        return k < 6 ? ids[k](t) : juce::String();
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
    m.addItem(10, "SYNC BPM TO HOST", true, m_bpmSync.getToggleState());
    m.addSubMenu("SKIN", skins);
    m.addItem(7, "SHOW ENGINE STATUS", true, m_showStatus);
    m.addItem(4, juce::String("SAMPLE MEMORY USED ") + juce::String(int(std::lround(m_proc.sampleMemoryUsed() * 100.0))) + "%", false);
    m.addSeparator();
    m.addItem(8, "PLUGIN INFO...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_menuButton), [this](int r) {
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
            const bool seq = m_proc.apvts.getRawParameterValue(seqId())->load() >= 0.5f && m_proc.seqPattern() >= 0 && m_proc.bankHasPattern(m_proc.seqPattern());
            m_keys.setSeq(m_proc.seqStep(), seq ? m_proc.seqLength() : 0, m_proc.seqTrigs(m_track));
            refreshGrid();
            if (m_outTab == 1) m_out.pull();
            if (m_heldStep >= 0) for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->pull();
            if (--m_ptnPoll <= 0) {   // the bank: its name on the panel, its empty slots marked
                m_ptnPoll = 10;
                const auto badge = m_proc.bankName().isNotEmpty() ? m_proc.bankName().toUpperCase().removeCharacters(" ").substring(0, 8) : juce::String("NO PTNS");
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
