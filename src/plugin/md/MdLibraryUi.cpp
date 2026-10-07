#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

namespace {
juce::String machineName(int id) { return juce::String(mnm::mdnames::machineName(id)); }
}

// ---------------------------------------------------------------------------------------------- strip

void MdKitStrip::setKit(const juce::String& name, bool modified)
{
    const auto n = name.isEmpty() ? juce::String("DEFAULT") : name.toUpperCase();
    if (n == m_kit && modified == m_kitMod) return;
    m_kit = n; m_kitMod = modified;
    repaint();
}

void MdKitStrip::setSound(int track, const juce::String& name, bool modified)
{
    const auto n = name.toUpperCase();
    if (n == m_sound && modified == m_soundMod && track == m_track) return;
    m_sound = n; m_soundMod = modified; m_track = track;
    repaint();
}

void MdKitStrip::resized()
{
    // joined parts share their edges; the kit and the sound are two strips with a gap between
    const int w = getWidth() / kS, arrowW = 12, iconW = 15, gap = 6;
    const int fixed = 2 * (2 * (arrowW - 1) + (iconW - 1)) + gap + (iconW - 1);
    const int kitW = (w - fixed) / 2, soundW = w - fixed - kitW;
    int x = 0;
    auto take = [&](Part p, int pw) { m_rects[size_t(p)] = {x, 0, pw, kLcdH}; x += pw - 1; };
    take(KitPrev, arrowW); take(Kit, kitW); take(KitNext, arrowW); take(KitSave, iconW);
    x += 1 + gap;
    take(SoundPrev, arrowW); take(Sound, soundW); take(SoundNext, arrowW); take(SoundSave, iconW); take(Library, iconW);
}

void MdKitStrip::paint(juce::Graphics& g)
{
    LcdCanvas cv(getWidth() / kS, kLcdH);
    auto part = [&](Part p, bool solid) {
        const auto r = m_rects[size_t(p)];
        if (solid) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        frame(cv, r);
        if (!solid && m_hover == p) frame(cv, r.reduced(1));
        return r;
    };
    auto selector = [&](Part p, const juce::String& label, const juce::String& name, bool modified) {
        const bool open = m_open == p;
        const auto r = part(p, open);
        const int lw = LcdCanvas::textWidth(spec::kFontTiny3x5, label.toRawUTF8());
        cv.text(spec::kFontTiny3x5, label.toRawUTF8(), r.getX() + 4, r.getY() + 5, !open);
        const int nx = r.getX() + 4 + lw + 4, maxW = r.getRight() - 10 - nx;
        textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, name, maxW - (modified ? 7 : 0)), nx, r.getY() + 4, !open, modified);
        caret(cv, r.getRight() - 9, r.getY() + 6, open, !open);
    };
    auto arrows = [&](Part prev, Part next) {
        auto r = part(prev, false); arrowH(cv, r.getCentreX(), r.getCentreY(), true, true);
        r = part(next, false); arrowH(cv, r.getCentreX(), r.getCentreY(), false, true);
    };
    arrows(KitPrev, KitNext);
    selector(Kit, "KIT", m_kit, m_kitMod);
    auto r = part(KitSave, m_kitMod); pixelIcon(cv, kIconSave, 9, r.getX() + 3, r.getY() + 3, !m_kitMod);
    arrows(SoundPrev, SoundNext);
    selector(Sound, "T" + juce::String(m_track + 1), m_sound.isEmpty() ? juce::String("-") : m_sound, m_soundMod);
    r = part(SoundSave, m_soundMod); pixelIcon(cv, kIconSave, 9, r.getX() + 3, r.getY() + 3, !m_soundMod);
    r = part(Library, m_libOpen); pixelIcon(cv, kIconLibrary, 9, r.getX() + 3, r.getY() + 3, !m_libOpen);
    cv.draw(g, 0, 0, kS);
}

// ---------------------------------------------------------------------------------------------- sequencer bar

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

void MdSeqBar::mouseDown(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kS;
    const auto p = partAt(lcd);
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
        case Trk: setTooltip("The track GRID edits. Shift+click: copy it. Ctrl+click: paste onto it. Alt+click: clear it"); break;
        case Ptn: setTooltip("The pattern. Shift+click: copy it. Ctrl+click: paste onto it. Alt+click: clear its steps"); break;
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

// ---------------------------------------------------------------------------------------------- MIDI settings panel

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
    if (c >= 20) {   // the PATTERN NOTES row, under the note map
        static const int x[5] = {110, 194, 262, 302, 352}, w[5] = {80, 64, 36, 46, 40};
        return {x[c - 20], kMpGridY + 46, w[c - 20], 12};
    }
    const int t = c - 3;
    return {6 + (t % 8) * 48, kMpGridY + (t / 8) * 22, 44, 19};
}

int MdMidiPanel::cellAt(juce::Point<int> p) const
{
    for (int c = 0; c < 25; ++c) if (cellRect(c).contains(p)) return c;
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
    static const char* const notes[3] = {"TRIGS ON IT, CCS ON IT AND THE NEXT 3", "= THE NEXT PATTERN (AUTO: BASE CH+0..3)", "THE PLAYED TRIGS AS NOTES (+ KNOBS, LOCKS AS CCS)"};
    for (int c = 0; c < 3; ++c) {
        const auto r = cellRect(c);
        cv.text(spec::kFontSmall4x5, labels[c], 6, r.getY() + 3, true);
        frame(cv, r);
        cv.text(spec::kFontBold8, vals[c].toRawUTF8(), r.getX() + 4, r.getY() + 2, true);
        cv.text(spec::kFontTiny3x5, notes[c], (c == 1 ? cellRect(19).getRight() : r.getRight()) + 8, r.getY() + 4, true);
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
        cell(21, "FROM", v.ptnFrom == -2 ? juce::String("CUSTOM") : v.ptnFrom < 0 ? juce::String("OFF") : noteName(v.ptnFrom));
        cell(22, "BNK", juce::String::charToString(juce::juce_wchar('A' + juce::jlimit(0, 7, v.ptnBank))));
        cell(23, "START", v.startNote < 0 ? juce::String("--") : noteName(v.startNote));
        cell(24, "STOP", v.stopNote < 0 ? juce::String("--") : noteName(v.stopNote));
    }
    {   // PRG CHANGE's channel
        const auto r = cellRect(19);
        frame(cv, r);
        cv.text(spec::kFontTiny3x5, "CH", r.getX() + 3, r.getY() + 4, true);
        const juce::String ch = v.pcChannel > 0 ? juce::String(v.pcChannel) : juce::String("AUTO");
        cv.text(spec::kFontBold8, ch.toRawUTF8(), r.getX() + 14, r.getY() + 2, true);
    }
    cv.dotsH(2, w - 3, kMpGridY - 17);
    cv.text(spec::kFontSmall4x5, "TRIG NOTES (THE NOTE MAP): DRAG A NOTE, WHEEL IT", 6, kMpGridY - 11, true);
    for (int t = 0; t < 16; ++t) {
        const auto r = cellRect(3 + t);
        frame(cv, r);
        cv.text(spec::kFontTiny3x5, ("T" + juce::String(t + 1)).toRawUTF8(), r.getX() + 3, r.getY() + 3, true);
        cv.text(spec::kFontBold8, noteName(v.note[size_t(t)]).toRawUTF8(), r.getX() + 3, r.getY() + 10, true);
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
    m_drag = c; m_dragY = e.getPosition().y; m_dragV = cellValue(get(), c);
    if (c >= 3 && c <= 18 && m_dragV < 0) m_dragV = 36 + (c - 3);
    if (c == 21 && m_dragV < 0) m_dragV = 64;   // E3, as the factory map
    if ((c == 23 || c == 24) && m_dragV < 0) m_dragV = c == 23 ? 91 : 93;
}

void MdMidiPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (m_drag < 0) return;
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
    if (k == juce::KeyPress::escapeKey) { setVisible(false); if (onClose) onClose(); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------- mixer

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
                const juce::String g = s.muteGroup >= 0 ? "T" + juce::String(s.muteGroup + 1) : juce::String("--");
                cv.textCentred(spec::kFontTiny3x5, g.toRawUTF8(), r.getX(), r.getWidth(), r.getY() + 2, true);
                continue;
            }
            const bool on = solo ? s.solo : s.mute;
            if (on) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
            frame(cv, r);
            if (solo) cv.text(spec::kFontTiny3x5, "S", r.getCentreX() - 1, r.getY() + 2, !on);
            else {   // a 5-wide M (the 3-wide one reads as H)
                static const char* const glyphM[5] = {"#...#", "##.##", "#.#.#", "#...#", "#...#"};
                for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) if (glyphM[y][x] == '#') cv.set(r.getCentreX() - 2 + x, r.getY() + 2 + y, !on);
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

// ---------------------------------------------------------------------------------------------- song editor

namespace {
constexpr int kSeTitleH = 10, kSeHeadY = 13, kSeRowY = 21, kSeRowH = 9, kSeBarH = 13;
constexpr int kSeColX[MdSongEditor::kCols] = {3, 22, 62, 96, 128, 160, 206};
constexpr int kSeMuteW = 8;
constexpr const char* kSeButtons[7] = {"+PATTERN", "+LOOP", "+END", "DUPLICATE", "UP", "DOWN", "DELETE"};
constexpr int kSeButtonOp[7] = {0, 1, 2, 6, 4, 5, 3};
int seButtonX(int i) { int x = 3; for (int k = 0; k < i; ++k) x += LcdCanvas::textWidth(spec::kFontTiny3x5, kSeButtons[k]) + 12; return x; }
}

int MdSongEditor::rowsVisible() const { return juce::jmax(1, (getHeight() / kS - kSeRowY - kSeBarH - 2) / kSeRowH); }

int MdSongEditor::value(const mnm::mddump::SongRow& r, int col) const
{
    const auto* b = r.bytes;
    switch (col) {
        case Ptn: return b[0] < 128 ? b[0] : b[0] == 0xFE ? 128 : 129;
        case Rep: return b[2];
        case Start: return b[0] == 0xFE ? b[3] : b[8];
        case End: return b[9];
        case Tempo: { const int t = (b[6] << 8) | b[7]; return t == 0xFFFF ? 0 : t / 24; }
        default: return 0;
    }
}

void MdSongEditor::setValue(int row, int col, int v, int coalesce)
{
    if (!edit) return;
    edit(col == Ptn ? "song pattern" : col == Rep ? "song repeats" : col == Tempo ? "song tempo" : "song steps", [&](mnm::mddump::Song& s) {
        if (row < 0 || row >= int(s.rows.size())) return;
        auto* b = s.rows[size_t(row)].bytes;
        switch (col) {
            case Ptn: {
                v = juce::jlimit(0, 129, v);
                const bool wasPattern = b[0] < 128;
                b[0] = uint8_t(v < 128 ? v : v == 128 ? 0xFE : 0xFF);
                if (v < 128 && !wasPattern) { b[2] = 0; b[3] = 0; b[4] = b[5] = 0; b[6] = b[7] = 0xFF; b[8] = 0; b[9] = uint8_t(patternLength ? patternLength(v) : 16); }
                if (v == 128 && wasPattern) { b[2] = 0; b[3] = 0; }
                break;
            }
            case Rep: b[2] = uint8_t(juce::jlimit(0, b[0] == 0xFE ? 127 : 63, v)); break;
            case Start:
                if (b[0] == 0xFE) b[3] = uint8_t(juce::jlimit(0, juce::jmax(0, int(s.rows.size()) - 1), v));
                else b[8] = uint8_t(juce::jlimit(0, 63, v));
                break;
            case End: b[9] = uint8_t(juce::jlimit(0, 64, v)); break;
            case Tempo: {
                const int bpm = juce::jlimit(30, 300, v), t = bpm * 24;
                b[6] = uint8_t(t >> 8); b[7] = uint8_t(t);
                break;
            }
            default: break;
        }
    }, coalesce);
    repaint();
}

void MdSongEditor::paint(juce::Graphics& g)
{
    const int w = getWidth() / kS, h = getHeight() / kS;
    LcdCanvas cv(w, h);
    cv.fillRect(0, 0, w, h, false);
    frame(cv, {0, 0, w, h});
    cv.fillRect(0, 0, w, kSeTitleH, true);
    const auto song = getSong ? getSong() : nullptr;
    const juce::String title = "SONG " + juce::String(m_slot + 1).paddedLeft('0', 2) + (song && song->name.size() ? "  " + juce::String(song->name).toUpperCase() : juce::String());
    cv.text(spec::kFontBold8, title.toRawUTF8(), 4, 1, false);
    cv.text(spec::kFontBold8, "X", w - 10, 1, false);   // close
    static const char* const heads[kCols] = {"#", "PATTERN", "REP", "START", "END", "TEMPO", "MUTED TRACKS 1-16"};
    for (int c = 0; c < kCols; ++c) cv.text(spec::kFontTiny3x5, heads[c], kSeColX[c], kSeHeadY, true);
    cv.dotsH(1, w - 2, kSeRowY - 2);
    const int rows = song ? int(song->rows.size()) : 0, vis = rowsVisible(), playing = playingRow ? playingRow() : -1;
    if (rows == 0) cv.text(spec::kFontSmall4x5, "NO ROWS YET: ADD ONE WITH +PATTERN BELOW", 6, kSeRowY + 2, true);
    for (int i = 0; i < vis && m_scroll + i < rows; ++i) {
        const int ri = m_scroll + i, y = kSeRowY + i * kSeRowH;
        const auto& r = song->rows[size_t(ri)];
        const auto* b = r.bytes;
        const bool sel = ri == m_sel;
        if (sel) cv.fillRect(1, y - 1, w - 2, kSeRowH, true);
        const bool ink = !sel;
        auto put = [&](int col, const juce::String& s, bool bold = false) { cv.text(bold ? spec::kFontBold8 : spec::kFontSmall4x5, s.toRawUTF8(), kSeColX[col], bold ? y - 1 : y + 1, ink); };
        put(Num, juce::String(ri + 1));
        if (b[0] == 0xFF) { put(Ptn, "END", true); }
        else if (b[0] == 0xFE) {
            put(Ptn, "LOOP", true);
            put(Rep, b[2] == 0 ? juce::String("INF") : "X" + juce::String(int(b[2])));
            put(Start, "TO " + juce::String(int(b[3]) + 1));
        } else {
            put(Ptn, mnm::mddump::patternSlotName(b[0]), true);
            put(Rep, "X" + juce::String(int(b[2]) + 1));
            put(Start, juce::String(int(b[8]) + 1));
            put(End, b[9] == 0 ? juce::String("--") : juce::String(int(b[9])));
            const int t = (b[6] << 8) | b[7];
            put(Tempo, t == 0xFFFF ? juce::String("--") : juce::String(t / 24.0, 1));
            const int mutes = (b[4] << 8) | b[5];
            for (int m = 0; m < 16; ++m) {
                const int mx = kSeColX[Mutes] + m * kSeMuteW + (m / 4) * 2;
                if ((mutes >> m) & 1) cv.fillRect(mx, y, kSeMuteW - 2, kSeRowH - 3, ink);
                else frame(cv, {mx, y, kSeMuteW - 2, kSeRowH - 3}, ink);
            }
        }
        if (ri == playing) { cv.invertRect(1, y - 1, 2, kSeRowH); cv.invertRect(w - 3, y - 1, 2, kSeRowH); }   // the playing row: bars at the edges
    }
    if (rows > vis) {   // a scroll bar
        const int top = kSeRowY - 1, bh = vis * kSeRowH, th = juce::jmax(4, bh * vis / rows), ty = top + (bh - th) * m_scroll / juce::jmax(1, rows - vis);
        cv.fillRect(w - 4, ty, 2, th, true);
    }
    const int by = h - kSeBarH;   // the row buttons
    cv.dotsH(1, w - 2, by - 2);
    for (int i = 0; i < 7; ++i) {
        const int bx = seButtonX(i), bw = LcdCanvas::textWidth(spec::kFontTiny3x5, kSeButtons[i]) + 8;
        frame(cv, {bx, by, bw, 10});
        cv.text(spec::kFontTiny3x5, kSeButtons[i], bx + 4, by + 3, true);
    }
    cv.draw(g, 0, 0, kS);
}

MdSongEditor::Hit MdSongEditor::hitAt(juce::Point<int> p) const
{
    Hit hit;
    const int w = getWidth() / kS, h = getHeight() / kS;
    if (p.y < kSeTitleH && p.x >= w - 14) { hit.close = true; return hit; }
    if (p.y >= h - kSeBarH) {
        for (int i = 0; i < 7; ++i) { const int bx = seButtonX(i), bw = LcdCanvas::textWidth(spec::kFontTiny3x5, kSeButtons[i]) + 8; if (p.x >= bx && p.x < bx + bw) hit.button = i; }
        return hit;
    }
    if (p.y < kSeRowY - 1) return hit;
    const int i = (p.y - (kSeRowY - 1)) / kSeRowH;
    if (i >= rowsVisible()) return hit;
    hit.row = m_scroll + i;
    for (int c = kCols - 1; c >= 0; --c) if (p.x >= kSeColX[c] - 1) { hit.col = c; break; }
    if (hit.col == Mutes) {
        for (int m = 0; m < 16; ++m) { const int mx = kSeColX[Mutes] + m * kSeMuteW + (m / 4) * 2; if (p.x >= mx && p.x < mx + kSeMuteW) hit.mute = m; }
    }
    return hit;
}

void MdSongEditor::mouseDown(const juce::MouseEvent& e)
{
    const auto hit = hitAt(e.getPosition() / kS);
    if (hit.close) { setVisible(false); if (onClose) onClose(); return; }
    if (hit.button >= 0) { rowOp(kSeButtonOp[hit.button], m_sel); return; }
    const auto song = getSong ? getSong() : nullptr;
    if (hit.row < 0 || !song || hit.row >= int(song->rows.size())) return;
    m_sel = hit.row;
    if (e.mods.isPopupMenu()) { repaint(); rowMenu(hit.row); return; }
    const auto& r = song->rows[size_t(hit.row)];
    if (hit.col == Mutes && hit.mute >= 0 && r.bytes[0] < 128) {
        const int m = hit.mute;
        if (edit) edit("song mute", [&](mnm::mddump::Song& s) { auto* b = s.rows[size_t(hit.row)].bytes; const int v = ((b[4] << 8) | b[5]) ^ (1 << m); b[4] = uint8_t(v >> 8); b[5] = uint8_t(v); }, -1);
    } else if (hit.col >= Ptn && hit.col <= Tempo) {
        m_drag = hit.row; m_dragCol = hit.col; m_dragY = e.getPosition().y; m_dragV = value(r, hit.col);
        if (hit.col == Tempo && m_dragV == 0) m_dragV = 120;
    }
    repaint();
}

void MdSongEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (m_drag < 0) return;
    const int steps = (m_dragY - e.getPosition().y) / (2 * kS);
    setValue(m_drag, m_dragCol, m_dragV + steps, 7000 + m_drag * 8 + m_dragCol);
}

void MdSongEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto hit = hitAt(e.getPosition() / kS);
    if (hit.row >= 0 && hit.col == Tempo && edit)   // the tempo back to "unchanged"
        edit("song tempo", [&](mnm::mddump::Song& s) { if (hit.row < int(s.rows.size())) { s.rows[size_t(hit.row)].bytes[6] = 0xFF; s.rows[size_t(hit.row)].bytes[7] = 0xFF; } }, -1);
    repaint();
}

void MdSongEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    const auto hit = hitAt(e.getPosition() / kS);
    const auto song = getSong ? getSong() : nullptr;
    const int dir = d.deltaY > 0 ? 1 : d.deltaY < 0 ? -1 : 0;
    if (dir == 0) return;
    if (song && hit.row >= 0 && hit.row < int(song->rows.size()) && hit.col >= Ptn && hit.col <= Tempo) {
        int v = value(song->rows[size_t(hit.row)], hit.col);
        if (hit.col == Tempo && v == 0) v = 120;
        setValue(hit.row, hit.col, v + dir, 7000 + hit.row * 8 + hit.col);
        return;
    }
    const int rows = song ? int(song->rows.size()) : 0;
    m_scroll = juce::jlimit(0, juce::jmax(0, rows - rowsVisible()), m_scroll - dir);
    repaint();
}

bool MdSongEditor::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { setVisible(false); if (onClose) onClose(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { rowOp(3, m_sel); return true; }
    return false;
}

void MdSongEditor::rowMenu(int row)
{
    juce::PopupMenu m;
    m.addSectionHeader("ROW " + juce::String(row + 1));
    m.addItem(8, "Insert a pattern row before");
    m.addItem(1, "Insert a pattern row after");
    m.addItem(2, "Insert a LOOP row after");
    m.addItem(3, "Insert an END row after");
    m.addItem(7, "Duplicate");
    m.addSeparator();
    m.addItem(5, "Move up", row > 0);
    m.addItem(6, "Move down");
    m.addItem(4, "Delete");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, row](int r) {
        static const int op[9] = {-1, 0, 1, 2, 3, 4, 5, 6, 7};
        if (r > 0 && r <= 8) rowOp(op[r], row);
    });
}

void MdSongEditor::rowOp(int op, int row)
{
    if (!edit) return;
    int newSel = row;
    static const char* const labels[8] = {"song row", "song loop", "song end", "delete song row", "move song row", "move song row", "duplicate song row", "song row"};
    edit(labels[juce::jlimit(0, 7, op)], [&](mnm::mddump::Song& s) {
        const int n = int(s.rows.size());
        const int at = row < 0 || row >= n ? n : row + 1;   // after the selection (or at the end)
        auto patternRow = [&] {
            mnm::mddump::SongRow r;
            const int p = defaultPattern ? juce::jlimit(0, 127, defaultPattern()) : 0;
            r.bytes[0] = uint8_t(p); r.bytes[2] = 0; r.bytes[3] = 0; r.bytes[4] = r.bytes[5] = 0; r.bytes[6] = r.bytes[7] = 0xFF;
            r.bytes[8] = 0; r.bytes[9] = uint8_t(patternLength ? patternLength(p) : 16);
            return r;
        };
        switch (op) {
            case 0: s.rows.insert(s.rows.begin() + at, patternRow()); newSel = at; break;
            case 7: { const int b = row < 0 || row >= n ? n : row; s.rows.insert(s.rows.begin() + b, patternRow()); newSel = b; break; }
            case 1: { mnm::mddump::SongRow r; r.bytes[0] = 0xFE; r.bytes[2] = 0; r.bytes[3] = 0; s.rows.insert(s.rows.begin() + at, r); newSel = at; break; }
            case 2: { mnm::mddump::SongRow r; r.bytes[0] = 0xFF; s.rows.insert(s.rows.begin() + at, r); newSel = at; break; }
            case 3: if (row >= 0 && row < n) { s.rows.erase(s.rows.begin() + row); newSel = juce::jmin(row, n - 2); } break;
            case 4: if (row > 0 && row < n) { std::swap(s.rows[size_t(row)], s.rows[size_t(row - 1)]); newSel = row - 1; } break;
            case 5: if (row >= 0 && row + 1 < n) { std::swap(s.rows[size_t(row)], s.rows[size_t(row + 1)]); newSel = row + 1; } break;
            case 6: if (row >= 0 && row < n) { s.rows.insert(s.rows.begin() + row + 1, s.rows[size_t(row)]); newSel = row + 1; } break;
            default: break;
        }
    }, -1);
    m_sel = newSel;
    const auto song = getSong ? getSong() : nullptr;
    const int rows = song ? int(song->rows.size()) : 0;
    if (m_sel >= 0 && m_sel < m_scroll) m_scroll = m_sel;
    if (m_sel >= m_scroll + rowsVisible()) m_scroll = m_sel - rowsVisible() + 1;
    m_scroll = juce::jlimit(0, juce::jmax(0, rows - rowsVisible()), m_scroll);
    repaint();
}

MdKitStrip::Part MdKitStrip::partAt(juce::Point<int> p) const
{
    const auto lcd = p / kS;
    for (int i = int(m_rects.size()) - 1; i >= 0; --i) if (m_rects[size_t(i)].contains(lcd)) return Part(i);
    return None;
}

void MdKitStrip::mouseDown(const juce::MouseEvent& e) { const auto p = partAt(e.getPosition()); if (p != None && onPart) onPart(p); }

void MdKitStrip::mouseMove(const juce::MouseEvent& e)
{
    const auto p = partAt(e.getPosition());
    if (p == m_hover) return;
    m_hover = p;
    static const char* const tips[] = {"Previous kit", "Kits", "Next kit", "Save this kit to the library",
                                       "Previous sound of this machine", "Sounds for the selected track", "Next sound of this machine", "Save this track's sound to the library", "Library"};
    setTooltip(p == None ? juce::String() : juce::String(tips[int(p)]));
    setMouseCursor(p == None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

// ---------------------------------------------------------------------------------------------- dropdown

void MdLibraryDrop::openKits(const juce::String& currentKey, juce::Point<int> topLeft, int maxHeight)
{
    m_kits = true; m_current = currentKey;
    open(topLeft, maxHeight);
}

void MdLibraryDrop::openSounds(int machine, int track, const juce::String& currentKey, juce::Point<int> topLeft, int maxHeight)
{
    if (!m_kits && machine != m_machine) m_alt = false;
    if (m_kits) m_alt = false;
    m_kits = false; m_machine = machine; m_track = track; m_current = currentKey;
    open(topLeft, maxHeight);
}

void MdLibraryDrop::open(juce::Point<int> topLeft, int maxHeight)
{
    m_scroll = 0; m_hover = -1;
    rebuild();
    const int want = (kHeadH + kFootH + juce::jmax(4, int(m_rows.size())) * kRowH + 1) * kS;
    setBounds(topLeft.x, topLeft.y, kLcdW * kS, juce::jmin(want, maxHeight));
    for (int i = 0; i < int(m_rows.size()); ++i)   // start with the loaded item in view
        if (!m_rows[size_t(i)].header && m_rows[size_t(i)].key == m_current)
            m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - listRows()), i - listRows() / 2);
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    repaint();
}

void MdLibraryDrop::rebuild()
{
    m_rows.clear();
    auto header = [&](const juce::String& text) { Row h; h.header = true; h.name = text; m_rows.push_back(h); };
    if (m_kits) {
        const auto all = m_lib.kits();
        m_countA = int(all.size()); m_countB = 0;
        for (const auto& k : all) m_countB += k.favourite ? 1 : 0;
        juce::String lastSource = "\x01";
        for (const auto& k : all) {
            if (m_alt && !k.favourite) continue;
            if (k.sourceId != lastSource) { lastSource = k.sourceId; header(k.source); }
            Row r; r.key = k.key; r.name = k.name; r.fav = k.favourite; r.kit = k;
            r.right = k.position >= 0 ? juce::String(k.position + 1).paddedLeft('0', 2) : juce::String();
            m_rows.push_back(r);
        }
    } else {
        const auto mine = m_lib.sounds(m_machine);
        const auto all = m_alt ? m_lib.sounds(-1) : mine;
        m_countA = int(mine.size()); m_countB = int(m_lib.sounds(-1).size());
        int last = -1;
        for (const auto& s : all) {
            if (m_alt && s.machine != last) { last = s.machine; header(machineName(s.machine)); }
            Row r; r.key = s.key; r.name = s.name; r.fav = s.favourite; r.sound = s; r.right = s.saved ? juce::String("SAVED") : s.source;
            m_rows.push_back(r);
        }
    }
    m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - listRows()), m_scroll);
}

int MdLibraryDrop::rowAt(juce::Point<int> lcd) const
{
    const int y = lcd.y - kHeadH;
    if (y < 0 || lcd.y >= getHeight() / kS - kFootH) return -1;
    const int i = m_scroll + y / kRowH;
    return i >= 0 && i < int(m_rows.size()) && y / kRowH < listRows() ? i : -1;
}

void MdLibraryDrop::paint(juce::Graphics& g)
{
    const int w = getWidth() / kS, h = getHeight() / kS;
    LcdCanvas cv(w, h);
    frame(cv, {0, 0, w, h});
    {   // scope: kits: all | favourites; sounds: this machine | every machine
        const juce::String a = m_kits ? "ALL KITS (" + juce::String(m_countA) + ")" : machineName(m_machine) + " (" + juce::String(m_countA) + ")";
        const juce::String b = m_kits ? "FAVOURITES (" + juce::String(m_countB) + ")" : "ALL MACHINES (" + juce::String(m_countB) + ")";
        const int half = w / 2;
        if (!m_alt) cv.fillRect(1, 1, half - 1, kHeadH - 2, true); else cv.fillRect(half, 1, w - half - 1, kHeadH - 2, true);
        cv.textCentred(spec::kFontSmall4x5, a.toRawUTF8(), 0, half, 5, m_alt);
        cv.textCentred(spec::kFontSmall4x5, b.toRawUTF8(), half, w - half, 5, !m_alt);
        cv.fillRect(0, kHeadH - 1, w, 1, true);
    }
    for (int n = 0; n < listRows(); ++n) {
        const int i = m_scroll + n;
        if (i >= int(m_rows.size())) break;
        const auto& row = m_rows[size_t(i)];
        const int y = kHeadH + n * kRowH;
        if (row.header) {
            const auto name = fit(spec::kFontSmall4x5, row.name, w - 20);
            cv.text(spec::kFontSmall4x5, name.toRawUTF8(), 4, y + 5, true);
            cv.dotsH(4 + LcdCanvas::textWidth(spec::kFontSmall4x5, name.toRawUTF8()) + 3, w - 5, y + 7);
            continue;
        }
        const bool on = row.key == m_current;
        if (on) cv.fillRect(1, y, w - 2, kRowH, true);
        else if (i == m_hover) dottedFrame(cv, {1, y, w - 2, kRowH});
        const auto right = fit(spec::kFontTiny3x5, row.right, 60);
        const int rw = LcdCanvas::textWidth(spec::kFontTiny3x5, right.toRawUTF8());
        const bool playing = isPlaying && isPlaying(row.key);
        playGlyph(cv, {2, y, 11, kRowH}, playing, !on);   // audition
        int nx = 15;
        if (playing && looping) { drawLoopGlyph(cv, 16, y + (kRowH - kLoopGlyphH) / 2, looping(), !on); nx += 13; }   // the loop toggle beside the stop
        textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, row.name, w - 16 - nx - rw), nx, y + 2, !on, row.fav);
        cv.text(spec::kFontTiny3x5, right.toRawUTF8(), w - 6 - rw, y + 4, !on);
    }
    if (m_rows.empty())
        cv.textCentred(spec::kFontSmall4x5, m_kits ? (m_alt ? "NO FAVOURITES YET" : "IMPORT A MACHINEDRUM .SYX") : "NO SOUNDS FOR THIS MACHINE YET", 0, w, kHeadH + 8, true);
    if (int(m_rows.size()) > listRows()) {   // scroll position
        const int trackH = listRows() * kRowH, barH = juce::jmax(6, trackH * listRows() / int(m_rows.size()));
        cv.fillRect(w - 3, kHeadH + (trackH - barH) * m_scroll / juce::jmax(1, int(m_rows.size()) - listRows()), 2, barH, true);
    }
    const int fy = h - kFootH;
    cv.fillRect(0, fy, w, 1, true);
    // the footer: the LIBRARY link (the panel on this list's tab), kits also IMPORT .SYX, then a hint in what is left
    const int lw = LcdCanvas::textWidth(spec::kFontSmall4x5, "LIBRARY") + 8;
    cv.fillRect(w - lw - 2, fy + 2, lw, kFootH - 4, true);
    cv.text(spec::kFontSmall4x5, "LIBRARY", w - lw + 2, fy + 5, false);
    int hintW = w - lw - 10;
    if (m_kits) {
        const int iw = LcdCanvas::textWidth(spec::kFontSmall4x5, "IMPORT .SYX") + 8;
        cv.fillRect(w - lw - iw - 4, fy + 2, iw, kFootH - 4, true);
        cv.text(spec::kFontSmall4x5, "IMPORT .SYX", w - lw - iw, fy + 5, false);
        hintW -= iw + 2;
    }
    const juce::String hint = m_kits ? juce::String("CLICK = LOAD")
                                     : "LOAD ON T" + juce::String(m_track + 1) + (m_alt ? juce::String(" (CHANGES THE MACHINE)") : juce::String());
    cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, hint, hintW).toRawUTF8(), 4, fy + 5, true);
    cv.draw(g, 0, 0, kS);
}

void MdLibraryDrop::mouseDown(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kS;
    const int w = getWidth() / kS, h = getHeight() / kS;
    if (lcd.y < kHeadH) { m_alt = lcd.x >= w / 2; m_scroll = 0; rebuild(); repaint(); return; }
    if (lcd.y >= h - kFootH) {
        const int lw = LcdCanvas::textWidth(spec::kFontSmall4x5, "LIBRARY") + 8, iw = LcdCanvas::textWidth(spec::kFontSmall4x5, "IMPORT .SYX") + 8;
        const bool kits = m_kits;
        if (lcd.x >= w - lw - 2) { close(); if (onLibrary) onLibrary(kits); }
        else if (kits && lcd.x >= w - lw - iw - 4 && onImport) { close(); onImport(); }
        return;
    }
    const int i = rowAt(lcd);
    if (i < 0 || m_rows[size_t(i)].header) return;
    const auto row = m_rows[size_t(i)];
    if (e.mods.isPopupMenu()) { m_lib.setFavourite(row.key, !row.fav); rebuild(); repaint(); return; }
    if (lcd.x < 14) { if (onAudition) onAudition(row.key, m_kits); repaint(); return; }   // the glyph: hear it first
    if (lcd.x < 27 && isPlaying && isPlaying(row.key)) { if (toggleLoop) toggleLoop(); repaint(); return; }   // the loop toggle
    close();
    if (m_kits) { if (onLoadKit) onLoadKit(row.kit); }
    else if (onLoadSound) onLoadSound(row.sound);
}

void MdLibraryDrop::mouseMove(const juce::MouseEvent& e)
{
    const int i = rowAt(e.getPosition() / kS);
    if (i != m_hover) { m_hover = i; repaint(); }
}

void MdLibraryDrop::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& d)
{
    const int step = d.deltaY > 0 ? -2 : d.deltaY < 0 ? 2 : 0;
    m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - listRows()), m_scroll + step);
    repaint();
}

bool MdLibraryDrop::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------- save dialog

void MdSaveDialog::open(const juce::String& title, const juce::String& name, const juce::String& projectOption, const juce::String& versionOf)
{
    m_project = projectOption.toUpperCase();
    m_versionOf = versionOf.toUpperCase();
    m_asVersion = m_versionOf.isNotEmpty();
    m_intoProject = false;
    m_title = title.toUpperCase();
    m_name = name.toUpperCase().substring(0, 16);
    m_error.clear();
    setVisible(true);
    toFront(true);
    grabKeyboardFocus();
    repaint();
}

void MdSaveDialog::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper.withAlpha(0.6f));
    const auto b = box();
    LcdCanvas cv(kLcdW, kLcdH);
    cv.clear(false);
    frame(cv, {0, 0, kLcdW, kLcdH});
    cv.fillRect(0, 0, kLcdW, 11, true);
    cv.text(spec::kFontBold8, m_title.toRawUTF8(), 4, 2, false);
    cv.text(spec::kFontTiny3x5, "NAME", 6, 20, true);
    dottedFrame(cv, {28, 15, kLcdW - 34, 15});
    textMarked(cv, spec::kFontBold8, m_name, 32, 19, true, false, true);
    m_version = {};
    if (m_versionOf.isNotEmpty()) {   // a new version of the loaded item (its history keeps the old ones), or a new item
        m_version = {6, 33, kLcdW - 12, 12};
        frame(cv, {6, 34, 9, 9});
        if (m_asVersion) cv.fillRect(8, 36, 5, 5, true);
        cv.text(spec::kFontSmall4x5, fit(spec::kFontSmall4x5, "NEW VERSION OF " + m_versionOf, kLcdW - 30).toRawUTF8(), 19, 36, true);
    } else {
        cv.text(spec::kFontTiny3x5, "A NEW ITEM IN THE LIBRARY; NOTHING IS OVERWRITTEN", 6, 37, true);
    }
    if (m_error.isNotEmpty()) cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, m_error.toUpperCase(), kLcdW - 128).toRawUTF8(), 6, kLcdH - 14, true);
    m_option = {};
    if (m_project.isNotEmpty()) {   // also into the project slot it came from, as a new version of the project
        m_option = {6, 46, kLcdW - 12, 13};
        frame(cv, {6, 47, 9, 9});
        if (m_intoProject) cv.fillRect(8, 49, 5, 5, true);
        cv.text(spec::kFontSmall4x5, fit(spec::kFontSmall4x5, "ALSO PUT IT INTO " + m_project, kLcdW - 30).toRawUTF8(), 19, 49, true);
        cv.text(spec::kFontTiny3x5, "(A NEW VERSION OF THE PROJECT; EXPORT IT TO THE UNIT FROM THE LIBRARY APP)", 19, 57, true);
    }
    m_cancel = {kLcdW - 116, kLcdH - 19, 52, 13};
    m_save = {kLcdW - 58, kLcdH - 19, 52, 13};
    frame(cv, m_cancel);
    cv.textCentred(spec::kFontSmall4x5, "CANCEL", m_cancel.getX(), m_cancel.getWidth(), m_cancel.getY() + 4, true);
    cv.fillRect(m_save.getX(), m_save.getY(), m_save.getWidth(), m_save.getHeight(), true);
    cv.textCentred(spec::kFontSmall4x5, "SAVE", m_save.getX(), m_save.getWidth(), m_save.getY() + 4, false);
    cv.draw(g, b.getX(), b.getY(), kS);
}

void MdSaveDialog::save()
{
    if (m_name.trim().isEmpty()) { m_error = "TYPE A NAME"; repaint(); return; }
    const auto err = onSave ? onSave(m_name.trim(), m_intoProject, m_asVersion && m_versionOf.isNotEmpty()) : juce::String();
    if (err.isNotEmpty()) { m_error = err; repaint(); return; }
    setVisible(false);
}

void MdSaveDialog::mouseDown(const juce::MouseEvent& e)
{
    const auto b = box();
    if (!b.contains(e.getPosition())) { setVisible(false); return; }
    const auto lcd = (e.getPosition() - b.getPosition()) / kS;
    if (m_option.contains(lcd)) { m_intoProject = !m_intoProject; repaint(); return; }
    if (m_version.contains(lcd)) { m_asVersion = !m_asVersion; repaint(); return; }
    if (m_cancel.contains(lcd)) setVisible(false);
    else if (m_save.contains(lcd)) save();
}

bool MdSaveDialog::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { setVisible(false); return true; }
    if (k == juce::KeyPress::returnKey) { save(); return true; }
    if (typeInto(m_name, k, 16)) { m_error.clear(); repaint(); return true; }
    return false;
}

} // namespace mnm::plugin::md
