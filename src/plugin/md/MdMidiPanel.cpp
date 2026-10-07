// The MIDI settings panel (the MD's GLOBAL > MIDI pages, plus the note map)
#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

namespace {
juce::String noteName(int n)   // as Ableton names them: 60 = C3
{
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return n < 0 ? juce::String("--") : juce::String(names[n % 12]) + juce::String(n / 12 - 2);
}
constexpr int kMpTitleH = 10, kMpRowY = 16, kMpRowH = 14, kMpGridY = 78;
}

juce::Rectangle<int> MdMidiPanel::cellRect(int c) const
{
    if (c < 3) return {110, kMpRowY + c * kMpRowH - 2, 80, 12};
    if (c == 19) return {194, kMpRowY + kMpRowH - 2, 46, 12};   // PRG CHANGE's channel
    if (c == 25) return {194, kMpRowY + 2 * kMpRowH - 2, 54, 12};   // CTRL IN (next to MIDI OUT)
    if (c >= 20) {   // the PATTERN NOTES row, under the note map
        static const int x[5] = {110, 194, 262, 302, 352}, w[5] = {80, 64, 36, 46, 40};
        return {x[c - 20], kMpGridY + 46, w[c - 20], 12};
    }
    const int t = c - 3;
    return {6 + (t % 8) * 48, kMpGridY + (t / 8) * 22, 44, 19};
}

int MdMidiPanel::cellAt(juce::Point<int> p) const
{
    for (int c = 0; c < 26; ++c) if (cellRect(c).contains(p)) return c;
    return -1;
}

int MdMidiPanel::cellValue(const Values& v, int c) const
{
    if (c == 0) return v.baseChannel;
    if (c == 1) return v.programChange;
    if (c == 2) return v.midiOut;
    if (c == 19) return v.pcChannel;
    if (c == 20) return v.ptnMode;
    if (c == 21) return v.ptnFrom;
    if (c == 22) return v.ptnBank;
    if (c == 23) return v.startNote;
    if (c == 24) return v.stopNote;
    if (c == 25) return v.ctrlIn;
    return v.note[size_t(c - 3)];
}

void MdMidiPanel::change(int c, int to)
{
    if (!get || !set) return;
    auto v = get();
    if (c == 0) v.baseChannel = juce::jlimit(-1, 15, to);
    else if (c == 1) v.programChange = juce::jlimit(0, 3, to);
    else if (c == 2) v.midiOut = juce::jlimit(0, 2, to);
    else if (c == 19) v.pcChannel = juce::jlimit(0, 16, to);
    else if (c == 20) v.ptnMode = juce::jlimit(0, 2, to);
    else if (c == 21) {   // white keys only: a step is the next white key
        const bool black[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
        int n = juce::jlimit(-1, 127, to);
        const int dir = to >= cellValue(get(), 21) ? 1 : -1;
        while (n >= 0 && n < 128 && black[n % 12]) n += dir;
        v.ptnFrom = juce::jlimit(-1, 127, n);
    }
    else if (c == 22) v.ptnBank = juce::jlimit(0, 7, to);
    else if (c == 23) v.startNote = juce::jlimit(-1, 127, to);
    else if (c == 24) v.stopNote = juce::jlimit(-1, 127, to);
    else if (c == 25) v.ctrlIn = juce::jlimit(0, 1, to);
    else {
        const int t = c - 3, n = juce::jlimit(-1, 127, to);
        if (n >= 0) for (auto& o : v.note) if (o == n) o = -1;   // a note plays one track: taken from another
        v.note[size_t(t)] = n;
    }
    set(v);
    repaint();
}

void MdMidiPanel::paint(juce::Graphics& g)
{
    const int w = getWidth() / kS, h = getHeight() / kS;
    LcdCanvas cv(w, h);
    cv.fillRect(0, 0, w, h, false);
    frame(cv, {0, 0, w, h});
    cv.fillRect(0, 0, w, kMpTitleH, true);
    cv.text(spec::kFontBold8, "MIDI SETTINGS", 4, 1, false);
    cv.text(spec::kFontBold8, "X", w - 10, 1, false);
    const auto v = get ? get() : Values{};
    static const char* const labels[3] = {"BASE CHANNEL", "PROGRAM CHANGE", "MIDI OUT"};
    static const char* const pcs[4] = {"OFF", "IN", "OUT", "IN+OUT"};
    static const char* const outs[3] = {"OFF", "TRIGS", "TRIGS+CCS"};
    const juce::String vals[3] = {v.baseChannel < 0 ? juce::String("OFF") : juce::String(v.baseChannel + 1), pcs[juce::jlimit(0, 3, v.programChange)], outs[juce::jlimit(0, 2, v.midiOut)]};
    static const char* const notes[3] = {"TRIGS ON IT, CCS ON IT AND THE NEXT 3", "= THE NEXT PATTERN (AUTO: BASE CH+0..3)", "TRIGS AS NOTES (+ KNOBS / LOCKS AS CCS)"};
    for (int c = 0; c < 3; ++c) {
        const auto r = cellRect(c);
        cv.text(spec::kFontSmall4x5, labels[c], 6, r.getY() + 3, true);
        frame(cv, r);
        cv.text(spec::kFontBold8, vals[c].toRawUTF8(), r.getX() + 4, r.getY() + 2, true);
        cv.text(spec::kFontTiny3x5, notes[c], (c == 1 ? cellRect(19).getRight() : c == 2 ? cellRect(25).getRight() : r.getRight()) + 8, r.getY() + 4, true);
    }
    {   // PATTERN NOTES: notes that play patterns (the unit's key map), START, STOP
        const int y = cellRect(20).getY();
        cv.text(spec::kFontSmall4x5, "PATTERN NOTES", 6, y + 3, true);
        static const char* const modes[3] = {"GATE", "MOMENTARY", "QUEUE"};
        auto cell = [&](int c, const char* label, const juce::String& value) {
            const auto r = cellRect(c);
            frame(cv, r);
            int x = r.getX() + 3;
            if (label) { cv.text(spec::kFontTiny3x5, label, x, r.getY() + 4, true); x += LcdCanvas::textWidth(spec::kFontTiny3x5, label) + 3; }
            cv.text(spec::kFontBold8, value.toRawUTF8(), x, r.getY() + 2, true);
        };
        cell(20, nullptr, modes[juce::jlimit(0, 2, v.ptnMode)]);
        cell(21, "FROM", m_learn == 21 ? juce::String("?") : v.ptnFrom == -2 ? juce::String("CUSTOM") : v.ptnFrom < 0 ? juce::String("OFF") : noteName(v.ptnFrom));
        cell(22, "BNK", juce::String::charToString(juce::juce_wchar('A' + juce::jlimit(0, 7, v.ptnBank))));
        cell(23, "START", m_learn == 23 ? juce::String("?") : v.startNote < 0 ? juce::String("--") : noteName(v.startNote));
        cell(24, "STOP", m_learn == 24 ? juce::String("?") : v.stopNote < 0 ? juce::String("--") : noteName(v.stopNote));
    }
    {   // CTRL IN: Ableton's play / stop run the sequencer (OFF: a sound module; PLAY runs it at Ableton's tempo)
        const auto r = cellRect(25);
        frame(cv, r);
        cv.text(spec::kFontTiny3x5, "CTRL IN", r.getX() + 3, r.getY() + 4, true);
        cv.text(spec::kFontBold8, v.ctrlIn ? "ON" : "OFF", r.getX() + 32, r.getY() + 2, true);
    }
    {   // PRG CHANGE's channel
        const auto r = cellRect(19);
        frame(cv, r);
        cv.text(spec::kFontTiny3x5, "CH", r.getX() + 3, r.getY() + 4, true);
        const juce::String ch = v.pcChannel > 0 ? juce::String(v.pcChannel) : juce::String("AUTO");
        cv.text(spec::kFontBold8, ch.toRawUTF8(), r.getX() + 14, r.getY() + 2, true);
    }
    cv.dotsH(2, w - 3, kMpGridY - 17);
    cv.text(spec::kFontSmall4x5, "TRIG NOTES (THE NOTE MAP): CLICK, THEN PLAY A NOTE", 6, kMpGridY - 11, true);
    for (int t = 0; t < 16; ++t) {
        const auto r = cellRect(3 + t);
        frame(cv, r);
        const bool lrn = m_learn == 3 + t;
        if (lrn) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        cv.text(spec::kFontTiny3x5, ("T" + juce::String(t + 1)).toRawUTF8(), r.getX() + 3, r.getY() + 3, !lrn);
        cv.text(lrn ? spec::kFontTiny3x5 : spec::kFontBold8, lrn ? "PLAY A NOTE" : noteName(v.note[size_t(t)]).toRawUTF8(), r.getX() + 3, r.getY() + (lrn ? 11 : 10), !lrn);
    }
    const int by = h - 14;   // the buttons
    cv.dotsH(2, w - 3, by - 3);
    frame(cv, {6, by, 64, 10}); cv.text(spec::kFontTiny3x5, "DEFAULT MAP", 10, by + 3, true);
    frame(cv, {76, by, 72, 10}); cv.text(spec::kFontTiny3x5, "FROM PROJECT...", 80, by + 3, true);
    cv.draw(g, 0, 0, kS);
}

void MdMidiPanel::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kS;
    const int w = getWidth() / kS, h = getHeight() / kS;
    if (p.y < kMpTitleH && p.x >= w - 14) { setVisible(false); if (onClose) onClose(); return; }
    const int by = h - 14;
    if (p.y >= by && p.y < by + 10) {
        if (p.x >= 6 && p.x < 70) { if (onDefault) onDefault(); repaint(); }
        else if (p.x >= 76 && p.x < 148) { if (onFromProject) onFromProject(); }
        return;
    }
    const int c = cellAt(p);
    if (c < 0 || !get) return;
    if (c == 25) { change(25, get().ctrlIn ? 0 : 1); return; }   // a switch
    m_drag = c; m_dragY = e.getPosition().y; m_dragV = cellValue(get(), c);
    if (c >= 3 && c <= 18 && m_dragV < 0) m_dragV = 36 + (c - 3);
    if (c == 21 && m_dragV < 0) m_dragV = 64;   // E3, as the factory map
    if ((c == 23 || c == 24) && m_dragV < 0) m_dragV = c == 23 ? 91 : 93;
}

void MdMidiPanel::mouseUp(const juce::MouseEvent& e)
{
    const int c = m_drag;
    m_drag = -1;
    if (c < 0 || e.mouseWasDraggedSinceMouseDown()) return;
    const bool noteCell = (c >= 3 && c <= 18) || c == 21 || c == 23 || c == 24;
    if (!noteCell) return;
    if (m_learn == c) { m_learn = -1; if (onLearnCancel) onLearnCancel(); }   // again: cancelled
    else { m_learn = c; if (onLearn) onLearn(); }
    repaint();
}

void MdMidiPanel::learned(int note)
{
    if (m_learn < 0) return;
    const int c = m_learn;
    m_learn = -1;
    change(c, juce::jlimit(0, 127, note));
}

void MdMidiPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (m_drag < 0) return;
    if (std::abs(e.getDistanceFromDragStartY()) < 2) return;   // a click is LEARN, not a turn
    const int steps = (m_dragY - e.getPosition().y) / (2 * kS);
    change(m_drag, m_dragV + steps);
}

void MdMidiPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    const int c = cellAt(e.getPosition() / kS);
    if (c < 0 || !get) return;
    const int dir = d.deltaY > 0 ? 1 : d.deltaY < 0 ? -1 : 0;
    int v = cellValue(get(), c);
    if (c >= 3 && c <= 18 && v < 0) v = 36 + (c - 3);
    if (c == 21 && v == -2) v = 64;
    change(c, v + dir);
}

bool MdMidiPanel::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) {
        if (m_learn >= 0) { m_learn = -1; if (onLearnCancel) onLearnCancel(); repaint(); return true; }
        setVisible(false); if (onClose) onClose(); return true;
    }
    return false;
}

} // namespace mnm::plugin::md
