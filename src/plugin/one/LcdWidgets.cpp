#include "LcdWidgets.h"
#include <cmath>

namespace mnm::plugin::one {

// ---------------------------------------------------------------------------
// KnobCell

KnobCell::KnobCell()
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity(160);
    setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);   // drag in any direction
    setOpaque(false);
}

void KnobCell::mouseMove(const juce::MouseEvent& e)
{
    Slider::mouseMove(e);
    setMouseCursor(m_valueArea.contains(e.getPosition()) ? m_valueCursor : juce::MouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor));
}

void KnobCell::mouseUp(const juce::MouseEvent& e)
{
    Slider::mouseUp(e);
    // a click that did not turn the knob, released on the value row, edits the value
    if (onValueClick && !e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1
        && m_valueArea.contains(e.getMouseDownPosition()) && m_valueArea.contains(e.getPosition()))
        onValueClick();
}

void KnobCell::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (m_valueArea.contains(e.getPosition())) return;   // double-click on the value is editing, not reset
    Slider::mouseDoubleClick(e);
}

// ---------------------------------------------------------------------------
// KnobPage

KnobPage::KnobPage(juce::AudioProcessorValueTreeState& apvts, const char* title) : m_apvts(apvts), m_title(title)
{
    for (int k = 0; k < 8; ++k) {
        auto& c = m_cells[size_t(k)];
        c.onValueChange = [this] { repaint(); };
        c.onValueClick = [this, k] {
            const auto d = m_params[size_t(k)].display;
            if (d == spec::Display::Numeric || d == spec::Display::Bipolar) beginEdit(k);
            else showValueList(k);
        };
        addAndMakeVisible(c);
    }
    m_editor.setJustification(juce::Justification::centred);
    m_editor.setInputRestrictions(4, "0123456789+-");
    m_editor.setSelectAllWhenFocused(true);
    m_editor.setFont(juce::Font(juce::FontOptions(float(4 * kScale))));
    m_editor.setIndents(2, 1);
    m_editor.onReturnKey = [this] { endEdit(true); };
    m_editor.onEscapeKey = [this] { endEdit(false); };
    m_editor.onFocusLost = [this] { endEdit(true); };
    addChildComponent(m_editor);
    setOpaque(true);
}

void KnobPage::beginEdit(int k)
{
    if (m_editing >= 0) endEdit(true);
    const auto& p = m_params[size_t(k)];
    m_editing = k;
    m_editor.setText(valueText(p, int(std::lround(m_cells[size_t(k)].getValue()))), false);
    const int x0 = (k % 4) * kCell, y0 = overhangRows() + kGridY + (k / 4) * kCell;
    m_editor.setBounds(knobValueBox(x0, y0) * kScale);
    m_editor.setVisible(true);
    m_editor.grabKeyboardFocus();
    m_editor.selectAll();
    // clicks that land on components which do not take keyboard focus (the knobs, the page itself)
    // never make the editor lose focus, so watch the whole window for the next press
    if (auto* top = getTopLevelComponent()) { m_listenedTop = top; top->addMouseListener(this, true); }
    repaint();
}

void KnobPage::mouseMove(const juce::MouseEvent& e)
{
    bool overTab = false;
    if (e.eventComponent == this && !m_tabs.isEmpty()) {
        const auto lcd = e.getPosition() / kScale;
        for (int t = 0; t < m_tabs.size() && t < int(m_tabRects.size()); ++t)
            if (t != m_tab && m_tabRects[size_t(t)].contains(lcd)) overTab = true;
    }
    setMouseCursor(overTab ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void KnobPage::mouseDown(const juce::MouseEvent& e)
{
    if (m_editing >= 0) {
        auto* c = e.eventComponent;
        if (c == &m_editor || m_editor.isParentOf(c)) return;
        endEdit(true);
    }
    if (e.eventComponent == this && !m_tabs.isEmpty()) {   // a press on the title bar switches tabs
        const auto lcd = e.getPosition() / kScale;
        for (int t = 0; t < m_tabs.size() && t < int(m_tabRects.size()); ++t)
            if (m_tabRects[size_t(t)].contains(lcd) && t != m_tab) {
                m_tab = t;
                if (m_onTab) m_onTab(t);
                repaint();
                return;
            }
    }
}

void KnobPage::endEdit(bool commit)
{
    if (m_editing < 0) return;
    const int k = m_editing;
    m_editing = -1;
    if (m_listenedTop != nullptr) m_listenedTop->removeMouseListener(this);
    m_listenedTop = nullptr;
    m_editor.setVisible(false);
    const juce::String txt = m_editor.getText().trim();
    if (commit && txt.isNotEmpty() && txt.containsAnyOf("0123456789")) {
        const auto& p = m_params[size_t(k)];
        int v = txt.getIntValue();   // "+3" -> 3, "-19" -> -19
        // out-of-range input snaps to the displayed range's limits
        v = p.display == spec::Display::Bipolar ? juce::jlimit(-64, 63, v) + 64 : juce::jlimit(0, int(p.maxRaw), v);
        m_cells[size_t(k)].setValue(double(v), juce::sendNotificationSync);
    }
    repaint();
}

void KnobPage::showValueList(int k)
{
    refreshDynamic();
    const auto& p = m_params[size_t(k)];
    if (p.display != spec::Display::List && p.display != spec::Display::Readout) return;
    const int raw = int(std::lround(m_cells[size_t(k)].getValue()));
    const int current = p.display == spec::Display::List ? spec::listIndex(raw, p.valueCount) : raw;
    juce::PopupMenu m;
    for (int i = 0; i < p.valueCount; ++i) m.addItem(i + 1, p.values[i], true, i == current);
    const bool list = p.display == spec::Display::List;
    const int n = p.valueCount;
    const int x0 = (k % 4) * kCell, y0 = overhangRows() + kGridY + (k / 4) * kCell;
    const auto anchor = (knobValueBox(x0, y0) * kScale) + getScreenPosition();
    m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(anchor).withMinimumWidth(kCell * kScale),
        [this, k, list, n](int r) {
            if (r <= 0) return;
            const int idx = r - 1;
            m_cells[size_t(k)].setValue(double(list ? spec::listRawMid(idx, n) : idx), juce::sendNotificationSync);
        });
}

// A page's cells are repainted when any of its values change, so DEST follows PAGE live.


void KnobPage::bind(const spec::Param* params8, const std::function<juce::String(int)>& paramId)
{
    for (int k = 0; k < 8; ++k) {
        auto& cell = m_cells[size_t(k)];
        m_attach[size_t(k)].reset();
        m_params[size_t(k)] = params8[k];
        const auto d = params8[k].display;
        const bool blank = d == spec::Display::Blank;
        cell.setVisible(!blank);   // a blank knob is dead on the hardware: nothing drawn, nothing to turn
        const bool dial = d == spec::Display::Numeric || d == spec::Display::Bipolar;
        cell.setValueArea(juce::Rectangle<int>(0, (kValueBoxY - 1) * kScale, (kCell - 1) * kScale, kValueBoxH * kScale),
                          dial ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::PointingHandCursor);
        if (blank) continue;   // a blank cell may have no parameter behind it (the MD's short pages)
        m_attach[size_t(k)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(m_apvts, paramId(k), cell);
        cell.setDoubleClickReturnValue(true, double(params8[k].defaultRaw));
    }
    endEdit(false);
    repaint();
}

void KnobPage::setTabs(const juce::StringArray& names, int initial, std::function<void(int)> onTab)
{
    m_tabs = names;
    m_tab = juce::jlimit(0, juce::jmax(0, names.size() - 1), initial);
    m_onTab = std::move(onTab);
    repaint();
}

void KnobPage::refreshDynamic()
{
    for (int k = 0; k < 8; ++k)
        if (m_valuesFn[size_t(k)]) m_params[size_t(k)].values = m_valuesFn[size_t(k)]();
}

// One tab of the title bar, x..x+w-1 wide: its rounded top stands in the gap above the bar (kTabOverhang
// rows) so both tabs read as tabs. The active tab's face is paper: an ink cap where it stands on paper,
// then the face runs through the bar and out of its bottom into the content, with no edge in between.
// The inactive tab's face is the bar itself: a black bump above the bar, and inside it two paper edge
// lines that stop a row short of the bar's bottom edge, so the tab reads as tucked behind the header.
static void drawTab(LcdCanvas& cv, int x, int w, int barY, bool active)
{
    const int capY = barY - KnobPage::kTabOverhang;
    if (active) {
        for (int c = x + 2; c <= x + w - 3; ++c) cv.set(c, capY, true);          // cap: top line ...
        cv.set(x + 1, capY + 1, true); cv.set(x + w - 2, capY + 1, true);          // ... and rounded corners
        for (int r = barY; r <= barY + KnobPage::kTitleH; ++r)                     // face: through the bar and the blank row
            for (int c = x + 1; c <= x + w - 2; ++c) cv.set(c, r, false);
    } else {
        for (int c = x + 2; c <= x + w - 3; ++c) cv.set(c, capY, true);          // solid cap, same silhouette
        for (int c = x + 1; c <= x + w - 2; ++c) cv.set(c, capY + 1, true);
        for (int r = barY; r <= barY + KnobPage::kTitleH - 2; ++r) { cv.set(x, r, false); cv.set(x + w - 1, r, false); }
    }
}

void KnobPage::paint(juce::Graphics& g)
{
    refreshDynamic();
    const int oy = overhangRows();
    LcdCanvas cv(kLcdW, oy + kLcdH);
    cv.fillRect(0, oy, kLcdW, kTitleH, true);   // the full-width title bar, on every page
    if (m_tabs.isEmpty()) cv.text(spec::kFontBold8, m_title.toRawUTF8(), 2, oy + 1, false);
    else {
        int x = 0;
        for (int t = 0; t < m_tabs.size() && t < int(m_tabRects.size()); ++t) {
            const juce::String name = m_tabs[t].toUpperCase();
            const int tw = LcdCanvas::textWidth(spec::kFontBold8, name.toRawUTF8()) + 6;   // 1 px edge + 2 px padding each side
            m_tabRects[size_t(t)] = {x, 0, tw, oy + kTitleH};
            drawTab(cv, x, tw, oy, t == m_tab);
            cv.text(spec::kFontBold8, name.toRawUTF8(), x + 3, oy + 1, t == m_tab);   // title position; ink on the paper face, paper on the bar
            x += tw + 2;
        }
    }
    if (m_badge.isNotEmpty()) {   // paper box with ink text inside the inverted bar
        const int bw = LcdCanvas::textWidth(spec::kFontBold8, m_badge.toRawUTF8()) + 4;
        cv.fillRect(kLcdW - bw - 1, oy + 1, bw, kTitleH - 2, false);
        cv.text(spec::kFontBold8, m_badge.toRawUTF8(), kLcdW - bw + 1, oy + 1, true);
    }
    for (int k = 0; k < 8; ++k) {
        const auto& p = m_params[size_t(k)];
        const auto& cell = m_cells[size_t(k)];
        const int x0 = (k % 4) * kCell, y0 = oy + kGridY + (k / 4) * kCell;
        const int raw = int(std::lround(cell.getValue()));
        const bool hot = cell.isVisible() && (cell.isMouseOverOrDragging() || m_editing == k);
        drawKnobCell(cv, x0, y0, p, raw, hot, m_iconFn[size_t(k)] ? m_iconFn[size_t(k)]() : nullptr);
    }
    cv.dotsV(kLcdW - 1, oy + kGridY, oy + kLcdH - 1);   // grid right edge
    cv.dotsH(0, kLcdW - 1, oy + kLcdH - 1);             // grid bottom edge
    cv.draw(g, 0, 0);
}

void KnobPage::resized()
{
    const int oy = overhangRows();
    for (int k = 0; k < 8; ++k) {
        const int x0 = (k % 4) * kCell, y0 = oy + kGridY + (k / 4) * kCell;
        m_cells[size_t(k)].setBounds((x0 + 1) * kScale, (y0 + 1) * kScale, (kCell - 1) * kScale, (kCell - 1) * kScale);
    }
    if (m_editing >= 0) {
        const int x0 = (m_editing % 4) * kCell, y0 = oy + kGridY + (m_editing / 4) * kCell;
        m_editor.setBounds(knobValueBox(x0, y0) * kScale);
    }
}

// ---------------------------------------------------------------------------
// LevelColumn

LevelColumn::LevelColumn()
{
    setSliderStyle(juce::Slider::LinearVertical);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setSliderSnapsToMousePosition(false);
    setMouseDragSensitivity(200);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);   // vertical drag
    setOpaque(true);
}

void LevelColumn::setMeter(float linear)
{
    const float v = juce::jlimit(0.0f, 1.0f, linear);
    if (std::abs(v - m_meter) < 0.01f) return;
    m_meter = v;
    repaint();
}

void LevelColumn::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    cv.textCentred(spec::kFontBold8, "LEV", 0, w, 1);
    // dotted frame, as the hardware's LEV column
    const int fx = 0, fy = 11, fw = w, fh = h - fy;
    cv.dotsH(fx, fx + fw - 1, fy); cv.dotsH(fx, fx + fw - 1, fy + fh - 1);
    cv.dotsV(fx, fy, fy + fh - 1); cv.dotsV(fx + fw - 1, fy, fy + fh - 1);
    const int innerY = fy + 2, innerH = fh - 4;
    const double frac = getMaximum() > getMinimum() ? (getValue() - getMinimum()) / (getMaximum() - getMinimum()) : 0.0;
    const int barW = (fw - 7) / 2;
    const int lvlH = int(std::lround(frac * innerH));
    cv.fillRect(fx + 2, innerY + innerH - lvlH, barW, lvlH, true);                 // level: solid bar
    const int metH = int(std::lround(m_meter * innerH));
    for (int y = innerY + innerH - metH; y < innerY + innerH; y += 2)                // output: dotted bar
        for (int x = fx + 3 + barW; x < fx + fw - 2; ++x) cv.set(x, y);
    cv.draw(g, 0, 0);
}

// ---------------------------------------------------------------------------
// LcdText / LcdButton

LcdText::LcdText(const spec::Font& font, juce::String text, int scale, bool inverted, juce::Justification just)
    : m_font(font), m_text(std::move(text)), m_scale(scale), m_inverted(inverted), m_just(just) {}

void LcdText::paint(juce::Graphics& g)
{
    const int w = juce::jmax(1, getWidth() / m_scale), h = juce::jmax(1, getHeight() / m_scale);
    LcdCanvas cv(w, h);
    if (m_inverted) cv.fillRect(0, 0, w, h, true);
    const juce::String caps = m_text.toUpperCase();   // the LCD fonts have no lowercase
    const int tw = LcdCanvas::textWidth(m_font, caps.toRawUTF8());
    const int x = m_just.testFlags(juce::Justification::right) ? w - tw - 1
                : m_just.testFlags(juce::Justification::horizontallyCentred) ? (w - tw) / 2 : 1;
    cv.text(m_font, caps.toRawUTF8(), x, (h - m_font.h) / 2, !m_inverted);
    cv.draw(g, 0, 0, m_scale);
}

void LcdButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    const bool inv = !down;   // solid block normally, outlined while pressed
    if (inv) cv.fillRect(0, 0, w, h, true);
    else { cv.dotsH(0, w - 1, 0); cv.dotsH(0, w - 1, h - 1); cv.dotsV(0, 0, h - 1); cv.dotsV(w - 1, 0, h - 1); }
    cv.textCentred(spec::kFontBold8, getButtonText().toRawUTF8(), 0, w, (h - spec::kFontBold8.h) / 2, !inv);
    cv.draw(g, 0, 0);
    if (highlighted && !down) { g.setColour(lcd::ink.withAlpha(0.15f)); g.fillRect(getLocalBounds()); }
}

void LcdToggle::paintButton(juce::Graphics& g, bool highlighted, bool)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    const bool on = getToggleState();
    if (on) cv.fillRect(0, 0, w, h, true);
    else { cv.dotsH(0, w - 1, 0); cv.dotsH(0, w - 1, h - 1); cv.dotsV(0, 0, h - 1); cv.dotsV(w - 1, 0, h - 1); }
    cv.textCentred(spec::kFontBold8, getButtonText().toRawUTF8(), 0, w, (h - spec::kFontBold8.h) / 2, !on);
    cv.draw(g, 0, 0);
    if (highlighted) { g.setColour(lcd::ink.withAlpha(0.15f)); g.fillRect(getLocalBounds()); }
}

// ---------------------------------------------------------------------------
// BpmReadout (Monomodule and Monomodule MD headers)

BpmReadout::BpmReadout()
{
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setSliderSnapsToMousePosition(false);
    setMouseDragSensitivity(300);
    setOpaque(true);
    m_editor.setJustification(juce::Justification::centredRight);
    m_editor.setInputRestrictions(5, "0123456789.");
    m_editor.setSelectAllWhenFocused(true);
    m_editor.setFont(juce::Font(juce::FontOptions(float(6 * kScale))));
    m_editor.setIndents(2, 1);
    m_editor.onReturnKey = [this] { endEdit(true); };
    m_editor.onEscapeKey = [this] { endEdit(false); };
    m_editor.onFocusLost = [this] { endEdit(true); };
    addChildComponent(m_editor);
    setSynced(true);
}

void BpmReadout::setSynced(bool synced)
{
    m_synced = synced;
    if (synced) endEdit(false);
    setMouseCursor(synced ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);
    repaint();
}

void BpmReadout::setHostBpm(float bpm)
{
    if (std::abs(bpm - m_hostBpm) < 0.05f) return;
    m_hostBpm = bpm;
    if (m_synced) repaint();
}

void BpmReadout::paint(juce::Graphics& g)
{
    const int w = getWidth() / kScale, h = getHeight() / kScale;
    LcdCanvas cv(w, h);
    const juce::String s(double(m_synced ? m_hostBpm : float(getValue())), 1);
    const int tw = LcdCanvas::tallDigitsWidth(s.toRawUTF8());
    cv.tallDigits(s.toRawUTF8(), w - tw, (h - 10) / 2);
    cv.draw(g, 0, 0);
}

void BpmReadout::resized() { m_editor.setBounds(getLocalBounds()); }

void BpmReadout::mouseDown(const juce::MouseEvent& e) { if (!m_synced && !m_editing) Slider::mouseDown(e); }
void BpmReadout::mouseDrag(const juce::MouseEvent& e) { if (!m_synced && !m_editing) Slider::mouseDrag(e); }

void BpmReadout::mouseUp(const juce::MouseEvent& e)
{
    if (m_synced || m_editing) return;
    Slider::mouseUp(e);
    // a click that did not drag the value types a new one
    if (!e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1) beginEdit();
}

void BpmReadout::beginEdit()
{
    m_editing = true;
    m_editor.setText(juce::String(getValue(), 1), false);
    m_editor.setVisible(true);
    m_editor.grabKeyboardFocus();
    m_editor.selectAll();
}

void BpmReadout::endEdit(bool commit)
{
    if (!m_editing) return;
    m_editing = false;
    m_editor.setVisible(false);
    const juce::String txt = m_editor.getText().trim();
    if (commit && txt.containsAnyOf("0123456789"))
        setValue(juce::jlimit(getMinimum(), getMaximum(), txt.getDoubleValue()), juce::sendNotificationSync);   // clamped to 30..300
    repaint();
}

} // namespace mnm::plugin::one
