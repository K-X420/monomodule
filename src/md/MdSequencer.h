// The Machinedrum's pattern sequencer, as the OS runs it (MainOS 0x239762, called once per audio block from the
// control task at 0x20BAA4), for the plugin's pattern playback and the library previews.
//
// Time is counted in MIDI clocks (24 per quarter note). The OS's step timer is 30,000,000 / (BPM x 24) timer units
// per clock (0x23B8F0) times the clocks of a step, which the pattern's tempo multiplier picks from the table at
// 0x247E4C: 1x = 6, 2x = 3, 3/4x = 8, 3/2x = 4 clocks. A swung step's trigs come late by step x amount / 16384
// (0x23BA56; the amount is (percent - 50) x 16384 / 50). Which steps are swung, accented or slid is the pattern's
// global mask, or the track's own when the pattern's "edit all" flag for it is off.
//
// Locks (0x23B342..0x23B7D8): at a trig, every parameter locked on that step jumps to the lock value (target and live
// word, no slew); a parameter the track's last trig locked and this one does not jumps back to the kit value. A
// parameter keeps its lock until the track's next trig.
//
// Slides (0x23B49C..0x23B70A, applied per clock at 0x23AC7C): on a slide step, every parameter locked on it glides
// from the lock toward its value at the track's next trig (that trig's lock, else the kit value). The glide adds
// delta / clocks to the raw word (value << 7) once per clock, where clocks = the steps to the next trig x the step's
// clocks, less the swing delay (in whole clocks) when only this step is swung, plus it when only the next one is.
// It stops before it would pass the end. The slid value is the knob's target: the OS's slew smooths it.
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "MdDump.h"

namespace mnm::md {

struct SeqTrig {
    double clock = 0;                      // when (clocks from the sequence origin), swing included
    int64_t stepIndex = 0;                 // steps from the origin (the pattern's step = stepIndex % length)
    int step = 0, track = 0;
    bool accent = false;
    std::array<int16_t, 24> locks{};       // this step's lock per parameter, -1 = not locked
    uint32_t slideMask = 0;                // the parameters that slide from this trig
    std::array<int16_t, 24> slideTo{};     // their end: the next trig's lock, -1 = the kit value
    std::array<int32_t, 24> slideClocks{}; // their duration in clocks (the OS's integer count)
};

class PatternPlayer {
public:
    static int stepClocks(int multiplier) { static const int t[4] = {6, 3, 8, 4}; return t[multiplier & 3]; }

    PatternPlayer() = default;
    explicit PatternPlayer(const mddump::Pattern& p) { set(p); }
    void set(const mddump::Pattern& p);
    // A song row plays steps [start, end) of its pattern (0x1001F48 / 0x1001F4C; a slide's next trig wraps in there too)
    void setRange(int start, int end);
    const mddump::Pattern& pattern() const { return m_p; }
    int length() const { return m_len; }
    int rangeStart() const { return m_start; }
    int span() const { return m_span; }        // the steps of one pass (the range)
    int clocksPerStep() const { return m_step; }
    double lengthClocks() const { return double(m_span) * m_step; }
    int stepOf(int64_t stepIndex) const { return m_start + int(stepIndex % m_span); }
    double swingClocks() const { return m_swing; }    // the delay of a swung step

    bool swung(int track, int step) const { return bit(m_p.swingEditAll, m_p.swing, m_p.swingPerTrack[track], step); }
    bool accented(int track, int step) const { return bit(m_p.accentEditAll, m_p.accent, m_p.accentPerTrack[track], step); }
    bool slid(int track, int step) const { return bit(m_p.slideEditAll, m_p.slide, m_p.slidePerTrack[track], step); }
    bool hasTrig(int track, int step) const { return step >= 0 && step < 64 && ((m_p.trigs[track] >> step) & 1); }
    int lock(int track, int param, int step) const;   // -1 = none

    // The trigs that fire in [from, to) (clocks from the origin, where the first pass starts), in time order, of the
    // steps 0 .. maxSteps - 1 counted from the origin (-1 = no end). A swung step's trigs belong to it even when they
    // come after the last step's end (a pattern change, a song row's end).
    void trigs(double from, double to, std::vector<SeqTrig>& out, int64_t maxSteps = -1) const;
    SeqTrig trigAt(int64_t stepIndex, int track) const;   // the trig of that step (it must have one)

private:
    static bool bit(uint32_t editAll, uint64_t global, uint64_t perTrack, int step) { return ((editAll ? global : perTrack) >> step) & 1; }
    mddump::Pattern m_p;
    int m_len = 16, m_step = 6, m_start = 0, m_span = 16;
    double m_swing = 0;
    int m_swingWhole = 0;   // the OS's integer clocks of the swing delay (slide durations)
    std::array<std::array<int16_t, 24>, 16> m_row{};   // lock row per (track, param), -1 = not locked
};

// One parameter's glide (the OS keeps its value, rate and end per track and parameter; 0x2AE996 / 0x2AFD4A / limits)
struct Glide {
    bool active = false;
    int32_t value = 0, rate = 0, end = 0;   // raw words (value << 7)
    double nextClock = 0;                   // the clock of its next step
    // start: the lock value (0..127) at the trig's clock; to: the end value (0..127)
    void start(int from, int to, int32_t clocks, double trigClock);
    // Steps it to the clock `now`; returns true when its value (>> 7) changed
    bool advance(double now);
    int target() const { return value >> 7; }
};

} // namespace mnm::md
