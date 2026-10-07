// The editor's wiring: what each of its parts (the strip, the keys, the bar, the panels) asks of it
#include "MdEditorInternal.h"
#include "Transfer.h"
#include <cmath>

namespace mnm::plugin::md {

// The kit strip: the kit / sound arrows, lists and SAVE
void MdEditor::wireStrip()
{
    m_strip.onPart = [this](MdKitStrip::Part part) {
        if (part == MdKitStrip::KitPrev || part == MdKitStrip::KitNext || part == MdKitStrip::Kit) m_browseKits = true;
        if (part == MdKitStrip::SoundPrev || part == MdKitStrip::SoundNext || part == MdKitStrip::Sound) m_browseKits = false;
        grabKeyboardFocus();
        const bool wasKits = m_drop.isVisible() && m_drop.showingKits(), wasSounds = m_drop.isVisible() && !m_drop.showingKits();
        m_drop.setVisible(false);
        m_strip.setOpen(MdKitStrip::None);
        switch (part) {
            case MdKitStrip::KitPrev:   stepKit(-1); break;
            case MdKitStrip::KitNext:   stepKit(1); break;
            case MdKitStrip::Kit:       if (!wasKits) openKitList(); break;
            case MdKitStrip::KitSave:
                m_saveDialog.onSave = [this](const juce::String& name, bool into, bool asVersion) { return saveKit(name, into, asVersion); };
                m_saveDialog.open("SAVE KIT", m_proc.kitName().isEmpty() ? juce::String("NEW KIT") : m_proc.kitName(), slotText(m_lib->projectSlotOfKit(m_proc.loadedKitKey())),
                                  m_proc.loadedKitKey().isNotEmpty() ? m_proc.kitName() : juce::String());
                break;
            case MdKitStrip::SoundPrev: stepSound(-1); break;
            case MdKitStrip::SoundNext: stepSound(1); break;
            case MdKitStrip::Sound:     if (!wasSounds) openSoundList(); break;
            case MdKitStrip::SoundSave:
                m_saveDialog.onSave = [this](const juce::String& name, bool into, bool asVersion) { return saveSound(name, into, asVersion); };
                m_saveDialog.open("SAVE SOUND T" + juce::String(m_track + 1), soundDisplayName(m_track).isEmpty() || soundDisplayName(m_track) == "-" ? juce::String("NEW SOUND") : soundDisplayName(m_track),
                                  slotText(m_lib->projectSlotOfSound(m_proc.loadedSoundKey(m_track))));
                break;
            case MdKitStrip::Library:   if (m_panel.isOpen()) m_panel.close(); else { m_picker.close(false); m_panel.open(); } break;
            case MdKitStrip::None: break;
        }
    };
}

// The library panel and the kit / sound dropdown
void MdEditor::wireLibrary()
{
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
    m_drop.onLoadKit = [this](const KitEntry& e) { m_browseKits = true; loadKit(e); };
    m_drop.onLoadSound = [this](const SoundEntry& e) { m_browseKits = false; loadSound(e); };
    m_drop.onImport = [this] { importSyx(); };
    m_drop.onAudition = [this](const juce::String& key, bool kit) { audition(key, kit ? int(MdLibraryPanel::Kits) : int(MdLibraryPanel::Sounds)); };
    m_drop.isPlaying = [this](const juce::String& key) { return m_proc.previewKey() == key; };
    m_drop.onClosed = [this] { m_strip.setOpen(MdKitStrip::None); if (isShowing()) grabKeyboardFocus(); };
    m_drop.looping = [this] { return m_proc.previewLoop(); };
    m_drop.toggleLoop = [this] { m_proc.previewSetLoop(!m_proc.previewLoop()); };
    m_drop.onLibrary = [this](bool kits) {
        m_picker.close(false);
        m_panel.open();
        m_panel.setTab(kits ? MdLibraryPanel::Kits : MdLibraryPanel::Sounds);
    };
}

// The track keys: steps, painting, marks, mutes, the held step's locks
void MdEditor::wireKeys()
{
    m_keys.onStep = [this](int s) {
        const int t = m_track;
        bool removed = false;
        doEdit(editSlot(), "step " + juce::String(s + 1), [&](mnm::mddump::Pattern& p) {
            if ((p.trigs[t] >> s) & 1) { p.trigs[t] &= ~(1ull << s); p.clearStepLocks(t, s); removed = true; }
            else p.trigs[t] |= 1ull << s;
        });
        if (removed && s == m_heldStep) holdStep(-1);
        refreshGrid();
    };
    m_keys.onPaint = [this](int s, bool on, bool first) {   // one stroke = one undo step
        if (first) ++m_paintStroke;
        const int t = m_track;
        doEdit(editSlot(), on ? "paint steps" : "erase steps", [&](mnm::mddump::Pattern& p) {
            if (on) p.trigs[t] |= 1ull << s;
            else { p.trigs[t] &= ~(1ull << s); p.clearStepLocks(t, s); }
        }, 20000 + (m_paintStroke & 0xFFFF));
        if (!on && s == m_heldStep) holdStep(-1);
        refreshGrid();
    };
    m_keys.onHold = [this](int s) { holdStep(s == m_heldStep ? -1 : s); };
    m_keys.onFlag = [this](int s, int f) { flipStepFlag(s, f); };
    m_keys.onMarkPaint = [this](int s, int f, bool on, bool first) {   // one stroke = one undo step
        if (first) ++m_paintStroke;
        static const char* const names[3] = {"accent", "slide", "swing"};
        const int t = m_track;
        doEdit(editSlot(), names[f], [&](mnm::mddump::Pattern& p) {
            uint32_t all[3] = {p.accentEditAll, p.slideEditAll, p.swingEditAll};
            uint64_t* global[3] = {&p.accent, &p.slide, &p.swing};
            uint64_t* own[3] = {&p.accentPerTrack[t], &p.slidePerTrack[t], &p.swingPerTrack[t]};
            uint64_t& m = *(all[f] ? global[f] : own[f]);
            if (on) m |= 1ull << s; else m &= ~(1ull << s);
        }, 20000 + (m_paintStroke & 0xFFFF));
        refreshGrid();
    };
    m_keys.onSelect = [this](int t) { selectTrack(t); };
    m_keys.onMuteKey = [this](int t) { toggleMute(t); };
    m_keys.onMuteQueue = [this](int t) { m_muteQueue ^= uint16_t(1u << t); m_keys.setMuteQueue(m_muteQueue); };
    m_keys.stepLocks = [this](int step) {   // "PTCH 90, DEC 40": the selected track's locks on that step
        const auto p = m_proc.bankPattern(editSlot());
        juce::String s;
        if (!p || step < 0 || step >= 64) return s;
        for (int q = 0; q < 24; ++q) {
            const int row = p->lockRow(m_track, q);
            if (row < 0 || p->locks[row][step] > 127) continue;
            juce::String name = q < 24 ? juce::String(q) : juce::String();
            if (auto* prm = m_proc.apvts.getParameter(trackParamId(m_track, q))) name = prm->getName(32).fromFirstOccurrenceOf(" ", false, false);
            if (s.isNotEmpty()) s << ", ";
            s << name << " " << int(p->locks[row][step]);
        }
        return s;
    };
    m_keys.onStepMenu = [this](int s) { stepMenu(s); };
    m_keys.onPress = [this](int t) { if (t != m_track) selectTrack(t); m_proc.auditionTrack(t); };
    m_keys.onMute = [this](int t) {
        if (auto* p = m_proc.apvts.getParameter(muteId(t))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
        timerCallback();
    };
    m_keys.onLock = [this](int t) { m_proc.setTrackLocked(t, !m_proc.trackLocked(t)); timerCallback(); };
}

// The knob pages' Alt + turn
void MdEditor::wireAltTurn()
{
    // Alt + turn: the parameter on every track (as the MD's FUNCTION + knob; not MID, RAM-R or CTR tracks)
    auto everyTrack = [this](std::function<juce::String(int, int)> id) {
        return [this, id](int k, int v) {
            for (int u = 0; u < kTracks; ++u) {
                const int mid = m_proc.machineIdOf(u);
                if (u == m_track || isMidMachine(mid) || isCtrMachine(mid) || mid == 160 || mid == 161 || mid == 165 || mid == 166) continue;
                if (auto* p = m_proc.apvts.getParameter(id(u, k))) p->setValueNotifyingHost(p->convertTo0to1(float(v)));
            }
            flash("ALL TRACKS");
        };
    };
    m_syn.onAltTurn = everyTrack([](int u, int k) { return knobId(u, k); });
    m_fx.onAltTurn = everyTrack([](int u, int k) { return fxId(u, k); });
    m_routing.onAltTurn = everyTrack([](int u, int k) {
        static juce::String (* const ids[6])(int) = {distId, volId, panId, delId, revId, routeId};
        return k < 6 ? ids[k](u) : juce::String();
    });
    m_lfo.onAltTurn = everyTrack([](int u, int k) { return lfoId(u, k); });
}

// The sequencer bar
void MdEditor::wireSeqBar()
{
    m_seqBar.onPart = [this](MdSeqBar::Part p) {
        switch (p) {
            case MdSeqBar::Play: {
                if (m_proc.hostPlaying()) return;
                const bool on = !m_proc.internalPlay();
                if (on)   // the sequencer has to be on to play
                    if (auto* a = m_proc.apvts.getParameter(seqId()); a && a->getValue() < 0.5f) a->setValueNotifyingHost(1.0f);
                m_proc.setInternalPlay(on);
                break;
            }
            case MdSeqBar::Grid: toggleGrid(); break;
            case MdSeqBar::Mix: toggleMixer(); break;
            case MdSeqBar::Edit: doublePattern(); return;
            case MdSeqBar::DelPg: deletePage(juce::jlimit(0, 3, m_gridPage)); return;
            case MdSeqBar::Rec: m_proc.setRecord(!m_proc.recordArmed()); if (m_proc.recordArmed()) seqOn(); break;
            case MdSeqBar::Mute: toggleMute(m_track); break;
            case MdSeqBar::TrkPrev: selectTrack((m_track + kTracks - 1) % kTracks); break;
            case MdSeqBar::TrkNext: selectTrack((m_track + 1) % kTracks); break;
            case MdSeqBar::PtnPrev: case MdSeqBar::PtnNext:
                if (auto* a = m_proc.apvts.getParameter(patternId())) {
                    const int v = (editSlot() + (p == MdSeqBar::PtnNext ? 1 : 127)) % 128;
                    a->setValueNotifyingHost(a->convertTo0to1(float(v)));
                    holdStep(-1);
                }
                break;
            default: break;
        }
        refreshGrid();
    };
    m_seqBar.onDragOut = [this](MdSeqBar::Part p) { dragOutClip(p == MdSeqBar::Trk); };
    m_seqBar.onEditClick = [this](MdSeqBar::Part p, int page, const juce::ModifierKeys& mods) {
        if (p == MdSeqBar::PtnPrev || p == MdSeqBar::PtnNext) {   // Shift + the PTN arrows: the chain grows / shrinks
            if (!mods.isShiftDown()) return;
            auto ch = m_proc.chain();
            if (ch.empty()) ch.push_back(editSlot());
            if (p == MdSeqBar::PtnNext) ch.push_back((ch.back() + 1) % 128);
            else if (!ch.empty()) ch.pop_back();
            if (ch.size() > 16) ch.resize(16);
            seqOn();
            m_proc.setChain(ch);
            flash(ch.size() > 1 ? "CHAIN " + juce::String(int(ch.size())) : juce::String("NO CHAIN"));
            return;
        }
        const int op = mods.isAltDown() ? 2 : (mods.isCommandDown() || mods.isCtrlDown()) ? 1 : 0;   // Alt clear, Ctrl paste, Shift copy
        if (p == MdSeqBar::Pages) pageOp(op, page);
        else if (p == MdSeqBar::Trk) trackOp(op);
        else if (p == MdSeqBar::Ptn) patternOp(op);
    };
    m_seqBar.onPage = [this](int page) { m_gridPage = page; m_pagePinned = m_proc.seqPlaying(); refreshGrid(); };
}

// The mixer
void MdEditor::wireMixer()
{
    m_mixer.strip = [this](int t) {
        MdMixer::Strip s;
        const int mi = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_proc.apvts.getRawParameterValue(machineId(t))->load())));
        s.family = familyOf(mi);
        s.machine = shortOf(mi);
        s.level = int(std::lround(m_proc.apvts.getRawParameterValue(levelId(t))->load()));
        s.pan = int(std::lround(m_proc.apvts.getRawParameterValue(panId(t))->load()));
        s.mute = m_proc.apvts.getRawParameterValue(muteId(t))->load() >= 0.5f;
        s.muteGroup = int(std::lround(m_proc.apvts.getRawParameterValue(muteGroupId(t))->load())) - 1;
        s.solo = m_proc.soloed(t);
        s.active = m_proc.trackActivity(t) > 0.012f;
        s.selected = t == m_track;
        s.peak = m_proc.trackPeak(t);
        return s;
    };
    m_mixer.set = [this](int t, MdMixer::What what, int v) {
        if (what == MdMixer::Select) { selectTrack(t); return; }
        if (what == MdMixer::Solo) { m_proc.setSolo(t, v != 0); return; }
        if (what == MdMixer::MuteGroup) {   // the same MUTG as the ROUTING page
            if (auto* p = m_proc.apvts.getParameter(muteGroupId(t))) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(float(v + 1))); p->endChangeGesture(); }
            m_routing.pull(); m_routing.repaint();
            return;
        }
        auto* p = m_proc.apvts.getParameter(what == MdMixer::Level ? levelId(t) : what == MdMixer::Pan ? panId(t) : muteId(t));
        if (!p) return;
        const float to = what == MdMixer::Mute ? (v != 0 ? 1.0f : 0.0f) : p->convertTo0to1(float(v));
        if (what == MdMixer::Mute) p->beginChangeGesture();
        p->setValueNotifyingHost(to);
        if (what == MdMixer::Mute) { p->endChangeGesture(); refreshGrid(); }
    };
    m_mixer.gesture = [this](int t, MdMixer::What what, bool begin) {
        if (auto* p = m_proc.apvts.getParameter(what == MdMixer::Level ? levelId(t) : panId(t))) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
    };
    m_mixer.onClose = [this] { grabKeyboardFocus(); };
}

// The UW sample manager
void MdEditor::wireSamplePanel()
{
    m_samplePanel.setWantsKeyboardFocus(true);
    m_samplePanel.setTooltip("UW samples. Arrows: a slot. Enter / double-click: rename. Delete: clear. RAM 1-4: copy that recording here. RESAMPLE: the selected track's sound here");
    m_samplePanel.name = [this](int i) { return m_proc.sampleName(romSlotIndex(i)); };
    m_samplePanel.seconds = [this](int i) { return m_proc.sampleSeconds(romSlotIndex(i)); };
    m_samplePanel.ramSeconds = [this](int r) { return m_proc.ramSeconds(r); };
    m_samplePanel.memoryUsed = [this] { return m_proc.sampleMemoryUsed(); };
    m_samplePanel.onClose = [this] { grabKeyboardFocus(); };
    m_samplePanel.onClear = [this](int i) { m_proc.clearSample(romSlotIndex(i)); m_samplePanel.repaint(); };
    m_samplePanel.track = [this] { return m_track; };
    m_samplePanel.onResample = [this](int i) {   // the selected track's sound, rendered into the slot
        const auto err = m_proc.resampleTrack(m_track, romSlotIndex(i));
        flash(err.isNotEmpty() ? err.toUpperCase() : "T" + juce::String(m_track + 1) + " > ROM-" + juce::String(i + 1).paddedLeft('0', 2));
        m_samplePanel.repaint();
    };
    m_samplePanel.onRename = [this](int i) {
        const int slot = romSlotIndex(i);
        if (m_proc.sampleName(slot).isEmpty()) { flash("EMPTY SLOT"); return; }
        auto* aw = new juce::AlertWindow("Rename ROM-" + juce::String(i + 1).paddedLeft('0', 2), {}, juce::MessageBoxIconType::NoIcon, this);
        aw->addTextEditor("name", m_proc.sampleName(slot));
        aw->addButton("RENAME", 1, juce::KeyPress(juce::KeyPress::returnKey));
        aw->addButton("CANCEL", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw, slot](int r) {
            if (r == 1) { const auto err = m_proc.renameSample(slot, aw->getTextEditorContents("name")); if (err.isNotEmpty()) flash(err.toUpperCase()); }
            m_samplePanel.repaint();
            m_samplePanel.grabKeyboardFocus();
        }), true);
    };
    m_samplePanel.onCopyRam = [this](int ram, int i) {   // as the MD: the RAM machines start empty after (the memory reloads)
        const int slot = romSlotIndex(i);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "RAM " + juce::String(ram + 1) + " to ROM-" + juce::String(i + 1).paddedLeft('0', 2),
            "Copy RAM " + juce::String(ram + 1) + "'s recording into ROM-" + juce::String(i + 1).paddedLeft('0', 2)
                + (m_proc.sampleName(slot).isNotEmpty() ? " (replacing " + m_proc.sampleName(slot) + ")" : juce::String())
                + "? The sample memory reloads, so the RAM machines start empty.",
            "COPY", "CANCEL", this, juce::ModalCallbackFunction::create([this, ram, slot](int r) {
                if (r != 1) return;
                const auto err = m_proc.copyRamToRom(ram, slot);
                flash(err.isNotEmpty() ? err.toUpperCase() : juce::String("COPIED TO ROM"));
                m_samplePanel.repaint();
            }));
    };
}

// The MIDI settings panel
void MdEditor::wireMidiPanel()
{
    m_midiPanel.get = [this] {
        const auto s = m_proc.midiSettings();
        MdMidiPanel::Values v;
        v.baseChannel = s.baseChannel; v.programChange = s.programChange; v.midiOut = s.midiOut; v.pcChannel = s.pcChannel;
        v.note.fill(-1);
        for (int n = 0; n < 128; ++n) { const int t = s.noteTrack[size_t(n)]; if (t >= 0 && t < kTracks && v.note[size_t(t)] < 0) v.note[size_t(t)] = n; }
        v.ptnMode = s.patternNoteMode;
        v.ctrlIn = s.ctrlIn ? 1 : 0;
        for (int n = 0; n < 128; ++n) {
            const int a = s.noteAction[size_t(n)];
            if (a == MdProcessor::kStartNote && v.startNote < 0) v.startNote = n;
            if (a == MdProcessor::kStopNote && v.stopNote < 0) v.stopNote = n;
        }
        // the pattern notes as FROM / BANK when they are a bank's 16 patterns on consecutive white keys, else CUSTOM
        int from = -1, count = 0;
        for (int n = 0; n < 128; ++n) if (s.noteAction[size_t(n)] >= 0 && s.noteAction[size_t(n)] < 128) { ++count; if (from < 0) from = n; }
        if (count > 0) {
            const int bank = s.noteAction[size_t(from)] / 16;
            bool regular = s.noteAction[size_t(from)] % 16 == 0;
            int n = from, k = 0;
            for (; regular && k < 16 && n < 128; ++n) {
                if (juce::MidiMessage::isMidiNoteBlack(n)) continue;
                regular = s.noteAction[size_t(n)] == bank * 16 + k;
                ++k;
            }
            v.ptnFrom = regular && k == count ? from : -2;
            v.ptnBank = regular ? bank : 0;
        }
        return v;
    };
    m_midiPanel.set = [this](const MdMidiPanel::Values& v) {
        auto s = m_proc.midiSettings();
        s.baseChannel = v.baseChannel; s.programChange = v.programChange; s.midiOut = v.midiOut; s.pcChannel = v.pcChannel;
        s.noteTrack.fill(-1);
        for (int t = 0; t < kTracks; ++t) if (v.note[size_t(t)] >= 0) s.noteTrack[size_t(v.note[size_t(t)])] = int8_t(t);
        s.patternNoteMode = v.ptnMode;
        s.ctrlIn = v.ctrlIn != 0;
        if (v.ptnFrom != -2) {   // CUSTOM (a project's map) stays as it is until FROM is turned
            for (auto& a : s.noteAction) if (a >= 0 && a < 128) a = -1;
            for (int n = v.ptnFrom, k = 0; v.ptnFrom >= 0 && n < 128 && k < 16; ++n)
                if (!juce::MidiMessage::isMidiNoteBlack(n)) s.noteAction[size_t(n)] = int16_t(v.ptnBank * 16 + k++);
        }
        for (auto& a : s.noteAction) if (a == MdProcessor::kStartNote || a == MdProcessor::kStopNote) a = -1;
        if (v.startNote >= 0) s.noteAction[size_t(v.startNote)] = MdProcessor::kStartNote;
        if (v.stopNote >= 0) s.noteAction[size_t(v.stopNote)] = MdProcessor::kStopNote;
        m_proc.setMidiSettings(s);
    };
    m_midiPanel.onLearn = [this] { m_proc.armLearn(); };
    m_midiPanel.onLearnCancel = [this] { m_proc.cancelLearn(); };
    m_midiPanel.onDefault = [this] {
        auto s = MdProcessor::defaultMidiSettings();
        const auto cur = m_proc.midiSettings();
        s.baseChannel = cur.baseChannel; s.programChange = cur.programChange; s.midiOut = cur.midiOut; s.pcChannel = cur.pcChannel;
        m_proc.setMidiSettings(s);
    };
    m_midiPanel.onFromProject = [this] {
        const auto globals = m_proc.bankGlobals();
        juce::PopupMenu m;
        if (globals.empty()) m.addItem(-1, "The pattern bank's project has no globals", false);
        for (size_t i = 0; i < globals.size(); ++i) {
            const auto& g = globals[i];
            juce::String what = "GLOBAL " + juce::String(g.position + 1) + ": channel " + (g.baseChannel < 16 ? juce::String(g.baseChannel + 1) : juce::String("OFF"));
            int mapped = 0; for (int n = 0; n < 128; ++n) if (g.trackOfNote(n) >= 0) ++mapped;
            what << ", " << mapped << " notes mapped";
            m.addItem(int(i) + 1, what);
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_midiPanel), [this, globals](int r) {
            if (r <= 0 || r > int(globals.size())) return;
            m_proc.setMidiSettings(MdProcessor::fromGlobal(globals[size_t(r - 1)], m_proc.midiSettings()));
            m_midiPanel.repaint();
        });
    };
}

// The song editor
void MdEditor::wireSongEditor()
{
    m_songEd.getSong = [this] { return m_proc.bankSong(m_songEd.slot()); };
    m_songEd.edit = [this](const juce::String& label, const std::function<void(mnm::mddump::Song&)>& fn, int coalesce) { doEditSong(m_songEd.slot(), label, fn, coalesce); };
    m_songEd.playingRow = [this] {
        const bool song = m_proc.apvts.getRawParameterValue(seqModeId())->load() >= 0.5f;
        const int sl = juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load())));
        return song && m_proc.seqPlaying() && sl == m_songEd.slot() ? m_proc.seqSongRow() : -1;
    };
    m_songEd.defaultPattern = [this] { return editSlot(); };
    m_songEd.onEnter = [this](int row) {   // as the MD's ENTER in song edit
        if (m_proc.seqPlaying()) { m_proc.cueSongRow(row); flash("NEXT: ROW " + juce::String(row + 1)); }
        else { m_proc.setSongStartRow(row); flash("START: ROW " + juce::String(row + 1)); }
    };
    m_songEd.startRow = [this] { return m_proc.songStartRow(); };
    m_songEd.cuedRow = [this] { return m_proc.songCue(); };
    m_songEd.patternLength = [this](int p) { const auto pat = m_proc.bankPattern(p); return pat ? juce::jlimit(1, 64, int(pat->length)) : 16; };
}

} // namespace mnm::plugin::md
