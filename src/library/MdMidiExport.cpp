#include "MdMidiExport.h"
#include "MdNames.h"

namespace mnm::library {

using namespace mnm::mddump;

namespace {
constexpr int kPPQ = 480;
constexpr int kTrackNotes[16] = {36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53, 55, 57, 59, 60, 62};
constexpr int kCcBases[4] = {16, 40, 72, 96};   // + param 0-23, on channel 1 + track / 4

juce::MidiMessageSequence trackSequence(const Kit* kit, const Pattern& pat, int track)
{
    const int len = juce::jlimit(1, 64, int(pat.length));
    static const int kStepClocks[8] = {6, 3, 8, 4, 12, 24, 48, 2};   // per speed (as md::PatternPlayer), 24 a quarter note
    const int stepTicks = kPPQ * kStepClocks[pat.doubleTempo & 7] / 24;
    const int swing = pat.swingPercent();
    const uint64_t swingMask = pat.swingEditAll ? pat.swing : pat.swingPerTrack[track];
    const uint64_t accentMask = pat.accentEditAll ? pat.accent : pat.accentPerTrack[track];
    auto tickOf = [&](int j) {
        int t = j * stepTicks;
        if (((swingMask >> j) & 1) && swing > 50) t += (2 * stepTicks * swing) / 100 - stepTicks;
        return t;
    };
    const int noteChannel = 1, ccChannel = 1 + track / 4, ccBase = kCcBases[track % 4];

    juce::MidiMessageSequence seq;
    const juce::String name = juce::String(patternSlotName(pat.position)) + " T" + juce::String(track + 1)
        + (kit ? " " + juce::String(mnm::mdnames::machineName(kit->model(track))) : juce::String());
    seq.addEvent(juce::MidiMessage::textMetaEvent(3, name), 0.0);

    std::array<int, 24> sent;
    sent.fill(-1);
    std::array<bool, 24> lockedParam{};
    for (int q = 0; q < 24; ++q) lockedParam[size_t(q)] = pat.lockRow(track, q) >= 0;
    for (int j = 0; j < len; ++j) {
        if (!((pat.trigs[track] >> j) & 1)) continue;
        const double t = tickOf(j);
        for (int q = 0; q < 24; ++q) {   // this step's locks, or the kit's value back after a locked step
            if (!lockedParam[size_t(q)]) continue;
            int v = pat.locks[pat.lockRow(track, q)][j];
            if (v > 127) v = kit ? kit->params[track][q] : -1;
            if (v >= 0 && v != sent[size_t(q)]) {
                seq.addEvent(juce::MidiMessage::controllerEvent(ccChannel, ccBase + q, v), t);
                sent[size_t(q)] = v;
            }
        }
        const auto velocity = juce::uint8(((accentMask >> j) & 1) ? 127 : 100);
        seq.addEvent(juce::MidiMessage::noteOn(noteChannel, kTrackNotes[track], velocity), t);
        seq.addEvent(juce::MidiMessage::noteOff(noteChannel, kTrackNotes[track]), t + stepTicks);
    }
    seq.addEvent(juce::MidiMessage::textMetaEvent(6, "loop end"), double(len * stepTicks));
    seq.updateMatchedPairs();
    seq.sort();
    return seq;
}
} // namespace

int mdTrackNote(int track) { return kTrackNotes[juce::jlimit(0, 15, track)]; }

int mdPatternFromMidi(const juce::MidiFile& mfIn, const std::array<int, 128>& noteTrack, int onlyTrack, const Kit* kit,
                      Pattern& pat, juce::String* why)
{
    const int ppq = mfIn.getTimeFormat() > 0 ? mfIn.getTimeFormat() : 96;
    static const int kStepClocks[8] = {6, 3, 8, 4, 12, 24, 48, 2};
    const double stepTicks = double(ppq) * kStepClocks[pat.doubleTempo & 7] / 24.0;
    struct Hit { int track, step, vel; };
    struct Cc { int track, param, step, value; };
    std::vector<Hit> hits;
    std::vector<Cc> ccs;
    double endTicks = 0;
    for (int ti = 0; ti < mfIn.getNumTracks(); ++ti) {
        const auto* seq = mfIn.getTrack(ti);
        endTicks = std::max(endTicks, seq->getEndTime());
        for (const auto* ev : *seq) {
            const auto& m = ev->message;
            const int step = int(std::lround(m.getTimeStamp() / stepTicks));
            if (step < 0 || step >= 64) continue;
            if (m.isNoteOn()) {
                const int t = onlyTrack >= 0 ? onlyTrack : noteTrack[size_t(m.getNoteNumber() & 127)];
                if (t >= 0 && t < kTracks) hits.push_back({t, step, m.getVelocity()});
            } else if (m.isController()) {   // the CC map: channel 1 + track / 4, CC [16 40 72 96][track % 4] + param
                const int cc = m.getControllerNumber(), ch = m.getChannel() - 1;
                for (int b = 0; b < 4; ++b)
                    if (cc >= kCcBases[b] && cc < kCcBases[b] + 24) {
                        const int t = onlyTrack >= 0 ? onlyTrack : ch * 4 + b;
                        if (t >= 0 && t < kTracks) ccs.push_back({t, cc - kCcBases[b], step, m.getControllerValue()});
                    }
            }
        }
    }
    if (hits.empty()) { if (why) *why = onlyTrack < 0 ? "no notes on the tracks' trig notes" : "no notes"; return 0; }
    // the clip's length: its end (the clip's end in Ableton), else just past its last note
    int len = int(std::ceil(endTicks / stepTicks - 0.01));
    for (const auto& h : hits) len = std::max(len, h.step + 1);
    len = juce::jlimit(1, 64, len);
    std::array<bool, kTracks> replaced{};
    for (const auto& h : hits) replaced[size_t(h.track)] = true;
    for (int t = 0; t < kTracks; ++t) if (replaced[size_t(t)]) pat.clearSteps(0, 64, t);
    int placed = 0;
    for (const auto& h : hits) {
        if (!((pat.trigs[h.track] >> h.step) & 1)) ++placed;
        pat.trigs[h.track] |= uint64_t(1) << h.step;
        if (h.vel >= 112) (pat.accentEditAll ? pat.accent : pat.accentPerTrack[h.track]) |= uint64_t(1) << h.step;
    }
    for (const auto& c : ccs) {
        if (!replaced[size_t(c.track)] || !((pat.trigs[c.track] >> c.step) & 1)) continue;
        if (kit && kit->params[c.track][c.param] == c.value) continue;   // the kit's value: a lock's way back, not a lock
        pat.setLock(c.track, c.param, c.step, c.value);
    }
    pat.length = uint8_t(len);
    pat.scale = uint8_t((len - 1) / 16);
    pat.extended = pat.extended || len > 32;
    return placed;
}

juce::MidiFile buildMdTrackMidiFile(const Kit* kit, const Pattern& pat, int track)
{
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kPPQ);
    mf.addTrack(trackSequence(kit, pat, juce::jlimit(0, 15, track)));
    return mf;
}

juce::MidiFile buildMdPatternMidiFile(const Kit* kit, const Pattern& pat)
{
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kPPQ);
    for (int t = 0; t < kTracks; ++t)
        if (pat.trigCount(t) > 0) mf.addTrack(trackSequence(kit, pat, t));
    if (mf.getNumTracks() == 0) mf.addTrack(trackSequence(kit, pat, 0));
    return mf;
}

} // namespace mnm::library
