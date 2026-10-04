#include "MdLibraryPanel.h"
#include <map>
#include "MdNames.h"

namespace mnm::plugin::md {

using namespace one;
namespace spec = mnm::uispec;

namespace {
constexpr int kAnimHz = 60;
constexpr float kAnimSeconds = 0.22f;
const char* const kGroups[] = {"GND", "TRX", "EFM", "E12", "P-I", "INP", "ROM", "RAM"};
juce::String U(const std::string& s) { return juce::String(s).toUpperCase(); }
juce::String groupOf(int machine)
{
    const juce::String n(mnm::mdnames::machineName(machine));
    return n.startsWith("P-I") ? juce::String("P-I") : n.substring(0, 3);
}
bool matches(const juce::String& name, const juce::String& q) { return q.isEmpty() || name.containsIgnoreCase(q); }

// Where the Monomodule Library app is installed (as Monomodule's own panel looks for it)
juce::File libraryAppFile()
{
    using F = juce::File;
#if JUCE_MAC
    const juce::Array<F> candidates{F("/Applications/Monomodule Library.app"),
                                    F::getSpecialLocation(F::userHomeDirectory).getChildFile("Applications/Monomodule Library.app")};
#elif JUCE_WINDOWS
    const juce::Array<F> candidates{F::getSpecialLocation(F::globalApplicationsDirectory).getChildFile("Shnolk/Monomodule/Monomodule Library.exe"),
                                    F::getSpecialLocation(F::userApplicationDataDirectory).getParentDirectory()
                                        .getChildFile("Local/Programs/Shnolk/Monomodule/Monomodule Library.exe")};
#else
    const juce::Array<F> candidates{F::getSpecialLocation(F::userHomeDirectory).getChildFile(".local/bin/Monomodule Library"),
                                    F("/usr/local/bin/Monomodule Library"), F("/usr/bin/Monomodule Library")};
#endif
    for (const auto& f : candidates) if (f.exists()) return f;
    return candidates.getFirst();
}
} // namespace

juce::Rectangle<int> MdLibraryPanel::gridArea() const
{
    const int w = m_target.getWidth() / kS, h = m_target.getHeight() / kS;
    return {kFilterW + 3, kBarH + 2, w - kFilterW - 6, h - kBarH - kFootH - 3};
}

void MdLibraryPanel::rebuild()
{
    m_seenRevision = m_lib.revision();
    m_items.clear(); m_filters.clear();
    const auto grid = gridArea();
    const int colW = (grid.getWidth() - 6) / kCols;
    int y = 2, col = 0;
    auto header = [&](const juce::String& name, int count) {
        if (col) { col = 0; y += kCellH + 1; }
        Item h; h.header = true; h.name = name; h.right = juce::String(count); h.r = {2, y, grid.getWidth() - 8, kHeadRowH};
        m_items.push_back(h); y += kHeadRowH;
    };
    auto cell = [&](const juce::String& key, const juce::String& name, const juce::String& right, bool on, bool fav) {
        Item c; c.key = key; c.name = name; c.right = right; c.on = on; c.fav = fav; c.r = {2 + col * colW, y, colW - 1, kCellH};
        m_items.push_back(c);
        if (++col == kCols) { col = 0; y += kCellH + 1; }
    };
    auto filterHead = [&](const juce::String& label) { Filter f; f.header = true; f.label = label; m_filters.push_back(f); };
    auto filter = [&](const juce::String& label, const juce::String& value, int section) { Filter f; f.label = label; f.value = value; f.section = section; m_filters.push_back(f); };
    auto& model = m_lib.model();
    const auto& cat = model.mdCatalog();

    if (m_tab == Sounds) {
        const auto cur = currentSound ? currentSound() : juce::String();
        std::vector<SoundEntry> list;
        for (const auto& s : m_lib.sounds(-1)) {
            if (m_group.isNotEmpty() && groupOf(s.machine) != m_group) continue;
            if ((m_favourites && !s.favourite) || (m_savedOnly && !s.saved) || !matches(s.name, m_query)) continue;
            list.push_back(s);
        }
        std::map<int, int> counts;
        for (const auto& s : list) ++counts[s.machine];
        int last = -1;
        for (const auto& s : list) {
            if (s.machine != last) { last = s.machine; header(juce::String(mnm::mdnames::machineName(last)), counts[last]); }
            cell(s.key, s.name, s.saved ? "SAVED" : "", s.key == cur, s.favourite);
        }
        filterHead("MACHINE");
        filter("ALL", "", 0);
        for (const char* g : kGroups) filter(g, g, 0);
        filterHead("SHOW");
        filter("FAVOURITES", "fav", 1);
        filter("SAVED", "saved", 1);
    } else if (m_tab == Kits) {
        const auto cur = currentKit ? currentKit() : juce::String();
        for (const auto& k : m_lib.kits()) {
            if (m_source.isNotEmpty() && k.sourceId != m_source) continue;
            if ((m_favourites && !k.favourite) || !matches(k.name, m_query)) continue;
            const auto* item = cat.kit(k.key.toStdString());
            const int pats = item ? int(item->patternIds.size()) : 0;
            cell(k.key, k.name, k.saved ? juce::String("SAVED") : pats ? juce::String(pats) + "P" : juce::String(), k.key == cur, k.favourite);
        }
        filterHead("SOURCE");
        filter("ALL", "", 2);
        for (const auto& p : model.projects()) if (p.isMd()) filter(p.name.toUpperCase(), p.id, 2);
        filter("SAVED", "saved", 2);
        filterHead("SHOW");
        filter("FAVOURITES", "fav", 1);
    } else {
        std::vector<const mnm::mdcatalog::PatternItem*> list;
        for (const auto& p : cat.patterns) {
            if (m_bank >= 0 && (p.sources.empty() || p.sources.front().slot / 16 != m_bank)) continue;
            if (m_source.isNotEmpty() && (p.sources.empty() || juce::String(p.sources.front().importId) != m_source)) continue;
            const auto* k = cat.kit(p.kitId);
            if (!matches(U(p.name), m_query) && !(k && matches(U(k->name), m_query))) continue;
            list.push_back(&p);
        }
        std::stable_sort(list.begin(), list.end(), [](const auto* a, const auto* b) {
            const auto& sa = a->sources.front(); const auto& sb = b->sources.front();
            return sa.importName != sb.importName ? sa.importName < sb.importName : sa.slot < sb.slot; });
        std::string lastSource = "\x01";
        for (const auto* p : list) {
            const auto& src = p->sources.front();
            if (src.importId != lastSource) {
                lastSource = src.importId;
                int n = 0; for (const auto* q : list) n += q->sources.front().importId == lastSource ? 1 : 0;
                header(U(src.importName), n);
            }
            const auto* k = cat.kit(p->kitId);
            cell(juce::String(p->id), juce::String(mnm::mddump::patternSlotName(src.slot)), k ? U(k->name) : juce::String("NO KIT"), false, model.user().isFavourite(juce::String(p->id)));
        }
        filterHead("BANK");
        filter("ALL", "-1", 3);
        for (int b = 0; b < 8; ++b) filter(juce::String::charToString(juce::juce_wchar('A' + b)), juce::String(b), 3);
        filterHead("SOURCE");
        filter("ALL", "", 2);
        for (const auto& p : model.projects()) if (p.isMd()) filter(p.name.toUpperCase(), p.id, 2);
    }
    int fy = kBarH + 3;
    for (auto& f : m_filters) { if (f.header && fy > kBarH + 3) fy += 4; f.r = {2, fy, kFilterW - 4, f.header ? 9 : 11}; fy += f.r.getHeight(); }
    clampScroll();
    repaint();
}

void MdLibraryPanel::refreshIfChanged()
{
    if (m_wantOpen && m_lib.revision() != m_seenRevision) rebuild();   // a save, an import, the Library app
}

void MdLibraryPanel::clampScroll() { m_scroll = juce::jlimit(0, juce::jmax(0, contentHeight() - gridArea().getHeight()), m_scroll); }

int MdLibraryPanel::itemAt(juce::Point<int> lcd) const
{
    const auto grid = gridArea();
    if (!grid.contains(lcd)) return -1;
    const auto p = lcd - grid.getPosition() + juce::Point<int>(0, m_scroll);
    for (int i = 0; i < int(m_items.size()); ++i) if (!m_items[size_t(i)].header && m_items[size_t(i)].r.contains(p)) return i;
    return -1;
}

void MdLibraryPanel::paint(juce::Graphics& g)
{
    const int w = m_target.getWidth() / kS, h = m_target.getHeight() / kS;
    if (w <= 0 || h <= 0) return;
    LcdCanvas cv(w, h);
    frame(cv, {0, 0, w, h});
    // bar: title, tabs, what is typed, CLOSE
    cv.fillRect(0, 0, w, kBarH, true);
    cv.text(spec::kFontBold8, "LIBRARY", 5, 5, false);
    int x = 5 + LcdCanvas::textWidth(spec::kFontBold8, "LIBRARY") + 10;
    static const char* const names[] = {"SOUNDS", "KITS", "PATTERNS"};
    for (int t = 0; t < 3; ++t) {
        const int tw = LcdCanvas::textWidth(spec::kFontSmall4x5, names[t]) + 10;
        m_tabRects[size_t(t)] = {x, 3, tw, kBarH - 6};
        if (int(m_tab) == t) cv.fillRect(x, 3, tw, kBarH - 6, false); else frame(cv, m_tabRects[size_t(t)], false);
        cv.text(spec::kFontSmall4x5, names[t], x + 5, 6, int(m_tab) == t);
        x += tw + 3;
    }
    x += 8;
    if (m_query.isEmpty()) cv.text(spec::kFontSmall4x5, "TYPE TO FIND", x, 6, false);
    else textMarked(cv, spec::kFontSmall4x5, "FIND: " + m_query, x, 6, false, false, true);
    const int cw = LcdCanvas::textWidth(spec::kFontSmall4x5, "CLOSE") + 10;
    m_closeRect = {w - cw - 3, 3, cw, kBarH - 6};
    frame(cv, m_closeRect, false);
    cv.text(spec::kFontSmall4x5, "CLOSE", m_closeRect.getX() + 5, 6, false);

    // filter column
    cv.dotsV(kFilterW, kBarH, h - kFootH - 1);
    for (const auto& f : m_filters) {
        if (f.header) { cv.text(spec::kFontTiny3x5, f.label.toRawUTF8(), f.r.getX() + 2, f.r.getY() + 2, true); continue; }
        const bool on = f.section == 0 ? m_group == f.value : f.section == 1 ? (f.value == "fav" ? m_favourites : m_savedOnly)
                      : f.section == 2 ? m_source == f.value : m_bank == f.value.getIntValue();
        if (on) cv.fillRect(f.r.getX(), f.r.getY(), f.r.getWidth(), f.r.getHeight(), true);
        cv.text(spec::kFontSmall4x5, fit(spec::kFontSmall4x5, f.label, f.r.getWidth() - 6).toRawUTF8(), f.r.getX() + 3, f.r.getY() + 3, !on);
    }

    // grid
    const auto grid = gridArea();
    for (int i = 0; i < int(m_items.size()); ++i) {
        const auto& it = m_items[size_t(i)];
        const auto r = it.r.translated(grid.getX(), grid.getY() - m_scroll);
        if (r.getY() < grid.getY() || r.getBottom() > grid.getBottom()) continue;   // whole rows only
        if (it.header) {
            cv.text(spec::kFontBold8, it.name.toRawUTF8(), r.getX() + 1, r.getY() + 4, true);
            const int nx = r.getX() + LcdCanvas::textWidth(spec::kFontBold8, it.name.toRawUTF8()) + 6, cwid = LcdCanvas::textWidth(spec::kFontTiny3x5, it.right.toRawUTF8());
            cv.dotsH(nx, r.getRight() - cwid - 5, r.getY() + 8);
            cv.text(spec::kFontTiny3x5, it.right.toRawUTF8(), r.getRight() - cwid, r.getY() + 6, true);
            continue;
        }
        if (it.on) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true); else dottedFrame(cv, r);
        const bool ink = !it.on, hover = i == m_hover, playing = isPlaying && isPlaying(it.key);
        if (hover && !it.on) frame(cv, r);
        int nx = r.getX() + 4;
        if (hover || playing) { playGlyph(cv, {r.getX() + 1, r.getY(), 11, r.getHeight()}, playing, ink); nx = r.getX() + 13; }
        if (playing && looping) { drawLoopGlyph(cv, nx + 2, r.getY() + (r.getHeight() - kLoopGlyphH) / 2, looping(), ink); nx += 13; }
        const auto right = fit(spec::kFontTiny3x5, it.right, r.getWidth() / 2);
        const int rw = LcdCanvas::textWidth(spec::kFontTiny3x5, right.toRawUTF8());
        textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, it.name, r.getRight() - nx - rw - 12), nx, r.getY() + 3, ink, it.fav);
        cv.text(spec::kFontTiny3x5, right.toRawUTF8(), r.getRight() - rw - 3, r.getY() + 5, ink);
    }
    if (m_items.empty())
        cv.textCentred(spec::kFontSmall4x5, m_lib.model().mdCatalog().kits.empty() ? "THE LIBRARY HAS NO MACHINEDRUM KITS: IMPORT A .SYX (KIT LIST OR MENU)" : "NOTHING MATCHES",
                       grid.getX(), grid.getWidth(), grid.getY() + 20, true);
    if (contentHeight() > grid.getHeight()) {
        const int barH = juce::jmax(8, grid.getHeight() * grid.getHeight() / contentHeight());
        cv.fillRect(w - 4, grid.getY() + (grid.getHeight() - barH) * m_scroll / juce::jmax(1, contentHeight() - grid.getHeight()), 2, barH, true);
    }

    // footer: what a click does (or the last problem), and the way to the full app
    const int fy = h - kFootH;
    cv.dotsH(1, w - 2, fy);
    juce::String hint = message;
    if (hint.isEmpty()) {
        const int t = selectedTrack ? selectedTrack() : 0;
        if (m_tab == Sounds) hint = "CLICK = LOAD ONTO TRACK " + juce::String(t + 1) + "   DRAG ONTO A TRACK KEY FOR ANOTHER TRACK   GLYPH = AUDITION";
        else if (m_tab == Kits) hint = "CLICK = LOAD THE KIT   LOCKED TRACK KEYS KEEP THEIR SOUND   GLYPH = AUDITION";
        else hint = "DRAG A PATTERN INTO THE DAW FOR ITS MIDI   CLICK = LOAD ITS KIT   GLYPH = AUDITION";
    }
    cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, hint, w - 110).toRawUTF8(), 4, fy + 5, true);
    const int aw = LcdCanvas::textWidth(spec::kFontTiny3x5, "OPEN THE LIBRARY APP");
    m_appRect = {w - aw - 8, fy + 1, aw + 6, kFootH - 2};
    cv.text(spec::kFontTiny3x5, "OPEN THE LIBRARY APP", m_appRect.getX() + 3, fy + 5, true);
    cv.fillRect(m_appRect.getX() + 3, fy + 11, aw, 1, true);
    cv.draw(g, 0, 0, kS);
}

void MdLibraryPanel::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto lcd = e.getPosition() / kS;
    m_pressed = -1; m_dragging = false;
    message.clear();
    if (m_closeRect.contains(lcd)) { close(); return; }
    if (m_appRect.contains(lcd)) {
        const auto app = libraryAppFile();
        if (app.exists()) app.startAsProcess(); else { message = "THE LIBRARY APP IS NOT INSTALLED"; repaint(); }
        return;
    }
    for (int t = 0; t < 3; ++t) if (m_tabRects[size_t(t)].contains(lcd)) { m_query.clear(); m_source.clear(); setTab(Tab(t)); return; }
    for (const auto& f : m_filters) {
        if (f.header || !f.r.contains(lcd)) continue;
        if (f.section == 0) m_group = f.value;
        else if (f.section == 1) { if (f.value == "fav") m_favourites = !m_favourites; else m_savedOnly = !m_savedOnly; }
        else if (f.section == 2) m_source = f.value;
        else m_bank = f.value.getIntValue();
        m_scroll = 0; rebuild(); return;
    }
    const int i = itemAt(lcd);
    if (i >= 0 && e.mods.isPopupMenu()) {   // right-click: favourite
        const auto& it = m_items[size_t(i)];
        m_lib.setFavourite(it.key, !it.fav);
        rebuild();
        return;
    }
    m_pressed = i;
}

void MdLibraryPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (m_pressed < 0) return;
    if (!m_dragging) {
        if (e.getDistanceFromDragStart() < 6) return;
        m_dragging = true;
        if (m_tab == Patterns) {   // the DAW takes the pattern's MIDI
            const auto f = patternMidiFile ? patternMidiFile(m_items[size_t(m_pressed)].key) : juce::File();
            if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this);
            else { message = "THE MIDI OF THIS PATTERN COULD NOT BE WRITTEN"; repaint(); }
            m_pressed = -1; m_dragging = false;
            return;
        }
        if (m_tab == Sounds) setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    if (m_tab == Sounds && onSoundDragging) onSoundDragging(e.getScreenPosition());
}

void MdLibraryPanel::mouseUp(const juce::MouseEvent& e)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
    const int i = m_pressed;
    m_pressed = -1;
    if (i < 0 || i >= int(m_items.size())) return;
    const auto key = m_items[size_t(i)].key;
    if (m_dragging) {
        m_dragging = false;
        if (m_tab == Sounds && onSoundDropped) onSoundDropped(key, e.getScreenPosition());
        rebuild();
        return;
    }
    const auto lcd = e.getPosition() / kS;
    if (itemAt(lcd) != i) return;
    const auto r = m_items[size_t(i)].r.translated(gridArea().getX(), gridArea().getY() - m_scroll);
    const bool playing = isPlaying && isPlaying(key);
    if (lcd.x < r.getX() + 13) { if (audition) audition(key, m_tab); repaint(); return; }
    if (lcd.x < r.getX() + 26 && playing) { if (toggleLoop) toggleLoop(); repaint(); return; }
    if (m_tab == Sounds) { if (loadSound) loadSound(key); }
    else if (m_tab == Kits) { if (loadKit) loadKit(key); }
    else if (loadPatternKit) loadPatternKit(key);
    rebuild();
}

void MdLibraryPanel::mouseMove(const juce::MouseEvent& e)
{
    const auto lcd = e.getPosition() / kS;
    const int i = itemAt(lcd);
    if (i != m_hover) { m_hover = i; repaint(); }
    const bool hand = i >= 0 || m_closeRect.contains(lcd) || m_appRect.contains(lcd);
    setMouseCursor(hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void MdLibraryPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& d)
{
    m_scroll -= int(std::lround(d.deltaY * 120.0f));
    clampScroll();
    repaint();
}

bool MdLibraryPanel::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (m_query.isNotEmpty()) { m_query.clear(); rebuild(); } else close(); return true; }
    if (k == juce::KeyPress::spaceKey && m_query.isEmpty()) return false;   // the host's transport
    if (typeInto(m_query, k, 16)) { m_scroll = 0; rebuild(); return true; }
    return false;
}

// ---- unrolling

void MdLibraryPanel::setTargetBounds(juce::Rectangle<int> fullyOpen)
{
    if (m_target == fullyOpen) return;
    m_target = fullyOpen;
    applyAnimation();
    if (m_wantOpen) rebuild();
}

void MdLibraryPanel::open(bool animate)
{
    const bool was = m_wantOpen;
    m_wantOpen = true;
    rebuild();
    setVisible(true);
    toFront(false);
    grabKeyboardFocus();
    if (!animate) { m_anim = 1.0f; stopTimer(); applyAnimation(); }
    else startTimerHz(kAnimHz);
    if (!was && onOpenChanged) onOpenChanged(true);
    repaint();
}

void MdLibraryPanel::close(bool animate)
{
    const bool was = m_wantOpen;
    m_wantOpen = false;
    if (!animate) { m_anim = 0.0f; stopTimer(); applyAnimation(); }
    else startTimerHz(kAnimHz);
    if (was && onOpenChanged) onOpenChanged(false);
}

void MdLibraryPanel::applyAnimation()
{
    const float eased = 1.0f - (1.0f - m_anim) * (1.0f - m_anim) * (1.0f - m_anim);
    const int h = (int(std::lround(float(m_target.getHeight()) * eased)) / kS) * kS;   // whole LCD rows
    setBounds(m_target.withHeight(juce::jmax(0, h)));
    if (m_anim <= 0.0f && !m_wantOpen) setVisible(false);
}

void MdLibraryPanel::timerCallback()
{
    const float step = 1.0f / (kAnimSeconds * float(kAnimHz));
    const bool moving = m_wantOpen ? m_anim < 1.0f : m_anim > 0.0f;
    if (moving) { m_anim = m_wantOpen ? juce::jmin(1.0f, m_anim + step) : juce::jmax(0.0f, m_anim - step); applyAnimation(); return; }
    stopTimer();
}

} // namespace mnm::plugin::md
