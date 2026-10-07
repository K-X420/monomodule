// LCD widgets shared by the Monomodule editors (One / Six / FX and MD): the knob page (title bar over a 2x4 grid of
// knob cells bound to parameters), the LEV column, and LCD text, buttons and toggles. Everything is drawn at LCD
// pixel resolution in the skin's two colours (Lcd.h).
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Lcd.h"

namespace mnm::plugin::one {

// Invisible drag surface over one knob cell; the page draws the cell from the slider's value.
// A press anywhere in the cell drags the knob; a clean click (no drag) on the value row fires
// onValueClick, which opens the inline editor or the value list.
class KnobCell : public juce::Slider {
public:
    KnobCell();
    std::function<void()> onValueClick;
    std::function<bool()> onReset;   // a double-click on the knob: true = handled (not the default value)
    std::function<void()> onKnobClick;   // a single click on the knob that did not turn it
    // A two-state switch (a Readout of two values with the Toggle icon): a click flips it, no drag, no value list
    void setToggle(bool on) { m_toggle = on; }
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override { if (!m_toggle) Slider::mouseDrag(e); }
    void setValueArea(juce::Rectangle<int> r, juce::MouseCursor valueCursor) { m_valueArea = r; m_valueCursor = valueCursor; }
    void paint(juce::Graphics&) override {}
    void mouseEnter(const juce::MouseEvent& e) override { Slider::mouseEnter(e); notifyPage(); }
    void mouseExit(const juce::MouseEvent& e) override { Slider::mouseExit(e); notifyPage(); }
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;   // lists: a value a notch
    void startedDragging() override { notifyPage(); }
    void stoppedDragging() override { notifyPage(); }
private:
    float m_wheelAcc = 0.0f;
    juce::uint32 m_wheelLast = 0;
    void notifyPage() { if (auto* p = getParentComponent()) p->repaint(); }
    juce::Rectangle<int> m_valueArea;
    juce::MouseCursor m_valueCursor{juce::MouseCursor::IBeamCursor};
    bool m_toggle = false;
};

// One hardware page: inverted title bar over the 2x4 knob grid, drawn at LCD resolution.
class KnobPage : public juce::Component {
public:
    static constexpr int kTitleH = 10, kGridY = kTitleH + 1;               // title bar, a blank row (room for the tie arch), grid
    static constexpr int kLcdW = 4 * kCell + 1, kLcdH = kGridY + 2 * kCell + 1;
    static constexpr int kWidth = kLcdW * kScale, kHeight = kLcdH * kScale;
    // A tabbed page's tabs overhang the title bar by two rows (their rounded tops stand in the gap above
    // the page), so the component extends that far above the page's nominal top edge.
    static constexpr int kTabOverhang = 2;
    int overhangRows() const { return m_tabs.isEmpty() ? 0 : kTabOverhang; }
    int overhangPx() const { return overhangRows() * kScale; }

    explicit KnobPage(juce::AudioProcessorValueTreeState& apvts, const char* title);
    // Tag drawn at the right end of the title bar (e.g. "PREVIEW"); null clears it.
    void setBadge(const char* badge) { m_badge = badge ? badge : ""; repaint(); }
    // Title bar as tabs (LFO2 | LFO3); onTab is called with the new index and must rebind the page.
    void setTabs(const juce::StringArray& names, int initial, std::function<void(int)> onTab);
    int currentTab() const { return m_tab; }
    // Per-cell hooks for the LFO DEST knob, whose names and icon follow the PAGE knob.
    void setValuesSource(int k, std::function<const char* const*()> fn) { m_valuesFn[size_t(k)] = std::move(fn); }
    void setIconSource(int k, std::function<const spec::Bitmap*()> fn) { m_iconFn[size_t(k)] = std::move(fn); }
    int cellValue(int k) const { return int(std::lround(m_cells[size_t(k)].getValue())); }
    // Binds the eight cells to parameters; the descriptors are copied (the SYN page changes with the machine).
    void bind(const spec::Param* params8, const std::function<juce::String(int)>& paramId);
    // Binds the cells to values that are not parameters (MD pattern settings, a held step's locks): get(k) gives the
    // value, set(k, v) takes a turn. pull() refreshes the cells from get (the editor's timer).
    // reset(k): a double-click on knob k (null: back to its default value); marked(k): its value box drawn inverted
    void bindCustom(const spec::Param* params8, std::function<int(int)> get, std::function<void(int, int)> set,
                    std::function<void(int)> reset = nullptr, std::function<bool(int)> marked = nullptr);
    void pull();
    bool isCustom() const { return m_custom; }
    void devTurn(int k, int v) { m_cells[size_t(k)].setValue(double(v), juce::sendNotificationSync); }   // dev/tests: a turn
    void devClick(int k) { if (m_cells[size_t(k)].onKnobClick) m_cells[size_t(k)].onKnobClick(); }      // dev/tests: a click
    juce::String devTip(int k) { return m_cells[size_t(k)].getTooltip(); }                      // dev/tests
    // Alt + a knob turned by the mouse (parameter bound pages): k and the new value, for "every track" (the MD's FUNCTION + knob)
    std::function<void(int k, int value)> onAltTurn;
    // the cells' tooltips: (cell, its label) -> text, applied when the page is bound
    std::function<juce::String(int k, const juce::String& label)> tipFor;
    void devReset(int k) { auto& c = m_cells[size_t(k)]; if (c.onReset) c.onReset(); }                  // dev/tests: a double-click
    bool devMarked(int k) const { return m_custom && m_marked && m_marked(k); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;   // any press outside the inline editor commits it
    void mouseMove(const juce::MouseEvent&) override;   // hand cursor over the inactive tab

private:
    void beginEdit(int k);        // numeric/bipolar: inline text entry, clamped to the displayed range
    void endEdit(bool commit);
    void showValueList(int k);    // list/readout: pick an entry by name
    void refreshDynamic();        // pull DEST-style names from their sources before drawing/listing

    juce::AudioProcessorValueTreeState& m_apvts;
    juce::String m_title, m_badge;
    juce::StringArray m_tabs;
    std::array<juce::Rectangle<int>, 4> m_tabRects{};   // LCD px, in the title bar
    int m_tab = 0;
    std::function<void(int)> m_onTab;
    std::array<std::function<const char* const*()>, 8> m_valuesFn;
    std::array<std::function<const spec::Bitmap*()>, 8> m_iconFn;
    std::array<spec::Param, 8> m_params{};
    std::array<KnobCell, 8> m_cells;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> m_attach;
    juce::TextEditor m_editor;
    int m_editing = -1;
    bool m_custom = false, m_pulling = false;
    std::function<int(int)> m_get;
    std::function<void(int, int)> m_set;
    std::function<void(int)> m_reset;
    std::function<bool(int)> m_marked;
    juce::Component::SafePointer<juce::Component> m_listenedTop;   // window we listen on while editing
};

// LEV: level fader drawn as the LCD level bar (dotted frame, filled to the value) with the output
// meter as a second bar beside it. Vertical drag.
class LevelColumn : public juce::Slider {
public:
    LevelColumn();
    void setMeter(float linear);
    void paint(juce::Graphics&) override;
private:
    float m_meter = 0.0f;
};

// A two-state LCD button (SYNC): solid when on, dotted frame when off.
class LcdToggle : public juce::Button {
public:
    explicit LcdToggle(const juce::String& text) : juce::Button(text) { setClickingTogglesState(true); }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// BPM: the tempo in the tall digit face. Synced to the host it is a readout of the transport tempo;
// free, a horizontal drag adjusts it and a clean click types a value (as the numeric knobs do).
class BpmReadout : public juce::Slider {
public:
    BpmReadout();
    void setSynced(bool synced);
    void setHostBpm(float bpm);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    void beginEdit();
    void endEdit(bool commit);
    juce::TextEditor m_editor;
    bool m_synced = true, m_editing = false;
    float m_hostBpm = 120.0f;
};

// Static text in an LCD font (title, footer).
class LcdText : public juce::Component {
public:
    LcdText(const spec::Font& font, juce::String text, int scale, bool inverted = false, juce::Justification just = juce::Justification::centredLeft);
    void paint(juce::Graphics&) override;
    void setText(juce::String s) { if (m_text != s) { m_text = std::move(s); repaint(); } }
private:
    const spec::Font& m_font;
    juce::String m_text;
    int m_scale;
    bool m_inverted;
    juce::Justification m_just;
};

// A push button drawn as inverted LCD text (MENU).
class LcdButton : public juce::Button {
public:
    explicit LcdButton(const juce::String& text) : juce::Button(text) {}
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

} // namespace mnm::plugin::one
