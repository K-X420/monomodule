// What each SYNTHESIS knob does, by its label, in a few words (written for this project from the parameter list in the
// Machinedrum manual's machine appendix). A label means much the same on every machine that has it; ROM / RAM machines
// get their own lines for the sample knobs (machineWords).
#pragma once
#include <cstring>

namespace mnm::plugin::md::text {

struct ParamWord { const char* label; const char* words; };
inline constexpr ParamWord kParamWords[] = {
    {"ADEC", "Input amp envelope: decay"}, {"AG", "Brightens the top end"}, {"AHLD", "Input amp envelope: hold"}, {"ALEV", "Input level"},
    {"AT", "Aftertouch sent"}, {"ATCK", "Input gate: attack"}, {"ATT", "Attack length"}, {"AU", "Darkens the top end"}, {"AVOL", "Input level"},
    {"BC", "Bongo (low) to conga (high)"}, {"BEND", "Pitch bend depth"}, {"BENV", "How the bump fades"}, {"BR", "Low-end amount"},
    {"BRR", "Bit-rate reduction"}, {"BUMP", "A pitch kick at the start"}, {"CDEC", "Clap decay"}, {"CLIC", "A click at the start"},
    {"CLIP", "Clipping distortion"}, {"CLOS", "When the hi-hat closes"}, {"CLPS", "How many claps"}, {"CLPY", "How tight the claps are"},
    {"DAMP", "Damps the decay"}, {"DEC", "Decay time"}, {"DIST", "Distortion"}, {"DOWN", "How long the impulse stays negative"},
    {"DTYP", "Distortion hardness"}, {"DUAL", "A second attack"}, {"DVAL", "Level of the negative phase"}, {"END", "Sample end point"},
    {"ENH", "Fuller body"}, {"FATK", "Input filter envelope: attack"}, {"FB", "Feedback"}, {"FDEC", "Input filter envelope: decay"},
    {"FDPH", "Input filter envelope depth"}, {"FFRQ", "Input filter cutoff"}, {"FHLD", "Input filter envelope: hold"}, {"FQ", "Input filter resonance"},
    {"GAP", "The hi-hat's gap"}, {"GATE", "Input gate sensitivity"}, {"GLEN", "Grain decay"}, {"GRAB", "When the cymbal is grabbed (choked)"},
    {"GRNS", "How many grains (beads)"}, {"HAMR", "Mallet hardness (higher = softer)"}, {"HARD", "Harder claps"}, {"HARM", "Extra harmonics"},
    {"HLD", "Input gate hold"}, {"HOLD", "Keeps the amp envelope open for this long"}, {"HP", "High-pass cutoff"}, {"HPF", "High-pass filter"},
    {"HPQ", "High-pass resonance"}, {"IBAL", "Input balance in the recording"}, {"ILEV", "Input level into the recording"}, {"LEN", "Note / recording length"},
    {"LFOD", "LFO depth"}, {"LFOS", "LFO speed"}, {"LPF", "Low-pass filter"}, {"MBAL", "Main-mix balance in the recording"},
    {"MDEC", "Modulator decay"}, {"MFB", "Modulator feedback"}, {"MFRQ", "Modulator frequency"}, {"MLEV", "Main-mix level into the recording"},
    {"MOD", "FM depth"}, {"MTAL", "Metallic character"}, {"MW", "Mod wheel sent"}, {"NDEC", "Noise decay"}, {"NOIS", "Noise at the start"},
    {"NOTE", "The MIDI note the trig plays"}, {"PB", "Pitch bend sent"}, {"PEAK", "Sharper edge"}, {"POS", "Strike point: centre to rim"},
    {"PTCH", "Pitch"}, {"RAMP", "Pitch sweep"}, {"RATE", "Clap / sample rate"}, {"RATL", "Rattle"}, {"RDEC", "Pitch sweep speed"},
    {"REAL", "Brush realism"}, {"REV", "Reverses the shake"}, {"RICH", "Richer sound"}, {"RING", "Ringing overtones"}, {"ROOM", "Room sound"},
    {"RRTL", "Ringing"}, {"RSIZ", "Room size"}, {"RTIM", "Time between retrigs"}, {"RTRG", "How many retrigs"}, {"RTUN", "Room tone"},
    {"RTYP", "Rattle type"}, {"RVOL", "Snare rattle level"}, {"SDEC", "Snare body decay"}, {"SIZE", "Cymbal size"}, {"SLEW", "Shake back and forth"},
    {"SMOD", "Snare body modulation"}, {"SNAP", "Snap amount"}, {"SNAR", "Adds a snare body"}, {"SPTC", "Snare body pitch"},
    {"STRT", "A harder start"}, {"SUS", "Sustain length"}, {"TENS", "How hard the hit is"}, {"TFRQ", "Tremolo rate"}, {"TIME", "Time"},
    {"TONE", "Tone colour"}, {"TOP", "High harmonics"}, {"TREM", "Tremolo depth"}, {"TTUN", "Top tuning"}, {"TUNE", "Detune"},
    {"UP", "How long the impulse stays positive"}, {"UVAL", "Level of the positive phase"}, {"VEL", "Note velocity (NOTE, N2, N3)"},
    {"VOL", "Volume"}, {"N2", "A second note, relative to NOTE"}, {"N3", "A third note, relative to NOTE"}, {"PCHG", "Program change sent"},
};

inline const char* paramWords(const char* label)
{
    for (const auto& w : kParamWords) if (std::strcmp(w.label, label) == 0) return w.words;
    return nullptr;
}

// ROM / RAM-P: the sample knobs
inline const char* machineWords(const char* family, const char* label)
{
    if (std::strcmp(family, "ROM") == 0 || std::strcmp(family, "RAM") == 0) {
        if (std::strcmp(label, "STRT") == 0) return "Sample start point";
        if (std::strcmp(label, "END") == 0) return "Sample end point (below STRT: plays reversed)";
        if (std::strcmp(label, "RATE") == 0) return "Playback rate";
    }
    return paramWords(label);
}

} // namespace mnm::plugin::md::text
