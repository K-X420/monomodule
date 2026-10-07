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
