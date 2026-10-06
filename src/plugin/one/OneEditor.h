// Monomodule One / Six editor: header, LEV column, machine block, the six pages (2 rows x 3), and on
// Six a column of six track keys on the right (the hardware's track keys) that selects which track the
// pages, machine block and LEV column edit. Every element is drawn from the hardware's LCD glyphs and
// fonts, black on white.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "OneProcessor.h"
#include "OneLookAndFeel.h"
#include "Lcd.h"
#include "LcdWidgets.h"
#include "Overlays.h"
#include "MachinePicker.h"
#include "SkinDialog.h"
#include "LibraryUi.h"

namespace mnm::plugin::one {

// The machine block: an inverted block with the group's logo (its name for
// GND/FX) and the machine name beside it, then the picker arrow; the block hugs its contents. A click
// toggles the machine picker (MachinePicker); the arrow points up while it is open.
class MachineBar : public juce::Component {
public:
    static constexpr int kLcdH = 26, kLogoX = 4, kLogoH = 18, kLogoGap = 6, kArrowGap = 4, kArrowW = 5, kPadR = 4;   // LCD px
    MachineBar();
    void setParameter(juce::RangedAudioParameter& param);   // the machine parameter of the shown track
    std::function<void()> onOpen;
    std::function<void()> onContentChanged;   // the width follows the machine; the parent must re-layout
    void setOpen(bool open) { if (m_open != open) { m_open = open; repaint(); } }
    int preferredWidth() const { return widthLcd() * kScale; }
    int preferredHeight() const { return kLcdH * kScale; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    int logoWidthLcd() const;   // the group logo in a kLogoH-row band (drawGroupLogo), or the group's name
    int widthLcd() const;
    std::unique_ptr<juce::ParameterAttachment> m_attach;
    int m_slot = 0;
    bool m_open = false;
};

// Six: the column of track keys. One cell per track, stacked to the height of the two page rows:
// the track number, the machine's name, an activity LED lit by the track's output, a LOCK key and a MUTE key.
// A click selects the track (inverted cell); the mute key toggles the track's mute parameter; a locked track
// keeps its sound when a kit is loaded from the library.
class TrackColumn : public juce::Component {
public:
    static constexpr int kLcdW = 32, kCellH = 26;   // LCD px; 6 x 26 = the two page rows plus their gap
    TrackColumn(juce::AudioProcessorValueTreeState& apvts, int numTracks);
    std::function<void(int)> onSelect;
    std::function<bool(int)> isLocked;
    std::function<void(int)> onLock;      // toggle
    void setDropTarget(int t) { if (m_dropTarget != t) { m_dropTarget = t; repaint(); } }   // a preset dragged over a key
    int trackAt(juce::Point<int> local) const { const int t = local.y / (kCellH * kScale); return getLocalBounds().contains(local) && t >= 0 && t < m_numTracks ? t : -1; }
    void setSelected(int t) { if (m_selected != t) { m_selected = t; repaint(); } }
    int selected() const { return m_selected; }
    void setPeak(int t, float linear);   // from the editor's timer
    void refresh();                      // re-read machines and mutes; repaints on change
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    juce::Rectangle<int> muteBox(int t) const;   // LCD px
    juce::Rectangle<int> lockBox(int t) const;
    int m_dropTarget = -1;
    juce::AudioProcessorValueTreeState& m_apvts;
    int m_numTracks, m_selected = 0;
    std::array<int, kMaxTracks> m_slot{};
    std::array<bool, kMaxTracks> m_mute{}, m_active{};
};

class OneEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer,
                  private juce::AudioProcessorValueTreeState::Listener {
public:
    explicit OneEditor(MnmOneProcessor&);
    ~OneEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;   // the drop-target frame
    // Up / Down: the previous / next patch of the selector last used (presets, or kits on Six), as its arrows step
    bool keyPressed(const juce::KeyPress& k) override;
    void resized() override;
    // Library drag and drop: a .mnmtrack lands on the selected track (or, on Six, the track key it is dropped
    // on); a .mnmkit fills a Six. Returns false with a message when the file cannot be applied.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    bool dropFile(const juce::File& file, int track, juce::String& error);
    void mouseDown(const juce::MouseEvent&) override;   // a press outside the picker closes it
    void refresh() { m_proc.syncMachineSideEffects(); timerCallback(); }   // dev/snapshot: apply pending rebinds without the message loop
    void showSkinDialog() { m_skinDialog.open(); }   // (UI snapshots)
    void showMachinePicker() { m_picker.open(false); }   // dev/snapshot: fully open at once
    void showLibrary(int tab = 0) { m_libPanel.setTab(LibraryPanel::Tab(tab)); openLibrary(false); }   // dev/snapshot
    void showPresetMenu(bool kits = false) { openMenu(kits); }
    void showSaveDialog(bool kit = false) { m_saveDialog.open(kit); }
    void selectTrack(int t);   // Six: rebinds every control to track t (0-based)
    int selectedTrack() const { return m_track; }

private:
    void rebuildSynPage();
    void showConfigMenu();
    void applySkin(const skin::Skin& s, bool save);   // makes s the skin (all windows follow through the shared settings)
    void skinChanged();                             // re-skins the widgets and repaints after the skin colours changed
    void openOsChooser();
    void parameterChanged(const juce::String& id, float newValue) override;
    void timerCallback() override;

    MnmOneProcessor& m_proc;
    OneLookAndFeel m_lnf;
    juce::Rectangle<int> m_logoBounds;   // the shnolk logo (vector art)
    LcdText m_footerVersion, m_footerAlpha, m_bpmLabel;
    LcdToggle m_bpmSync{"SYNC"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_bpmSyncAttach;
    juce::Label m_status, m_fwPath;
    LcdButton m_menuButton{"MENU"};
    bool m_browseKits = false;   // the arrow keys step kits (the KIT selector was used last), else presets
    // Six, on the face (also in MENU): POLY (spread the notes over tracks 1-6) and the selected track's sound copied to all
    LcdToggle m_poly{"POLY"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> m_polyAttach;
    LcdButton m_copyAll{"COPY T1 TO ALL"};
    void copySoundToAll();
    MachineBar m_machineBar;
    BpmReadout m_bpm;
    LevelColumn m_level;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_levelAttach, m_bpmAttach;
    KnobPage m_syn, m_amp, m_filt, m_efx, m_lfo1, m_lfo23;
    std::unique_ptr<TrackColumn> m_trackColumn;   // Six only
    int m_track = 0;   // the track the controls are bound to
    int dropTargetTrack(int x, int y) const;      // the track a drop at (x, y) lands on
    juce::Rectangle<int> dropFrame(const juce::StringArray& files, int x, int y) const;
    bool m_dragOver = false;
    juce::Rectangle<int> m_dropRect;
    void bindLfo(KnobPage& page, int lfo);
    void bindTrackPages();
    std::unique_ptr<juce::FileChooser> m_chooser;
    MissingOsOverlay m_missingOs;
    AboutOverlay m_about;
    MachinePicker m_picker;
    // the library (LibraryUi.h)
    void openMenu(bool kits);
    void openLibrary(bool animate = true);
    void afterLibraryLoad();
    void updateStrip();
    LibraryBridge m_lib;
    PresetStrip m_strip;
    LibraryDrop m_drop;
    LibraryPanel m_libPanel;
    SaveDialog m_saveDialog;
    SkinDialog m_skinDialog;
    int m_shownSlot = -1;   // machine slot currently bound in the SYN page
    std::atomic<bool> m_synDirty{true};
    bool m_showStatus = false;
    int m_sharedPollCountdown = 0;
    juce::String m_artPath;   // the OS path the LCD artwork was last requested for
    int m_skinPollCountdown = 0;
};

} // namespace mnm::plugin::one
