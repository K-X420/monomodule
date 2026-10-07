// Monomodule MD's editor: building the panels, the knob pages, the timer, the layout
#include "MdEditorInternal.h"
#include <cmath>
#include "ShnolkLogo.h"
#include "SharedSettings.h"

namespace mnm::plugin::md {

// ---------------------------------------------------------------------------------------------- editor

MdEditor::MdEditor(MdProcessor& p)
    : AudioProcessorEditor(p), m_proc((skin::apply(skin::load()), p)),   // the skin first: m_lnf reads it when it is built
      m_footerVersion(spec::kFontSmall4x5, kPluginVersion, 2),
      m_footerBy(spec::kFontSmall4x5, "BY SHNOLK - MD BY KX", 2, false, juce::Justification::centredRight),
      m_status(spec::kFontBold8, "NO MACHINEDRUM OS FILE", kScale),
      m_bpmLabel(spec::kFontBold8, "BPM", kScale, false, juce::Justification::centredRight),
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
    m_machineBlock.onOpen = [this] {
        if (m_picker.isOpen()) { m_picker.close(); return; }
        if (m_drop.isVisible()) m_drop.close();
        if (m_panel.isOpen()) m_panel.close(false);
        m_picker.open(m_machineIndex);
        m_machineBlock.setOpen(true);
    };
    addMouseListener(this, true);   // a press anywhere else closes the picker and the list
    m_picker.onPick = [this](int idx) { setMachine(idx); };
    m_picker.onClosed = [this] { m_machineBlock.setOpen(false); };
    m_picker.sampleName = [this](int idx) { const int id = kMachines[idx].id; return isRomMachine(id) ? m_proc.sampleName(romSlotOf(id)) : juce::String(); };
    addChildComponent(m_picker);

    m_menuButton.onClick = [this] { showMenu(); };
    m_osButton.onClick = [this] { chooseOsFile(); };
    for (juce::Component* c : {static_cast<juce::Component*>(&m_menuButton), static_cast<juce::Component*>(&m_footerVersion),
                               static_cast<juce::Component*>(&m_footerBy), static_cast<juce::Component*>(&m_level),
                               static_cast<juce::Component*>(&m_strip)})
        addAndMakeVisible(c);
    wireStrip();
    wireLibrary();
    addChildComponent(m_status);
    addAndMakeVisible(m_bpmLabel);
    addAndMakeVisible(m_bpm);
    m_bpmAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, bpmId(), m_bpm);
    addAndMakeVisible(m_bpmSync);
    m_bpmSyncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(m_proc.apvts, bpmSyncId(), m_bpmSync);
    m_bpmSync.onStateChange = [this] { m_bpm.setSynced(m_bpmSync.getToggleState()); };
    m_bpm.setSynced(m_bpmSync.getToggleState());
    addChildComponent(m_osButton);

    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_master, &m_out}) addAndMakeVisible(pg);
    m_master.setTabs({kMasterTabs[0], kMasterTabs[1], kMasterTabs[2], kMasterTabs[3]}, 0, [this](int tab) { bindMasterFx(tab); });
    for (int s = 0; s < 128; ++s) { m_ptnNames[size_t(s)] = kPatternNames[s]; m_ptnNamePtrs[size_t(s)] = m_ptnNames[size_t(s)].c_str(); }
    m_out.setTabs({"OUT", "PTN"}, 0, [this](int tab) { bindOutPage(tab); });
    bindOutPage(0);
    wireKeys();
    wireAltTurn();
    setWantsKeyboardFocus(true);
    addAndMakeVisible(m_seqBar);
    wireSeqBar();
    addChildComponent(m_songEd);
    setTooltipsOn(loadSharedSetting("tooltips", "1") != "0");
    {   // tooltips for the knobs, the header
        int pi = 0;
        for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_master, &m_out}) {
            const int page = pi++;
            pg->tipFor = [this, page, pg](int, const juce::String& label) { return knobTip(label, page == 5 && pg->currentTab() == 1 ? 6 : page, familyOf(juce::jlimit(0, kNumMachines - 1, m_machineIndex))); };
        }
        m_bpm.setTooltip("The tempo (drag or wheel). With SYNC on it follows Ableton");
        m_bpmSync.setTooltip("SYNC: the tempo follows Ableton's; off: the BPM here");
        m_menuButton.setTooltip("Kits, songs, MIDI settings, the mixer, undo, kit tools, tooltips on / off, the OS file");
        m_level.setTooltip("The selected track's LEVEL, with its meter");
        m_machineBlock.setTooltip("The selected track's machine: click to choose another");
        bindOutPage(m_outTab);      // (bound before the tips were set)
        bindMasterFx(m_masterTab);
    }
    m_mixer.setTooltip("Mixer: level, pan, mute (M) and solo (S) of every track. Alt: the mute groups (Alt+click M: next)");
    m_songEd.setTooltip("Song editor. UP / DOWN: a row. ENTER: start there (or play it next). CTRL+DOWN / UP: insert / delete a row. CTRL+C / V: copy / paste a row");
    m_midiPanel.setTooltip("MIDI settings");
    addChildComponent(m_mixer);
    m_mixer.setWantsKeyboardFocus(true);
    wireMixer();
    addChildComponent(m_samplePanel);
    wireSamplePanel();
    addChildComponent(m_midiPanel);
    wireMidiPanel();
    wireSongEditor();
    bindMasterFx(0);

    m_sample.onClick = [this] { sampleMenu(); };
    addChildComponent(m_sample);

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
    if (t != m_track) {
        if (m_picker.isOpen()) m_picker.close(false);
        if (m_drop.isVisible()) m_drop.close();
    }
    if (t != m_track) m_heldStep = -1;
    m_track = t;
    m_keys.setSelected(t);
    bindTrackPages();
    refreshGrid();
}

int MdEditor::editSlot() const
{
    return juce::jlimit(0, 127, int(std::lround(m_proc.apvts.getRawParameterValue(patternId())->load())));
}

void MdEditor::bindTrackPage(one::KnobPage& page, const spec::Param* params, std::function<juce::String(int)> id, std::function<int(int)> mdParam)
{
    if (m_heldStep < 0) { page.bind(params, id); return; }
    const int t = m_track, step = m_heldStep, slot = editSlot();
    auto paramValue = [this, id](int k) {
        if (auto* a = m_proc.apvts.getParameter(id(k))) return int(std::lround(a->convertFrom0to1(a->getValue())));
        return 0;
    };
    page.bindCustom(params,
        [this, t, step, slot, mdParam, paramValue](int k) {
            const int p = mdParam(k);
            if (p >= 0)
                if (const auto pat = m_proc.bankPattern(slot)) {
                    const int row = pat->lockRow(t, p);
                    if (row >= 0 && pat->locks[row][step] <= 127) return int(pat->locks[row][step]);
                }
            return paramValue(k);
        },
        [this, t, step, slot, id, mdParam](int k, int v) {
            const int p = mdParam(k);
            if (p < 0) {   // not lockable: the parameter itself
                if (auto* a = m_proc.apvts.getParameter(id(k))) a->setValueNotifyingHost(a->convertTo0to1(float(v)));
                return;
            }
            doEdit(slot, "lock", [&](mnm::mddump::Pattern& pat) { pat.trigs[t] |= 1ull << step; pat.setLock(t, p, step, v); }, 1000 + 24 * step + p);
            refreshGrid();
        },
        [this, t, step, slot, mdParam](int k) {   // a double-click: that parameter's lock goes (the knob shows the track's value)
            const int p = mdParam(k);
            if (p >= 0) doEdit(slot, "clear lock", [&](mnm::mddump::Pattern& pat) { pat.clearLock(t, p, step); });
            refreshGrid();
        },
        [this, t, step, slot, mdParam](int k) {   // locked on this step: the value box inverted
            const int p = mdParam(k);
            if (p < 0) return false;
            const auto pat = m_proc.bankPattern(slot);
            if (!pat) return false;
            const int row = pat->lockRow(t, p);
            return row >= 0 && pat->locks[row][step] <= 127;
        });
}

void MdEditor::bindOutPage(int tab)
{
    m_outTab = tab;
    for (int k = 0; k < 8; ++k) m_out.setValuesSource(k, nullptr);
    if (tab == 0) {
        m_out.bind(kOutParams, [](int k) { return k == 0 ? masterId() : k == 1 ? velModeId() : k == 2 ? accentId() : k == 3 ? extendedId() : k == 4 ? seqId() : k == 5 ? patternId() : k == 6 ? seqModeId() : k == 7 ? songId() : juce::String(); });
        m_out.setValuesSource(5, [this] { return m_ptnNamePtrs.data(); });
        return;
    }
    static const auto names = [] {
        struct N { std::array<std::string, 64> len, kit; std::array<std::string, 31> swing; std::array<const char*, 64> lenP{}, kitP{}; std::array<const char*, 31> swingP{}; } n;
        for (int i = 0; i < 64; ++i) { n.len[size_t(i)] = std::to_string(i + 1); n.kit[size_t(i)] = (i < 9 ? "0" : "") + std::to_string(i + 1); n.lenP[size_t(i)] = n.len[size_t(i)].c_str(); n.kitP[size_t(i)] = n.kit[size_t(i)].c_str(); }
        for (int i = 0; i < 31; ++i) { n.swing[size_t(i)] = std::to_string(50 + i) + "%"; n.swingP[size_t(i)] = n.swing[size_t(i)].c_str(); }
        return n;
    }();
    static const char* const kSpd[8] = {"1X", "2X", "3/4X", "3/2X", "1/2X", "1/4X", "1/8X", "3X"};   // 4-7: the plugin's extras
    static const char* const kPages[4] = {"1", "2", "3", "4"};
    const spec::Param params[8] = {readout("LEN", names.lenP.data(), 64, 15), readout("SPD", kSpd, 8), readout("SWNG", names.swingP.data(), 31), numeric("ACC", 64),
                                   readout("KIT", names.kitP.data(), 64), toggle("GRID", kSeqNames), readout("PAGE", kPages, 4), readout("PTN", kPatternNames, 128)};
    m_out.bindCustom(params,
        [this](int k) {
            const auto p = m_proc.bankPattern(editSlot());
            switch (k) {
                case 0: return p ? juce::jlimit(0, 63, int(p->length) - 1) : 15;
                case 1: return p ? int(p->doubleTempo & 7) : 0;
                case 2: return p ? juce::jlimit(0, 30, p->swingPercent() - 50) : 0;
                case 3: return p ? int(p->accentAmount) : 64;
                case 4: return p ? juce::jlimit(0, 63, int(p->kit)) : 0;
                case 5: return m_gridOn ? 1 : 0;
                case 6: return m_gridPage;
                default: return editSlot();
            }
        },
        [this](int k, int v) {
            if (k == 5) { m_gridOn = v != 0; if (!m_gridOn) holdStep(-1); refreshGrid(); return; }
            if (k == 6) { m_gridPage = v; refreshGrid(); return; }
            if (k == 7) {
                if (auto* a = m_proc.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(float(v)));
                holdStep(-1);
                refreshGrid();
                return;
            }
            static const char* const what[5] = {"length", "speed", "swing", "accent", "kit"};
            doEdit(editSlot(), what[juce::jlimit(0, 4, k)], [&](mnm::mddump::Pattern& p) {
                switch (k) {
                    case 0: p.length = uint8_t(v + 1); p.scale = uint8_t(v / 16); break;   // SCALE: the pages the length needs
                    case 1: p.doubleTempo = uint8_t(v & 7); break;
                    case 2: p.swingAmount = uint32_t(v * 16384 / 50); break;
                    case 3: p.accentAmount = uint8_t(v); break;
                    case 4: p.kit = uint8_t(v); break;
                    default: break;
                }
            });
            refreshGrid();
        });
    m_out.setValuesSource(7, [this] { return m_ptnNamePtrs.data(); });
}

void MdEditor::setTooltipsOn(bool on)
{
    if (on && !m_tips) m_tips = std::make_unique<juce::TooltipWindow>(this, 700);
    if (!on) m_tips.reset();
}

void MdEditor::toggleGrid()
{
    m_gridOn = !m_gridOn;
    if (!m_gridOn) holdStep(-1);
    if (m_outTab == 1) m_out.pull();
    refreshGrid();
}

void MdEditor::toggleMixer()
{
    if (m_mixer.isVisible()) { m_mixer.close(); return; }
    m_midiPanel.setVisible(false);
    m_songEd.setVisible(false);
    m_mixer.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
    m_mixer.setVisible(true);
    m_mixer.toFront(true);
    m_mixer.grabKeyboardFocus();
}

void MdEditor::openSamplePanel()
{
    m_mixer.setVisible(false); m_midiPanel.setVisible(false); m_songEd.setVisible(false);
    m_samplePanel.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
    m_samplePanel.open();
}

void MdEditor::openMidiPanel()
{
    m_mixer.setVisible(false);
    m_midiPanel.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
    m_midiPanel.setVisible(true);
    m_midiPanel.toFront(true);
    m_midiPanel.grabKeyboardFocus();
    m_midiPanel.repaint();
}

void MdEditor::seqOn()
{
    if (auto* a = m_proc.apvts.getParameter(seqId()); a && a->getValue() < 0.5f) {   // sequencing: SEQ on (the host's play then runs it)
        a->beginChangeGesture(); a->setValueNotifyingHost(1.0f); a->endChangeGesture();
    }
}

void MdEditor::openSongEditor()
{
    m_mixer.setVisible(false);
    m_songEd.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
    m_songEd.open(juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load()))));
    m_songEd.grabKeyboardFocus();
}

void MdEditor::refreshGrid()
{
    {   // GRID follows the playing page of the pattern it shows (unless a page was picked while playing)
        const bool playingHere = m_proc.seqPlaying() && m_proc.seqPattern() == editSlot();
        if (!m_proc.seqPlaying()) m_pagePinned = false;
        else if (playingHere && !m_pagePinned && m_heldStep < 0 && m_proc.seqStep() >= 0) m_gridPage = m_proc.seqStep() / 16;
    }
    {   // the bar
        MdSeqBar::State s;
        const int slot = editSlot();
        const auto p = m_proc.bankPattern(slot);
        s.playing = m_proc.seqPlaying() || m_proc.internalPlay();
        s.hostPlaying = m_proc.hostPlaying();
        s.grid = m_gridOn;
        s.mark = m_gridOn ? m_markMode : 0;
        s.beat = m_proc.beatPhase() < 0.5f;
        {   // the chain, as the PTN box shows it
            const auto ch = m_proc.chain();
            juce::String c;
            for (size_t i = 0; i < ch.size(); ++i) c << (i ? ">" : "") << kPatternNames[juce::jlimit(0, 127, ch[i])];
            s.chain = c;
        }
        s.mix = m_mixer.isVisible();
        s.track = m_track;
        s.muted = m_proc.apvts.getRawParameterValue(muteId(m_track))->load() >= 0.5f;
        s.rec = m_proc.recordArmed();
        s.recording = m_proc.recording();
        const int mi = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(m_track))->load())));
        s.machine = shortOf(mi) == "---" ? familyOf(mi) : shortOf(mi);
        s.pattern = slot;
        s.empty = p == nullptr;
        s.length = p ? juce::jlimit(1, 64, int(p->length)) : 16;
        s.page = juce::jlimit(0, (s.length - 1) / 16, m_gridPage);
        s.step = m_proc.seqPlaying() && m_proc.seqPattern() == slot ? m_proc.seqStep() : -1;
        s.row = m_proc.seqPlaying() ? m_proc.seqSongRow() : -1;
        s.seqOff = m_proc.apvts.getRawParameterValue(seqId())->load() < 0.5f;
        if (m_flash.isNotEmpty() && juce::Time::getMillisecondCounter() > m_flashUntil) m_flash.clear();
        s.flash = m_flash;
        m_seqBar.setState(s);
    }
    MdTrackKeys::Grid g;
    g.on = m_gridOn;
    if (!m_gridOn) m_markMode = 0;
    g.mark = m_markMode;
    if (m_gridOn) {
        const int slot = editSlot(), t = m_track;
        const auto p = m_proc.bankPattern(slot);
        g.length = p ? juce::jlimit(1, 64, int(p->length)) : 16;
        m_gridPage = juce::jlimit(0, (g.length - 1) / 16, m_gridPage);
        g.page = m_gridPage;
        g.held = m_heldStep;
        if (p) {
            g.trigs = p->trigs[t];
            g.accent = p->accentEditAll ? p->accent : p->accentPerTrack[t];
            g.slide = p->slideEditAll ? p->slide : p->slidePerTrack[t];
            g.swing = p->swingEditAll ? p->swing : p->swingPerTrack[t];
            for (int q = 0; q < 24; ++q) {
                const int row = p->lockRow(t, q);
                if (row < 0) continue;
                for (int s = 0; s < 64; ++s) if (p->locks[row][s] <= 127) g.locks |= 1ull << s;
            }
        }
        if (m_proc.seqPattern() == slot) g.play = m_proc.seqStep();
    }
    m_keys.setGrid(g);
}

// Binds the LEV column and the track pages to the selected track
void MdEditor::bindTrackPages()
{
    const int t = m_track;
    m_levelAttach.reset();
    m_levelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, levelId(t), m_level);
    m_level.setDoubleClickReturnValue(true, 100.0);
    m_shownMachineId = -2; m_pagesMachineId = -2;
    rebuildSynPage();
}

// The SYNTHESIS page follows the machine: its labels and defaults from the OS file's descriptor
void MdEditor::rebuildSynPage()
{
    const int id = m_proc.machineIdOf(m_track);
    int shown = id;   // CTR-8P: its knob labels also follow the TRK / PAR assignments
    if (id == kCtr8p)
        for (int p = 8; p < 24; ++p) shown = (shown * 131 + int(std::lround(m_proc.apvts.getRawParameterValue(trackParamId(m_track, p))->load()))) & 0x7FFFFF;
    if (shown == m_shownMachineId) return;
    m_shownMachineId = shown;
    const auto* m = m_proc.machineInfo(id);
    for (int k = 0; k < 8; ++k) {
        m_synLabels[size_t(k)] = m ? m->labels[size_t(k)] : std::string();
        auto& p = m_synParams[size_t(k)];
        p = m_synLabels[size_t(k)].empty() ? blank() : numeric("", m ? m->defaults[size_t(k)] : 0);
        p.label = m_synLabels[size_t(k)].c_str();
    }
    if (isMidMachine(id)) {   // NOTE as a note, N2 N3 PB centred
        m_synParams[0] = named(m_synParams[0].label, 0, 64);
        for (int k : {1, 2, 5}) { const auto* l = m_synParams[size_t(k)].label; m_synParams[size_t(k)] = bipolar(l); }
    }
    if (id == kCtr8p)   // P1..P8 show the parameter they turn
        for (int k = 0; k < 8; ++k) {
            const int tt = juce::jmin(15, int(std::lround(m_proc.apvts.getRawParameterValue(trackParamId(m_track, 8 + 2 * k))->load())));
            const int tp = juce::jmin(23, int(std::lround(m_proc.apvts.getRawParameterValue(trackParamId(m_track, 9 + 2 * k))->load())));
            static const char* const code[24] = {"S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "AMD", "AMF", "EQF", "EQG",
                                                 "FF", "FW", "FQ", "SRR", "DIS", "VOL", "PAN", "DEL", "REV", "LS", "LD", "LM"};
            m_synLabels[size_t(k)] = (juce::String(tt + 1) + code[tp]).toStdString();   // "5S1" = track 5 SYN1, "16VOL"
            m_synParams[size_t(k)].label = m_synLabels[size_t(k)].c_str();
        }
    const int t = m_track;
    bindTrackPage(m_syn, m_synParams.data(), [t](int k) { return knobId(t, k); }, [](int k) { return k; });
    // the other pages follow the machine family (rebound on a machine change only: a turn of an 8P TRK / PAR is not one)
    if (id == m_pagesMachineId) return;
    m_pagesMachineId = id;
    const bool mid = isMidMachine(id), master = ctrMasterFx(id) >= 0;
    bindTrackPage(m_fx, mid ? kMidFx : id == kCtr8p ? kCtr8pFx : master ? kBlankPage : kFxParams, [t](int k) { return fxId(t, k); }, [](int k) { return 8 + k; });
    bindTrackPage(m_routing, mid ? kMidRouting : id == kCtr8p ? kCtr8pRouting : master ? kGroupsOnly : id == kCtrAll ? kCtrAllRouting : kRoutingParams, [t](int k) {
        static juce::String (* const ids[8])(int) = {distId, volId, panId, delId, revId, routeId, trigGroupId, muteGroupId};
        return ids[k](t);
    }, [](int k) { return k < 5 ? 16 + k : -1; });
    bindTrackPage(m_lfo, id == kCtr8p ? kCtr8pLfo : id == kCtrAll ? kCtrAllLfo : kLfoParams, [t](int k) { return lfoId(t, k); }, [](int k) { return k >= 5 ? 21 + (k - 5) : -1; });
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
    juce::PopupMenu outputs;   // as Monomodule Six's Plugin Outputs
    const int outMode = int(std::lround(m_proc.apvts.getRawParameterValue(outputModeId())->load()));
    outputs.addItem(40, "HARDWARE (MAIN A/B, OUT C/D, OUT E/F)", true, outMode == int(OutputMode::Hardware));
    outputs.addItem(41, "PER TRACK (TRACK 1-16, WHERE ENABLED)", true, outMode == int(OutputMode::Tracks));
    m.addSubMenu("PLUGIN OUTPUTS", outputs);
    m.addItem(16, "MIXER  (M)", true, m_mixer.isVisible());
    m.addItem(17, "TOOLTIPS", true, m_tips != nullptr);
    m.addSeparator();
    m.addItem(18, "UNDO KIT CHANGE  (CTRL+ALT+Z)", m_kitUndo.valid);
    m.addItem(19, "RELOAD KIT  (CTRL+R)", m_proc.loadedKitKey().isNotEmpty());
    m.addItem(20, "COPY T" + juce::String(m_track + 1) + " MACHINE  (CTRL+SHIFT+C)");
    m.addItem(21, "PASTE MACHINE ONTO T" + juce::String(m_track + 1) + "  (CTRL+SHIFT+V)", m_soundClip.has_value());
    m.addItem(22, "CLEAR T" + juce::String(m_track + 1) + " MACHINE  (CTRL+SHIFT+DEL)");
    m.addItem(11, "MIDI SETTINGS...");
    m.addItem(23, "SAMPLES...");
    m.addSeparator();
    {
        const int sl = juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load())));
        m.addItem(12, "EDIT SONG " + juce::String(sl + 1).paddedLeft('0', 2) + "...");
    }
    m.addItem(13, m_proc.bankProjectId().isNotEmpty() ? "SAVE PATTERNS + SONGS TO LIBRARY (" + m_proc.bankName().toUpperCase() + ")"
                                                      : juce::String("SAVE PATTERNS + SONGS TO LIBRARY (NEW PROJECT)"));
    m.addItem(14, m_undo.empty() ? juce::String("UNDO") : "UNDO " + m_undo.back().label.toUpperCase() + "  (CTRL+Z)", !m_undo.empty());
    m.addItem(15, m_redo.empty() ? juce::String("REDO") : "REDO " + m_redo.back().label.toUpperCase() + "  (CTRL+SHIFT+Z)", !m_redo.empty());
    m.addItem(10, "SYNC BPM TO HOST", true, m_bpmSync.getToggleState());
    m.addSubMenu("SKIN", skins);
    m.addItem(7, "SHOW ENGINE STATUS", true, m_showStatus);
    m.addItem(4, juce::String("SAMPLE MEMORY USED ") + juce::String(int(std::lround(m_proc.sampleMemoryUsed() * 100.0))) + "%", false);
    m.addSeparator();
    m.addItem(8, "PLUGIN INFO...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_menuButton), [this](int r) {
        if (r == 11) { openMidiPanel(); return; }
        if (r == 23) { openSamplePanel(); return; }
        if (r == 16) { toggleMixer(); return; }
        if (r == 18) { undoKit(); return; }
        if (r == 19) { reloadKit(); return; }
        if (r == 20) { copyMachine(); return; }
        if (r == 21) { pasteMachine(); return; }
        if (r == 22) { clearMachine(); return; }
        if (r == 17) { setTooltipsOn(m_tips == nullptr); saveSharedSetting("tooltips", m_tips ? "1" : "0"); return; }
        if (r == 12) { openSongEditor(); return; }
        if (r == 13) { saveBankToLibrary(); return; }
        if (r == 14) { undo(); return; }
        if (r == 15) { redo(); return; }
        if (r == 1) chooseOsFile();
        else if (r == 2) importSyx();
        else if (r == 5) { m_proc.initKit(); selectTrack(m_track); timerCallback(); }
        else if (r == 6) { m_proc.clearFirmware(); timerCallback(); }
        else if (r == 7) { m_showStatus = !m_showStatus; resized(); timerCallback(); }
        else if (r == 8) { m_about.setVisible(true); m_about.toFront(false); }
        else if (r == 9) { if (m_panel.isOpen()) m_panel.close(); else m_panel.open(); }
        else if (r == 10) m_bpmSync.setToggleState(!m_bpmSync.getToggleState(), juce::sendNotificationSync);
        else if (r == 40 || r == 41) { if (auto* p = m_proc.apvts.getParameter(outputModeId())) p->setValueNotifyingHost(p->convertTo0to1(float(r - 40))); }
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

void MdEditor::mouseDown(const juce::MouseEvent& e)
{
    auto* c = e.eventComponent;
    if (m_drop.isVisible() && c != &m_drop && !m_drop.isParentOf(c) && c != &m_strip) m_drop.close();
    if (m_picker.isOpen() && c != &m_picker && !m_picker.isParentOf(c) && c != &m_machineBlock) m_picker.close();
}

void MdEditor::timerCallback()
{
    if (m_muteQueue != 0 && !juce::ModifierKeys::currentModifiers.isShiftDown()) applyMuteQueue();   // Shift let go: the queued mutes flip together
    if (m_midiPanel.learning() >= 0) if (const int n = m_proc.takeLearned(); n >= 0) m_midiPanel.learned(n);   // LEARN: the note played
    // the LCD artwork follows the Monomachine OS file the other Monomodule plugins use
    if (const auto path = loadSharedOsPath(); path != m_artPath) {
        m_artPath = path;
        if (one::loadLcdArt(path)) { resized(); repaint(); }
    }
    m_bpm.setHostBpm(float(m_proc.hostBpm()));
    if (--m_osPoll <= 0) { m_osPoll = 40; m_proc.refreshSharedOsPath(); }   // an OS file picked in another instance (every ~2 s)
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
        if (t == 0) {
            const bool seq = m_proc.apvts.getRawParameterValue(seqId())->load() >= 0.5f && m_proc.seqPattern() >= 0;   // an empty pattern runs too
            m_keys.setSeq(m_proc.seqStep(), seq ? m_proc.seqLength() : 0, m_proc.seqTrigs(m_track));
            refreshGrid();
            {   // a finished recording run: one undo step
                MdProcessor::RecordedEdit re;
                while (m_proc.takeRecordedEdit(re)) { m_undo.push_back({re.slot, re.before, re.after, "recording"}); m_redo.clear(); m_lastCoalesce = -1; }
                if (m_proc.recording()) m_seqBar.repaint();   // the blinking dot
                if (m_songEd.isVisible()) m_songEd.repaint();
                if (m_mixer.isVisible()) m_mixer.repaint();   // the meters
            }
            if (m_outTab == 1) m_out.pull();
            if (m_heldStep >= 0) for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->pull();
            if (--m_ptnPoll <= 0) {   // the bank: its name on the panel, its empty slots marked
                m_ptnPoll = 10;
                const auto bn = m_proc.bankName();
                const auto badge = bn.isEmpty() ? juce::String("BANK: --") : bn == "PLUGIN" ? juce::String("BANK: NEW") : "BANK: " + bn.toUpperCase().removeCharacters(" ").substring(0, 7);
                if (badge != m_bankBadge) { m_bankBadge = badge; m_out.setBadge(m_bankBadge.toRawUTF8()); }
                bool changed = false;
                for (int s = 0; s < 128; ++s) {
                    const std::string want = std::string(kPatternNames[s]) + (m_proc.bankHasPattern(s) ? "" : " --");
                    if (want != m_ptnNames[size_t(s)]) { m_ptnNames[size_t(s)] = want; m_ptnNamePtrs[size_t(s)] = m_ptnNames[size_t(s)].c_str(); changed = true; }
                }
                if (changed) m_out.repaint();
            }
        }
        m_keys.setFlags(t, m_proc.apvts.getRawParameterValue(muteId(t))->load() >= 0.5f, m_proc.trackLocked(t));
        if (t == m_track && idx != m_machineIndex) { m_machineIndex = idx; m_machineBlock.setMachine(idx); }
    }
    rebuildSynPage();
    m_level.setMeter(m_proc.trackPeak(m_track));
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
    m_bpmSync.setBounds(headerTop.removeFromRight(28 * kScale));
    headerTop.removeFromRight(8);
    m_bpm.setBounds(headerTop.removeFromRight(62 * kScale));   // "120.0" in the tall digit face is 59 px
    m_bpmLabel.setBounds(headerTop.removeFromRight(22 * kScale));
    {   // the kit strip: centred between the machine block and MENU, as wide as that leaves
        const int x0 = m_machineBlock.getRight() + 12, x1 = m_bpmLabel.getX() - 4;
        const int w = m_strip.preferredWidth(juce::jmax(0, x1 - x0));
        m_strip.setBounds(x0 + (x1 - x0 - w) / 2, top, w, MdKitStrip::kLcdH * MdKitStrip::kS);
    }
    {   // without an OS file: the notice and its button beside the machine block
        auto s = headerTop.withLeft(m_machineBlock.getRight() + 16);
        m_status.setBounds(s.removeFromLeft(LcdCanvas::textWidth(spec::kFontBold8, "NO MACHINEDRUM OS FILE") * kScale + 6));
        s.removeFromLeft(10);
        m_osButton.setBounds(s.removeFromLeft((LcdCanvas::textWidth(spec::kFontBold8, "SELECT OS FILE") + 8) * kScale));
    }
    {   // the sequencer bar: under the kit strip, from the machine block to the right edge
        const int x0 = m_machineBlock.getRight() + 12;
        m_seqBar.setBounds(x0, m_strip.getBottom() + 6, (r.getRight() - x0) / MdSeqBar::kS * MdSeqBar::kS, MdSeqBar::kLcdH * MdSeqBar::kS);
        auto col = [&](MdKitStrip::Part p, bool right) { const auto b = m_strip.partBounds(p) + m_strip.getPosition(); return ((right ? b.getRight() : b.getX()) - x0) / MdSeqBar::kS; };
        m_seqBar.setAnchors({(m_bpmSync.getX() - x0) / MdSeqBar::kS, m_bpmSync.getWidth() / MdSeqBar::kS,
                             (m_menuButton.getX() - x0) / MdSeqBar::kS, (m_menuButton.getRight() - x0) / MdSeqBar::kS - (m_menuButton.getX() - x0) / MdSeqBar::kS,
                             col(MdKitStrip::KitPrev, false), col(MdKitStrip::KitSave, true), col(MdKitStrip::SoundPrev, false), col(MdKitStrip::Library, true)});
    }
    r.removeFromTop(6);

    auto body = r;
    auto lev = body.removeFromLeft(levW);
    body.removeFromLeft(8);
    body.removeFromTop(36);   // the machine block's lower part and the gap under it
    auto keys = body.removeFromBottom(MdTrackKeys::kLcdH * kScale);
    body.removeFromBottom(gap);
    // the track keys run from the LEV column's left edge to the pages' right edge (the LEV column ends above them)
    {   // ... at a whole number of LCD pixels (the keys paint in LCD pixels; a remainder would stay unpainted)
        const int right = keys.getX() + 3 * KnobPage::kWidth + 2 * gap;
        const int w = (right - lev.getX()) / kScale * kScale;
        m_keys.setBounds(keys.withLeft(right - w).withRight(right));
    }
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
    m_picker.setTargetBounds(juce::Rectangle<int>(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY()));
    m_panel.setTargetBounds(juce::Rectangle<int>(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY()));
}

} // namespace mnm::plugin::md
