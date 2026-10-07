// The sequencer bar: PLAY|REC, pattern / track pickers, pages, X2|DEL, GRID|MIX
#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

void MdSeqBar::resized()
{
    const int arrowW = 12, gap = 6;
    int x = 0;
    auto take = [&](Part p, int pw) { m_rects[size_t(p)] = {x, 0, pw, kLcdH}; x += pw - 1; };
    take(Play, 33);
    x += 1 + gap;
    take(Rec, 26);
    x += 1 + gap;
    take(Grid, 30);
    x += 1 + gap;
    take(TrkPrev, arrowW); take(Trk, 52); take(TrkNext, arrowW); take(Mute, 13);
    x += 1 + gap;
    take(PtnPrev, arrowW); take(Ptn, 42); take(PtnNext, arrowW);
    x += 1 + gap;
    take(Pages, 3 + 4 * 9 + 1);
    x += 1 + gap;
    take(Edit, 22);
    x += 1 + gap;
    take(DelPg, 23);
    x += 1 + gap;
    take(Mix, 25);
    x += 1 + gap;
    take(Step, juce::jmax(40, getWidth() / kS - x));
    if (m_anchors.sndR > m_anchors.sndL && m_anchors.kitR > m_anchors.kitL) {   // the left side in the kit strip's columns
        x = m_anchors.kitL;   // PLAY and REC joined, spanning the KIT selector to its save icon
        const int pr = juce::jmax(52, m_anchors.kitR - x);
        take(Play, pr / 2 + 1); take(Rec, pr - pr / 2);
        x = m_anchors.sndL;   // TRK's left arrow under the sound selector's
        take(TrkPrev, arrowW); take(Trk, 40); take(TrkNext, arrowW); take(Mute, 13);
        x += 1 + 4;
        take(PtnPrev, arrowW); take(Ptn, 42); take(PtnNext, arrowW);
    }
    if (m_anchors.menuW > 0) {   // STEP under MENU; GRID|MIX joined, ending under SYNC; X2|DEL joined; the pages before them
        m_rects[size_t(Step)] = {m_anchors.menuX, 0, m_anchors.menuW, kLcdH};
        int right = m_anchors.syncX + m_anchors.syncW;
        auto pair = [&](Part a, int aw, Part b, int bw) {   // joined: a shared edge
            m_rects[size_t(b)] = {right - bw, 0, bw, kLcdH};
            m_rects[size_t(a)] = {right - bw - aw + 1, 0, aw, kLcdH};
            right -= aw + bw - 1 + 3;
        };
        pair(Grid, 30, Mix, 24);
        pair(Edit, 22, DelPg, 24);
        for (Part p : {Pages}) {
            const int pw = m_rects[size_t(p)].getWidth();
            m_rects[size_t(p)] = {right - pw, 0, pw, kLcdH};
            right -= pw + gap;
        }
        const int ptnEnd = m_rects[size_t(PtnNext)].getRight() + gap;   // where the PTN group ends (in whichever layout it got)
        if (right < ptnEnd - gap) {   // too narrow: back to packing left to right
            x = ptnEnd;
            for (Part p : {Pages}) { const int pw = m_rects[size_t(p)].getWidth(); m_rects[size_t(p)] = {x, 0, pw, kLcdH}; x += pw + gap; }
        }
    }
}

void MdSeqBar::paint(juce::Graphics& g)
{
    LcdCanvas cv(getWidth() / kS, kLcdH);
    auto box = [&](Part p, bool solid) {
        const auto r = m_rects[size_t(p)];
        if (solid) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        frame(cv, r);
        if (!solid && m_hover == p) frame(cv, r.reduced(1));
        return r;
    };
    {   // PLAY: a triangle, a square while playing
        const auto r = box(Play, m_s.playing);
        const bool ink = !m_s.playing;
        const int cx = r.getX() + 5, cy = r.getCentreY();
        if (m_s.playing) cv.fillRect(cx, cy - 3, 7, 7, ink);
        else for (int i = 0; i < 5; ++i) for (int y = -4 + i; y <= 4 - i; ++y) cv.set(cx + i, cy + y, ink);
        cv.text(spec::kFontTiny3x5, m_s.playing ? "STOP" : "PLAY", r.getX() + 12, r.getY() + 5, ink);
        (void) ink;
    }
    {   // REC: a dot, the box solid while armed (the dot blinks while recording)
        const auto r = box(Rec, m_s.rec);
        const bool ink = !m_s.rec;
        const bool dot = !m_s.recording || (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
        if (dot) { const int cx = r.getX() + 4, cy = r.getCentreY() - 2; cv.fillRect(cx + 1, cy, 3, 5, ink); cv.fillRect(cx, cy + 1, 5, 3, ink); }
        cv.text(spec::kFontTiny3x5, "REC", r.getX() + 11, r.getY() + 5, ink);
    }
    {   // GRID: solid while the keys are steps
        const auto r = box(Grid, m_s.grid);
        static const char* const names[4] = {"GRID", "ACC", "SLD", "SWG"};
        cv.textCentred(spec::kFontBold8, names[juce::jlimit(0, 3, m_s.mark)], r.getX(), r.getWidth(), r.getY() + 4, !m_s.grid);
    }
    auto arrows = [&](Part prev, Part next) {
        auto r = box(prev, false); arrowH(cv, r.getCentreX(), r.getCentreY(), true, true);
        r = box(next, false); arrowH(cv, r.getCentreX(), r.getCentreY(), false, true);
    };
    auto labelled = [&](Part p, const char* label, const juce::String& text) {
        const auto r = box(p, false);
        cv.text(spec::kFontTiny3x5, label, r.getX() + 4, r.getY() + 5, true);
        const int lw = LcdCanvas::textWidth(spec::kFontTiny3x5, label), tx = r.getX() + (*label ? 8 + lw : 4);
        cv.text(spec::kFontBold8, fit(spec::kFontBold8, text, r.getRight() - tx - 3).toRawUTF8(), tx, r.getY() + 4, true);
    };
    arrows(TrkPrev, TrkNext);
    {   // MUTE of the selected track: a speaker (crossed out and solid while muted)
        const auto r = box(Mute, m_s.muted);
        pixelIcon(cv, m_s.muted ? kIconMuted : kIconSpeaker, 7, r.getX() + 3, r.getY() + 4, !m_s.muted);
    }
    labelled(Trk, "", "T" + juce::String(m_s.track + 1) + " " + m_s.machine);   // "T1 BD" says it is the track
    arrows(PtnPrev, PtnNext);
    if (m_s.chain.isNotEmpty()) labelled(Ptn, "CHN", m_s.chain);
    else labelled(Ptn, "PTN", juce::String(kPatternNames[juce::jlimit(0, 127, m_s.pattern)]) + (m_s.empty ? "-" : ""));
    {   // the pages: a ring each (a point past the length), solid when playing, underlined when GRID shows it
        const auto r = box(Pages, false);
        const int pages = (juce::jlimit(1, 64, m_s.length) + 15) / 16, playing = m_s.step >= 0 ? m_s.step / 16 : -1;
        for (int i = 0; i < 4; ++i) {
            const int x = dotX(i), y = r.getY() + 3;
            if (i >= pages) { cv.fillRect(x + 3, y + 3, 1, 1, true); continue; }
            static const char* const ring[7] = {"..###..", ".#...#.", "#.....#", "#.....#", "#.....#", ".#...#.", "..###.."};
            static const char* const disc[7] = {"..###..", ".#####.", "#######", "#######", "#######", ".#####.", "..###.."};
            const auto& shape = i == playing && m_s.beat ? disc : ring;   // the playing page blinks on the beat
            for (int dy = 0; dy < 7; ++dy) for (int dx = 0; dx < 7; ++dx) if (shape[dy][dx] == '#') cv.set(x + dx, y + dy, true);
            if (m_s.grid && i == m_s.page) cv.fillRect(x, y + 9, 7, 1, true);
        }
    }
    {   // X2: the pattern doubled (two pages, x2)
        const auto r = box(Edit, false);
        cv.text(spec::kFontBold8, "X2", r.getX() + 4, r.getY() + 4, true);
    }
    {   // DEL PG: the shown page removed (the pages after it move up)
        const auto r = box(DelPg, false);
        cv.textCentred(spec::kFontBold8, "DEL", r.getX(), r.getWidth(), r.getY() + 4, true);
    }
    {   // MIX: solid while the mixer is open
        const auto r = box(Mix, m_s.mix);
        cv.textCentred(spec::kFontBold8, "MIX", r.getX(), r.getWidth(), r.getY() + 4, !m_s.mix);
    }
    {
        juce::String s = m_s.flash.isNotEmpty() ? m_s.flash : m_s.seqOff ? juce::String("SEQ OFF")
                       : m_s.step >= 0 ? juce::String(m_s.step + 1).paddedLeft('0', 2) + "/" + juce::String(m_s.length) : "--/" + juce::String(m_s.length);
        if (m_s.row >= 0 && m_s.flash.isEmpty()) s << "  ROW " << (m_s.row + 1);
        if (m_s.flash.isNotEmpty()) {   // a confirmation: the whole box, no label
            const auto r = box(Step, true);
            cv.text(spec::kFontBold8, fit(spec::kFontBold8, s, r.getWidth() - 6).toRawUTF8(), r.getX() + 4, r.getY() + 4, false);
        } else {
            labelled(Step, "STEP", s);
        }
    }
    cv.draw(g, 0, 0, kS);
}

MdSeqBar::Part MdSeqBar::partAt(juce::Point<int> lcd) const
{
    for (int i = 0; i < kParts; ++i) if (m_rects[size_t(i)].contains(lcd)) return Part(i);
    return None;
}

void MdSeqBar::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragged || (m_downPart != Ptn && m_downPart != Trk) || e.getDistanceFromDragStart() < 8) return;
    m_dragged = true;
    if (onDragOut) onDragOut(m_downPart);
}

void MdSeqBar::mouseDown(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kS;
    const auto p = partAt(lcd);
    m_downPart = p; m_dragged = false;
    const bool edit = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isAltDown();
    if (p == Pages) {
        for (int i = 0; i < 4; ++i)
            if (lcd.x >= dotX(i) - 2 && lcd.x <= dotX(i) + 8) {
                if (edit) { if (onEditClick) onEditClick(Pages, i, e.mods); }
                else if (onPage) onPage(i);
                return;
            }
        return;
    }
    if (edit && (p == Trk || p == Ptn || p == PtnPrev || p == PtnNext)) { if (onEditClick) onEditClick(p, -1, e.mods); return; }
    if (p != None && onPart) onPart(p);
}

void MdSeqBar::mouseMove(const juce::MouseEvent& e)
{
    const auto p = partAt(e.getPosition() / kS);
    if (p != m_hover) { m_hover = p; repaint(); }
    switch (p) {
        case Play: setTooltip(m_s.hostPlaying ? "The host's transport is running: the pattern follows it" : "Play / stop the pattern on the plugin's own clock (while the host is stopped)"); break;
        case Rec: setTooltip("REC: while the pattern plays, the trigs you play (MIDI notes, the track keys) go onto the step playing, "
                             "and a knob turned during a step locks its value on the next step's trig. Ctrl+Z takes a run back"); break;
        case Grid: setTooltip(m_s.grid ? "GRID (G): the keys are the selected track's steps (click a step to place a trig). Click to get the tracks back"
                                       : "GRID (G): click to place steps with the keys (the selected track's steps; TRK < > picks the track)"); break;
        case Mute: setTooltip(m_s.muted ? "The track is muted: its trigs don't play. Click to unmute (Alt+click a key in GRID: that key's track)"
                                         : "Mute the track (Alt+click a key in GRID: that key's track)"); break;
        case TrkPrev: case TrkNext: setTooltip("The track whose steps GRID shows and edits"); break;
        case Trk: setTooltip("The track GRID edits. Drag it into Ableton: its MIDI clip. Shift+click: copy it. Ctrl+click: paste onto it. Alt+click: clear it"); break;
        case Ptn: setTooltip("The pattern. Drag it into Ableton: a MIDI clip. Shift+click: copy it. Ctrl+click: paste onto it. Alt+click: clear its steps"); break;
        case PtnPrev: case PtnNext: setTooltip("The pattern (PTN): played by SEQ, edited by GRID (\"-\" = an empty slot). Shift+click >: add the next pattern to a chain; Shift+click <: take the last one off"); break;
        case Pages: setTooltip("Pages of 16 steps: solid = playing, underlined = shown by GRID. Click: show it. Shift+click: copy it (all tracks). "
                               "Ctrl+click: paste onto it. Alt+click: clear it. Keys: Ctrl+C / Ctrl+V / Delete on the shown page"); break;
        case Step: setTooltip(m_s.seqOff ? "SEQ is OFF (OUT tab): the pattern does not play. PLAY, a placed step or REC turns it on" : "The playing step / the pattern's length"); break;
        case Edit: setTooltip("X2: the pattern doubled (its steps again after themselves, twice the length). Ctrl+D"); break;
        case DelPg: setTooltip("DEL PAGE: remove the shown page (the pages after it move up; a 1-page pattern is cleared). Ctrl+Z undoes it"); break;
        case Mix: setTooltip("MIX: the 16 tracks' levels, pans, mutes and solos (M)"); break;
        default: setTooltip({}); break;
    }
}

} // namespace mnm::plugin::md
