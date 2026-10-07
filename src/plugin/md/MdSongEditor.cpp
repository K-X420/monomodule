// The song editor: rows, JUMP / HALT, the start / cue arrows
#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

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
                if (b[0] == 0xFE) { b[3] = uint8_t(juce::jlimit(0, juce::jmax(0, int(s.rows.size()) - 1), v)); if (b[3] > row) b[2] = 0; }   // forward: a JUMP
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
        else if (b[0] == 0xFE && b[3] == ri) { put(Ptn, "HALT", true); }   // a loop onto itself: the song stops here
        else if (b[0] == 0xFE && b[3] > ri) {                               // a loop forward: a JUMP (no count)
            put(Ptn, "JUMP", true);
            put(Start, "TO " + juce::String(int(b[3]) + 1));
        }
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
        {   // the start row: two filled arrows; the cued row (plays next): two hollow ones
            const int st = startRow ? startRow() : -1, cu = cuedRow ? cuedRow() : -1;
            if (ri == st || ri == cu) {
                const bool filled = ri == st && ri != cu;
                static const char* const solid[7] = {"#...", "##..", "###.", "####", "###.", "##..", "#..."};
                static const char* const hollow[7] = {"#...", "##..", "#.#.", "#..#", "#.#.", "##..", "#..."};
                const auto* a = filled ? solid : hollow;
                for (int yy = 0; yy < 7; ++yy)
                    for (int xx = 0; xx < 4; ++xx)
                        if (a[yy][xx] == '#') { cv.set(kSeColX[Ptn] - 7 + xx, y - 1 + yy, ink); cv.set(kSeColX[Mutes] - 4 - xx, y - 1 + yy, ink); }
            }
        }
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

// The MD's song edit keys (Ctrl = FUNCTION): UP / DOWN pick a row, Ctrl+DOWN inserts a row, Ctrl+UP deletes it,
// Ctrl+C / Ctrl+V copy and paste a row, Delete deletes it, ENTER starts from it (stopped) or cues it (playing)
bool MdSongEditor::keyPressed(const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const bool fn = mods.isCtrlDown() || mods.isCommandDown();
    const int code = k.getKeyCode();
    const auto song = getSong ? getSong() : nullptr;
    const int rows = song ? int(song->rows.size()) : 0;
    auto select = [&](int r) {
        m_sel = juce::jlimit(-1, rows - 1, r);
        if (m_sel >= 0 && m_sel < m_scroll) m_scroll = m_sel;
        if (m_sel >= m_scroll + rowsVisible()) m_scroll = m_sel - rowsVisible() + 1;
        repaint();
    };
    if (k == juce::KeyPress::escapeKey) { setVisible(false); if (onClose) onClose(); return true; }
    if (code == juce::KeyPress::downKey && fn) { rowOp(7, m_sel < 0 ? rows : m_sel); return true; }   // insert at the cursor
    if (code == juce::KeyPress::upKey && fn) { if (m_sel >= 0) rowOp(3, m_sel); return true; }
    if (code == juce::KeyPress::upKey) { select(m_sel <= 0 ? 0 : m_sel - 1); return true; }
    if (code == juce::KeyPress::downKey) { select(m_sel < 0 ? 0 : m_sel + 1); return true; }
    if (code == juce::KeyPress::returnKey) { if (m_sel >= 0 && onEnter) { onEnter(m_sel); repaint(); } return true; }
    if (fn && (code == 'C' || code == 'c') && m_sel >= 0 && m_sel < rows) { m_rowClip = song->rows[size_t(m_sel)]; m_hasRowClip = true; return true; }
    if (fn && (code == 'V' || code == 'v') && m_hasRowClip && m_sel >= 0 && m_sel < rows && edit) {
        const int at = m_sel; const auto clip = m_rowClip;
        edit("paste song row", [&](mnm::mddump::Song& s) { if (at < int(s.rows.size())) s.rows[size_t(at)] = clip; }, -1);
        repaint();
        return true;
    }
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

} // namespace mnm::plugin::md
