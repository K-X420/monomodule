// Machinedrum pattern -> Standard MIDI File, for dragging a pattern (or one track) into the DAW, as MidiExport.h does
// for the Monomachine. Each trig is a note on its track's trig note (the Machinedrum's default map, which Monomodule MD
// plays: 36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62), a sixteenth long; accents are velocity 127 (others 100), so
// both VOLUME and ACCENT velocity modes hear them; swing moves the swung steps; parameter locks are the CCs of the
// Machinedrum's map (channel 1 + track / 4, Monomodule MD's CC map), the kit's value sent back after a locked step.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "MdDump.h"

namespace mnm::library {

// kit = the pattern's kit (for the values the locks return to), may be null
juce::MidiFile buildMdTrackMidiFile(const mnm::mddump::Kit* kit, const mnm::mddump::Pattern& pat, int track);
juce::MidiFile buildMdPatternMidiFile(const mnm::mddump::Kit* kit, const mnm::mddump::Pattern& pat);   // a MIDI track per track with trigs
int mdTrackNote(int track);

} // namespace mnm::library
