// The editor's own widgets: the machine block, the track keys (and GRID), the machine picker, the badge button
#include "MdEditorInternal.h"
#include <cmath>

namespace mnm::plugin::md {

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

} // namespace mnm::plugin::md
