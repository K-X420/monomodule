// Monomodule MD: the kit library in the editor, in the LCD faces like Monomodule's own library strip.
//   MdKitStrip    the joined strip at the top centre of the header: previous / kit selector / next / save
//   MdKitDrop     the selector's list: every kit (or the favourites), grouped by where it came from; a click loads,
//                 right-click marks a favourite; IMPORT .SYX adds Machinedrum sysex files to the library
//   MdSaveDialog  saving never overwrites: the kit is added to the library under the typed name
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MdLibrary.h"
#include "one/LcdDraw.h"

namespace mnm::plugin::md {

class MdKitStrip : public juce::Component, public juce::SettableTooltipClient {
public:
    enum Part { None = -1, Prev, Kit, Next, Save };
    static constexpr int kS = 2, kLcdH = 15;
    std::function<void(Part)> onPart;
    void setKit(const juce::String& name, bool modified);
    void setOpen(bool open) { if (m_open != open) { m_open = open; repaint(); } }
    int preferredWidth(int available) const { return juce::jmin(available, 190 * kS); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover != None) { m_hover = None; repaint(); } }
private:
    Part partAt(juce::Point<int> p) const;
    juce::String m_kit = "DEFAULT";
    bool m_modified = false, m_open = false;
    Part m_hover = None;
    std::array<juce::Rectangle<int>, 4> m_rects{};   // LCD px
};

class MdKitDrop : public juce::Component {
public:
    static constexpr int kS = 2, kRowH = 12, kHeadH = 15, kFootH = 15, kLcdW = 210;
    explicit MdKitDrop(MdLibrary& lib) : m_lib(lib) { setWantsKeyboardFocus(true); }
    void open(const juce::String& currentKey, juce::Point<int> topLeft, int maxHeight);
    void close() { setVisible(false); if (onClosed) onClosed(); }
    std::function<void()> onClosed, onImport;
    std::function<void(const KitEntry&)> onLoad;
    void rebuild();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Row { bool header = false; KitEntry kit; juce::String name; };
    int listRows() const { return (getHeight() / kS - kHeadH - kFootH) / kRowH; }
    int rowAt(juce::Point<int> lcd) const;
    MdLibrary& m_lib;
    juce::String m_current;
    bool m_favs = false;
    std::vector<Row> m_rows;
    int m_scroll = 0, m_hover = -1, m_allCount = 0, m_favCount = 0;
};

class MdSaveDialog : public juce::Component {
public:
    static constexpr int kS = 2, kLcdW = 220, kLcdH = 70;
    MdSaveDialog() { setWantsKeyboardFocus(true); }
    void open(const juce::String& name);
    std::function<juce::String(const juce::String& name)> onSave;   // returns an error, empty on success
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    void save();
    juce::Rectangle<int> box() const { return juce::Rectangle<int>(0, 0, kLcdW * kS, kLcdH * kS).withCentre(getLocalBounds().getCentre()); }
    juce::String m_name, m_error;
    juce::Rectangle<int> m_cancel, m_save;   // LCD px inside the box
};

} // namespace mnm::plugin::md
