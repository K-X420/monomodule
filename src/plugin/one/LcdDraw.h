// Small LCD drawing helpers shared by the library UIs (Monomodule One / Six, Monomodule MD): frames, arrows, icons,
// fitted and marked text, and short upper-case text entry.
#pragma once
#include "Lcd.h"

namespace mnm::plugin::one {

// The text cut to maxW LCD pixels (the faces are proportional).
inline juce::String fit(const spec::Font& f, juce::String s, int maxW)
{
    while (s.isNotEmpty() && LcdCanvas::textWidth(f, s.toRawUTF8()) > maxW) s = s.dropLastCharacters(1);
    return s;
}

inline void frame(LcdCanvas& cv, juce::Rectangle<int> r, bool on = true)
{
    cv.fillRect(r.getX(), r.getY(), r.getWidth(), 1, on); cv.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1, on);
    cv.fillRect(r.getX(), r.getY(), 1, r.getHeight(), on); cv.fillRect(r.getRight() - 1, r.getY(), 1, r.getHeight(), on);
}

inline void dottedFrame(LcdCanvas& cv, juce::Rectangle<int> r)
{
    cv.dotsH(r.getX(), r.getRight() - 1, r.getY()); cv.dotsH(r.getX(), r.getRight() - 1, r.getBottom() - 1);
    cv.dotsV(r.getX(), r.getY(), r.getBottom() - 1); cv.dotsV(r.getRight() - 1, r.getY(), r.getBottom() - 1);
}

// Play triangle (or the stop block while it plays), 5x7, centred in the zone.
inline void playGlyph(LcdCanvas& cv, juce::Rectangle<int> zone, bool playing, bool on)
{
    const int x = zone.getCentreX() - 2, y = zone.getCentreY() - 3;
    if (playing) { cv.fillRect(x, y + 1, 5, 5, on); return; }
    for (int c = 0; c < 4; ++c) cv.fillRect(x + c, y + c, 1, 7 - 2 * c, on);
}

inline void pixelIcon(LcdCanvas& cv, const char* const* rows, int n, int x, int y, bool on)
{
    for (int r = 0; r < n; ++r)
        for (int c = 0; rows[r][c]; ++c) if (rows[r][c] == '#') cv.set(x + c, y + r, on);
}

inline const char* const kIconSpeaker[] = {"...#...", "..##..#", "##.#.#.", "#..#.#.", "##.#.#.", "..##..#", "...#..."};   // 7x7: a speaker, its sound
inline const char* const kIconMuted[] = {"...#...", "..##...", "##.##.#", "#..#.#.", "##.##.#", "..##...", "...#..."};     // 7x7: the speaker, crossed
inline const char* const kIconLock[] = {"..###..", ".#...#.", ".#...#.", "#######", "#.....#", "#..#..#", "#######"};       // 7x7 padlock
inline const char* const kIconFaders[] = {".#..#..#.", ".#..#..#.", "###.#..#.", ".#.###.#.", ".#..#..#.", ".#..#.###", ".#..#..#.", ".#..#..#.", ".#..#..#."};   // 9x9
inline const char* const kIconDouble[] = {"###.###", "#.#.#.#", "#.#.#.#", "#.#.#.#", "###.###"};   // 7x5: two pages
inline const char* const kIconDelPage[] = {"#####...", "#...#...", "#...#...", "#...#...", "#####..."};   // 5x5: a page (DEL under it)
inline const char* const kIconSave[] = {   // a disk
    "########.", "#.#..#.##", "#.#..#..#", "#.####..#", "#.......#", "#.#####.#", "#.#...#.#", "#.#...#.#", "#########"};
inline const char* const kIconLibrary[] = {   // four slots
    "####.####", "#..#.#..#", "#..#.#..#", "####.####", ".........", "####.####", "#..#.#..#", "#..#.#..#", "####.####"};

inline void arrowH(LcdCanvas& cv, int cx, int cy, bool left, bool on)
{
    // left: the point at the left (column cx - 2), widening to the right; right: the mirror
    for (int c = 0; c < 4; ++c) { const int x = cx - 2 + c; const int h = left ? 2 * c + 1 : 7 - 2 * c; cv.fillRect(x, cy - h / 2, 1, h, on); }
}

inline void caret(LcdCanvas& cv, int x, int y, bool up, bool on)
{
    for (int r = 0; r < 3; ++r) { const int w = up ? 1 + 2 * r : 5 - 2 * r; cv.fillRect(x + (5 - w) / 2, y + r, w, 1, on); }
}

// Text followed by a mark the faces have no glyph for: a 3x3 block (modified, favourite) or a cursor bar.
inline int textMarked(LcdCanvas& cv, const spec::Font& f, const juce::String& s, int x, int y, bool on, bool block, bool cursor = false)
{
    cv.text(f, s.toRawUTF8(), x, y, on);
    int end = x + LcdCanvas::textWidth(f, s.toRawUTF8());
    if (block) { cv.fillRect(end + 3, y + f.h / 2 - 2, 3, 3, on); end += 6; }
    if (cursor) { cv.fillRect(end + (s.isEmpty() ? 0 : 2), y + f.h - 1, 5, 1, on); end += 7; }
    return end;
}

// Typing edits a short upper-case ASCII string (the faces have no lower case). Returns true when the key was used.
inline bool typeInto(juce::String& s, const juce::KeyPress& k, int maxLen)
{
    if (k == juce::KeyPress::backspaceKey) { s = s.dropLastCharacters(1); return true; }
    const auto c = k.getTextCharacter();
    if (c >= 32 && c < 127 && !k.getModifiers().isCommandDown()) { if (s.length() < maxLen) s += juce::String::charToString(c).toUpperCase(); return true; }
    return false;
}

} // namespace mnm::plugin::one
