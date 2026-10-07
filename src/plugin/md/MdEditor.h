// Monomodule MD editor, in the Monomodule LCD style: the same knob pages, LEV column, header and skin as the One / Six
// editors (one/LcdWidgets.h), drawn from the LCD fonts of the user's Monomachine OS file when there is one.
//   header      logo, the machine block (family + machine; click = the machine picker), KIT, MENU
//   LEV column  the selected track's kit level, with its activity meter
//   pages       SYNTHESIS (labels from the MD OS file)   EFFECTS (AMD..SRR)        ROUTING (DIST VOL PAN DEL REV OUT)
//               LFO (TRK PARAM SHP1 SHP2 TYPE SPD DEP MIX)  MASTER FX (REV DEL EQ DYN tabs)  OUTPUT (VOL VEL ACNT)
//   track keys  the MD's 16 tracks: number, activity LED, machine; click = select + audition
#pragma once
#include <optional>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MdProcessor.h"
#include "one/LcdWidgets.h"
#include "one/OneLookAndFeel.h"
#include "one/SkinDialog.h"
#include "Overlays.h"
#include "MdLibraryUi.h"
#include "MdLibraryPanel.h"

namespace mnm::plugin::md {

namespace spec = mnm::uispec;

// The machine block: an inverted block with the machine's family and name, then the picker arrow.
class MdMachineBlock : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr int kLcdH = 26;
    MdMachineBlock();
    void setMachine(int index);   // kMachines index
    void setOpen(bool open) { if (m_open != open) { m_open = open; repaint(); } }
    int preferredWidth() const;
    std::function<void()> onOpen;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { if (onOpen) onOpen(); }
private:
    int m_index = 0;
    bool m_open = false;
};

// The 16 track keys in a row, as the MD's trig keys: number, activity LED, family and machine.
class MdTrackKeys : public juce::Component, public juce::SettableTooltipClient {
public:
    static constexpr int kLcdH = 36;
    MdTrackKeys();
    std::function<void(int)> onPress;   // select + audition
    std::function<void(int)> onMute, onLock;   // toggle
    std::function<void(int)> onMuteQueue;      // Shift + M: queued, applied when Shift is let go
    void setMuteQueue(uint16_t q) { if (q != m_muteQueue) { m_muteQueue = q; repaint(); } }
    void setFlags(int t, bool muted, bool locked)
    {
        if (m_muted[size_t(t)] != muted || m_locked[size_t(t)] != locked) { m_muted[size_t(t)] = muted; m_locked[size_t(t)] = locked; repaint(); }
    }
    void setSelected(int t) { if (m_selected != t) { m_selected = t; repaint(); } }
    void setMachine(int t, int index) { if (m_machine[size_t(t)] != index) { m_machine[size_t(t)] = index; repaint(); } }
    void setActive(int t, bool on) { if (m_active[size_t(t)] != on) { m_active[size_t(t)] = on; repaint(); } }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    int trackAt(juce::Point<int> local) const { for (int t = 0; t < kTracks; ++t) if (keyRect(t).contains(local / one::kScale)) return t; return -1; }
    juce::Rectangle<int> keyBounds(int t) const { return keyRect(t); }   // LCD px
    void setDropTarget(int t) { if (m_dropTarget != t) { m_dropTarget = t; repaint(); } }   // a sound dragged over a key
    // Pattern playback, as the hardware's trig keys: key n is step n of the playing page (16 steps a page); a step LED
    // per key (solid = the selected track has a trig there), the playing step marked along the key's top.
    // length 0 = no pattern (keys as usual); step -1 = not playing (page 1)
    // GRID (the OUTPUT panel's PATTERN tab), as the hardware's grid recording: the keys are the selected track's steps
    // on the page. Click: its trig on / off; Shift + click: hold the step (the track pages set its locks); Ctrl / Cmd +
    // click: select that key's track (FUNC + trig); right-click: the step's menu (accent, slide, swing, locks, track).
    struct Grid {
        bool on = false;
        int page = 0, length = 16, play = -1, held = -1;
        uint64_t trigs = 0, accent = 0, slide = 0, swing = 0, locks = 0;
        int mark = 0;   // 0 the trigs; 1 ACCENT, 2 SLIDE, 3 SWING: the keys show / flip that mark (as the MD's edit windows)
        bool operator==(const Grid& o) const { return mark == o.mark && on == o.on && page == o.page && length == o.length && play == o.play && held == o.held && trigs == o.trigs && accent == o.accent && slide == o.slide && swing == o.swing && locks == o.locks; }
    };
    void setGrid(const Grid& g) { if (!(g == m_grid)) { m_grid = g; repaint(); } }
    std::function<juce::String(int step)> stepLocks;   // GRID: a step's locks as text for its tooltip ("" none)
    std::function<void(int step)> onStep, onHold;
    std::function<void(int step, bool on, bool first)> onPaint;   // GRID click / drag: set a step's trig (first = a new stroke)
    std::function<void(int step, int flag)> onFlag;               // GRID: a step's A / S / W button (0 accent, 1 slide, 2 swing)
    std::function<void(int step)> onStepMenu;
    std::function<void(int track)> onSelect, onMuteKey;   // Ctrl / Cmd + click: select; Alt + click: mute (as FUNC + trig)
    int devSeqStep() const { return m_seqStep; }   // dev/tests
    int devSeqLength() const { return m_seqLen; }
    void setSeq(int step, int length, uint64_t trigs)
    {
        if (m_seqStep != step || m_seqLen != length || m_seqTrigs != trigs) { m_seqStep = step; m_seqLen = length; m_seqTrigs = trigs; repaint(); }
    }
private:
    juce::Rectangle<int> keyRect(int t) const;   // LCD px
    // GRID: A S W stacked down the key's right side (round buttons as L / M)
    juce::Rectangle<int> flagBox(int key, int f) const { const auto r = keyRect(key); return {r.getRight() - 11, r.getY() + 2 + f * 11, 9, 9}; }
    juce::Rectangle<int> lockBox(int t) const { const auto r = keyRect(t); return {r.getX() + 2, r.getBottom() - 11, 9, 9}; }
    juce::Rectangle<int> muteBox(int t) const { const auto r = keyRect(t); return {r.getRight() - 11, r.getBottom() - 11, 9, 9}; }
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { m_painting = false; }
    bool m_painting = false, m_paintOn = false;
    uint16_t m_muteQueue = 0;
    int m_paintLast = -1;
    int m_selected = 0;
    std::array<int, kTracks> m_machine{};
    std::array<bool, kTracks> m_active{}, m_muted{}, m_locked{};
    int m_dropTarget = -1;
    int m_seqStep = -1, m_seqLen = 0;
    uint64_t m_seqTrigs = 0;
    Grid m_grid;
    void paintGrid(one::LcdCanvas& cv);
};

// The machine picker over the pages, as Monomodule's: a column per family (GND TRX EFM E12 P-I INP, ROM over three
// columns, RAM MID CTR) with a logo and name header and a blurb (a hovered machine's description takes its place),
// then the machines as rows (ROM rows with their sample's name); the current one inverted. It unrolls from the top
// edge when opened and rolls back up when closed. Keys: arrows move the cursor (up/down in a column, left/right to
// the next column at the same row), Return picks, Escape or a click outside a row closes.
class MdMachinePicker : public juce::Component, private juce::Timer {
public:
    MdMachinePicker();
    std::function<void(int)> onPick;                 // kMachines index
    std::function<juce::String(int)> sampleName;     // kMachines index -> the ROM slot's sample name ("" = empty)
    std::function<void()> onClosed;                  // when it starts to close (the machine block's arrow follows)
    void setTargetBounds(juce::Rectangle<int> fullyOpen);   // the six pages' area, in the parent
    void open(int current, bool animate = true);
    void close(bool animate = true);
    bool isOpen() const { return m_wantOpen; }
    void setHover(int index) { m_hover = index; repaint(); }   // dev/snapshot
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover >= 0) { m_hover = -1; repaint(); } }
    bool keyPressed(const juce::KeyPress&) override;
private:
    // A family: its machines in sub-columns of up to 16 (ROM takes three), under one header (logo + name) and blurb
    struct Family { juce::String name; std::vector<int> items; int firstCol = 0, cols = 1; juce::Rectangle<int> bounds, header, blurb; };
    void layout();
    int itemAt(juce::Point<int> p) const;
    int familyOf(int index) const;
    juce::String describe(int index) const;
    void drawFamily(juce::Graphics& g, const Family& f) const;
    void timerCallback() override;
    void applyAnimation();
    std::vector<Family> m_families;
    std::vector<std::vector<int>> m_grid;       // the visual columns (ROM's three apart): kMachines indices top to bottom
    std::vector<juce::Rectangle<int>> m_rows;   // per kMachines index (empty when not laid out)
    juce::Rectangle<int> m_target;
    int m_current = 0, m_hover = -1;
    bool m_wantOpen = false;
    float m_anim = 0.0f;   // 0 rolled up, 1 open
};

// A paper box with ink text standing in a page's title bar (the SYNTHESIS page's sample slot), clickable.
class MdBadgeButton : public juce::Button {
public:
    MdBadgeButton() : juce::Button("badge") { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    int preferredWidth() const;
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

class MdEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit MdEditor(MdProcessor&);
    ~MdEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void selectTrack(int t);
    void refresh() { timerCallback(); }   // dev/snapshot: apply pending state without the message loop
    void pickerKey(const juce::KeyPress& k) { m_picker.keyPressed(k); }   // dev/snapshot
    void showMachinePicker(int hover = -1) { m_picker.open(m_machineIndex, false); m_picker.setHover(hover); }   // dev/snapshot
    void showKitList() { openKitList(); }                         // dev/snapshot
    void showLibrary(int tab) { m_panel.open(false); m_panel.setTab(MdLibraryPanel::Tab(tab)); }   // dev/snapshot
    void showAbout() { m_about.setVisible(true); m_about.toFront(false); }                     // dev/snapshot
    // dev/tests: the GRID as clicks drive it
    void devStep(int s) { if (m_keys.onStep) m_keys.onStep(s); }
    void devHold(int s) { holdStep(s); }
    void devShiftTrack(int dir) { shiftTrack(dir); }
    void devFlag(int s, int f) { flipStepFlag(s, f); }
    void devPerTrack(int f) { togglePerTrack(f); }
    void devMarkMode(int m) { m_markMode = m; refreshGrid(); }
    void devUndoKit() { undoKit(); }
    void devSelectTrack(int t) { selectTrack(t); }
    void devCopyMachine() { copyMachine(); }
    void devPasteMachine() { pasteMachine(); }
    void devClearMachine() { clearMachine(); }
    void devLoadKitData(const mnm::mddump::Kit& k, const juce::String& name) { m_kitUndo = {true, m_proc.captureMdKit(), m_proc.loadedKitKey(), m_proc.kitName()}; m_proc.loadMdKit({}, k, name); }
    void devCopyNote(int s) { copyNote(s); }
    void devPasteNote(int s) { pasteNote(s); }
    void devQueueMute(int t) { if (m_keys.onMuteQueue) m_keys.onMuteQueue(t); }
    void devApplyMuteQueue() { applyMuteQueue(); }
    void applyMuteQueue();
    void devPaint(int s, bool on, bool first) { if (m_keys.onPaint) m_keys.onPaint(s, on, first); }
    void devDeletePage(int page) { deletePage(page); }
    void devSetPage(int page) { m_gridPage = page; }
    void devMuteKey(int t) { if (m_keys.onMuteKey) m_keys.onMuteKey(t); }
    void devStripPart(MdKitStrip::Part p) { if (m_strip.onPart) m_strip.onPart(p); }
    void devUndo() { undo(); }
    void devRedo() { redo(); }
    one::KnobPage& devSynPage() { return m_syn; }
    MdSongEditor& devSongEditor() { return m_songEd; }
    void devOpenMidi() { openMidiPanel(); }
    void devToggleMixer() { toggleMixer(); }
    MdMixer& devMixer() { return m_mixer; }
    bool devGridOn() const { return m_gridOn; }
    const MdTrackKeys& devKeys() const { return m_keys; }
    void devBarClick(MdSeqBar::Part p, int page, const juce::ModifierKeys& mods) { if (m_seqBar.onEditClick) m_seqBar.onEditClick(p, page, mods); }
    void devBarPart(MdSeqBar::Part p) { if (m_seqBar.onPart) m_seqBar.onPart(p); }
    void devShowPage(int page) { m_gridPage = page; }
    void devOpenSong(int slot) { m_songEd.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY()); m_songEd.open(slot); }
    void showGrid(int held)   // dev/snapshot: the PATTERN tab, GRID on, a step held (-1 none)
    {
        m_out.setTabs({"OUT", "PTN"}, 1, [this](int tab) { bindOutPage(tab); });
        bindOutPage(1);
        m_gridOn = true;
        holdStep(held);
        refreshGrid();
    }
    void showSkinDialog() { m_skinDialog.setBounds(getLocalBounds()); m_skinDialog.open(); }   // dev/snapshot
    // Dropped on the editor: .syx files go into the library (a file of one kit is loaded as well); a .mdkit from the
    // Library app loads; a .mdsound lands on the track key it is dropped on, else on the selected track
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    // while files are dragged over: a dotted frame round where they will land (a track key for a sound, else the pages)
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void paintOverChildren(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& k) override;   // Ctrl/Cmd+Z undo, Ctrl/Cmd+Shift+Z (or +Y) redo
    void mouseDown(const juce::MouseEvent& e) override;   // a press elsewhere closes the machine picker and the list

private:
    void timerCallback() override;
    void bindTrackPages();
    void rebuildSynPage();
    // A track page: bound to the parameters, or (a step held in GRID) to that step's locks: mdParam(k) = the MD
    // parameter (0..23) the cell is, -1 for one that cannot be locked (it stays the parameter)
    void bindTrackPage(one::KnobPage& page, const spec::Param* params, std::function<juce::String(int)> id, std::function<int(int)> mdParam);
    void bindOutPage(int tab);   // OUTPUT: the plugin's output and playback; PATTERN: the pattern's settings and GRID
    int editSlot() const;        // PTN: the pattern GRID and the PATTERN tab edit
    void holdStep(int step);     // -1 releases
    void stepMenu(int step);
    // Copy / paste / clear at face level (the bar's modifier clicks, the keys): a page (all tracks), the track, the
    // pattern; double. Each says what it did in the bar for a moment.
    void editMenu();   // the former EDIT menu (every operation in one list; kept for reference, not on the bar)
    void pageOp(int op, int page);   // op 0 copy, 1 paste, 2 clear
    void trackOp(int op);
    void patternOp(int op);
    void doublePattern();
    void flash(const juce::String& s) { m_flash = s; m_flashUntil = juce::Time::getMillisecondCounter() + 1500; refreshGrid(); }
    juce::String m_flash;
    juce::uint32 m_flashUntil = 0;
    void openSongEditor();
    void saveBankToLibrary();   // the patterns and songs into their project (a new version), or a new project
    // Every pattern edit of the editor, with undo: `coalesce` >= 0 merges a run of edits of the same kind (a knob drag)
    // into one undo step
    void doEdit(int slot, const juce::String& label, const std::function<void(mnm::mddump::Pattern&)>& fn, int coalesce = -1);
    void undo();
    void redo();
    void toggleMute(int t);
    void deletePage(int page);
    void flipStepFlag(int step, int flag);   // a step's accent / slide / swing mark (the all-tracks mask or the track's own)
    int m_markMode = 0;                      // GRID's edit window: 0 trigs, 1 accent, 2 slide, 3 swing
    void togglePerTrack(int flag);           // A / S / W keys: that mark between all tracks and per track
    // Kit tools (as the MD's UNDO KIT / kit reload / copy, paste, clear machine)
    struct KitUndo { bool valid = false; mnm::mddump::Kit kit; juce::String key, name; };
    KitUndo m_kitUndo;   // the kit before the last kit load (undo swaps them)
    void undoKit();
    void reloadKit();
    std::optional<mnm::mdcatalog::Sound> m_soundClip;
    juce::String m_soundClipName;
    void copyMachine();
    void pasteMachine();
    void clearMachine();
    void shiftTrack(int dir);      // the selected track's trigs (and their locks / marks) one step later / earlier, wrapping
    void copyNote(int step);       // the held step: its trig, marks and locks
    void pasteNote(int step);
    uint16_t m_muteQueue = 0;      // Shift + M: mutes to flip when Shift is let go
    int m_paintStroke = 0;
    struct UndoStep {
        int slot = 0; std::shared_ptr<const mnm::mddump::Pattern> before, after; juce::String label;
        int song = -1; std::shared_ptr<const mnm::mddump::Song> songBefore, songAfter;   // a song edit (song >= 0)
    };
    void seqOn();   // SEQ ON, when the user sequences (a step placed, REC armed)
    void doEditSong(int slot, const juce::String& label, const std::function<void(mnm::mddump::Song&)>& fn, int coalesce = -1);
    MdSongEditor m_songEd;
    MdMidiPanel m_midiPanel;
    void openMidiPanel();
    MdMixer m_mixer;
    void toggleMixer();
    void toggleGrid();
    std::vector<UndoStep> m_undo, m_redo;
    int m_lastCoalesce = -1;
    juce::int64 m_lastEditMs = 0;
    struct Clip { int kind = 0; bool all = false; int track = 0, page = 0; mnm::mddump::Pattern pat; };   // kind 1 page, 2 track, 3 pattern, 4 note (page = its step)
    Clip m_clip;
    void refreshGrid();
    void bindMasterFx(int fx);
    void showMenu();
    void chooseOsFile();
    void importSyx();
    void importFiles(const juce::Array<juce::File>& files);
    void openKitList();
    void loadKit(const KitEntry& e);
    void stepKit(int dir);
    juce::String saveKit(const juce::String& name, bool intoProject = false, bool asVersion = true);
    juce::Rectangle<int> dropFrame(const juce::StringArray& files, int x, int y) const;
    bool m_dragOver = false;
    juce::Rectangle<int> m_dropRect;
    void openSoundList();
    void loadSound(const SoundEntry& e);
    void stepSound(int dir);
    juce::String saveSound(const juce::String& name, bool intoProject = false, bool asVersion = true);
    static juce::String slotText(const mnm::library::LibraryModel::Slot& s);
    void skinChanged();
    juce::String soundDisplayName(int t);
    void audition(const juce::String& key, int kind);   // MdLibraryPanel::Tab: sound, kit, pattern
    void loadKitKey(const juce::String& key);
    void loadSoundKey(int track, const juce::String& key);
    void sampleMenu();
    void setMachine(int index);
    void applySkin(const skin::Skin& s);

    MdProcessor& m_proc;
    one::OneLookAndFeel m_lnf;
    juce::Rectangle<int> m_logoBounds;
    MdMachineBlock m_machineBlock;
    one::LcdButton m_menuButton{"MENU"};
    // tempo, as Monomodule's header: BPM (the host's while synced; drag or type it when free) and SYNC
    one::LcdText m_bpmLabel;
    one::BpmReadout m_bpm;
    one::LcdToggle m_bpmSync{"SYNC"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_bpmAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_bpmSyncAttach;
    int m_osPoll = 0;
    one::LcdText m_footerVersion, m_footerBy, m_status;
    juce::SharedResourcePointer<MdLibrary> m_lib;
    MdKitStrip m_strip;
    MdLibraryDrop m_drop{*m_lib};
    MdSaveDialog m_saveDialog;
    MdLibraryPanel m_panel{*m_lib};
    MissingOsOverlay m_missingOs;
    AboutOverlay m_about{"MONOMODULE MD", "Machinedrum"};
    one::SkinDialog m_skinDialog;
    juce::Label m_engineStatus;
    bool m_showStatus = false;
    int m_skinPoll = 0;
    one::LcdButton m_osButton{"SELECT OS FILE"};
    one::LevelColumn m_level;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_levelAttach;
    one::KnobPage m_syn, m_fx, m_routing, m_lfo, m_master, m_out;
    MdBadgeButton m_sample;
    MdTrackKeys m_keys;
    std::unique_ptr<juce::TooltipWindow> m_tips;   // shows the panels' tooltips (hover ~0.7 s); none while TOOLTIPS is off
    void setTooltipsOn(bool on);
    MdSeqBar m_seqBar;
    bool m_gridOn = false;
    int m_gridPage = 0, m_heldStep = -1, m_outTab = 0, m_ptnPoll = 0;
    bool m_pagePinned = false;
    bool m_browseKits = false;   // Up / Down step kits (the KIT selector was used last), else the track's sounds   // a page dot clicked while playing: GRID stays there (else it follows the playing page)
    std::array<std::string, 128> m_ptnNames;      // "A01", or "A01 --" for an empty slot
    std::array<const char*, 128> m_ptnNamePtrs{};
    juce::String m_bankBadge;
    MdMachinePicker m_picker;

    // descriptors the pages draw from (the labels must outlive the bind)
    std::array<std::string, 8> m_synLabels;
    std::array<spec::Param, 8> m_synParams{};
    int m_track = 0, m_machineIndex = -1, m_shownMachineId = -2, m_pagesMachineId = -2, m_masterTab = 0;
    juce::String m_artPath;
    int m_tick = 0;
    bool m_ready = false;
    std::unique_ptr<juce::FileChooser> m_chooser;
};

} // namespace mnm::plugin::md
