// Machinedrum pattern -> Standard MIDI File, for dragging a pattern (or one track) into the DAW, as MidiExport.h does
// for the Monomachine. Each trig is a note on its track's trig note (the Machinedrum's default map, which Monomodule MD
// plays: 36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62), a sixteenth long; accents are velocity 127 (others 100), so
// both VOLUME and ACCENT velocity modes hear them; swing moves the swung steps; parameter locks are the CCs of the
// Machinedrum's map (channel 1 + track / 4, Monomodule MD's CC map), the kit's value sent back after a locked step.
#pragma once
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>
#include "MdDump.h"

namespace mnm::library {

// kit = the pattern's kit (for the values the locks return to), may be null
juce::MidiFile buildMdTrackMidiFile(const mnm::mddump::Kit* kit, const mnm::mddump::Pattern& pat, int track);
juce::MidiFile buildMdPatternMidiFile(const mnm::mddump::Kit* kit, const mnm::mddump::Pattern& pat);   // a MIDI track per track with trigs
int mdTrackNote(int track);

// A MIDI file (an Ableton clip dropped on Monomodule MD) into a pattern, as the export writes one: each note onto the
// track its number maps to (noteTrack, -1 = none), or every note onto onlyTrack (>= 0: a clip dropped on a track key);
// times to the nearest step at the pattern's speed, velocity 112 and up as an accent, the CC map's controllers at a
// trig as that trig's locks (a value equal to the kit's, a lock's way back, is left out). The tracks the clip plays
// (onlyTrack: that one) are replaced; the pattern's length becomes the clip's (up to 64 steps), its other settings
// stay. Returns the trigs placed (0: nothing usable, why says).
int mdPatternFromMidi(const juce::MidiFile& mf, const std::array<int, 128>& noteTrack, int onlyTrack, const mnm::mddump::Kit* kit,
                      mnm::mddump::Pattern& pat, juce::String* why = nullptr);

} // namespace mnm::library
