// Monomodule MD: the library proper, as Monomodule's LIBRARY panel. It unrolls from under the header over the pages
// (the track keys stay in view: they are the drop targets for sounds and carry the LOCKs); a bar with the tabs (SOUNDS,
// KITS, PATTERNS), what is typed (type to find) and CLOSE; a filter column; a four-column grid. A click loads and the
// panel stays open: a sound onto the selected track (drag it onto a track key for another), a kit, a pattern's kit (the
// pattern itself is dragged into the DAW as MIDI). The glyph auditions, with the loop toggle beside the stop.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MdLibrary.h"
#include "one/LcdDraw.h"

namespace mnm::plugin::md {

class MdLibraryPanel : public juce::Component, private juce::Timer {
public:
    enum Tab { Sounds, Kits, Patterns };
    static constexpr int kS = 2, kBarH = 17, kFootH = 13, kFilterW = 78, kCellH = 13, kHeadRowH = 13, kCols = 4;
    explicit MdLibraryPanel(MdLibrary& lib) : m_lib(lib) { setWantsKeyboardFocus(true); }

    // what the panel needs from the editor
    std::function<juce::String()> currentKit, currentSound;   // the loaded items' keys (marked in the grid)
    std::function<int()> selectedTrack;
    std::function<void(const juce::String& key)> loadKit, loadSound, loadPatternKit;
    std::function<void(const juce::String& key, Tab kind)> audition;   // play / stop
    std::function<bool(const juce::String& key)> isPlaying;
    std::function<bool()> looping;
    std::function<void()> toggleLoop;
    std::function<juce::File(const juce::String& patternKey)> patternMidiFile;
    std::function<void(juce::Point<int> screen)> onSoundDragging;   // over a track key: it lights up
    std::function<void(const juce::String& key, juce::Point<int> screen)> onSoundDropped;
    std::function<void(bool)> onOpenChanged;
    juce::String message;   // the last problem (the footer shows it)

    void setTargetBounds(juce::Rectangle<int> fullyOpen);
    void open(bool animate = true);
    void close(bool animate = true);
    bool isOpen() const { return m_wantOpen; }
    void setTab(Tab t) { m_tab = t; m_scroll = 0; rebuild(); }
    void rebuild();
    void refreshIfChanged();   // editor timer
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    struct Item { bool header = false; juce::String key, name, right; bool on = false, fav = false; juce::Rectangle<int> r; };
    struct Filter { juce::String label, value; int section = 0; bool header = false; juce::Rectangle<int> r; };   // 0 group, 1 show, 2 source, 3 bank
    juce::Rectangle<int> gridArea() const;   // LCD px
    int itemAt(juce::Point<int> lcd) const;
    int contentHeight() const { return m_items.empty() ? 0 : m_items.back().r.getBottom() + 2; }
    void clampScroll();
    void timerCallback() override;
    void applyAnimation();

    MdLibrary& m_lib;
    Tab m_tab = Sounds;
    juce::String m_group, m_source, m_query;   // "" = all
    int m_bank = -1;
    bool m_favourites = false, m_savedOnly = false;
    std::vector<Item> m_items;
    std::vector<Filter> m_filters;
    std::array<juce::Rectangle<int>, 3> m_tabRects{};
    juce::Rectangle<int> m_closeRect, m_appRect;
    int m_scroll = 0, m_hover = -1, m_pressed = -1, m_seenRevision = -1;
    bool m_dragging = false, m_wantOpen = false;
    juce::Rectangle<int> m_target;
    float m_anim = 0.0f;
};

} // namespace mnm::plugin::md
