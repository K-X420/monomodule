// The UW sample manager: 48 ROM slots, rename / clear / RAM > ROM / resample
#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

namespace { constexpr int kSpTitleH = 10, kSpGridY = 14, kSpRowH = 9; }

juce::Rectangle<int> MdSamplePanel::slotRect(int i) const
{
    const int w = getWidth() / kS, cw = (w - 6) / kCols;
    return {3 + (i / kRows) * cw, kSpGridY + (i % kRows) * kSpRowH, cw - 2, kSpRowH - 1};
}

juce::Rectangle<int> MdSamplePanel::buttonRect(int b) const
{
    const int h = getHeight() / kS, by = h - 14;
    static const int x[7] = {6, 50, 100, 158, 216, 274, 334}, wd[7] = {40, 36, 54, 54, 54, 54, 58};
    return {x[b], by, wd[b], 10};
}

void MdSamplePanel::paint(juce::Graphics& g)
{
    const int w = getWidth() / kS, h = getHeight() / kS;
    LcdCanvas cv(w, h);
    cv.fillRect(0, 0, w, h, false);
    frame(cv, {0, 0, w, h});
    cv.fillRect(0, 0, w, kSpTitleH, true);
    cv.text(spec::kFontBold8, "SAMPLES", 4, 1, false);
    const juce::String mem = "MEMORY " + juce::String(int(std::lround((memoryUsed ? memoryUsed() : 0.0) * 100.0))) + "%";
    cv.text(spec::kFontTiny3x5, mem.toRawUTF8(), 60, 3, false);
    cv.text(spec::kFontBold8, "X", w - 10, 1, false);
    for (int i = 0; i < 48; ++i) {
        const auto r = slotRect(i);
        const bool sel = i == m_sel;
        if (sel) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        const auto n = name ? name(i) : juce::String();
        const double sec = seconds ? seconds(i) : 0.0;
        cv.text(spec::kFontTiny3x5, juce::String(i + 1).paddedLeft('0', 2).toRawUTF8(), r.getX() + 1, r.getY() + 2, !sel);
        cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, n.isEmpty() ? juce::String("--") : n, r.getWidth() - 34).toRawUTF8(), r.getX() + 11, r.getY() + 2, !sel);
        if (sec > 0) {
            const juce::String s = juce::String(sec, 1) + "S";
            cv.text(spec::kFontTiny3x5, s.toRawUTF8(), r.getRight() - 1 - LcdCanvas::textWidth(spec::kFontTiny3x5, s.toRawUTF8()), r.getY() + 2, !sel);
        }
    }
    const int by = h - 14;
    cv.dotsH(2, w - 3, by - 3);
    for (int b = 0; b < 7; ++b) {
        const auto r = buttonRect(b);
        if (b == 6) {   // RESAMPLE: the selected track's sound into the slot
            frame(cv, r);
            cv.text(spec::kFontTiny3x5, ("RESAMPLE T" + juce::String((track ? track() : 0) + 1)).toRawUTF8(), r.getX() + 4, r.getY() + 3, true);
            continue;
        }
        const double rs = b >= 2 && ramSeconds ? ramSeconds(b - 2) : 1.0;
        if (rs > 0) frame(cv, r); else dottedFrame(cv, r);
        const juce::String label = b == 0 ? juce::String("RENAME") : b == 1 ? juce::String("CLEAR")
                                 : "RAM" + juce::String(b - 1) + (rs > 0 ? " " + juce::String(rs, 1) + "S>" : juce::String(" --"));
        cv.text(spec::kFontTiny3x5, label.toRawUTF8(), r.getX() + 4, r.getY() + 3, true);
    }
    cv.draw(g, 0, 0, kS);
}

void MdSamplePanel::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kS;
    const int w = getWidth() / kS;
    if (p.y < kSpTitleH) { if (p.x >= w - 14) { setVisible(false); if (onClose) onClose(); } return; }
    for (int i = 0; i < 48; ++i) if (slotRect(i).contains(p)) { m_sel = i; repaint(); return; }
    for (int b = 0; b < 7; ++b)
        if (buttonRect(b).contains(p)) {
            if (b == 6) { if (onResample) onResample(m_sel); repaint(); return; }
            if (b == 0 && onRename) onRename(m_sel);
            else if (b == 1 && onClear) onClear(m_sel);
            else if (b >= 2 && onCopyRam && ramSeconds && ramSeconds(b - 2) > 0) onCopyRam(b - 2, m_sel);
            repaint();
            return;
        }
}

void MdSamplePanel::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kS;
    for (int i = 0; i < 48; ++i) if (slotRect(i).contains(p) && onRename) { m_sel = i; onRename(i); return; }
}

bool MdSamplePanel::keyPressed(const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    auto move = [&](int d) { m_sel = juce::jlimit(0, 47, m_sel + d); repaint(); return true; };
    if (code == juce::KeyPress::escapeKey) { setVisible(false); if (onClose) onClose(); return true; }
    if (code == juce::KeyPress::upKey) return move(-1);
    if (code == juce::KeyPress::downKey) return move(1);
    if (code == juce::KeyPress::leftKey) return move(-kRows);
    if (code == juce::KeyPress::rightKey) return move(kRows);
    if (code == juce::KeyPress::returnKey) { if (onRename) onRename(m_sel); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { if (onClear) onClear(m_sel); repaint(); return true; }
    return false;
}

} // namespace mnm::plugin::md
