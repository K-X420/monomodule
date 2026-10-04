// Monomodule MD editor, in the Monomodule LCD style: the same knob pages, LEV column, header and skin as the One / Six
// editors (one/LcdWidgets.h), drawn from the LCD fonts of the user's Monomachine OS file when there is one.
//   header      logo, the machine block (family + machine; click = the machine picker), KIT, MENU
//   LEV column  the selected track's kit level, with its activity meter
//   pages       SYNTHESIS (labels from the MD OS file)   EFFECTS (AMD..SRR)        ROUTING (DIST VOL PAN DEL REV OUT)
//               LFO (TRK PARAM SHP1 SHP2 TYPE SPD DEP MIX)  MASTER FX (REV DEL EQ DYN tabs)  OUTPUT (VOL VEL ACNT)
//   track keys  the MD's 16 tracks: number, activity LED, machine; click = select + audition
#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MdProcessor.h"
#include "one/LcdWidgets.h"
#include "one/OneLookAndFeel.h"
#include "MdLibraryUi.h"

namespace mnm::plugin::md {

namespace spec = mnm::uispec;

// The machine block: an inverted block with the machine's family and name, then the picker arrow.
class MdMachineBlock : public juce::Component {
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
    void setFlags(int t, bool muted, bool locked)
    {
        if (m_muted[size_t(t)] != muted || m_locked[size_t(t)] != locked) { m_muted[size_t(t)] = muted; m_locked[size_t(t)] = locked; repaint(); }
    }
    void setSelected(int t) { if (m_selected != t) { m_selected = t; repaint(); } }
    void setMachine(int t, int index) { if (m_machine[size_t(t)] != index) { m_machine[size_t(t)] = index; repaint(); } }
    void setActive(int t, bool on) { if (m_active[size_t(t)] != on) { m_active[size_t(t)] = on; repaint(); } }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
private:
    juce::Rectangle<int> keyRect(int t) const;   // LCD px
    juce::Rectangle<int> lockBox(int t) const { const auto r = keyRect(t); return {r.getX() + 2, r.getBottom() - 10, 8, 8}; }
    juce::Rectangle<int> muteBox(int t) const { const auto r = keyRect(t); return {r.getRight() - 10, r.getBottom() - 10, 8, 8}; }
    void mouseMove(const juce::MouseEvent&) override;
    int m_selected = 0;
    std::array<int, kTracks> m_machine{};
    std::array<bool, kTracks> m_active{}, m_muted{}, m_locked{};
};

// The machine picker over the pages: a column per family (GND TRX EFM E12 P-I INP, ROM over three columns, RAM),
// one cell per machine; the current one inverted, ROM slots holding a sample marked.
class MdMachinePicker : public juce::Component {
public:
    MdMachinePicker();
    std::function<void(int)> onPick;        // kMachines index
    std::function<bool(int)> hasSample;     // kMachines index -> a ROM slot with a sample
    void open(int current) { m_current = current; setVisible(true); toFront(false); repaint(); }
    void close() { setVisible(false); if (onClosed) onClosed(); }
    std::function<void()> onClosed;
    void paint(juce::Graphics&) override;
    void resized() override { layout(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover >= 0) { m_hover = -1; repaint(); } }
private:
    struct Column { juce::String title; int span; std::vector<int> items; };   // span: header width in columns
    void layout();
    int itemAt(juce::Point<int> lcd) const;
    std::vector<Column> m_columns;
    std::vector<std::pair<int, juce::Rectangle<int>>> m_cells;      // kMachines index, LCD rect
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> m_heads;
    int m_current = 0, m_hover = -1;
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
    void showMachinePicker() { m_picker.open(m_machineIndex); }   // dev/snapshot
    void showKitList() { openKitList(); }                         // dev/snapshot
    // .syx files dropped on the editor go into the kit library (a file of one kit is loaded as well)
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void bindTrackPages();
    void rebuildSynPage();
    void bindMasterFx(int fx);
    void showMenu();
    void chooseOsFile();
    void importSyx();
    void importFiles(const juce::Array<juce::File>& files);
    void openKitList();
    void loadKit(const KitEntry& e);
    void stepKit(int dir);
    juce::String saveKit(const juce::String& name);
    void sampleMenu();
    void setMachine(int index);
    void applySkin(const skin::Skin& s);

    MdProcessor& m_proc;
    one::OneLookAndFeel m_lnf;
    juce::Rectangle<int> m_logoBounds;
    MdMachineBlock m_machineBlock;
    one::LcdButton m_menuButton{"MENU"};
    one::LcdText m_footerVersion, m_footerBy, m_status;
    juce::SharedResourcePointer<MdLibrary> m_lib;
    MdKitStrip m_strip;
    MdKitDrop m_drop{*m_lib};
    MdSaveDialog m_saveDialog;
    one::LcdButton m_osButton{"SELECT OS FILE"};
    one::LevelColumn m_level;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_levelAttach;
    one::KnobPage m_syn, m_fx, m_routing, m_lfo, m_master, m_out;
    MdBadgeButton m_sample;
    MdTrackKeys m_keys;
    MdMachinePicker m_picker;

    // descriptors the pages draw from (the labels must outlive the bind)
    std::array<std::string, 8> m_synLabels;
    std::array<spec::Param, 8> m_synParams{};
    int m_track = 0, m_machineIndex = -1, m_shownMachineId = -2, m_masterTab = 0;
    juce::String m_artPath;
    int m_tick = 0;
    bool m_ready = false;
    std::unique_ptr<juce::FileChooser> m_chooser;
};

} // namespace mnm::plugin::md
