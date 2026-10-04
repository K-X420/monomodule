#include "MdLibraryUi.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

// ---------------------------------------------------------------------------------------------- strip

void MdKitStrip::setKit(const juce::String& name, bool modified)
{
    const auto n = name.isEmpty() ? juce::String("DEFAULT") : name.toUpperCase();
    if (n == m_kit && modified == m_modified) return;
    m_kit = n; m_modified = modified;
    repaint();
}

void MdKitStrip::resized()
{
    // joined parts share their edges: each starts on the previous part's last column
    const int w = getWidth() / kS, arrowW = 12, iconW = 15;
    const int kitW = w - 2 * (arrowW - 1) - (iconW - 1);
    int x = 0;
    auto take = [&](Part p, int pw) { m_rects[size_t(p)] = {x, 0, pw, kLcdH}; x += pw - 1; };
    take(Prev, arrowW); take(Kit, kitW); take(Next, arrowW); take(Save, iconW);
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
    auto r = part(Prev, false); arrowH(cv, r.getCentreX(), r.getCentreY(), true, true);
    r = part(Kit, m_open);
    const int lw = LcdCanvas::textWidth(spec::kFontTiny3x5, "KIT");
    cv.text(spec::kFontTiny3x5, "KIT", r.getX() + 4, r.getY() + 5, !m_open);
    const int nx = r.getX() + 4 + lw + 4, maxW = r.getRight() - 10 - nx;
    textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, m_kit, maxW - (m_modified ? 7 : 0)), nx, r.getY() + 4, !m_open, m_modified);
    caret(cv, r.getRight() - 9, r.getY() + 6, m_open, !m_open);
    r = part(Next, false); arrowH(cv, r.getCentreX(), r.getCentreY(), false, true);
    r = part(Save, m_modified); pixelIcon(cv, kIconSave, 9, r.getX() + 3, r.getY() + 3, !m_modified);
    cv.draw(g, 0, 0, kS);
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
    static const char* const tips[] = {"Previous kit", "Kits", "Next kit", "Save this kit to the library"};
    setTooltip(p == None ? juce::String() : juce::String(tips[int(p)]));
    setMouseCursor(p == None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

// ---------------------------------------------------------------------------------------------- dropdown

void MdKitDrop::open(const juce::String& currentKey, juce::Point<int> topLeft, int maxHeight)
{
    m_current = currentKey; m_scroll = 0; m_hover = -1;
    rebuild();
    const int want = (kHeadH + kFootH + juce::jmax(4, int(m_rows.size())) * kRowH + 1) * kS;
    setBounds(topLeft.x, topLeft.y, kLcdW * kS, juce::jmin(want, maxHeight));
    for (int i = 0; i < int(m_rows.size()); ++i)   // start with the loaded kit in view
        if (!m_rows[size_t(i)].header && m_rows[size_t(i)].kit.key == m_current)
            m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - listRows()), i - listRows() / 2);
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    repaint();
}

void MdKitDrop::rebuild()
{
    m_rows.clear();
    const auto all = m_lib.kits();
    m_allCount = int(all.size());
    m_favCount = 0;
    for (const auto& k : all) m_favCount += k.favourite ? 1 : 0;
    juce::String lastSource = "\x01";
    for (const auto& k : all) {
        if (m_favs && !k.favourite) continue;
        if (k.sourceId != lastSource) { lastSource = k.sourceId; Row h; h.header = true; h.name = k.source; m_rows.push_back(h); }
        Row r; r.kit = k; r.name = k.name;
        m_rows.push_back(r);
    }
    m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - listRows()), m_scroll);
}

int MdKitDrop::rowAt(juce::Point<int> lcd) const
{
    const int y = lcd.y - kHeadH;
    if (y < 0 || lcd.y >= getHeight() / kS - kFootH) return -1;
    const int i = m_scroll + y / kRowH;
    return i >= 0 && i < int(m_rows.size()) && y / kRowH < listRows() ? i : -1;
}

void MdKitDrop::paint(juce::Graphics& g)
{
    const int w = getWidth() / kS, h = getHeight() / kS;
    LcdCanvas cv(w, h);
    frame(cv, {0, 0, w, h});
    {   // scope: every kit | favourites
        const juce::String a = "ALL KITS (" + juce::String(m_allCount) + ")", b = "FAVOURITES (" + juce::String(m_favCount) + ")";
        const int half = w / 2;
        if (!m_favs) cv.fillRect(1, 1, half - 1, kHeadH - 2, true); else cv.fillRect(half, 1, w - half - 1, kHeadH - 2, true);
        cv.textCentred(spec::kFontSmall4x5, a.toRawUTF8(), 0, half, 5, m_favs);
        cv.textCentred(spec::kFontSmall4x5, b.toRawUTF8(), half, w - half, 5, !m_favs);
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
        const bool on = row.kit.key == m_current;
        if (on) cv.fillRect(1, y, w - 2, kRowH, true);
        else if (i == m_hover) dottedFrame(cv, {1, y, w - 2, kRowH});
        const juce::String right = row.kit.position >= 0 ? juce::String(row.kit.position + 1).paddedLeft('0', 2) : juce::String();
        const int rw = LcdCanvas::textWidth(spec::kFontTiny3x5, right.toRawUTF8());
        textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, row.name, w - 22 - rw), 6, y + 2, !on, row.kit.favourite);
        cv.text(spec::kFontTiny3x5, right.toRawUTF8(), w - 6 - rw, y + 4, !on);
    }
    if (m_rows.empty())
        cv.textCentred(spec::kFontSmall4x5, m_favs ? "NO FAVOURITES YET" : "IMPORT A MACHINEDRUM .SYX", 0, w, kHeadH + 8, true);
    if (int(m_rows.size()) > listRows()) {   // scroll position
        const int trackH = listRows() * kRowH, barH = juce::jmax(6, trackH * listRows() / int(m_rows.size()));
        cv.fillRect(w - 3, kHeadH + (trackH - barH) * m_scroll / juce::jmax(1, int(m_rows.size()) - listRows()), 2, barH, true);
    }
    const int fy = h - kFootH;
    cv.fillRect(0, fy, w, 1, true);
    cv.text(spec::kFontTiny3x5, "CLICK = LOAD   RIGHT-CLICK = FAVOURITE", 4, fy + 5, true);
    const int iw = LcdCanvas::textWidth(spec::kFontSmall4x5, "IMPORT .SYX") + 8;
    cv.fillRect(w - iw - 2, fy + 2, iw, kFootH - 4, true);
    cv.text(spec::kFontSmall4x5, "IMPORT .SYX", w - iw + 2, fy + 5, false);
    cv.draw(g, 0, 0, kS);
}

void MdKitDrop::mouseDown(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kS;
    const int w = getWidth() / kS, h = getHeight() / kS;
    if (lcd.y < kHeadH) { m_favs = lcd.x >= w / 2; m_scroll = 0; rebuild(); repaint(); return; }
    if (lcd.y >= h - kFootH) {
        const int iw = LcdCanvas::textWidth(spec::kFontSmall4x5, "IMPORT .SYX") + 8;
        if (lcd.x >= w - iw - 2 && onImport) { close(); onImport(); }
        return;
    }
    const int i = rowAt(lcd);
    if (i < 0 || m_rows[size_t(i)].header) return;
    const auto kit = m_rows[size_t(i)].kit;
    if (e.mods.isPopupMenu()) { m_lib.setFavourite(kit.key, !kit.favourite); rebuild(); repaint(); return; }
    close();
    if (onLoad) onLoad(kit);
}

void MdKitDrop::mouseMove(const juce::MouseEvent& e)
{
    const int i = rowAt(e.getPosition() / kS);
    if (i != m_hover) { m_hover = i; repaint(); }
}

void MdKitDrop::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& d)
{
    const int step = d.deltaY > 0 ? -2 : d.deltaY < 0 ? 2 : 0;
    m_scroll = juce::jlimit(0, juce::jmax(0, int(m_rows.size()) - listRows()), m_scroll + step);
    repaint();
}

bool MdKitDrop::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------- save dialog

void MdSaveDialog::open(const juce::String& name)
{
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
    cv.text(spec::kFontBold8, "SAVE KIT", 4, 2, false);
    cv.text(spec::kFontTiny3x5, "NAME", 6, 20, true);
    dottedFrame(cv, {28, 15, kLcdW - 34, 15});
    textMarked(cv, spec::kFontBold8, m_name, 32, 19, true, false, true);
    const juce::String note = m_error.isNotEmpty() ? m_error.toUpperCase() : juce::String("A NEW KIT IN THE LIBRARY; NOTHING IS OVERWRITTEN");
    cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, note, kLcdW - 12).toRawUTF8(), 6, 37, true);
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
    const auto err = onSave ? onSave(m_name.trim()) : juce::String();
    if (err.isNotEmpty()) { m_error = err; repaint(); return; }
    setVisible(false);
}

void MdSaveDialog::mouseDown(const juce::MouseEvent& e)
{
    const auto b = box();
    if (!b.contains(e.getPosition())) { setVisible(false); return; }
    const auto lcd = (e.getPosition() - b.getPosition()) / kS;
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
