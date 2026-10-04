#include "MdEditor.h"
#include <cmath>
#include "ShnolkLogo.h"
#include "ParamDisplay.h"
#include "SharedSettings.h"
#include "Transfer.h"

namespace mnm::plugin::md {

using one::kScale;
using one::LcdCanvas;
using one::KnobPage;
namespace lcd = one::lcd;

namespace {

// "TRX-BD" -> "TRX" / "BD"; "P-I-BD" -> "P-I" / "BD"; "GND---" -> "GND" / "---"
juce::String familyOf(int index)
{
    const juce::String n(kMachines[index].name);
    return n.startsWith("P-I-") ? juce::String("P-I") : n.substring(0, 3);
}
juce::String shortOf(int index)
{
    const juce::String n(kMachines[index].name);
    const auto s = n.substring(familyOf(index).length() + 1);
    return s.containsOnly("-") ? juce::String("---") : s;   // GND---: the empty machine
}

void dottedFrame(LcdCanvas& cv, int x, int y, int w, int h)
{
    cv.dotsH(x, x + w - 1, y); cv.dotsH(x, x + w - 1, y + h - 1);
    cv.dotsV(x, y, y + h - 1); cv.dotsV(x + w - 1, y, y + h - 1);
}

constexpr spec::Param numeric(const char* label, int def) { return {label, spec::Display::Numeric, false, uint8_t(def), 127, 128, spec::Icons::None, nullptr}; }
constexpr spec::Param bipolar(const char* label, int def = 64) { return {label, spec::Display::Bipolar, false, uint8_t(def), 127, 128, spec::Icons::None, nullptr}; }
constexpr spec::Param readout(const char* label, const char* const* names, int n, int def = 0)
{
    return {label, spec::Display::Readout, false, uint8_t(def), uint8_t(n - 1), uint8_t(n), spec::Icons::Switch, names};
}
constexpr spec::Param blank() { return {"", spec::Display::Blank, false, 0, 127, 128, spec::Icons::None, nullptr}; }

constexpr const char* kTrackNames[kTracks] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11", "T12", "T13", "T14", "T15", "T16"};
constexpr const char* kShapeNames[8] = {"0", "1", "2", "3", "4", "5", "6", "7"};
constexpr const char* kVelNames[2] = {"VOLUME", "ACCENT"};

const spec::Param kFxParams[8] = {numeric("AMD", 0), numeric("AMF", 0), numeric("EQF", 64), bipolar("EQG"),
                                  numeric("FLTF", 0), numeric("FLTW", 127), numeric("FLTQ", 0), numeric("SRR", 0)};
const spec::Param kRoutingParams[8] = {numeric("DIST", 0), numeric("VOL", 100), bipolar("PAN"), numeric("DEL", 0),
                                       numeric("REV", 0), readout("OUT", kRouteNames, kNumRoutes, kNumRoutes - 1), blank(), blank()};
const spec::Param kLfoParams[8] = {readout("TRK", kTrackNames, kTracks), readout("PARAM", kLfoParamNames, 24), readout("SHP1", kShapeNames, 8),
                                   readout("SHP2", kShapeNames, 8), readout("TYPE", kLfoTypes, 3), numeric("SPD", 64), numeric("DEP", 0), numeric("MIX", 0)};
const spec::Param kOutParams[8] = {numeric("VOL", 80), readout("VEL", kVelNames, 2), numeric("ACNT", 64), blank(), blank(), blank(), blank(), blank()};
constexpr const char* kMasterTabs[4] = {"REV", "DEL", "EQ", "DYN"};

const spec::Param& masterParam(int fx, int k)
{
    static const auto table = [] {
        std::array<std::array<spec::Param, 8>, 4> t{};
        for (int f = 0; f < 4; ++f)
            for (int i = 0; i < 8; ++i) {
                const bool gain = f == 2 && (i == 1 || i == 3 || i == 5 || i == 7);   // EQ: LG HG PG GAIN are centred
                t[size_t(f)][size_t(i)] = gain ? bipolar(kMasterFxLabels[f][i], kMasterFxDefaults[f][i]) : numeric(kMasterFxLabels[f][i], kMasterFxDefaults[f][i]);
            }
        return t;
    }();
    return table[size_t(fx)][size_t(k)];
}

} // namespace

// ---------------------------------------------------------------------------------------------- machine block

MdMachineBlock::MdMachineBlock() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

void MdMachineBlock::setMachine(int index)
{
    if (index == m_index) return;
    m_index = index;
    if (auto* p = getParentComponent()) p->resized();   // the block hugs its name
    repaint();
}

int MdMachineBlock::preferredWidth() const
{
    const int w = 4 + LcdCanvas::textWidth(spec::kFontBold8, familyOf(m_index).toRawUTF8()) + 6
                + LcdCanvas::textWidth(spec::kFontBold8, shortOf(m_index).toRawUTF8()) + 4 + 5 + 4;
    return juce::jmax(64, w) * kScale;
}

void MdMachineBlock::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    cv.fillRect(0, 0, w, h, true);
    const auto fam = familyOf(m_index), name = shortOf(m_index);
    const int ty = (h - spec::kFontBold8.h) / 2;
    cv.text(spec::kFontBold8, fam.toRawUTF8(), 4, ty, false);
    const int nameX = 4 + LcdCanvas::textWidth(spec::kFontBold8, fam.toRawUTF8()) + 6;
    cv.text(spec::kFontBold8, name.toRawUTF8(), nameX, ty, false);
    const int ax = nameX + LcdCanvas::textWidth(spec::kFontBold8, name.toRawUTF8()) + 4, ay = h / 2 - 1;
    for (int r = 0; r < 3; ++r) {   // the picker arrow: down when closed, up while open
        const int half = m_open ? r : 2 - r;
        for (int c = 2 - half; c <= 2 + half; ++c) cv.set(ax + c, ay + r, false);
    }
    cv.draw(g, 0, 0);
}

// ---------------------------------------------------------------------------------------------- track keys

MdTrackKeys::MdTrackKeys()
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setOpaque(true);
}

juce::Rectangle<int> MdTrackKeys::keyRect(int t) const
{
    const int w = getWidth() / kScale, gap = 2;
    const int kw = (w - (kTracks - 1) * gap) / kTracks;
    const int extra = w - (kTracks * kw + (kTracks - 1) * gap);   // spread the remainder over the first keys
    const int x = t * (kw + gap) + juce::jmin(t, extra);
    return {x, 0, kw + (t < extra ? 1 : 0), kLcdH};
}

void MdTrackKeys::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    for (int t = 0; t < kTracks; ++t) {
        const auto r = keyRect(t);
        const bool sel = t == m_selected, ink = !sel;
        if (sel) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        else dottedFrame(cv, r.getX(), r.getY(), r.getWidth(), r.getHeight());
        cv.text(spec::kFontBold8, juce::String(t + 1).toRawUTF8(), r.getX() + 3, r.getY() + 3, ink);
        const int lx = r.getRight() - 7, ly = r.getY() + 4;   // activity LED: solid while the track sounds
        if (m_active[size_t(t)]) cv.fillRect(lx, ly, 4, 4, ink);
        else {
            for (int c = 0; c < 4; ++c) { cv.set(lx + c, ly, ink); cv.set(lx + c, ly + 3, ink); }
            cv.set(lx, ly + 1, ink); cv.set(lx, ly + 2, ink); cv.set(lx + 3, ly + 1, ink); cv.set(lx + 3, ly + 2, ink);
        }
        const int m = m_machine[size_t(t)];
        cv.textCentred(spec::kFontTiny3x5, familyOf(m).toRawUTF8(), r.getX() + 1, r.getWidth() - 1, r.getY() + 13, ink);
        cv.textCentred(spec::kFontTiny3x5, shortOf(m).toRawUTF8(), r.getX() + 1, r.getWidth() - 1, r.getY() + 19, ink);
        // LOCK and MUTE keys along the bottom: solid when on, dotted otherwise
        auto key = [&](juce::Rectangle<int> b, const char* letter, bool on) {
            if (on) cv.fillRect(b.getX(), b.getY(), b.getWidth(), b.getHeight(), ink);
            else dottedFrame(cv, b.getX(), b.getY(), b.getWidth(), b.getHeight());
            cv.textCentred(spec::kFontTiny3x5, letter, b.getX(), b.getWidth(), b.getY() + 2, on ? !ink : ink);
        };
        key(lockBox(t), "L", m_locked[size_t(t)]);
        key(muteBox(t), "M", m_muted[size_t(t)]);
        if (t == m_dropTarget) {   // a sound from the library would land here: an inner frame
            cv.invertRect(r.getX() + 1, r.getY() + 1, r.getWidth() - 2, 1); cv.invertRect(r.getX() + 1, r.getBottom() - 2, r.getWidth() - 2, 1);
            cv.invertRect(r.getX() + 1, r.getY() + 2, 1, r.getHeight() - 4); cv.invertRect(r.getRight() - 2, r.getY() + 2, 1, r.getHeight() - 4);
        }
    }
    cv.draw(g, 0, 0);
}

void MdTrackKeys::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kScale;
    for (int t = 0; t < kTracks; ++t) {
        if (!keyRect(t).contains(p)) continue;
        if (lockBox(t).expanded(1).contains(p)) { if (onLock) onLock(t); }
        else if (muteBox(t).expanded(1).contains(p)) { if (onMute) onMute(t); }
        else if (onPress) onPress(t);
        return;
    }
}

void MdTrackKeys::mouseMove(const juce::MouseEvent& e)
{
    const auto p = e.getPosition() / kScale;
    juce::String tip;
    for (int t = 0; t < kTracks; ++t)
        if (lockBox(t).expanded(1).contains(p)) tip = "Lock: keep this track's sound when a kit is loaded";
        else if (muteBox(t).expanded(1).contains(p)) tip = "Mute: ignore this track's trigs";
    setTooltip(tip);
}

// ---------------------------------------------------------------------------------------------- machine picker

MdMachinePicker::MdMachinePicker()
{
    setOpaque(true);
    auto add = [this](const juce::String& fam, int span = 1) { m_columns.push_back({fam, span, {}}); };
    for (int i = 0; i < kNumMachines; ++i) {
        const auto fam = familyOf(i);
        if (m_columns.empty() || (m_columns.back().title != fam && !(fam == "ROM" && m_columns.back().title.isEmpty())))
            add(fam);
        if (fam == "ROM" && m_columns.back().items.size() == 16) add({});   // ROM continues in untitled columns
        m_columns.back().items.push_back(i);
    }
    for (size_t c = 0; c < m_columns.size(); ++c)   // the ROM header spans its columns
        if (m_columns[c].title == "ROM") for (size_t k = c + 1; k < m_columns.size() && m_columns[k].title.isEmpty(); ++k) ++m_columns[c].span;
}

void MdMachinePicker::layout()
{
    m_cells.clear(); m_heads.clear();
    const int w = getWidth() / kScale, gap = 4, n = int(m_columns.size());
    const int cw = juce::jmax(16, (w - 8 - (n - 1) * gap) / juce::jmax(1, n));
    const int headY = 14, cellY = headY + 12, cellH = 8;
    for (int c = 0; c < n; ++c) {
        const auto& col = m_columns[size_t(c)];
        const int x = 4 + c * (cw + gap);
        if (col.title.isNotEmpty()) m_heads.push_back({col.title, {x, headY, col.span * cw + (col.span - 1) * gap, 10}});
        for (size_t i = 0; i < col.items.size(); ++i) m_cells.push_back({col.items[i], {x, cellY + int(i) * cellH, cw, cellH}});
    }
}

int MdMachinePicker::itemAt(juce::Point<int> lcd) const
{
    for (const auto& [idx, r] : m_cells) if (r.contains(lcd)) return idx;
    return -1;
}

void MdMachinePicker::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    dottedFrame(cv, 0, 0, w, h);
    cv.text(spec::kFontBold8, "MACHINES", 4, 3);
    for (const auto& [title, r] : m_heads) {
        cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        cv.text(spec::kFontBold8, title.toRawUTF8(), r.getX() + 2, r.getY() + 1, false);
    }
    for (const auto& [idx, r] : m_cells) {
        const bool cur = idx == m_current;
        if (cur) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        cv.text(spec::kFontSmall4x5, shortOf(idx).toRawUTF8(), r.getX() + 2, r.getY() + (r.getHeight() - spec::kFontSmall4x5.h) / 2, !cur);
        if (hasSample && hasSample(idx)) cv.fillRect(r.getRight() - 4, r.getY() + 3, 2, 2, !cur);   // a ROM slot holding a sample
        if (idx == m_hover && !cur) cv.invertRect(r.getX(), r.getY(), r.getWidth(), r.getHeight());
    }
    cv.draw(g, 0, 0);
}

void MdMachinePicker::mouseMove(const juce::MouseEvent& e)
{
    const int h = itemAt(e.getPosition() / kScale);
    setMouseCursor(h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != m_hover) { m_hover = h; repaint(); }
}

void MdMachinePicker::mouseDown(const juce::MouseEvent& e)
{
    const int idx = itemAt(e.getPosition() / kScale);
    if (idx >= 0 && onPick) onPick(idx);
    close();
}

// ---------------------------------------------------------------------------------------------- badge button

int MdBadgeButton::preferredWidth() const { return (LcdCanvas::textWidth(spec::kFontSmall4x5, getButtonText().toRawUTF8()) + 5) * kScale; }

void MdBadgeButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    if (down) cv.fillRect(0, 0, w, h, true);
    cv.text(spec::kFontSmall4x5, getButtonText().toRawUTF8(), 2, (h - spec::kFontSmall4x5.h) / 2, !down);
    cv.draw(g, 0, 0);
    if (highlighted && !down) { g.setColour(lcd::ink.withAlpha(0.15f)); g.fillRect(getLocalBounds()); }
}

// ---------------------------------------------------------------------------------------------- editor

MdEditor::MdEditor(MdProcessor& p)
    : AudioProcessorEditor(p), m_proc((skin::apply(skin::load()), p)),   // the skin first: m_lnf reads it when it is built
      m_footerVersion(spec::kFontSmall4x5, kPluginVersion, 2),
      m_footerBy(spec::kFontSmall4x5, "BY SHNOLK", 2, false, juce::Justification::centredRight),
      m_status(spec::kFontBold8, "NO MACHINEDRUM OS FILE", kScale),
      m_syn(p.apvts, "SYNTHESIS"), m_fx(p.apvts, "EFFECTS"), m_routing(p.apvts, "ROUTING"),
      m_lfo(p.apvts, "LFO"), m_master(p.apvts, "MASTER FX"), m_out(p.apvts, "OUTPUT"),
      m_missingOs([this] { chooseOsFile(); }, [] {
          OsRequirement os;
          os.device = "Machinedrum"; os.osFile = "Elektron_SPS1-1UW_OS1.63.syx"; os.zipFile = "Elektron_SPS1-1UW_OS1.63.zip";
          os.url = "https://www.elektron.se/support-downloads/machinedrum"; os.linkText = "elektron.se  -  Machinedrum support and downloads";
          os.directDownload = false;
          return os;
      }())
{
    setLookAndFeel(&m_lnf);
    m_artPath = loadSharedOsPath();   // the LCD fonts and dials come from the Monomachine OS file, when there is one
    one::loadLcdArt(m_artPath);

    addAndMakeVisible(m_machineBlock);
    m_machineBlock.onOpen = [this] { if (m_picker.isVisible()) m_picker.close(); else { m_picker.open(m_machineIndex); m_machineBlock.setOpen(true); } };
    m_picker.onPick = [this](int idx) { setMachine(idx); };
    m_picker.onClosed = [this] { m_machineBlock.setOpen(false); };
    m_picker.hasSample = [this](int idx) { const int id = kMachines[idx].id; return isRomMachine(id) && m_proc.sampleName(romSlotOf(id)).isNotEmpty(); };
    addChildComponent(m_picker);

    m_menuButton.onClick = [this] { showMenu(); };
    m_osButton.onClick = [this] { chooseOsFile(); };
    for (juce::Component* c : {static_cast<juce::Component*>(&m_menuButton), static_cast<juce::Component*>(&m_footerVersion),
                               static_cast<juce::Component*>(&m_footerBy), static_cast<juce::Component*>(&m_level),
                               static_cast<juce::Component*>(&m_strip)})
        addAndMakeVisible(c);
    m_strip.onPart = [this](MdKitStrip::Part part) {
        const bool wasKits = m_drop.isVisible() && m_drop.showingKits(), wasSounds = m_drop.isVisible() && !m_drop.showingKits();
        m_drop.setVisible(false);
        m_strip.setOpen(MdKitStrip::None);
        switch (part) {
            case MdKitStrip::KitPrev:   stepKit(-1); break;
            case MdKitStrip::KitNext:   stepKit(1); break;
            case MdKitStrip::Kit:       if (!wasKits) openKitList(); break;
            case MdKitStrip::KitSave:
                m_saveDialog.onSave = [this](const juce::String& name, bool into) { return saveKit(name, into); };
                m_saveDialog.open("SAVE KIT", m_proc.kitName().isEmpty() ? juce::String("NEW KIT") : m_proc.kitName(), slotText(m_lib->projectSlotOfKit(m_proc.loadedKitKey())));
                break;
            case MdKitStrip::SoundPrev: stepSound(-1); break;
            case MdKitStrip::SoundNext: stepSound(1); break;
            case MdKitStrip::Sound:     if (!wasSounds) openSoundList(); break;
            case MdKitStrip::SoundSave:
                m_saveDialog.onSave = [this](const juce::String& name, bool into) { return saveSound(name, into); };
                m_saveDialog.open("SAVE SOUND T" + juce::String(m_track + 1), soundDisplayName(m_track).isEmpty() || soundDisplayName(m_track) == "-" ? juce::String("NEW SOUND") : soundDisplayName(m_track),
                                  slotText(m_lib->projectSlotOfSound(m_proc.loadedSoundKey(m_track))));
                break;
            case MdKitStrip::Library:   if (m_panel.isOpen()) m_panel.close(); else { m_picker.setVisible(false); m_panel.open(); } break;
            case MdKitStrip::None: break;
        }
    };
    // the library panel over the pages
    m_panel.currentKit = [this] { return m_proc.loadedKitKey(); };
    m_panel.currentSound = [this] { return m_proc.loadedSoundKey(m_track); };
    m_panel.selectedTrack = [this] { return m_track; };
    m_panel.loadKit = [this](const juce::String& key) { loadKitKey(key); };
    m_panel.loadSound = [this](const juce::String& key) { loadSoundKey(m_track, key); };
    m_panel.loadPatternKit = [this](const juce::String& key) {
        if (const auto* p = m_lib->model().mdCatalog().pattern(key.toStdString()); p && !p->kitId.empty()) loadKitKey(juce::String(p->kitId));
        else m_panel.message = "THIS PATTERN'S KIT SLOT IS EMPTY";
    };
    m_panel.audition = [this](const juce::String& key, MdLibraryPanel::Tab kind) { audition(key, int(kind)); };
    m_panel.isPlaying = [this](const juce::String& key) { return m_proc.previewKey() == key; };
    m_panel.looping = [this] { return m_proc.previewLoop(); };
    m_panel.toggleLoop = [this] { m_proc.previewSetLoop(!m_proc.previewLoop()); };
    m_panel.patternMidiFile = [this](const juce::String& key) {
        const auto& cat = m_lib->model().mdCatalog();
        const auto* p = cat.pattern(key.toStdString());
        if (!p) return juce::File();
        const auto* k = cat.kit(p->kitId);
        return mnm::library::writeMdPatternMidiDragFile(k ? &k->kit : nullptr, p->pattern, juce::String(p->name).replace(" ", "-"), -1);
    };
    m_panel.onSoundDragging = [this](juce::Point<int> screen) { m_keys.setDropTarget(m_keys.trackAt(m_keys.getLocalPoint(nullptr, screen))); };
    m_panel.onSoundDropped = [this](const juce::String& key, juce::Point<int> screen) {
        m_keys.setDropTarget(-1);
        const int t = m_keys.trackAt(m_keys.getLocalPoint(nullptr, screen));
        if (t >= 0) { loadSoundKey(t, key); if (t != m_track) selectTrack(t); }
    };
    m_panel.onOpenChanged = [this](bool open) { m_strip.setLibraryOpen(open); };
    m_drop.onLoadKit = [this](const KitEntry& e) { loadKit(e); };
    m_drop.onLoadSound = [this](const SoundEntry& e) { loadSound(e); };
    m_drop.onImport = [this] { importSyx(); };
    m_drop.onAudition = [this](const juce::String& key, bool kit) { audition(key, kit ? int(MdLibraryPanel::Kits) : int(MdLibraryPanel::Sounds)); };
    m_drop.isPlaying = [this](const juce::String& key) { return m_proc.previewKey() == key; };
    m_drop.onClosed = [this] { m_strip.setOpen(MdKitStrip::None); };
    addChildComponent(m_status);
    addChildComponent(m_osButton);

    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_master, &m_out}) addAndMakeVisible(pg);
    m_master.setTabs({kMasterTabs[0], kMasterTabs[1], kMasterTabs[2], kMasterTabs[3]}, 0, [this](int tab) { bindMasterFx(tab); });
    m_out.bind(kOutParams, [](int k) { return k == 0 ? masterId() : k == 1 ? velModeId() : k == 2 ? accentId() : juce::String(); });
    bindMasterFx(0);

    m_sample.onClick = [this] { sampleMenu(); };
    addChildComponent(m_sample);

    m_keys.onPress = [this](int t) { if (t != m_track) selectTrack(t); m_proc.auditionTrack(t); };
    m_keys.onMute = [this](int t) {
        if (auto* p = m_proc.apvts.getParameter(muteId(t))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
        timerCallback();
    };
    m_keys.onLock = [this](int t) { m_proc.setTrackLocked(t, !m_proc.trackLocked(t)); timerCallback(); };
    addAndMakeVisible(m_keys);
    addChildComponent(m_panel);
    m_engineStatus.setFont(juce::Font(juce::FontOptions(12.0f)));
    addChildComponent(m_engineStatus);
    addChildComponent(m_about);
    m_skinDialog.onChanged = [this] { skinChanged(); };
    addChildComponent(m_skinDialog);
    addChildComponent(m_missingOs);
    addChildComponent(m_drop);
    addChildComponent(m_saveDialog);

    selectTrack(0);
    const int levW = 19 * kScale, gap = 12;
    setSize(20 + levW + 8 + 3 * KnobPage::kWidth + 2 * gap,
            16 + 16 + 48 + 6 + 36 + 2 * KnobPage::kHeight + gap + gap + MdTrackKeys::kLcdH * kScale);
    timerCallback();
    startTimerHz(20);
}

MdEditor::~MdEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void MdEditor::selectTrack(int t)
{
    m_track = t;
    m_keys.setSelected(t);
    bindTrackPages();
}

// Binds the LEV column and the track pages to the selected track
void MdEditor::bindTrackPages()
{
    const int t = m_track;
    m_levelAttach.reset();
    m_levelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, levelId(t), m_level);
    m_level.setDoubleClickReturnValue(true, 127.0);
    m_fx.bind(kFxParams, [t](int k) { return fxId(t, k); });
    m_routing.bind(kRoutingParams, [t](int k) {
        static juce::String (* const ids[6])(int) = {distId, volId, panId, delId, revId, routeId};
        return k < 6 ? ids[k](t) : juce::String();
    });
    m_lfo.bind(kLfoParams, [t](int k) { return lfoId(t, k); });
    m_shownMachineId = -2;
    rebuildSynPage();
}

// The SYNTHESIS page follows the machine: its labels and defaults from the OS file's descriptor
void MdEditor::rebuildSynPage()
{
    const int id = m_proc.machineIdOf(m_track);
    if (id == m_shownMachineId) return;
    m_shownMachineId = id;
    const auto* m = m_proc.machineInfo(id);
    for (int k = 0; k < 8; ++k) {
        m_synLabels[size_t(k)] = m ? m->labels[size_t(k)] : std::string();
        auto& p = m_synParams[size_t(k)];
        p = m_synLabels[size_t(k)].empty() ? blank() : numeric("", m ? m->defaults[size_t(k)] : 0);
        p.label = m_synLabels[size_t(k)].c_str();
    }
    const int t = m_track;
    m_syn.bind(m_synParams.data(), [t](int k) { return knobId(t, k); });
}

void MdEditor::bindMasterFx(int fx)
{
    m_masterTab = fx;
    std::array<spec::Param, 8> params{};
    for (int k = 0; k < 8; ++k) params[size_t(k)] = masterParam(fx, k);
    m_master.bind(params.data(), [fx](int k) { return masterFxId(fx, k); });
}

void MdEditor::setMachine(int index)
{
    if (auto* p = m_proc.apvts.getParameter(machineId(m_track))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(float(index)));
        p->endChangeGesture();
    }
    timerCallback();
}

void MdEditor::showMenu()
{
    juce::PopupMenu skins;
    const auto current = skin::current().preset;
    for (int i = 0; i < skin::kNumPresets; ++i)
        skins.addItem(100 + i, i == int(skin::Preset::Custom) ? juce::String("CUSTOM...") : juce::String(skin::kPresetNames[i]), true, int(current) == i);
    juce::PopupMenu m;
    m.addItem(5, "INIT KIT");
    m.addItem(1, "SELECT MACHINEDRUM OS FILE...");
    m.addItem(6, "CLEAR OS FILE SELECTION", m_proc.firmwarePath().isNotEmpty());
    m.addSeparator();
    m.addItem(2, "IMPORT MACHINEDRUM .SYX...");
    m.addItem(9, "LIBRARY", true, m_panel.isOpen());
    m.addSeparator();
    m.addSubMenu("SKIN", skins);
    m.addItem(7, "SHOW ENGINE STATUS", true, m_showStatus);
    m.addItem(4, juce::String("SAMPLE MEMORY USED ") + juce::String(int(std::lround(m_proc.sampleMemoryUsed() * 100.0))) + "%", false);
    m.addSeparator();
    m.addItem(8, "PLUGIN INFO...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_menuButton), [this](int r) {
        if (r == 1) chooseOsFile();
        else if (r == 2) importSyx();
        else if (r == 5) { m_proc.initKit(); selectTrack(m_track); timerCallback(); }
        else if (r == 6) { m_proc.clearFirmware(); timerCallback(); }
        else if (r == 7) { m_showStatus = !m_showStatus; resized(); timerCallback(); }
        else if (r == 8) { m_about.setVisible(true); m_about.toFront(false); }
        else if (r == 9) { if (m_panel.isOpen()) m_panel.close(); else m_panel.open(); }
        else if (r == 100 + int(skin::Preset::Custom)) { m_skinDialog.setBounds(getLocalBounds()); m_skinDialog.open(); }
        else if (r >= 100) applySkin(skin::presetSkin(skin::Preset(r - 100)));
    });
}

void MdEditor::skinChanged()
{
    m_lnf.applySkin();
    sendLookAndFeelChange();
    repaint();
}

void MdEditor::applySkin(const skin::Skin& s)
{
    skin::apply(s);
    skin::save(s);
    m_lnf.applySkin();
    sendLookAndFeelChange();
    repaint();
}

void MdEditor::chooseOsFile()
{
    const juce::File current(m_proc.firmwarePath());
    m_chooser = std::make_unique<juce::FileChooser>("Select the Machinedrum OS .syx (Elektron_SPS1-1UW_OS1.63.syx)",
        current.existsAsFile() ? current.getParentDirectory() : juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) { m_proc.setFirmwarePath(f.getFullPathName()); m_shownMachineId = -2; timerCallback(); }
    });
}

// Machinedrum sysex files (kits or whole dumps) into the kit library, then the list to pick from
void MdEditor::importSyx()
{
    m_chooser = std::make_unique<juce::FileChooser>("Import Machinedrum kits (.syx kit or dump)",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& fc) { importFiles(fc.getResults()); });
}

void MdEditor::importFiles(const juce::Array<juce::File>& files)
{
    juce::StringArray problems;
    juce::String lastId;
    int imported = 0;
    for (const auto& f : files) {
        if (!f.existsAsFile()) continue;
        juce::String id;
        const auto r = m_lib->importSyx(f, &id);
        if (r.failed()) problems.add(r.getErrorMessage());
        else { lastId = id; ++imported; }
    }
    if (!problems.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import", problems.joinIntoString("\n"));
    if (imported == 0) return;
    // a file of a single kit is what the user wants to hear: load it; a dump opens the list
    std::vector<KitEntry> fromLast;
    for (const auto& k : m_lib->kits()) if (k.sourceId == lastId) fromLast.push_back(k);
    if (imported == 1 && fromLast.size() == 1) loadKit(fromLast.front());
    else openKitList();
}

bool MdEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files) if (f.endsWithIgnoreCase(".syx") || mnm::library::isMdTransferFile(f)) return true;
    return false;
}

void MdEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::Array<juce::File> syx;
    for (const auto& path : files) {
        const juce::File f(path);
        if (path.endsWithIgnoreCase(".syx")) { syx.add(f); continue; }
        mnm::library::MdTransferPayload p;
        if (!mnm::library::readMdTransferFile(f, p)) continue;
        if (p.isKit) {
            const int emptied = m_proc.loadMdKit(juce::String(mnm::mdcatalog::Catalog::kitHash(p.kit)), p.kit, p.name.isNotEmpty() ? p.name.toUpperCase() : juce::String(p.kit.name));
            if (emptied > 0) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Kit", juce::String(emptied) + " track(s) use MIDI or controller machines, which are not available yet; they were left empty.");
            selectTrack(m_track);
        } else {
            const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
            const int t = over >= 0 ? over : m_track;
            if (!m_proc.loadSound(t, juce::String(mnm::mdcatalog::Catalog::soundHash(p.sound)), p.sound, p.name.toUpperCase()))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Sound", "This sound uses a machine Monomodule MD does not have yet.");
            if (t != m_track) selectTrack(t); else bindTrackPages();
        }
    }
    if (!syx.isEmpty()) importFiles(syx);
    timerCallback();
}

void MdEditor::openKitList()
{
    const int maxH = m_out.getBottom() - m_strip.getBottom() - 2;
    const auto part = m_strip.partBounds(MdKitStrip::Kit) + m_strip.getPosition();
    m_drop.openKits(m_proc.loadedKitKey(), {juce::jmax(0, part.getX()), m_strip.getBottom() + 2}, maxH);
    m_strip.setOpen(MdKitStrip::Kit);
}

void MdEditor::openSoundList()
{
    const int maxH = m_out.getBottom() - m_strip.getBottom() - 2;
    const auto part = m_strip.partBounds(MdKitStrip::Sound) + m_strip.getPosition();
    const int x = juce::jmin(part.getX(), getWidth() - MdLibraryDrop::kLcdW * MdLibraryDrop::kS - 10);
    m_drop.openSounds(m_proc.machineIdOf(m_track), m_track, m_proc.loadedSoundKey(m_track), {x, m_strip.getBottom() + 2}, maxH);
    m_strip.setOpen(MdKitStrip::Sound);
}

void MdEditor::loadKit(const KitEntry& e)
{
    mnm::mddump::Kit kit;
    if (!m_lib->loadKit(e.key, kit)) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Kit", "The kit " + e.name + " is no longer in the library.");
        return;
    }
    const int emptied = m_proc.loadMdKit(e.key, kit, e.name);
    if (emptied > 0)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Kit",
            juce::String(emptied) + " track(s) use MIDI or controller machines, which are not available yet; they were left empty.");
    selectTrack(m_track);
    timerCallback();
}

// previous / next kit in the list order, wrapping
void MdEditor::stepKit(int dir)
{
    const auto kits = m_lib->kits();
    if (kits.empty()) { openKitList(); return; }
    int i = -1;
    for (int k = 0; k < int(kits.size()); ++k) if (kits[size_t(k)].key == m_proc.loadedKitKey()) i = k;
    i = i < 0 ? (dir > 0 ? 0 : int(kits.size()) - 1) : (i + dir + int(kits.size())) % int(kits.size());
    loadKit(kits[size_t(i)]);
}

juce::String MdEditor::slotText(const mnm::library::LibraryModel::Slot& s)
{
    if (!s.valid() || s.kit < 0) return {};
    return "PROJECT " + s.projectName + ", KIT " + juce::String(s.kit + 1).paddedLeft('0', 2) + (s.track >= 0 ? " T" + juce::String(s.track + 1) : juce::String());
}

juce::String MdEditor::saveKit(const juce::String& name, bool intoProject)
{
    juce::String key;
    const auto into = intoProject ? m_lib->projectSlotOfKit(m_proc.loadedKitKey()) : mnm::library::LibraryModel::Slot{};
    const auto r = m_lib->saveKit(name, m_proc.captureMdKit(), m_proc.loadedKitKey(), &key, into);
    if (r.failed()) return r.getErrorMessage();
    m_proc.setLoadedKit(key, name.toUpperCase());
    timerCallback();
    return {};
}

void MdEditor::loadSound(const SoundEntry& e)
{
    mnm::mdcatalog::Sound s;
    if (!m_lib->loadSound(e.key, s)) return;
    if (!m_proc.loadSound(m_track, e.key, s, e.name))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Sound", "This sound uses a machine Monomodule MD does not have yet.");
    bindTrackPages();
    timerCallback();
}

// previous / next sound of the selected track's machine, wrapping
void MdEditor::stepSound(int dir)
{
    const auto sounds = m_lib->sounds(m_proc.machineIdOf(m_track));
    if (sounds.empty()) { openSoundList(); return; }
    int i = -1;
    for (int k = 0; k < int(sounds.size()); ++k) if (sounds[size_t(k)].key == m_proc.loadedSoundKey(m_track)) i = k;
    i = i < 0 ? (dir > 0 ? 0 : int(sounds.size()) - 1) : (i + dir + int(sounds.size())) % int(sounds.size());
    loadSound(sounds[size_t(i)]);
}

juce::String MdEditor::saveSound(const juce::String& name, bool intoProject)
{
    juce::String key;
    const auto into = intoProject ? m_lib->projectSlotOfSound(m_proc.loadedSoundKey(m_track)) : mnm::library::LibraryModel::Slot{};
    const auto r = m_lib->saveSound(name, m_proc.captureSound(m_track), m_proc.loadedSoundKey(m_track), &key, into);
    if (r.failed()) return r.getErrorMessage();
    m_proc.setLoadedSound(m_track, key, name.toUpperCase());
    timerCallback();
    return {};
}

// A kit (its best pattern, else a demo), a sound (one trig) or a pattern (on its kit), heard before loading it; a second
// click stops
void MdEditor::audition(const juce::String& key, int kind)
{
    if (m_proc.previewKey() == key) { m_proc.previewStop(); m_drop.repaint(); m_panel.repaint(); return; }
    const auto& cat = m_lib->model().mdCatalog();
    mnm::mdpreview::Options opt;
    if (kind == MdLibraryPanel::Kits) {
        const auto* k = cat.kit(key.toStdString());
        if (!k) return;
        const auto kitData = k->kit;
        const auto best = mnm::mdpreview::choosePreviewPattern(cat, *k);
        if (const auto* p = best.empty() ? nullptr : cat.pattern(best)) { const auto pat = p->pattern; m_proc.previewPlay(key, [kitData, pat, opt] { return mnm::mdpreview::patternPreview(kitData, pat, opt); }); }
        else m_proc.previewPlay(key, [kitData, opt] { return mnm::mdpreview::patternPreview(kitData, mnm::mdpreview::demoPattern(kitData), opt); });
    } else if (kind == MdLibraryPanel::Patterns) {
        const auto* p = cat.pattern(key.toStdString());
        const auto* k = p ? cat.kit(p->kitId) : nullptr;
        if (!p || !k) { m_panel.message = "THIS PATTERN'S KIT SLOT IS EMPTY"; m_panel.repaint(); return; }
        const auto pat = p->pattern; const auto kitData = k->kit;
        m_proc.previewPlay(key, [kitData, pat, opt] { return mnm::mdpreview::patternPreview(kitData, pat, opt); });
    } else {
        const auto* s = cat.sound(key.toStdString());
        if (!s) return;
        const auto snd = s->sound;
        m_proc.previewPlay(key, [snd, opt] { return mnm::mdpreview::soundPreview(snd, opt); });
    }
    if (const auto st = m_proc.previewStatus(); st.isNotEmpty()) { m_panel.message = st.toUpperCase(); juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Audition", st); }
    m_drop.repaint();
    m_panel.repaint();
}

void MdEditor::loadKitKey(const juce::String& key)
{
    for (const auto& k : m_lib->kits()) if (k.key == key) { loadKit(k); return; }
}

void MdEditor::loadSoundKey(int track, const juce::String& key)
{
    mnm::mdcatalog::Sound s;
    if (!m_lib->loadSound(key, s)) return;
    const auto* item = m_lib->model().mdCatalog().sound(key.toStdString());
    const auto name = item ? juce::String(item->name).toUpperCase() : juce::String();
    if (!m_proc.loadSound(track, key, s, name))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Sound", "This sound uses a machine Monomodule MD does not have yet.");
    bindTrackPages();
    timerCallback();
}

// The selected track's sound: the name it was loaded or saved under, else its catalog name, else "-"
juce::String MdEditor::soundDisplayName(int t)
{
    if (m_proc.machineIdOf(t) == 0) return "-";
    if (const auto n = m_proc.loadedSoundName(t); n.isNotEmpty()) return n;
    if (const auto key = m_proc.loadedSoundKey(t); key.isNotEmpty())
        if (const auto* s = m_lib->model().mdCatalog().sound(key.toStdString())) return juce::String(s->name).toUpperCase();
    return "-";
}

void MdEditor::sampleMenu()
{
    const int id = m_proc.machineIdOf(m_track);
    if (!isRomMachine(id)) return;
    const int slot = romSlotOf(id);
    juce::PopupMenu m;
    m.addItem(1, "LOAD SAMPLE...");
    m.addItem(2, "CLEAR SAMPLE", m_proc.sampleName(slot).isNotEmpty());
    m.addSeparator();
    if (m_proc.sampleName(slot).isNotEmpty())
        m.addItem(3, m_proc.sampleName(slot).toUpperCase() + "  " + juce::String(m_proc.sampleSeconds(slot), 2) + " S", false);
    m.addItem(4, juce::String("SAMPLE MEMORY USED ") + juce::String(int(std::lround(m_proc.sampleMemoryUsed() * 100.0))) + "%", false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_sample), [this, slot](int r) {
        if (r == 2) { m_proc.clearSample(slot); return; }
        if (r != 1) return;
        m_chooser = std::make_unique<juce::FileChooser>("Load a sample into " + juce::String(kMachines[machineIndexOf(slot + 128)].name),
            juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
        m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            const auto err = m_proc.loadSample(slot, f);
            if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Sample", err);
        });
    });
}

void MdEditor::timerCallback()
{
    // the LCD artwork follows the Monomachine OS file the other Monomodule plugins use
    if (const auto path = loadSharedOsPath(); path != m_artPath) {
        m_artPath = path;
        if (one::loadLcdArt(path)) { resized(); repaint(); }
    }
    const bool ready = m_proc.engineReady();
    if (ready != m_ready) m_shownMachineId = -2;   // the labels come from the OS file
    m_ready = ready;
    m_status.setVisible(false);
    m_osButton.setVisible(false);
    if (m_missingOs.isVisible() == ready) { m_missingOs.setVisible(!ready); if (!ready) m_missingOs.toFront(false); }
    if (!ready) m_missingOs.setStatusMessage(m_proc.firmwarePath().isNotEmpty() ? m_proc.statusText() : juce::String());
    m_engineStatus.setVisible(m_showStatus);
    if (m_showStatus) m_engineStatus.setText(m_proc.statusText() + (m_proc.firmwarePath().isNotEmpty() ? "   " + m_proc.firmwarePath() : juce::String()), juce::dontSendNotification);
    if (--m_skinPoll <= 0) {   // a skin chosen in another Monomodule window
        m_skinPoll = 40;
        if (!m_skinDialog.isVisible()) if (const auto s = skin::load(); s != skin::current()) { skin::apply(s); skinChanged(); }
    }
    // machines: the block, the keys, and the SYNTHESIS page when the selected track's machine changed
    for (int t = 0; t < kTracks; ++t) {
        const int idx = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(t))->load())));
        m_keys.setMachine(t, idx);
        m_keys.setActive(t, m_proc.trackActivity(t) > 0.012f);
        m_keys.setFlags(t, m_proc.apvts.getRawParameterValue(muteId(t))->load() >= 0.5f, m_proc.trackLocked(t));
        if (t == m_track && idx != m_machineIndex) { m_machineIndex = idx; m_machineBlock.setMachine(idx); }
    }
    rebuildSynPage();
    m_level.setMeter(juce::jlimit(0.0f, 1.0f, m_proc.trackActivity(m_track) * 4.0f));
    // ROM machines: their sample slot in the SYNTHESIS title bar
    const int id = m_proc.machineIdOf(m_track);
    const bool rom = isRomMachine(id);
    if (rom) {
        // as much of the sample's name as fits beside the page title
        const int room = KnobPage::kLcdW - LcdCanvas::textWidth(spec::kFontBold8, "SYNTHESIS") - 2 - 6 - 5;
        juce::String text = m_proc.sampleName(romSlotOf(id)).toUpperCase();
        if (text.isEmpty()) text = "LOAD SAMPLE";
        while (text.length() > 1 && LcdCanvas::textWidth(spec::kFontSmall4x5, text.toRawUTF8()) > room) text = text.dropLastCharacters(1);
        if (m_sample.getButtonText() != text) { m_sample.setButtonText(text); resized(); }
    }
    if (m_sample.isVisible() != rom) { m_sample.setVisible(rom); resized(); }
    if (m_proc.previewPoll()) { m_drop.repaint(); m_panel.repaint(); }   // an audition ended
    m_panel.refreshIfChanged();
    if ((m_tick++ % 5) == 0) {   // the modified marks: a few times a second
        m_strip.setKit(m_proc.kitName(), m_proc.kitModified());
        m_strip.setSound(m_track, soundDisplayName(m_track), m_proc.soundModified(m_track));
    }
    m_strip.setVisible(ready);
}

void MdEditor::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper);
    drawShnolkLogo(g, m_logoBounds.toFloat(), lcd::ink);
}

void MdEditor::resized()
{
    auto r = getLocalBounds().reduced(10, 8);
    auto footer = r.removeFromBottom(16);
    m_footerVersion.setBounds(footer.removeFromLeft(200));
    m_footerBy.setBounds(footer.removeFromRight(240));

    const int levW = 19 * kScale, top = r.getY(), gap = 12;
    auto header = r.removeFromTop(48);
    m_logoBounds = header.removeFromLeft(levW).withY(top).withHeight(18 * kScale);
    header.removeFromLeft(8);
    m_machineBlock.setBounds(header.getX(), top, m_machineBlock.preferredWidth(), MdMachineBlock::kLcdH * kScale);
    auto headerTop = header.removeFromTop(30);
    m_menuButton.setBounds(headerTop.removeFromRight(32 * kScale));
    headerTop.removeFromRight(10);
    {   // the kit strip: centred between the machine block and MENU, as wide as that leaves
        const int x0 = m_machineBlock.getRight() + 12, x1 = m_menuButton.getX() - 10;
        const int w = m_strip.preferredWidth(juce::jmax(0, x1 - x0));
        m_strip.setBounds(x0 + (x1 - x0 - w) / 2, top, w, MdKitStrip::kLcdH * MdKitStrip::kS);
    }
    {   // without an OS file: the notice and its button beside the machine block
        auto s = headerTop.withLeft(m_machineBlock.getRight() + 16);
        m_status.setBounds(s.removeFromLeft(LcdCanvas::textWidth(spec::kFontBold8, "NO MACHINEDRUM OS FILE") * kScale + 6));
        s.removeFromLeft(10);
        m_osButton.setBounds(s.removeFromLeft((LcdCanvas::textWidth(spec::kFontBold8, "SELECT OS FILE") + 8) * kScale));
    }
    r.removeFromTop(6);

    auto body = r;
    auto lev = body.removeFromLeft(levW);
    body.removeFromLeft(8);
    body.removeFromTop(36);   // the machine block's lower part and the gap under it
    auto keys = body.removeFromBottom(MdTrackKeys::kLcdH * kScale);
    body.removeFromBottom(gap);
    m_keys.setBounds(keys.withWidth(3 * KnobPage::kWidth + 2 * gap));
    lev = lev.withTop(body.getY() - 11 * kScale).withBottom(body.getY() + 2 * KnobPage::kHeight + gap);
    m_level.setBounds(lev.withHeight((lev.getHeight() / kScale) * kScale));

    auto place = [](juce::Rectangle<int> b, KnobPage& page) { page.setBounds(b.withTop(b.getY() - page.overhangPx())); };   // tabs stand above the page
    auto placeRow = [&](juce::Rectangle<int> row, KnobPage& a, KnobPage& b, KnobPage& c) {
        place(row.removeFromLeft(KnobPage::kWidth), a); row.removeFromLeft(gap);
        place(row.removeFromLeft(KnobPage::kWidth), b); row.removeFromLeft(gap);
        place(row.removeFromLeft(KnobPage::kWidth), c);
    };
    placeRow(body.removeFromTop(KnobPage::kHeight), m_syn, m_fx, m_routing);
    body.removeFromTop(gap);
    placeRow(body.removeFromTop(KnobPage::kHeight), m_lfo, m_master, m_out);

    // the sample slot stands in the SYNTHESIS title bar, right-aligned like a page badge
    const int bw = m_sample.preferredWidth();
    m_sample.setBounds(m_syn.getRight() - bw - kScale, m_syn.getY() + m_syn.overhangPx() + kScale, bw, (KnobPage::kTitleH - 2) * kScale);
    m_saveDialog.setBounds(getLocalBounds());
    m_missingOs.setBounds(getLocalBounds());
    m_about.setBounds(getLocalBounds());
    m_skinDialog.setBounds(getLocalBounds());
    m_engineStatus.setBounds(m_machineBlock.getRight() + 12, m_strip.getBottom() + 1, getWidth() - m_machineBlock.getRight() - 22, 16);
    m_picker.setBounds(juce::Rectangle<int>(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY()));
    m_panel.setTargetBounds(juce::Rectangle<int>(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY()));
}

} // namespace mnm::plugin::md
