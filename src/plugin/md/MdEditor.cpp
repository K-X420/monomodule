#include "MdEditor.h"
#include "Skin.h"

namespace mnm::plugin::md {

namespace {
juce::Colour ink() { return skin::inkColour(); }
juce::Colour paper() { return skin::paperColour(); }
juce::Font lcdFont(float h, bool bold = true) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }

constexpr int kW = 980, kH = 440;
constexpr int kMargin = 16;
constexpr int kHeaderH = 44;
constexpr int kPadsY = kHeaderH + 10, kPadH = 64;
constexpr int kPanelY = kPadsY + kPadH + 18;

const char* familyOf(int id)
{
    if (id == 0) return "";
    if (id < 16) return "GND";
    if (id < 32) return "TRX";
    if (id < 48) return "EFM";
    if (id < 64) return "E12";
    return "P-I";
}
}

// ---------------------------------------------------------------------------------------------- look and feel

MdLookAndFeel::MdLookAndFeel() { refreshColours(); }

void MdLookAndFeel::refreshColours()
{
    setColour(juce::ComboBox::backgroundColourId, paper());
    setColour(juce::ComboBox::textColourId, ink());
    setColour(juce::ComboBox::outlineColourId, ink());
    setColour(juce::ComboBox::arrowColourId, ink());
    setColour(juce::PopupMenu::backgroundColourId, paper());
    setColour(juce::PopupMenu::textColourId, ink());
    setColour(juce::PopupMenu::headerTextColourId, ink().withAlpha(0.55f));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ink());
    setColour(juce::PopupMenu::highlightedTextColourId, paper());
    setColour(juce::TextButton::buttonColourId, paper());
    setColour(juce::TextButton::textColourOffId, ink());
    setColour(juce::TextButton::textColourOnId, paper());
    setColour(juce::Label::textColourId, ink());
    setColour(juce::Slider::textBoxTextColourId, ink());
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::backgroundColourId, paper());
    setColour(juce::TextEditor::textColourId, ink());
    setColour(juce::TextEditor::highlightColourId, ink().withAlpha(0.25f));
    setColour(juce::CaretComponent::caretColourId, ink());
}

void MdLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    // value readout under the knob (the slider has no text box)
    auto area = juce::Rectangle<int>(x, y, w, h);
    const auto valueRow = area.removeFromBottom(18);
    if (s.isEnabled()) {
        g.setColour(ink());
        g.setFont(lcdFont(14.0f));
        g.drawText(juce::String(int(std::lround(s.getValue()))), valueRow, juce::Justification::centred);
    }
    const auto bounds = area.toFloat().reduced(4.0f);
    const float r = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float alpha = s.isEnabled() ? 1.0f : 0.25f;
    juce::Path track;
    track.addCentredArc(c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, start, end, true);
    g.setColour(ink().withAlpha(0.2f * alpha));
    g.strokePath(track, juce::PathStrokeType(3.0f));
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getMinimum() < 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;
    juce::Path value;
    value.addCentredArc(c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, std::min(from, angle), std::max(from, angle), true);
    g.setColour(ink().withAlpha(alpha));
    g.strokePath(value, juce::PathStrokeType(3.0f));
    g.fillEllipse(juce::Rectangle<float>(r * 1.0f, r * 1.0f).withCentre(c));
    g.setColour(paper());
    g.drawLine({c, c.getPointOnCircumference(r * 0.45f, angle)}, 2.0f);
}

void MdLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    g.setColour(paper()); g.fillRect(0, 0, w, h);
    g.setColour(ink()); g.drawRect(0, 0, w, h, 2);
    juce::Path arrow;
    arrow.addTriangle(float(w - 18), float(h) * 0.4f, float(w - 8), float(h) * 0.4f, float(w - 13), float(h) * 0.65f);
    g.fillPath(arrow);
}

void MdLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds();
    g.setColour(down ? ink() : paper()); g.fillRect(r);
    if (over && !down) { g.setColour(ink().withAlpha(0.08f)); g.fillRect(r); }
    g.setColour(ink()); g.drawRect(r, 2);
}

juce::Font MdLookAndFeel::getComboBoxFont(juce::ComboBox&) { return lcdFont(17.0f); }
juce::Font MdLookAndFeel::getPopupMenuFont() { return lcdFont(15.0f); }
juce::Font MdLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return lcdFont(14.0f); }

juce::Label* MdLookAndFeel::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(lcdFont(14.0f));
    l->setJustificationType(juce::Justification::centred);
    return l;
}

// ---------------------------------------------------------------------------------------------- editor

MdEditor::MdEditor(MdProcessor& p) : AudioProcessorEditor(p), m_proc(p)
{
    skin::apply(skin::load());
    m_lnf.refreshColours();
    setLookAndFeel(&m_lnf);

    m_osButton.onClick = [this] { chooseOsFile(); };
    addAndMakeVisible(m_osButton);

    // machine list grouped by family; item ids follow the parameter's choice order (ComboBoxAttachment)
    juce::String family;
    for (int i = 0; i < kNumMachines; ++i) {
        const juce::String f = familyOf(kMachines[i].id);
        if (f != family && f.isNotEmpty()) { m_machine.addSectionHeading(f); family = f; }
        m_machine.addItem(kMachines[i].name, i + 1);
    }
    addAndMakeVisible(m_machine);

    auto setupKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setVelocityBasedMode(false);
        s.setMouseDragSensitivity(200);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible(s);
        l.setText(text, juce::dontSendNotification);
        l.setFont(lcdFont(15.0f));
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    };
    for (int k = 0; k < 8; ++k) setupKnob(m_knobs[size_t(k)], m_knobLabels[size_t(k)], "");
    setupKnob(m_level, m_levelLabel, "LEV");
    setupKnob(m_pan, m_panLabel, "PAN");
    setupKnob(m_master, m_masterLabel, "MASTER");
    m_masterAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, masterId(), m_master);

    selectTrack(0);
    setSize(kW, kH);
    startTimerHz(30);
}

MdEditor::~MdEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

juce::Rectangle<int> MdEditor::padBounds(int t) const
{
    const int w = (kW - 2 * kMargin - 15 * 4) / kTracks;
    return {kMargin + t * (w + 4), kPadsY, w, kPadH};
}

void MdEditor::selectTrack(int t)
{
    m_track = t;
    m_machineAttach.reset();
    for (auto& a : m_knobAttach) a.reset();
    m_levelAttach.reset(); m_panAttach.reset();
    m_machineAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(m_proc.apvts, machineId(t), m_machine);
    for (int k = 0; k < 8; ++k)
        m_knobAttach[size_t(k)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, knobId(t, k), m_knobs[size_t(k)]);
    m_levelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, levelId(t), m_level);
    m_panAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, panId(t), m_pan);
    m_shownMachine = -1;
    refreshTrackPanel();
    repaint();
}

void MdEditor::refreshTrackPanel()
{
    const int id = m_proc.machineIdOf(m_track);
    if (id == m_shownMachine) return;
    m_shownMachine = id;
    const auto* m = m_proc.machineInfo(id);
    for (int k = 0; k < 8; ++k) {
        const juce::String label = m ? juce::String(m->labels[size_t(k)]) : (id == 0 ? juce::String() : "SYN" + juce::String(k + 1));
        m_knobLabels[size_t(k)].setText(label, juce::dontSendNotification);
        m_knobs[size_t(k)].setEnabled(label.isNotEmpty());
        m_knobs[size_t(k)].setAlpha(label.isNotEmpty() ? 1.0f : 0.35f);
        m_knobs[size_t(k)].repaint();
    }
    repaint();
}

void MdEditor::chooseOsFile()
{
    const juce::File current(m_proc.firmwarePath());
    m_chooser = std::make_unique<juce::FileChooser>("Select the Machinedrum OS .syx (Elektron_SPS1-1UW_OS1.63.syx)",
        current.existsAsFile() ? current.getParentDirectory() : juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) { m_proc.setFirmwarePath(f.getFullPathName()); m_shownMachine = -1; refreshTrackPanel(); repaint(); }
    });
}

void MdEditor::timerCallback()
{
    bool dirty = false;
    for (int t = 0; t < kTracks; ++t) {
        const float a = juce::jlimit(0.0f, 1.0f, m_proc.trackActivity(t) * 4.0f);
        if (std::abs(a - m_lights[size_t(t)]) > 0.02f) { m_lights[size_t(t)] = a; dirty = true; }
    }
    refreshTrackPanel();
    if (dirty) repaint(juce::Rectangle<int>(0, kPadsY, kW, kPadH));
}

void MdEditor::mouseDown(const juce::MouseEvent& e)
{
    for (int t = 0; t < kTracks; ++t)
        if (padBounds(t).contains(e.getPosition())) {
            if (t != m_track) selectTrack(t);
            m_proc.auditionTrack(t);
            return;
        }
}

void MdEditor::paint(juce::Graphics& g)
{
    g.fillAll(paper());
    g.setColour(ink());
    // header
    g.fillRect(0, 0, kW, kHeaderH);
    g.setColour(paper());
    g.setFont(lcdFont(22.0f));
    g.drawText("MONOMODULE MD", kMargin, 0, 300, kHeaderH, juce::Justification::centredLeft);
    g.setFont(lcdFont(13.0f, false));
    g.drawText(m_proc.statusText().toUpperCase(), 300, 0, kW - 300 - 130, kHeaderH, juce::Justification::centredRight);
    // pads
    for (int t = 0; t < kTracks; ++t) {
        const auto r = padBounds(t);
        const bool sel = t == m_track;
        g.setColour(sel ? ink() : paper()); g.fillRect(r);
        g.setColour(ink()); g.drawRect(r, 2);
        const auto fg = sel ? paper() : ink();
        g.setColour(fg);
        g.setFont(lcdFont(13.0f));
        g.drawText(juce::String(t + 1), r.withHeight(20).reduced(5, 0), juce::Justification::centredLeft);
        const auto name = juce::String(kMachines[machineIndexOf(m_proc.machineIdOf(t))].name);
        g.setFont(lcdFont(12.0f));
        g.drawText(name.upToFirstOccurrenceOf("-", false, false), r.withTrimmedTop(20).withHeight(16), juce::Justification::centred);
        g.drawText(name.fromFirstOccurrenceOf("-", false, false).trimCharactersAtStart("-"), r.withTrimmedTop(34).withHeight(16), juce::Justification::centred);
        // trig light
        const auto light = juce::Rectangle<int>(r.getRight() - 14, r.getY() + 6, 8, 8).toFloat();
        g.setColour(fg.withAlpha(0.25f + 0.75f * m_lights[size_t(t)]));
        if (m_lights[size_t(t)] > 0.05f) g.fillEllipse(light); else g.drawEllipse(light, 1.5f);
    }
    // track panel frame and title
    const auto panel = juce::Rectangle<int>(kMargin, kPanelY, kW - 2 * kMargin - 150, kH - kPanelY - kMargin);
    g.setColour(ink()); g.drawRect(panel, 2);
    g.fillRect(panel.withHeight(30));
    g.setColour(paper()); g.setFont(lcdFont(17.0f));
    g.drawText("TRACK " + juce::String(m_track + 1) + "  /  NOTE " + juce::String(kTrackNotes[m_track]), panel.withHeight(30).reduced(10, 0), juce::Justification::centredLeft);
    const auto masterPanel = juce::Rectangle<int>(kW - kMargin - 140, kPanelY, 140, kH - kPanelY - kMargin);
    g.setColour(ink()); g.drawRect(masterPanel, 2);
    g.fillRect(masterPanel.withHeight(30));
    g.setColour(paper());
    g.drawText("MIX", masterPanel.withHeight(30), juce::Justification::centred);
}

void MdEditor::resized()
{
    m_osButton.setBounds(kW - kMargin - 110, 9, 110, 26);
    const auto panel = juce::Rectangle<int>(kMargin, kPanelY, kW - 2 * kMargin - 150, kH - kPanelY - kMargin);
    m_machine.setBounds(panel.getX() + 260, panel.getY() + 3, 200, 24);
    const int knobW = 72, knobH = 92;
    const int x0 = panel.getX() + 14, y0 = panel.getY() + 44;
    for (int k = 0; k < 8; ++k) {
        const int x = x0 + k * (knobW + 8);
        m_knobLabels[size_t(k)].setBounds(x, y0, knobW, 18);
        m_knobs[size_t(k)].setBounds(x, y0 + 18, knobW, knobH);
    }
    const int y1 = y0 + 18 + knobH + 16;
    m_levelLabel.setBounds(x0, y1, knobW, 18);
    m_level.setBounds(x0, y1 + 18, knobW, knobH - 10);
    m_panLabel.setBounds(x0 + knobW + 8, y1, knobW, 18);
    m_pan.setBounds(x0 + knobW + 8, y1 + 18, knobW, knobH - 10);
    const auto masterPanel = juce::Rectangle<int>(kW - kMargin - 140, kPanelY, 140, kH - kPanelY - kMargin);
    m_masterLabel.setBounds(masterPanel.getX() + 10, masterPanel.getY() + 44, 120, 18);
    m_master.setBounds(masterPanel.getX() + 25, masterPanel.getY() + 62, 90, 110);
}

} // namespace mnm::plugin::md
