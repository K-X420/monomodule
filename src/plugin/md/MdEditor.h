// Monomodule MD editor, in the Monomodule LCD style (two colours from the shared skin):
//   header   title, OS file, status
//   pads     16 tracks: machine name, trig light; click = select + audition
//   panel    pages as on the hardware: SYNTH (machine + its eight knobs, labels from the OS file), EFFECTS, ROUTING
//            (DIST VOL PAN DEL REV LEV OUT) and MASTER FX (REVERB / DELAY / EQ / DYNAMIX)
//   mix      output volume
#pragma once
#include <array>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MdProcessor.h"

namespace mnm::plugin::md {

class MdLookAndFeel : public juce::LookAndFeel_V4 {
public:
    MdLookAndFeel();
    void refreshColours();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
};

class MdEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit MdEditor(MdProcessor&);
    ~MdEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    enum class Page { Synth = 0, Effects, Routing, Lfo, Master };
    static constexpr int kNumPages = 5;
    void timerCallback() override;
    void selectTrack(int t);
    void showPage(Page p);
    void attachKnobs();
    void refreshLabels();
    void chooseOsFile();
    void chooseKit();
    juce::Rectangle<int> padBounds(int t) const;
    juce::Rectangle<int> panelBounds() const;

    MdProcessor& m_proc;
    MdLookAndFeel m_lnf;
    int m_track = 0;
    Page m_page = Page::Synth;
    int m_masterFx = 0;
    int m_shownMachine = -1;
    juce::TextButton m_osButton{"OS FILE..."};
    juce::TextButton m_kitButton{"KIT..."};
    std::vector<mnm::md::Kit> m_kits;   // the kits of the last chosen file (for the menu)
    std::array<juce::TextButton, kNumPages> m_pageButtons;
    std::array<juce::TextButton, 4> m_fxButtons;
    juce::ComboBox m_machine;
    std::array<juce::Slider, 8> m_knobs;
    std::array<juce::Label, 8> m_knobLabels;
    juce::Slider m_master, m_accent;
    juce::Label m_masterLabel, m_accentLabel;
    juce::TextButton m_velButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> m_machineAttach;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> m_knobAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> m_masterAttach, m_accentAttach;
    std::unique_ptr<juce::FileChooser> m_chooser;
    std::array<float, kTracks> m_lights{};
};

} // namespace mnm::plugin::md
