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

// The sequencer bar under the kit strip: PLAY (the plugin's own transport while the host's is stopped), GRID (the track
// keys become the selected track's steps, as the hardware's grid recording; again: tracks), the track the
// GRID edits (< T1 BD >), the pattern (< A01 >), its pages (a dot each: ring = there, solid = playing, underlined = the
// page GRID shows; a click shows that page) and the playing step (SONG: the row as well).
class MdSeqBar : public juce::Component, public juce::SettableTooltipClient {
public:
    enum Part { None = -1, Play, Rec, Grid, TrkPrev, Trk, TrkNext, Mute, PtnPrev, Ptn, PtnNext, Pages, Edit, Step, kParts };
    static constexpr int kS = 2, kLcdH = 15;
    struct State {
        bool playing = false, hostPlaying = false, grid = false, empty = true, muted = false, rec = false, recording = false;
        int track = 0, pattern = 0, length = 16, page = 0, step = -1, row = -1;
        juce::String machine;
        bool operator==(const State& o) const { return playing == o.playing && rec == o.rec && recording == o.recording && muted == o.muted && hostPlaying == o.hostPlaying && grid == o.grid && empty == o.empty && track == o.track && pattern == o.pattern && length == o.length && page == o.page && step == o.step && row == o.row && machine == o.machine; }
    };
    std::function<void(Part)> onPart;
    std::function<void(int)> onPage;
    void setState(const State& s) { if (!(s == m_s)) { m_s = s; repaint(); } }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover != None) { m_hover = None; repaint(); } }
private:
    Part partAt(juce::Point<int> lcd) const;
    int dotX(int i) const { return m_rects[size_t(Pages)].getX() + 22 + i * 11; }
    State m_s;
    Part m_hover = None;
    std::array<juce::Rectangle<int>, kParts> m_rects{};   // LCD px
};

// The song editor, over the pages: a song's rows as the unit's song mode keeps them. A row plays a pattern's steps
// START..END (END exclusive, -- = the whole pattern) REP times with its MUTES; a LOOP row jumps back TO a row TIMES times
// (INF = forever); END ends the song; TEMPO (-- = unchanged) goes with the song to the unit. Drag a value (or wheel it),
// click a mute, double-click a tempo for --; right-click a row or use the buttons below for rows.
class MdSongEditor : public juce::Component {
public:
    static constexpr int kS = 3;   // LCD scale (one::kScale)
    std::function<std::shared_ptr<const mnm::mddump::Song>()> getSong;
    std::function<void(const juce::String& label, const std::function<void(mnm::mddump::Song&)>& fn, int coalesce)> edit;
    std::function<int()> playingRow;      // -1 none
    std::function<int()> defaultPattern;  // the pattern a new row plays
    std::function<int(int)> patternLength;
    std::function<void()> onClose;
    void open(int slot) { m_slot = slot; m_sel = -1; m_scroll = 0; setVisible(true); toFront(true); repaint(); }
    int slot() const { return m_slot; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { m_drag = -1; }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress& k) override;
    enum Col { Num, Ptn, Rep, Start, End, Tempo, Mutes, kCols };
private:
    struct Hit { int row = -1, col = -1, mute = -1, button = -1; bool close = false; };
    Hit hitAt(juce::Point<int> lcd) const;
    int rowsVisible() const;
    int value(const mnm::mddump::SongRow& r, int col) const;
    void setValue(int row, int col, int v, int coalesce);
    void rowMenu(int row);
    void rowOp(int op, int row);   // 0 +pattern after, 1 +LOOP after, 2 +END after, 3 delete, 4 up, 5 down, 6 duplicate, 7 +pattern before
    int m_slot = 0, m_sel = -1, m_scroll = 0;
    int m_drag = -1, m_dragCol = -1, m_dragY = 0, m_dragV = 0;
};

// MIDI settings, over the pages, as the unit's global MIDI page: BASE CHANNEL, PRG CHANGE, MIDI OUT and each track's
// trig note (drag a value or wheel it). DEFAULT MAP puts the standard note map back; FROM PROJECT loads the channel,
// the map and the program change mode from one of the bank project's globals.
class MdMidiPanel : public juce::Component {
public:
    static constexpr int kS = 3;
    struct Values { int baseChannel = 0, programChange = 1, midiOut = 0; std::array<int, 16> note{}; };   // note -1 = none
    std::function<Values()> get;
    std::function<void(const Values&)> set;
    std::function<void()> onDefault, onFromProject, onClose;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { m_drag = -1; }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress& k) override;
private:
    // cells: 0 base channel, 1 program change, 2 MIDI out, 3..18 the tracks' notes
    juce::Rectangle<int> cellRect(int c) const;
    int cellAt(juce::Point<int> lcd) const;
    void change(int cell, int to);
    int cellValue(const Values& v, int c) const;
    int m_drag = -1, m_dragY = 0, m_dragV = 0;
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
