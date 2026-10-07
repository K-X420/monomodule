// The mixer: levels, mutes, solos; Alt shows the mute groups
#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

namespace {
constexpr int kMxTitleH = 10, kMxNameY = 13, kMxPanY = 37, kMxPanH = 7, kMxFaderY = 48, kMxBtnH = 9;
}

juce::Rectangle<int> MdMixer::panRect(int t) const { return {colX(t) + 1, kMxPanY, colW() - 3, kMxPanH}; }

juce::Rectangle<int> MdMixer::faderRect(int t) const
{
    const int h = getHeight() / kS;
    return {colX(t) + 2, kMxFaderY, 7, h - kMxFaderY - 2 * kMxBtnH - 14};
}

juce::Rectangle<int> MdMixer::buttonRect(int t, bool solo) const
{
    const int h = getHeight() / kS;
    return {colX(t) + 1, h - 3 - (solo ? 1 : 2) * (kMxBtnH + 1) + 1, colW() - 3, kMxBtnH};
}

int MdMixer::levelAt(int t, int y) const
{
    const auto r = faderRect(t);
    return juce::jlimit(0, 127, juce::roundToInt(127.0 * (r.getBottom() - 2 - y) / juce::jmax(1, r.getHeight() - 4)));
}

int MdMixer::panAt(int t, int x) const
{
    const auto r = panRect(t);
    return juce::jlimit(0, 127, juce::roundToInt(127.0 * (x - r.getX() - 1) / juce::jmax(1, r.getWidth() - 3)));
}

// MUTE GROUP: -- T1 .. T16 (not itself), a step at a time
void MdMixer::stepGroup(int t, int dir)
{
    if (!strip || !set) return;
    int g = strip(t).muteGroup;
    for (int i = 0; i < 17; ++i) {
        g = g + dir < -1 ? 15 : g + dir > 15 ? -1 : g + dir;
        if (g != t) break;
    }
    set(t, MuteGroup, g);
    repaint();
}

MdMixer::Hit MdMixer::hitAt(juce::Point<int> p) const
{
    for (int t = 0; t < 16; ++t) {
        if (p.x < colX(t) || p.x >= colX(t) + colW()) continue;
        if (p.y >= kMxNameY && p.y < kMxPanY - 1) return {t, Select};
        if (panRect(t).expanded(0, 1).contains(p)) return {t, Pan};
        if (faderRect(t).withX(colX(t)).withWidth(colW()).expanded(0, 2).contains(p)) return {t, Level};
        if (buttonRect(t, false).contains(p)) return {t, Mute};
        if (buttonRect(t, true).contains(p)) return {t, Solo};
        return {};
    }
    return {};
}

void MdMixer::paint(juce::Graphics& g)
{
    const int w = getWidth() / kS, h = getHeight() / kS;
    LcdCanvas cv(w, h);
    cv.fillRect(0, 0, w, h, false);
    frame(cv, {0, 0, w, h});
    cv.fillRect(0, 0, w, kMxTitleH, true);
    cv.text(spec::kFontBold8, "MIXER", 4, 1, false);
    cv.text(spec::kFontTiny3x5, "ALT: MUTE GROUPS", 40, 3, false);
    cv.text(spec::kFontBold8, "X", w - 10, 1, false);
    for (int t = 0; t < 16; ++t) {
        const auto s = strip ? strip(t) : Strip{};
        const int x = colX(t), cw = colW();
        if (t > 0) for (int y = kMxNameY; y < h - 3; y += 2) cv.set(x - 1, y, true);
        {   // the name: T1 / family / machine, inverted when selected; a dot while it plays
            if (s.selected) cv.fillRect(x, kMxNameY - 2, cw - 1, kMxPanY - kMxNameY, true);
            const bool ink = !s.selected;
            const juce::String tn = "T" + juce::String(t + 1);
            cv.text(spec::kFontBold8, tn.toRawUTF8(), x + 2, kMxNameY, ink);
            if (s.active) cv.fillRect(x + cw - 5, kMxNameY + 1, 2, 2, ink);
            cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, s.family, cw - 3).toRawUTF8(), x + 2, kMxNameY + 9, ink);
            cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, s.machine, cw - 3).toRawUTF8(), x + 2, kMxNameY + 15, ink);
        }
        {   // PAN: a centre tick and the position
            const auto r = panRect(t);
            frame(cv, r);
            const int mid = r.getX() + 1 + (r.getWidth() - 3) / 2;
            cv.set(mid, r.getY() + 1, true); cv.set(mid, r.getBottom() - 2, true);
            const int px = r.getX() + 1 + juce::roundToInt((r.getWidth() - 3) * s.pan / 127.0);
            cv.fillRect(juce::jmin(px, mid), r.getY() + 3, std::abs(px - mid) + 1, 1, true);
            cv.fillRect(px, r.getY() + 1, 1, r.getHeight() - 2, true);
        }
        {   // LEVEL: the fader, its meter beside it, the value under it
            const auto r = faderRect(t);
            frame(cv, r);
            const int inner = r.getHeight() - 4, fill = juce::roundToInt(inner * s.level / 127.0);
            cv.fillRect(r.getX() + 2, r.getBottom() - 2 - fill, r.getWidth() - 4, fill, true);
            cv.fillRect(r.getX() - 1, r.getBottom() - 2 - fill, r.getWidth() + 2, 1, true);   // the cap
            const int mx = r.getRight() + 2, mw = juce::jmax(2, cw - (r.getRight() - x) - 5);
            const float db = s.peak > 0 ? juce::Decibels::gainToDecibels(s.peak) : -100.0f;
            const int m = juce::jlimit(0, inner, juce::roundToInt(inner * (db + 48.0f) / 48.0f));
            for (int y = 0; y < m; y += 2) cv.fillRect(mx, r.getBottom() - 3 - y, mw, 1, true);
            if (db >= 0.0f) cv.fillRect(mx, r.getY(), mw, 2, true);   // the clip light
            cv.text(spec::kFontTiny3x5, juce::String(s.level).toRawUTF8(), x + 2, r.getBottom() + 3, true);
        }
        const bool fn = juce::ModifierKeys::currentModifiers.isAltDown();   // FUNCTION held: the M buttons show the mute groups
        for (const bool solo : {false, true}) {
            const auto r = buttonRect(t, solo);
            if (!solo && fn) {
                frame(cv, r);
                const juce::String grp = s.muteGroup >= 0 ? "T" + juce::String(s.muteGroup + 1) : juce::String("--");
                cv.textCentred(spec::kFontTiny3x5, grp.toRawUTF8(), r.getX(), r.getWidth(), r.getY() + 2, true);
                continue;
            }
            const bool on = solo ? s.solo : s.mute;
            if (on) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
            frame(cv, r);
            if (solo) cv.text(spec::kFontTiny3x5, "S", r.getCentreX() - 1, r.getY() + 2, !on);
            else {   // a 5-wide M (the 3-wide one reads as H)
                static const char* const glyphM[5] = {"#...#", "##.##", "#.#.#", "#...#", "#...#"};
                for (int gy = 0; gy < 5; ++gy) for (int gx = 0; gx < 5; ++gx) if (glyphM[gy][gx] == '#') cv.set(r.getCentreX() - 2 + gx, r.getY() + 2 + gy, !on);
            }
        }
    }
    cv.draw(g, 0, 0, kS);
}

void MdMixer::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kS;
    if (p.y < kMxTitleH) { if (p.x >= getWidth() / kS - 14) close(); return; }
    const auto hit = hitAt(p);
    if (hit.track < 0 || !strip || !set) return;
    const auto s = strip(hit.track);
    if (hit.what == Select) { set(hit.track, Select, 0); repaint(); return; }
    if (hit.what == Mute && e.mods.isAltDown()) { stepGroup(hit.track, e.mods.isShiftDown() ? -1 : 1); return; }   // FUNCTION + M
    if (hit.what == Mute || hit.what == Solo) {
        if (e.mods.isShiftDown()) {   // only this track: the others off
            for (int t = 0; t < 16; ++t) set(t, hit.what, t == hit.track ? 1 : 0);
        } else {
            set(hit.track, hit.what, (hit.what == Mute ? s.mute : s.solo) ? 0 : 1);
        }
        repaint();
        return;
    }
    m_drag = hit;
    if (gesture) gesture(hit.track, hit.what, true);
    mouseDrag(e);
}

void MdMixer::mouseDrag(const juce::MouseEvent& e)
{
    if (m_drag.track < 0 || !set) return;
    const auto p = e.getPosition() / kS;
    set(m_drag.track, m_drag.what, m_drag.what == Level ? levelAt(m_drag.track, p.y) : panAt(m_drag.track, p.x));
    repaint();
}

void MdMixer::mouseUp(const juce::MouseEvent&)
{
    if (m_drag.track >= 0 && gesture) gesture(m_drag.track, m_drag.what, false);
    m_drag = {};
}

void MdMixer::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto hit = hitAt(e.getPosition() / kS);
    if (hit.track < 0 || !set || (hit.what != Level && hit.what != Pan)) return;
    set(hit.track, hit.what, hit.what == Level ? 100 : 64);
    repaint();
}

void MdMixer::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    const auto hit = hitAt(e.getPosition() / kS);
    if (hit.track >= 0 && hit.what == Mute && e.mods.isAltDown()) { stepGroup(hit.track, (d.deltaY != 0 ? d.deltaY : d.deltaX) > 0 ? -1 : 1); return; }
    if (hit.track < 0 || !strip || !set || (hit.what != Level && hit.what != Pan)) return;
    const int dir = (d.deltaY != 0 ? d.deltaY : d.deltaX) > 0 ? 1 : -1;
    const auto s = strip(hit.track);
    set(hit.track, hit.what, juce::jlimit(0, 127, (hit.what == Level ? s.level : s.pan) + dir * (e.mods.isShiftDown() ? 8 : 1)));
    repaint();
}

bool MdMixer::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    if (auto* p = getParentComponent()) return p->keyPressed(k);   // G, M and the rest still work while it is open
    return false;
}

} // namespace mnm::plugin::md
