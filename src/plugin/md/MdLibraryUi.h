// Monomodule MD: the shared library in the editor, in the LCD faces of Monomodule's own library strip.
//   MdKitStrip    the joined strip at the top centre of the header, as on Six: the kit (previous / selector / next /
//                 save), then the selected track's sound (previous / selector / next / save)
//   MdLibraryDrop a selector's list. Kits: every kit (or the favourites), grouped by project, IMPORT .SYX. Sounds: this
//                 machine's sounds or every machine's. A click loads, right-click marks a favourite.
//   MdSaveDialog  saving never overwrites: a new kit or sound in the library under the typed name
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MdLibrary.h"
#include "one/LcdDraw.h"

namespace mnm::plugin::md {

class MdKitStrip : public juce::Component, public juce::SettableTooltipClient {
public:
    enum Part { None = -1, KitPrev, Kit, KitNext, KitSave, SoundPrev, Sound, SoundNext, SoundSave, Library };
    static constexpr int kS = 2, kLcdH = 15;
    std::function<void(Part)> onPart;
    void setKit(const juce::String& name, bool modified);
    void setSound(int track, const juce::String& name, bool modified);
    void setOpen(Part menu) { if (m_open != menu) { m_open = menu; repaint(); } }
    void setLibraryOpen(bool open) { if (m_libOpen != open) { m_libOpen = open; repaint(); } }
    int preferredWidth(int available) const { return juce::jmin(available, 344 * kS); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover != None) { m_hover = None; repaint(); } }
    juce::Rectangle<int> partBounds(Part p) const { return m_rects[size_t(p)] * kS; }
private:
    Part partAt(juce::Point<int> p) const;
    juce::String m_kit = "DEFAULT", m_sound;
    int m_track = 0;
    bool m_kitMod = false, m_soundMod = false, m_libOpen = false;
    Part m_open = None, m_hover = None;
    std::array<juce::Rectangle<int>, 9> m_rects{};   // LCD px
};

class MdLibraryDrop : public juce::Component {
public:
    static constexpr int kS = 2, kRowH = 12, kHeadH = 15, kFootH = 15, kLcdW = 220;
    explicit MdLibraryDrop(MdLibrary& lib) : m_lib(lib) { setWantsKeyboardFocus(true); }
    void openKits(const juce::String& currentKey, juce::Point<int> topLeft, int maxHeight);
    void openSounds(int machine, int track, const juce::String& currentKey, juce::Point<int> topLeft, int maxHeight);
    void close() { setVisible(false); if (onClosed) onClosed(); }
    bool showingKits() const { return m_kits; }
    std::function<void()> onClosed, onImport;
    std::function<void(const KitEntry&)> onLoadKit;
    std::function<void(const SoundEntry&)> onLoadSound;
    std::function<void(const juce::String& key, bool kit)> onAudition;   // the row's glyph: play / stop
    std::function<bool(const juce::String& key)> isPlaying;
    std::function<bool()> looping;                      // the audition loops (the toggle beside a playing row's stop glyph)
    std::function<void()> toggleLoop;
    std::function<void(bool kits)> onLibrary;           // the footer's LIBRARY: the panel on the kits / sounds tab
    void rebuild();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { m_hover = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Row { bool header = false; juce::String key, name, right; bool fav = false; KitEntry kit; SoundEntry sound; };
    void open(juce::Point<int> topLeft, int maxHeight);
    int listRows() const { return (getHeight() / kS - kHeadH - kFootH) / kRowH; }
    int rowAt(juce::Point<int> lcd) const;
    MdLibrary& m_lib;
    bool m_kits = true, m_alt = false;   // alt: kits = favourites only; sounds = every machine
    int m_machine = 0, m_track = 0;
    juce::String m_current;
    std::vector<Row> m_rows;
    int m_scroll = 0, m_hover = -1, m_countA = 0, m_countB = 0;
};

class MdSaveDialog : public juce::Component {
public:
    static constexpr int kS = 2, kLcdW = 240, kLcdH = 88;
    MdSaveDialog() { setWantsKeyboardFocus(true); }
    // projectOption: "" = none, else what ticking it does ("PROJECT X, KIT 03 T2"): the item also goes into that slot
    // versionOf: the loaded item's name: ticked (the default), the save is a new version of it; else a new item
    void open(const juce::String& title, const juce::String& name, const juce::String& projectOption = {}, const juce::String& versionOf = {});
    std::function<juce::String(const juce::String& name, bool intoProject, bool asVersion)> onSave;   // returns an error, empty on success
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    void save();
    juce::Rectangle<int> box() const { return juce::Rectangle<int>(0, 0, kLcdW * kS, kLcdH * kS).withCentre(getLocalBounds().getCentre()); }
    juce::String m_title, m_name, m_error, m_project, m_versionOf;
    bool m_intoProject = false, m_asVersion = true;
    juce::Rectangle<int> m_cancel, m_save, m_option, m_version;   // LCD px inside the box
};

} // namespace mnm::plugin::md
