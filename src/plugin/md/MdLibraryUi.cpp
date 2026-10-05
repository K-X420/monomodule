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
    take(Grid, 30);
    x += 1 + gap;
    take(TrkPrev, arrowW); take(Trk, 64); take(TrkNext, arrowW);
    x += 1 + gap;
    take(PtnPrev, arrowW); take(Ptn, 46); take(PtnNext, arrowW);
    x += 1 + gap;
    take(Pages, 22 + 4 * 11 + 2);
    x += 1 + gap;
    take(Edit, 30);
    x += 1 + gap;
    take(Step, juce::jmax(40, getWidth() / kS - x));
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
        cv.text(spec::kFontTiny3x5, m_s.playing ? "STOP" : "PLAY", r.getX() + 14, r.getY() + 5, ink);
        (void) ink;
    }
    {   // GRID: solid while the keys are steps
        const auto r = box(Grid, m_s.grid);
        cv.text(spec::kFontBold8, "GRID", r.getX() + 4, r.getY() + 4, !m_s.grid);
    }
    auto arrows = [&](Part prev, Part next) {
        auto r = box(prev, false); arrowH(cv, r.getCentreX(), r.getCentreY(), true, true);
        r = box(next, false); arrowH(cv, r.getCentreX(), r.getCentreY(), false, true);
    };
    auto labelled = [&](Part p, const char* label, const juce::String& text) {
        const auto r = box(p, false);
        cv.text(spec::kFontTiny3x5, label, r.getX() + 4, r.getY() + 5, true);
        const int lw = LcdCanvas::textWidth(spec::kFontTiny3x5, label);
        cv.text(spec::kFontBold8, fit(spec::kFontBold8, text, r.getRight() - (r.getX() + 8 + lw) - 3).toRawUTF8(), r.getX() + 8 + lw, r.getY() + 4, true);
    };
    arrows(TrkPrev, TrkNext);
    labelled(Trk, "TRK", "T" + juce::String(m_s.track + 1) + " " + m_s.machine);
    arrows(PtnPrev, PtnNext);
    labelled(Ptn, "PTN", juce::String(kPatternNames[juce::jlimit(0, 127, m_s.pattern)]) + (m_s.empty ? "-" : ""));
    {   // the pages: a ring each (a point past the length), solid when playing, underlined when GRID shows it
        const auto r = box(Pages, false);
        cv.text(spec::kFontTiny3x5, "PAGE", r.getX() + 4, r.getY() + 5, true);
        const int pages = (juce::jlimit(1, 64, m_s.length) + 15) / 16, playing = m_s.step >= 0 ? m_s.step / 16 : -1;
        for (int i = 0; i < 4; ++i) {
            const int x = dotX(i), y = r.getY() + 3;
            if (i >= pages) { cv.fillRect(x + 3, y + 3, 1, 1, true); continue; }
            static const char* const ring[7] = {"..###..", ".#...#.", "#.....#", "#.....#", "#.....#", ".#...#.", "..###.."};
            static const char* const disc[7] = {"..###..", ".#####.", "#######", "#######", "#######", ".#####.", "..###.."};
            const auto& shape = i == playing ? disc : ring;
            for (int dy = 0; dy < 7; ++dy) for (int dx = 0; dx < 7; ++dx) if (shape[dy][dx] == '#') cv.set(x + dx, y + dy, true);
            if (m_s.grid && i == m_s.page) cv.fillRect(x, y + 9, 7, 1, true);
        }
    }
    {   // EDIT: the copy / paste / clear menu
        const auto r = box(Edit, false);
        cv.text(spec::kFontBold8, "EDIT", r.getX() + 4, r.getY() + 4, true);
    }
    {
        juce::String s = m_s.step >= 0 ? juce::String(m_s.step + 1).paddedLeft('0', 2) + "/" + juce::String(m_s.length) : "--/" + juce::String(m_s.length);
        if (m_s.row >= 0) s << "  ROW " << (m_s.row + 1);
        labelled(Step, "STEP", s);
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
    if (p == Pages && e.mods.isPopupMenu()) { if (onPart) onPart(Edit); return; }
    if (p == Pages) {
        for (int i = 0; i < 4; ++i)
            if (lcd.x >= dotX(i) - 2 && lcd.x <= dotX(i) + 8) { if (onPage) onPage(i); return; }
        return;
    }
    if (p != None && onPart) onPart(p);
}

void MdSeqBar::mouseMove(const juce::MouseEvent& e)
{
    const auto p = partAt(e.getPosition() / kS);
    if (p != m_hover) { m_hover = p; repaint(); }
    switch (p) {
        case Play: setTooltip(m_s.hostPlaying ? "The host's transport is running: the pattern follows it" : "Play / stop the pattern on the plugin's own clock (while the host is stopped)"); break;
        case Grid: setTooltip(m_s.grid ? "GRID: the keys are the selected track's steps (click a step to place a trig). Click to get the tracks back"
                                       : "GRID: click to place steps with the keys (the selected track's steps; TRK < > picks the track)"); break;
        case TrkPrev: case TrkNext: case Trk: setTooltip("The track whose steps GRID shows and edits"); break;
        case PtnPrev: case PtnNext: case Ptn: setTooltip("The pattern (PTN): played by SEQ, edited by GRID (\"-\" = an empty slot)"); break;
        case Pages: setTooltip("Pages of 16 steps: solid = playing, underlined = shown by GRID. Click one to show it; right-click: EDIT"); break;
        case Edit: setTooltip("Copy / paste / clear the shown page, the track or the pattern; double the pattern"); break;
        default: setTooltip({}); break;
    }
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
