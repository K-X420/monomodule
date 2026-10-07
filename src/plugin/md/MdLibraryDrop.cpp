// The library dropdown: kits and sounds, filtered by machine
#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

namespace {
juce::String machineName(int id) { return juce::String(mnm::mdnames::machineName(id)); }
}

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

} // namespace mnm::plugin::md
