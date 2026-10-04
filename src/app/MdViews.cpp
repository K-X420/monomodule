#include "MdViews.h"
#include <map>
#include "MdFirmware.h"
#include "MdNames.h"
#include "SharedSettings.h"

namespace mnm::app {

using namespace mnm::mddump;

// ---------------------------------------------------------------------------------------------- machine names

const MdMachineInfo& mdMachine(int id)
{
    static std::map<int, MdMachineInfo> cache;
    static std::unique_ptr<mnm::md::Firmware> fw;
    static juce::String fwPath;
    const auto path = mnm::plugin::loadSharedSetting("mdOsPath");   // Monomodule MD's OS file
    if (path != fwPath) {
        fwPath = path;
        cache.clear();
        fw.reset();
        if (path.isNotEmpty() && juce::File(path).existsAsFile()) {
            try { fw = std::make_unique<mnm::md::Firmware>(mnm::md::loadFirmware(path.toStdString())); } catch (const std::exception&) {}
        }
    }
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    MdMachineInfo info;
    info.name = juce::String(mnm::mdnames::machineName(id));
    const auto* m = fw ? fw->byId(id) : nullptr;
    for (int k = 0; k < 8; ++k) {
        info.labels[size_t(k)] = m ? juce::String(m->labels[size_t(k)]) : (id == 0 ? juce::String() : "SYN" + juce::String(k + 1));
        info.defaults[size_t(k)] = m ? m->defaults[size_t(k)] : 0;
    }
    return cache[id] = info;
}

juce::String mdSynLine(int machine, const uint8_t* p, int maxWidth)
{
    const auto& m = mdMachine(machine);
    juce::String s;
    for (int k = 0; k < 8; ++k) {
        if (m.labels[size_t(k)].isEmpty()) continue;
        const juce::String pair = m.labels[size_t(k)] + " " + juce::String(int(p[k]));
        if (ui::width(s + pair + "  ..") > maxWidth) { s += ".."; break; }
        s += pair + "  ";
    }
    return s.trimEnd();
}

namespace {
constexpr spec::Param numeric(const char* label) { return {label, spec::Display::Numeric, false, 0, 127, 128, spec::Icons::None, nullptr}; }
constexpr spec::Param bipolar(const char* label) { return {label, spec::Display::Bipolar, false, 64, 127, 128, spec::Icons::None, nullptr}; }
constexpr spec::Param readout(const char* label, const char* const* names, int n)
{
    return {label, spec::Display::Readout, false, 0, uint8_t(n - 1), uint8_t(n), spec::Icons::Switch, names};
}
constexpr spec::Param blankParam() { return {"", spec::Display::Blank, false, 0, 127, 128, spec::Icons::None, nullptr}; }
const char* const kTrackNames[17] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11", "T12", "T13", "T14", "T15", "T16", "SELF"};
const char* const kShapes[8] = {"0", "1", "2", "3", "4", "5", "6", "7"};
juce::String pad2(int pos) { return juce::String(pos + 1).paddedLeft('0', 2); }
juce::String groupText(uint8_t g) { return g >= 16 ? juce::String("-") : juce::String(int(g) + 1); }
} // namespace

// ---------------------------------------------------------------------------------------------- kit

MdKitView::MdKitView()
{
    addAndMakeVisible(m_patterns);
    addAndMakeVisible(m_sources);
    m_sources.onClick = [this](const juce::var& v) { if (onLink) onLink(v); };
    m_patterns.grid.onClick = [this](const Card& c) { if (onLink) onLink(c.tag); };
}

void MdKitView::set(const Kit& kit, const std::array<juce::String, 16>& soundNames, std::vector<Card> patterns, std::vector<LinkList::Row> sources)
{
    m_kit = kit; m_soundNames = soundNames;
    m_patterns.set("PATTERNS USING THIS KIT", std::move(patterns), "No pattern of these sources uses this kit.");
    m_sources.set("FROM", std::move(sources));
    resized(); repaint();
}

int MdKitView::preferredHeight() const
{
    const int w = getWidth();
    return sectionsTop() + m_patterns.heightFor(w) + 8 + m_sources.preferredHeight() + 8;
}

void MdKitView::resized()
{
    const int w = getWidth();
    int y = sectionsTop();
    m_patterns.setBounds(0, y, w, m_patterns.heightFor(w)); y += m_patterns.getHeight() + 8;
    m_sources.setBounds(0, y, w, m_sources.preferredHeight());
}

juce::Rectangle<int> MdKitView::trackRow(int t) const { return {0, (kTracksTop + t * kRowLcdH) * kScale, getWidth(), kRowLcdH * kScale}; }

int MdKitView::playZone(juce::Point<int> p) const
{
    const int fromRight = (getWidth() - p.x) / kScale;
    if (p.y < 10 * kScale) return playZoneAt(fromRight, m_playing == -1);
    for (int t = 0; t < 16; ++t) if (trackRow(t).contains(p)) return m_kit.model(t) ? playZoneAt(fromRight, m_playing == t) : 0;
    return 0;
}

void MdKitView::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = kTracksTop + 16 * kRowLcdH + 1;
    LcdCanvas cv(w, h);
    const juce::String title = m_title.isNotEmpty() ? m_title.toUpperCase() : "KIT  " + juce::String(m_kit.name).toUpperCase();
    drawTitleBar(cv, 0, 0, w, title.toRawUTF8(), nullptr, playInset(m_playing == -1));
    if (m_playing == -1) drawStopAndLoop(cv, w - 2 - kPlayW - 2, 1, false); else drawPlayGlyph(cv, w - 2 - kPlayW - 2, 1, false, false);
    for (int t = 0; t < 16; ++t) {
        const int y0 = kTracksTop + t * kRowLcdH;
        cv.dotsH(0, w - 1, y0); cv.dotsV(0, y0, y0 + kRowLcdH - 1); cv.dotsV(w - 1, y0, y0 + kRowLcdH - 1);
        if (t == 15) cv.dotsH(0, w - 1, y0 + kRowLcdH - 1);
        if (t == m_hover) for (int yy = y0 + 1; yy < y0 + kRowLcdH - 1; yy += 2) for (int xx = 2; xx < w - 2 - playInset(m_playing == t); xx += 2) cv.set(xx, yy);
        if (m_kit.model(t)) {
            if (m_playing == t) drawStopAndLoop(cv, w - 2 - kPlayW - 2, y0 + (kRowLcdH - kPlayH) / 2, true);
            else drawPlayGlyph(cv, w - 2 - kPlayW - 2, y0 + (kRowLcdH - kPlayH) / 2, false, true);
        }
    }
    cv.draw(g, 0, 0);
    const juce::String ownName(m_kit.name);
    for (int t = 0; t < 16; ++t) {
        const int y0 = kTracksTop + t * kRowLcdH;
        const int topY = (y0 + 1) * kScale, topH = 8 * kScale;
        ui::text(g, juce::String(t + 1), 3 * kScale, topY, topH, lcd::ink, ui::font(true));
        const int machine = m_kit.model(t);
        ui::text(g, mdMachine(machine).name, 14 * kScale, topY, topH, lcd::ink, ui::font(true));
        const auto* p = m_kit.params[t];
        const int infoX = 50 * kScale;
        // under the machine: the sound's name when it was first seen in another kit (cut to the column)
        const auto& sn = m_soundNames[size_t(t)];
        if (sn.isNotEmpty() && sn != ownName + " T" + juce::String(t + 1)) {
            juce::String s = "= " + sn;
            const auto f = ui::font(false, ui::kTinyPx);
            while (s.length() > 3 && ui::width(s, f) > infoX - 14 * kScale - 6) s = s.dropLastCharacters(1);
            ui::text(g, s, 14 * kScale, (y0 + 9) * kScale, 7 * kScale, lcd::ink.withAlpha(0.6f), f);
        }
        if (machine) {
            ui::text(g, mdSynLine(machine, p, getWidth() - infoX - (kPlayZone + 4) * kScale), infoX, topY, topH);
            juce::String fx = "LEV " + juce::String(int(m_kit.levels[t])) + "   VOL " + juce::String(int(p[17])) + "  PAN " + juce::String(int(p[18]) - 64)
                            + "  DEL " + juce::String(int(p[19])) + "  REV " + juce::String(int(p[20])) + "  DIST " + juce::String(int(p[16]));
            if (p[22] > 0) fx += "   LFO " + juce::String(mnm::mdnames::kLfoParamNames[juce::jlimit(0, 23, int(m_kit.lfos[t][1]))]) + " DEP " + juce::String(int(p[22]));
            ui::text(g, fx, infoX, (y0 + 9) * kScale, 7 * kScale, lcd::ink.withAlpha(0.75f), ui::font(false, ui::kSmallPx));
        }
    }
    // master effects and groups
    int y = fxTop();
    const uint8_t* fx[4] = {m_kit.reverb, m_kit.delay, m_kit.eq, m_kit.dynamics};
    for (int f = 0; f < 4; ++f) {
        juce::String line;
        for (int k = 0; k < 8; ++k) line += juce::String(mnm::mdnames::kMasterFxParams[f][k]) + " " + juce::String(int(fx[f][k])) + "  ";
        ui::labelled(g, 3 * kScale, y, ui::kLineH, mnm::mdnames::kMasterFxNames[f], line.trimEnd());
        y += ui::kLineH;
    }
    juce::String tg, mg;
    for (int t = 0; t < 16; ++t) { tg += groupText(m_kit.trigGroups[t]) + " "; mg += groupText(m_kit.muteGroups[t]) + " "; }
    const int x2 = ui::labelled(g, 3 * kScale, y, ui::kLineH, "TRIG GROUPS", tg.trimEnd());
    ui::labelled(g, x2 + 24, y, ui::kLineH, "MUTE GROUPS", mg.trimEnd());
}

void MdKitView::mouseDown(const juce::MouseEvent& e)
{
    m_dragging = false;
    const auto p = e.getPosition();
    if (const int z = playZone(p); z != 0) {
        if (z == 2) { ui::transport().toggleLoop(); repaint(); return; }
        if (p.y < 10 * kScale) { if (onPlayKit) onPlayKit(); }
        else for (int t = 0; t < 16; ++t) if (trackRow(t).contains(p)) { if (onPlayTrack) onPlayTrack(t); }
        return;
    }
    for (int t = 0; t < 16; ++t) if (trackRow(t).contains(p) && m_kit.model(t) && onTrack) { onTrack(t); return; }
}

void MdKitView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || e.getDistanceFromDragStart() < 8 || playZone(e.getMouseDownPosition()) != 0) return;
    const auto p = e.getMouseDownPosition();
    m_dragging = true;
    if (p.y < 10 * kScale) { if (onDragKit) onDragKit(); return; }
    for (int t = 0; t < 16; ++t) if (trackRow(t).contains(p) && m_kit.model(t) && onDragTrack) { onDragTrack(t); return; }
}

void MdKitView::mouseMove(const juce::MouseEvent& e)
{
    int h = -1;
    for (int t = 0; t < 16; ++t) if (trackRow(t).contains(e.getPosition()) && m_kit.model(t)) h = t;
    if (h != m_hover) { m_hover = h; repaint(); }
    setMouseCursor(h >= 0 || playZone(e.getPosition()) != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// ---------------------------------------------------------------------------------------------- sound

MdSoundView::MdSoundView()
{
    for (auto* c : {&m_fav, &m_tag}) addAndMakeVisible(c);
    m_fav.setClickingTogglesState(false);
    m_fav.onClick = [this] { if (onFavourite) onFavourite(); };
    m_tag.onClick = [this] { if (onTag) onTag(); };
    for (auto* s : {&m_versions, &m_kits, &m_patterns}) {
        addAndMakeVisible(s);
        s->grid.onClick = [this](const Card& c) { if (onLink) onLink(c.tag); };
    }
}

void MdSoundView::set(const mnm::mdcatalog::SoundItem& sound, const juce::String& source, const juce::StringArray& tags, bool favourite,
                      std::vector<Card> versions, std::vector<Card> kits, std::vector<Card> patterns)
{
    m_sound = sound; m_source = source; m_tags = tags; m_favourite = favourite;
    m_fav.setToggleState(favourite, juce::dontSendNotification);
    m_hasVersions = !versions.empty();
    m_versions.setVisible(m_hasVersions);
    m_versions.set("VERSIONS", std::move(versions), {});
    m_kits.set("IN KITS", std::move(kits), "Not in any kit of the library's projects.");
    m_patterns.set("USED IN PATTERNS", std::move(patterns), "No pattern plays a kit with this sound.");
    resized(); repaint();
}

int MdSoundView::preferredHeight() const
{
    const int w = getWidth();
    return sectionsTop() + (m_hasVersions ? m_versions.heightFor(w) + 8 : 0) + m_kits.heightFor(w) + 8 + m_patterns.heightFor(w) + 8;
}

void MdSoundView::resized()
{
    int x = 3 * kScale;
    for (auto* c : {&m_fav, &m_tag}) { c->setBounds(x, chipsTop(), c->preferredWidth(), c->preferredHeight()); x += c->getWidth() + 8; }
    int y = sectionsTop();
    const int w = getWidth();
    if (m_hasVersions) { m_versions.setBounds(0, y, w, m_versions.heightFor(w)); y += m_versions.getHeight() + 8; }
    m_kits.setBounds(0, y, w, m_kits.heightFor(w)); y += m_kits.getHeight() + 8;
    m_patterns.setBounds(0, y, w, m_patterns.heightFor(w));
}

void MdSoundView::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale;
    const auto& s = m_sound.sound;
    const auto& m = mdMachine(s.machine());
    {
        LcdCanvas cv(w, 10);
        drawTitleBar(cv, 0, 0, w, ("SOUND  " + juce::String(m_sound.name).toUpperCase()).toRawUTF8(), m.name.toRawUTF8(), playInset(m_playing));
        if (m_playing) drawStopAndLoop(cv, w - 2 - kPlayW - 2, 1, false); else drawPlayGlyph(cv, w - 2 - kPlayW - 2, 1, false, false);
        cv.draw(g, 0, 0);
    }
    const int x = 3 * kScale;
    int y = kSummaryTop;
    int xx = ui::labelled(g, x, y, ui::kLineH, "MACHINE", m.name);
    xx = ui::labelled(g, xx + 18, y, ui::kLineH, "SOURCE", m_source);
    ui::labelled(g, xx + 18, y, ui::kLineH, "TAGS", m_tags.isEmpty() ? juce::String("none") : m_tags.joinIntoString(", "));
    y += ui::kLineH;
    ui::labelled(g, x, y, ui::kLineH, "LFO", s.lfoOnSelf() ? juce::String("on its own track") : "on track " + juce::String(int(s.lfo[0]) + 1));

    // the four pages as Monomodule MD draws them
    std::array<spec::Param, 8> syn{};
    std::array<uint8_t, 8> synRaw{};
    for (int k = 0; k < 8; ++k) {
        syn[size_t(k)] = m.labels[size_t(k)].isEmpty() ? blankParam() : numeric("");
        synRaw[size_t(k)] = s.params[k];
    }
    std::array<std::string, 8> labelStore;
    for (int k = 0; k < 8; ++k) { labelStore[size_t(k)] = m.labels[size_t(k)].toStdString(); syn[size_t(k)].label = labelStore[size_t(k)].c_str(); }
    const spec::Param fxPage[8] = {numeric("AMD"), numeric("AMF"), numeric("EQF"), bipolar("EQG"), numeric("FLTF"), numeric("FLTW"), numeric("FLTQ"), numeric("SRR")};
    const spec::Param routePage[8] = {numeric("DIST"), numeric("VOL"), bipolar("PAN"), numeric("DEL"), numeric("REV"), blankParam(), blankParam(), blankParam()};
    const spec::Param lfoPage[8] = {readout("TRK", kTrackNames, 17), readout("PARAM", mnm::mdnames::kLfoParamNames, 24), readout("SHP1", kShapes, 8), readout("SHP2", kShapes, 8),
                                    readout("TYPE", mnm::mdnames::kLfoTypes, 3), numeric("SPD"), numeric("DEP"), numeric("MIX")};
    const uint8_t fxRaw[8] = {s.params[8], s.params[9], s.params[10], s.params[11], s.params[12], s.params[13], s.params[14], s.params[15]};
    const uint8_t routeRaw[8] = {s.params[16], s.params[17], s.params[18], s.params[19], s.params[20], 0, 0, 0};
    const uint8_t lfoRaw[8] = {uint8_t(s.lfoOnSelf() ? 16 : juce::jlimit(0, 15, int(s.lfo[0]))), uint8_t(juce::jlimit(0, 23, int(s.lfo[1]))), uint8_t(s.lfo[2] & 7), uint8_t(s.lfo[3] & 7),
                               uint8_t(juce::jmin(2, int(s.lfo[4]))), s.params[21], s.params[22], s.params[23]};
    const int cols = pageRows() == 2 ? 2 : 1;
    const int pw = (kPageW + 4), ph = (kPageH + 4);
    LcdCanvas cv(cols * pw, (4 / cols) * ph);
    const spec::Param* pages[4] = {syn.data(), fxPage, routePage, lfoPage};
    const uint8_t* raws[4] = {synRaw.data(), fxRaw, routeRaw, lfoRaw};
    const char* titles[4] = {"SYNTHESIS", "EFFECTS", "ROUTING", "LFO"};
    for (int i = 0; i < 4; ++i) drawPage(cv, (i % cols) * pw, (i / cols) * ph, titles[i], pages[i], raws[i]);
    cv.draw(g, x, kPagesTop, kPageScale);
}

void MdSoundView::mouseDown(const juce::MouseEvent& e) { m_dragging = false; switch (playZone(e.getPosition())) { case 1: if (onPlay) onPlay(); break; case 2: ui::transport().toggleLoop(); repaint(); break; default: break; } }
void MdSoundView::mouseMove(const juce::MouseEvent& e) { setMouseCursor(playZone(e.getPosition()) != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
void MdSoundView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || e.getDistanceFromDragStart() < 8 || playZone(e.getMouseDownPosition()) != 0 || e.getMouseDownPosition().y > 10 * kScale) return;
    m_dragging = true;
    if (onDrag) onDrag();
}

// ---------------------------------------------------------------------------------------------- pattern

MdPatternView::MdPatternView()
{
    addAndMakeVisible(m_kitSection);
    addAndMakeVisible(m_sources);
    m_sources.onClick = [this](const juce::var& v) { if (onLink) onLink(v); };
    m_kitSection.grid.onClick = [this](const Card& c) { if (onLink) onLink(c.tag); };
}

void MdPatternView::set(const mnm::mdcatalog::PatternItem& pattern, const Kit* kit, const std::array<juce::String, 16>& soundNames,
                        std::vector<Card> kitCards, std::vector<LinkList::Row> sources)
{
    m_pattern = pattern; m_hasKit = kit != nullptr; if (kit) m_kit = *kit; m_soundNames = soundNames;
    m_kitSection.set("KIT", std::move(kitCards), "Its kit slot is empty in this project.");
    m_sources.set("FROM", std::move(sources));
    resized(); repaint();
}

int MdPatternView::preferredHeight() const
{
    const int w = getWidth();
    return gridBottomPx() + 8 + m_kitSection.heightFor(w) + 8 + m_sources.preferredHeight() + 8;
}

void MdPatternView::resized()
{
    const int w = getWidth();
    int y = gridBottomPx() + 8;
    m_kitSection.setBounds(0, y, w, m_kitSection.heightFor(w)); y += m_kitSection.getHeight() + 8;
    m_sources.setBounds(0, y, w, m_sources.preferredHeight());
}

int MdPatternView::trackAt(juce::Point<int> p) const
{
    const int row = (p.y / kScale - kGridTop - kRowLcdH) / kRowLcdH;   // the first grid row is the step ruler
    return p.y / kScale >= kGridTop + kRowLcdH && row >= 0 && row < 16 ? row : -1;
}

void MdPatternView::paint(juce::Graphics& g)
{
    const auto& p = m_pattern.pattern;
    const int w = getWidth() / kScale;
    const int steps = juce::jlimit(1, 64, int(p.length));
    const int labelW = 54;   // LCD px: track number, machine
    const int cell = juce::jlimit(2, 9, (w - labelW - 2 - kPlayZone) / steps);
    const int h = kGridTop + 17 * kRowLcdH + 2;
    LcdCanvas cv(w, h);
    drawTitleBar(cv, 0, 0, w, ("PATTERN  " + juce::String(m_pattern.name).toUpperCase()).toRawUTF8(), nullptr, playInset(m_playing == -1));
    if (m_playing == -1) drawStopAndLoop(cv, w - 2 - kPlayW - 2, 1, false); else drawPlayGlyph(cv, w - 2 - kPlayW - 2, 1, false, false);
    // the step ruler: a tick per step, a taller one per beat
    const int gx = labelW, ry = kGridTop;
    for (int s = 0; s < steps; ++s) cv.fillRect(gx + s * cell + cell / 2, ry + (s % 4 == 0 ? 2 : 5), 1, s % 4 == 0 ? 5 : 2, true);
    for (int t = 0; t < 16; ++t) {
        const int y0 = kGridTop + (t + 1) * kRowLcdH;
        if (t == m_hover) for (int xx = 2; xx < gx - 2; xx += 2) cv.set(xx, y0 + kRowLcdH / 2);
        for (int s = 0; s < steps; ++s) {
            const int x0 = gx + s * cell;
            const bool trig = (p.trigs[t] >> s) & 1;
            const bool accent = ((p.accentPerTrack[t] | p.accent) >> s) & 1;
            if (trig) {
                cv.fillRect(x0 + 1, y0 + 2, juce::jmax(1, cell - 1), kRowLcdH - 4, true);
                if (accent && cell >= 3) cv.invertRect(x0 + 1 + (cell - 1) / 2, y0 + 3, 1, 1);   // a hole marks the accent
            } else if (s % 4 == 0) cv.set(x0 + cell / 2, y0 + kRowLcdH / 2, true);
        }
        if ((m_hasKit && m_kit.model(t)) || p.trigCount(t) > 0) {
            if (m_playing == t) drawStopAndLoop(cv, w - 2 - kPlayW - 2, y0 + 1, true);
            else drawPlayGlyph(cv, w - 2 - kPlayW - 2, y0 + 1, false, true);
        }
    }
    cv.draw(g, 0, 0);
    // summary
    const int x = 3 * kScale;
    int xx = ui::labelled(g, x, 12 * kScale, 7 * kScale, "LENGTH", juce::String(steps) + (p.doubleTempo ? "  x2 TEMPO" : ""));
    xx = ui::labelled(g, xx + 18, 12 * kScale, 7 * kScale, "SWING", juce::String(p.swingPercent()) + "%");
    xx = ui::labelled(g, xx + 18, 12 * kScale, 7 * kScale, "ACCENT", juce::String(int(p.accentAmount)));
    ui::labelled(g, xx + 18, 12 * kScale, 7 * kScale, "KIT", m_hasKit ? pad2(p.kit) + " " + juce::String(m_kit.name) : pad2(p.kit) + " (empty)");
    int locks = 0;
    for (int t = 0; t < 16; ++t) for (int q = 0; q < 24; ++q) locks += int((p.lockMasks[t] >> q) & 1);
    ui::text(g, "Locked parameters: " + juce::String(locks) + "   Click a track for its sound, drag it into the DAW for its MIDI.", x, 19 * kScale, 7 * kScale, lcd::ink.withAlpha(0.6f), ui::font(false, ui::kSmallPx));
    for (int t = 0; t < 16; ++t) {
        const int y0 = (kGridTop + (t + 1) * kRowLcdH) * kScale;
        ui::text(g, juce::String(t + 1), x, y0, kRowLcdH * kScale, lcd::ink, ui::font(true, ui::kSmallPx));
        if (m_hasKit) ui::text(g, mdMachine(m_kit.model(t)).name, x + 22, y0, kRowLcdH * kScale, lcd::ink, ui::font(false, ui::kSmallPx));
    }
}

void MdPatternView::mouseDown(const juce::MouseEvent& e)
{
    m_dragging = false;
    const auto pos = e.getPosition();
    if (const int z = playZone(pos); z != 0) { if (z == 1 && onPlayPattern) onPlayPattern(); else if (z == 2) { ui::transport().toggleLoop(); repaint(); } return; }
    const int t = trackAt(pos);
    if (t < 0) return;
    if ((getWidth() - pos.x) / kScale < kPlayZone) { if (onPlayTrack) onPlayTrack(t); return; }
    if (onSound) onSound(t);
}

void MdPatternView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_dragging || e.getDistanceFromDragStart() < 8 || playZone(e.getMouseDownPosition()) != 0) return;
    m_dragging = true;
    if (e.getMouseDownPosition().y < 10 * kScale) { if (onDragPattern) onDragPattern(); return; }
    const int t = trackAt(e.getMouseDownPosition());
    if (t >= 0 && onDragTrack) onDragTrack(t);
}

void MdPatternView::mouseMove(const juce::MouseEvent& e)
{
    const int t = trackAt(e.getPosition());
    if (t != m_hover) { m_hover = t; repaint(); }
    setMouseCursor(t >= 0 || playZone(e.getPosition()) != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

} // namespace mnm::app
