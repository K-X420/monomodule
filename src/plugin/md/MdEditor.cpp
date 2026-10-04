#include "MdEditor.h"
#include "Skin.h"

namespace mnm::plugin::md {

namespace {
juce::Colour ink() { return skin::inkColour(); }
juce::Colour paper() { return skin::paperColour(); }
juce::Font lcdFont(float h, bool bold = true) { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain)); }

constexpr int kW = 980, kH = 420;
constexpr int kMargin = 16;
constexpr int kHeaderH = 44;
constexpr int kPadsY = kHeaderH + 10, kPadH = 64;
constexpr int kPanelY = kPadsY + kPadH + 18;
constexpr int kMixW = 140;

const char* familyOf(int id)
{
    if (id == 0) return "";
    if (id < 16) return "GND";
    if (id < 32) return "TRX";
    if (id < 48) return "EFM";
    if (id < 64) return "E12";
    if (id < 80) return "P-I";
    if (id < 128) return "INP";
    return "ROM";
}
constexpr const char* kPageNames[5] = {"SYNTH", "EFFECTS", "ROUTING", "LFO", "MASTER FX"};
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
    setColour(juce::TextButton::buttonOnColourId, ink());
    setColour(juce::TextButton::textColourOffId, ink());
    setColour(juce::TextButton::textColourOnId, paper());
    setColour(juce::Label::textColourId, ink());
}

void MdLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto area = juce::Rectangle<int>(x, y, w, h);
    const auto valueRow = area.removeFromBottom(18);
    if (s.isEnabled()) {
        g.setColour(ink());
        g.setFont(lcdFont(14.0f));
        g.drawText(s.getTextFromValue(s.getValue()), valueRow, juce::Justification::centred);
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
    const float from = s.getMinimum() < 0.0 ? (start + end) * 0.5f : start;
    juce::Path value;
    value.addCentredArc(c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, std::min(from, angle), std::max(from, angle), true);
    g.setColour(ink().withAlpha(alpha));
    g.strokePath(value, juce::PathStrokeType(3.0f));
    g.fillEllipse(juce::Rectangle<float>(r, r).withCentre(c));
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
    const bool on = down || b.getToggleState();
    g.setColour(on ? ink() : paper()); g.fillRect(r);
    if (over && !on) { g.setColour(ink().withAlpha(0.08f)); g.fillRect(r); }
    g.setColour(on ? paper() : ink()); g.drawRect(r, on ? 0 : 2);
    if (!on) { g.setColour(ink()); g.drawRect(r, 2); }
}

void MdLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setColour((down || b.getToggleState()) ? paper() : ink());
    g.setFont(lcdFont(14.0f));
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Font MdLookAndFeel::getComboBoxFont(juce::ComboBox&) { return lcdFont(17.0f); }
juce::Font MdLookAndFeel::getPopupMenuFont() { return lcdFont(15.0f); }
juce::Font MdLookAndFeel::getTextButtonFont(juce::TextButton&, int) { return lcdFont(14.0f); }

// ---------------------------------------------------------------------------------------------- editor

MdEditor::MdEditor(MdProcessor& p) : AudioProcessorEditor(p), m_proc(p)
{
    skin::apply(skin::load());
    m_lnf.refreshColours();
    setLookAndFeel(&m_lnf);

    m_osButton.onClick = [this] { chooseOsFile(); };
    addAndMakeVisible(m_osButton);
    m_kitButton.onClick = [this] { chooseKit(); };
    addAndMakeVisible(m_kitButton);
    m_sampleButton.onClick = [this] { sampleMenu(); };
    addChildComponent(m_sampleButton);

    for (int i = 0; i < kNumPages; ++i) {
        auto& b = m_pageButtons[size_t(i)];
        b.setButtonText(kPageNames[i]);
        b.onClick = [this, i] { showPage(Page(i)); };
        addAndMakeVisible(b);
    }
    for (int i = 0; i < 4; ++i) {
        auto& f = m_fxButtons[size_t(i)];
        f.setButtonText(kMasterFxNames[i]);
        f.onClick = [this, i] { m_masterFx = i; attachKnobs(); };
        addChildComponent(f);
    }

    // machine list grouped by family; item ids follow the parameter's choice order (ComboBoxAttachment)
    juce::String family;
    for (int i = 0; i < kNumMachines; ++i) {
        const juce::String f = familyOf(kMachines[i].id);
        if (f != family && f.isNotEmpty()) { m_machine.addSectionHeading(f); family = f; }
        m_machine.addItem(kMachines[i].name, i + 1);
    }
    addAndMakeVisible(m_machine);

    auto setupKnob = [this](juce::Slider& s, juce::Label& l) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setMouseDragSensitivity(200);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible(s);
        l.setFont(lcdFont(15.0f));
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    };
    for (int k = 0; k < 8; ++k) setupKnob(m_knobs[size_t(k)], m_knobLabels[size_t(k)]);
    setupKnob(m_master, m_masterLabel);
    m_masterLabel.setText("VOLUME", juce::dontSendNotification);
    m_masterAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, masterId(), m_master);
    setupKnob(m_accent, m_accentLabel);
    m_accentLabel.setText("ACCENT", juce::dontSendNotification);
    m_accentAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, accentId(), m_accent);
    m_velButton.onClick = [this] {   // VEL: VOLUME <-> ACCENT
        if (auto* p = m_proc.apvts.getParameter(velModeId())) p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f);
    };
    addAndMakeVisible(m_velButton);

    selectTrack(0);
    setSize(kW, kH);
    timerCallback();   // VEL button text, ACCENT state
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

juce::Rectangle<int> MdEditor::panelBounds() const { return {kMargin, kPanelY, kW - 2 * kMargin - kMixW - 10, kH - kPanelY - kMargin}; }

void MdEditor::selectTrack(int t)
{
    m_track = t;
    m_machineAttach.reset();
    m_machineAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(m_proc.apvts, machineId(t), m_machine);
    attachKnobs();
}

void MdEditor::showPage(Page p)
{
    m_page = p;
    for (int i = 0; i < kNumPages; ++i) m_pageButtons[size_t(i)].setToggleState(i == int(p), juce::dontSendNotification);
    attachKnobs();
    resized();
}

// Points the eight knobs at the current page's parameters
void MdEditor::attachKnobs()
{
    for (auto& a : m_knobAttach) a.reset();
    const bool master = m_page == Page::Master;
    m_machine.setVisible(m_page == Page::Synth);
    for (int i = 0; i < 4; ++i) {
        m_fxButtons[size_t(i)].setVisible(master);
        m_fxButtons[size_t(i)].setToggleState(i == m_masterFx, juce::dontSendNotification);
    }
    for (int k = 0; k < 8; ++k) {
        juce::String id;
        switch (m_page) {
        case Page::Synth: id = knobId(m_track, k); break;
        case Page::Effects: id = fxId(m_track, k); break;
        case Page::Routing: {
            static juce::String (* const ids[7])(int) = {distId, volId, panId, delId, revId, levelId, routeId};
            if (k < 7) id = ids[k](m_track);
            break;
        }
        case Page::Lfo: id = lfoId(m_track, k); break;
        case Page::Master: id = masterFxId(m_masterFx, k); break;
        }
        auto& s = m_knobs[size_t(k)];
        s.setVisible(id.isNotEmpty());
        m_knobLabels[size_t(k)].setVisible(id.isNotEmpty());
        if (id.isNotEmpty()) m_knobAttach[size_t(k)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_proc.apvts, id, s);
    }
    m_shownMachine = -1;
    refreshLabels();
    for (int i = 0; i < kNumPages; ++i) m_pageButtons[size_t(i)].setToggleState(i == int(m_page), juce::dontSendNotification);
    repaint();
}

void MdEditor::refreshLabels()
{
    const int id = m_proc.machineIdOf(m_track);
    if (m_page == Page::Synth && id == m_shownMachine) return;
    m_shownMachine = id;
    const auto* m = m_proc.machineInfo(id);
    for (int k = 0; k < 8; ++k) {
        juce::String label;
        switch (m_page) {
        case Page::Synth: label = m ? juce::String(m->labels[size_t(k)]) : (id == 0 ? juce::String() : "SYN" + juce::String(k + 1)); break;
        case Page::Effects: label = kFxLabels[k]; break;
        case Page::Routing: label = k < 6 ? juce::String(kRouteLabels[k]) : (k == 6 ? juce::String("OUT") : juce::String()); break;
        case Page::Lfo: label = kLfoLabels[k]; break;
        case Page::Master: label = kMasterFxLabels[m_masterFx][k]; break;
        }
        m_knobLabels[size_t(k)].setText(label, juce::dontSendNotification);
        const bool on = label.isNotEmpty();
        m_knobs[size_t(k)].setEnabled(on);
        m_knobs[size_t(k)].setAlpha(on ? 1.0f : 0.35f);
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
        if (f.existsAsFile()) { m_proc.setFirmwarePath(f.getFullPathName()); m_shownMachine = -1; refreshLabels(); repaint(); }
    });
}

// A Machinedrum sysex file (a kit or a whole dump), then which of its kits
void MdEditor::chooseKit()
{
    m_chooser = std::make_unique<juce::FileChooser>("Load a Machinedrum kit (.syx kit or dump)",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (!f.existsAsFile()) return;
        try { m_kits = mnm::md::loadKits(f.getFullPathName().toStdString()); } catch (const std::exception&) { m_kits.clear(); }
        if (m_kits.empty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Kit", "No Machinedrum kits in " + f.getFileName());
            return;
        }
        auto apply = [this](int i) {
            const int emptied = m_proc.applyKit(m_kits[size_t(i)]);
            if (emptied > 0)
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Kit",
                    juce::String(emptied) + " track(s) use ROM/RAM, MIDI, controller or input machines, which are not available yet; they were left empty.");
            selectTrack(m_track);
            repaint();
        };
        if (m_kits.size() == 1) { apply(0); return; }
        juce::PopupMenu menu;
        for (size_t i = 0; i < m_kits.size(); ++i)
            menu.addItem(int(i) + 1, juce::String(m_kits[i].position + 1).paddedLeft('0', 2) + "  " + juce::String(m_kits[i].name));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_kitButton), [apply](int r) { if (r > 0) apply(r - 1); });
    });
}

void MdEditor::sampleMenu()
{
    const int id = m_proc.machineIdOf(m_track);
    if (!isRomMachine(id)) return;
    const int slot = romSlotOf(id);
    juce::PopupMenu m;
    m.addItem(1, "Load Sample...");
    m.addItem(2, "Clear Sample", m_proc.sampleName(slot).isNotEmpty());
    m.addSeparator();
    m.addItem(3, juce::String("Sample memory used: ") + juce::String(int(std::lround(m_proc.sampleMemoryUsed() * 100.0))) + "%", false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_sampleButton), [this, slot](int r) {
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
    bool dirty = false;
    for (int t = 0; t < kTracks; ++t) {
        const float a = juce::jlimit(0.0f, 1.0f, m_proc.trackActivity(t) * 4.0f);
        if (std::abs(a - m_lights[size_t(t)]) > 0.02f) { m_lights[size_t(t)] = a; dirty = true; }
    }
    if (m_page == Page::Synth) refreshLabels();
    {   // the sample of a ROM machine's slot
        const int id = m_proc.machineIdOf(m_track);
        const bool rom = m_page == Page::Synth && isRomMachine(id);
        m_sampleButton.setVisible(rom);
        if (rom) {
            const auto name = m_proc.sampleName(romSlotOf(id));
            const juce::String text = name.isEmpty() ? juce::String("LOAD SAMPLE...") : "SAMPLE: " + name.toUpperCase();
            if (m_sampleButton.getButtonText() != text) m_sampleButton.setButtonText(text);
        }
    }
    const bool accentMode = m_proc.apvts.getRawParameterValue(velModeId())->load() >= 0.5f;
    const juce::String vel = accentMode ? "VEL: ACCENT" : "VEL: VOLUME";
    if (m_velButton.getButtonText() != vel) m_velButton.setButtonText(vel);
    m_accent.setEnabled(accentMode);
    if (dirty) repaint(juce::Rectangle<int>(0, kPadsY, kW, kPadH));
}

void MdEditor::mouseDown(const juce::MouseEvent& e)
{
    for (int t = 0; t < kTracks; ++t)
        if (padBounds(t).contains(e.getPosition())) {
            if (t != m_track) selectTrack(t);
            m_proc.auditionTrack(t);
            repaint();
            return;
        }
}

void MdEditor::paint(juce::Graphics& g)
{
    g.fillAll(paper());
    g.setColour(ink());
    g.fillRect(0, 0, kW, kHeaderH);
    g.setColour(paper());
    g.setFont(lcdFont(22.0f));
    g.drawText("MONOMODULE MD", kMargin, 0, 300, kHeaderH, juce::Justification::centredLeft);
    g.setFont(lcdFont(13.0f, false));
    const auto kit = m_proc.kitName();
    g.drawText((kit.isNotEmpty() ? "KIT " + kit + "   " : juce::String()) + m_proc.statusText().toUpperCase(), 250, 0, kW - 250 - 230, kHeaderH, juce::Justification::centredRight);
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
        const auto light = juce::Rectangle<int>(r.getRight() - 14, r.getY() + 6, 8, 8).toFloat();
        g.setColour(fg.withAlpha(0.25f + 0.75f * m_lights[size_t(t)]));
        if (m_lights[size_t(t)] > 0.05f) g.fillEllipse(light); else g.drawEllipse(light, 1.5f);
    }
    const auto panel = panelBounds();
    g.setColour(ink()); g.drawRect(panel, 2);
    g.fillRect(panel.withHeight(30));
    g.setColour(paper()); g.setFont(lcdFont(17.0f));
    const juce::String title = m_page == Page::Master ? juce::String("MASTER FX") : "TRACK " + juce::String(m_track + 1);
    g.drawText(title, panel.withHeight(30).reduced(10, 0), juce::Justification::centredLeft);
    const auto mix = juce::Rectangle<int>(kW - kMargin - kMixW, kPanelY, kMixW, kH - kPanelY - kMargin);
    g.setColour(ink()); g.drawRect(mix, 2);
    g.fillRect(mix.withHeight(30));
    g.setColour(paper());
    g.drawText("OUT", mix.withHeight(30), juce::Justification::centred);
}

void MdEditor::resized()
{
    m_osButton.setBounds(kW - kMargin - 120, 9, 120, 26);
    m_kitButton.setBounds(kW - kMargin - 120 - 90, 9, 84, 26);
    const auto panel = panelBounds();
    // page tabs along the bottom of the panel, the machine / effect selector in the title bar
    const int tabW = 116;
    for (int i = 0; i < kNumPages; ++i) m_pageButtons[size_t(i)].setBounds(panel.getX() + 10 + i * (tabW + 6), panel.getBottom() - 34, tabW, 26);
    m_machine.setBounds(panel.getX() + 130, panel.getY() + 3, 190, 24);
    m_sampleButton.setBounds(panel.getX() + 330, panel.getY() + 3, 300, 24);
    for (int i = 0; i < 4; ++i) m_fxButtons[size_t(i)].setBounds(panel.getX() + 130 + i * 96, panel.getY() + 3, 92, 24);
    const int knobW = 76, knobH = 96;
    const int x0 = panel.getX() + 12, y0 = panel.getY() + 40;
    for (int k = 0; k < 8; ++k) {
        const int x = x0 + k * (knobW + 22);
        m_knobLabels[size_t(k)].setBounds(x, y0, knobW, 18);
        m_knobs[size_t(k)].setBounds(x, y0 + 18, knobW, knobH);
    }
    const auto mix = juce::Rectangle<int>(kW - kMargin - kMixW, kPanelY, kMixW, kH - kPanelY - kMargin);
    m_masterLabel.setBounds(mix.getX() + 10, mix.getY() + 36, kMixW - 20, 18);
    m_master.setBounds(mix.getX() + 30, mix.getY() + 54, kMixW - 60, 92);
    m_accentLabel.setBounds(mix.getX() + 10, mix.getY() + 150, kMixW - 20, 18);
    m_accent.setBounds(mix.getX() + 40, mix.getY() + 168, kMixW - 80, 70);
    m_velButton.setBounds(mix.getX() + 8, mix.getBottom() - 34, kMixW - 16, 26);
}

} // namespace mnm::plugin::md
