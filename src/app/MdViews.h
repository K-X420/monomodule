// Monomodule Library: the Machinedrum pages, built from the same pieces as the Monomachine ones (LibraryViews.h):
//   MdKitView      16 track rows (machine, the sound when it was first seen elsewhere, level, the synthesis values),
//                  the master effects, the patterns that use the kit and where it came from
//   MdSoundView    a sound (one track): a summary, its four pages drawn as in Monomodule MD, actions, references
//   MdPatternView  a pattern: summary, a trig grid per track (accents, locked steps), its kit and sources
//   MdSongsView    a project's songs: each song's rows (pattern, repeats, steps, tempo, mutes; LOOP and END rows)
// Machine names come from mdnames; the synthesis labels from the user's Machinedrum OS file (Monomodule MD's
// "mdOsPath" shared setting), or SYN1-8 without one.
#pragma once
#include "LibraryViews.h"
#include "MdCatalog.h"
#include "MdDump.h"

namespace mnm::app {

struct MdMachineInfo { juce::String name; std::array<juce::String, 8> labels; std::array<uint8_t, 8> defaults{}; };
const MdMachineInfo& mdMachine(int id);   // labels from the OS file when there is one
juce::String mdSynLine(int machine, const uint8_t* params8, int maxWidth = 1 << 30);   // "PTCH 64  DEC 32 ..." within maxWidth px

class MdKitView : public juce::Component {
public:
    static constexpr int kRowLcdH = 17;
    std::function<void(int)> onTrack;       // a track row clicked (-> its sound)
    std::function<void(int)> onDragTrack;
    std::function<void()> onDragKit, onPlayKit;
    std::function<void(int)> onPlayTrack;
    std::function<void(const juce::var&)> onLink;
    MdKitView();
    void setPlaying(int stem) { if (stem != m_playing) { m_playing = stem; repaint(); } }   // -2 nothing, -1 the kit, 0-15 a track
    void set(const mnm::mddump::Kit& kit, const std::array<juce::String, 16>& soundNames, std::vector<Card> patterns, std::vector<LinkList::Row> sources);
    void setTitle(const juce::String& title) { m_title = title; repaint(); }
    CardSection& patterns() { return m_patterns; }
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { if (m_hover != -1) { m_hover = -1; repaint(); } }
private:
    static constexpr int kTracksTop = 12, kFxLines = 5;
    int fxTop() const { return (kTracksTop + 16 * kRowLcdH + 3) * kScale; }
    int sectionsTop() const { return fxTop() + kFxLines * ui::kLineH + 8; }
    juce::Rectangle<int> trackRow(int t) const;
    int playZone(juce::Point<int> p) const;   // 1 play/stop, 2 loop (title bar or a row)
    mnm::mddump::Kit m_kit;
    std::array<juce::String, 16> m_soundNames;
    juce::String m_title;
    CardSection m_patterns;
    LinkList m_sources;
    int m_hover = -1, m_playing = -2;
    bool m_dragging = false;
};

class MdSoundView : public juce::Component {
public:
    std::function<void()> onDrag, onPlay, onFavourite, onTag;
    std::function<void(const juce::var&)> onLink;
    MdSoundView();
    void setPlaying(bool b) { if (b != m_playing) { m_playing = b; repaint(); } }
    void set(const mnm::mdcatalog::SoundItem& sound, const juce::String& source, const juce::StringArray& tags, bool favourite,
             std::vector<Card> versions, std::vector<Card> kits, std::vector<Card> patterns);
    CardSection& versions() { return m_versions; }
    CardSection& kits() { return m_kits; }
    CardSection& patterns() { return m_patterns; }
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    static constexpr int kPageScale = 2;
    static constexpr int kSummaryTop = 12 * kScale, kPagesTop = kSummaryTop + 2 * ui::kLineH + 6;
    int pagesHeight() const { return pageRows() * (kPageH + 4) * kPageScale; }
    int pageRows() const { return getWidth() >= 2 * (kPageW + 4) * kPageScale ? 2 : 4; }
    int chipsTop() const { return kPagesTop + pagesHeight() + 6; }
    int sectionsTop() const { return chipsTop() + 34; }
    int playZone(juce::Point<int> p) const { return p.y < 10 * kScale ? playZoneAt((getWidth() - p.x) / kScale, m_playing) : 0; }
    mnm::mdcatalog::SoundItem m_sound;
    juce::String m_source;
    juce::StringArray m_tags;
    bool m_favourite = false, m_playing = false, m_dragging = false, m_hasVersions = false;
    LcdChip m_fav{"FAVOURITE", spec::kFontBold8, 2, false}, m_tag{"ADD TAG", spec::kFontBold8, 2, false};
    CardSection m_versions, m_kits, m_patterns;
};

class MdPatternView : public juce::Component {
public:
    std::function<void()> onDragPattern, onPlayPattern;
    std::function<void(int)> onSound, onDragTrack, onPlayTrack;
    std::function<void(const juce::var&)> onLink;
    MdPatternView();
    void setPlaying(int stem) { if (stem != m_playing) { m_playing = stem; repaint(); } }
    // soundIds/names per track from the pattern's kit (empty without one)
    void set(const mnm::mdcatalog::PatternItem& pattern, const mnm::mddump::Kit* kit, const std::array<juce::String, 16>& soundNames,
             std::vector<Card> kitCards, std::vector<LinkList::Row> sources);
    CardSection& kitSection() { return m_kitSection; }
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
private:
    static constexpr int kGridTop = 12 + 2 * 7 + 4, kRowLcdH = 9;   // LCD rows: title, two summary lines, the grid
    int gridBottomPx() const { return (kGridTop + 17 * kRowLcdH + 4) * kScale; }
    int trackAt(juce::Point<int> p) const;
    int playZone(juce::Point<int> p) const { return p.y < 10 * kScale ? playZoneAt((getWidth() - p.x) / kScale, m_playing == -1) : 0; }
    mnm::mdcatalog::PatternItem m_pattern;
    bool m_hasKit = false;
    mnm::mddump::Kit m_kit;
    std::array<juce::String, 16> m_soundNames;
    CardSection m_kitSection;
    LinkList m_sources;
    int m_playing = -2, m_hover = -1;
    bool m_dragging = false;
};

// The song rows as the unit keeps them (MdDump: 10 bytes, unpacked): pattern (0..127; 0xFE LOOP, 0xFF END), kit (unused
// here), repeats - 1, the LOOP row's target, the muted tracks (16 bits, track 1 = bit 0), the tempo (BPM x 24,
// 0xFFFF = unchanged), the first and the last step + 1.
class MdSongsView : public juce::Component {
public:
    void set(const std::vector<mnm::mddump::Song>& songs);
    int preferredHeight() const;
    void paint(juce::Graphics&) override;
private:
    static constexpr int kTitleH = 11 * kScale, kRowH = 18, kGap = 14;
    std::vector<mnm::mddump::Song> m_songs;
};

} // namespace mnm::app
