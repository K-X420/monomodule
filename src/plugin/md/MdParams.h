// Monomodule MD parameters, as the Machinedrum's pages: per track the machine and its eight SYNTHESIS knobs, the eight
// EFFECTS knobs, ROUTING (DIST VOL PAN DEL REV), the kit LEVEL and the output; the four master effects; an output
// volume. Every knob is raw 0..127 as on the hardware (PAN shown -64..63). The machine list is the stock synthesis
// machines of OS 1.63 (names and knob labels shown come from the user's OS file at run time).
#pragma once
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

namespace mnm::plugin::md {

constexpr int kTracks = 16;
// The Machinedrum's default trig notes for tracks 1-16
constexpr int kTrackNotes[kTracks] = {36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53, 55, 57, 59, 60, 62};

struct MachineEntry { int id; const char* name; };
constexpr MachineEntry kMachines[] = {
    {0, "GND---"},
    {1, "GND-SN"}, {2, "GND-NS"}, {3, "GND-IM"},
    {16, "TRX-BD"}, {17, "TRX-SD"}, {18, "TRX-XT"}, {19, "TRX-CP"}, {20, "TRX-RS"}, {21, "TRX-CB"}, {22, "TRX-CH"},
    {23, "TRX-OH"}, {24, "TRX-CY"}, {25, "TRX-MA"}, {26, "TRX-CL"}, {27, "TRX-XC"}, {28, "TRX-B2"},
    {32, "EFM-BD"}, {33, "EFM-SD"}, {34, "EFM-XT"}, {35, "EFM-CP"}, {36, "EFM-RS"}, {37, "EFM-CB"}, {38, "EFM-HH"},
    {39, "EFM-CY"},
    {48, "E12-BD"}, {49, "E12-SD"}, {50, "E12-HT"}, {51, "E12-LT"}, {52, "E12-CP"}, {53, "E12-RS"}, {54, "E12-CB"},
    {55, "E12-CH"}, {56, "E12-OH"}, {57, "E12-RC"}, {58, "E12-CC"}, {59, "E12-BR"}, {60, "E12-TA"}, {61, "E12-TR"},
    {62, "E12-SH"}, {63, "E12-BC"},
    {64, "P-I-BD"}, {65, "P-I-SD"}, {66, "P-I-MT"}, {67, "P-I-ML"}, {68, "P-I-MA"}, {69, "P-I-RS"}, {70, "P-I-RC"},
    {71, "P-I-CC"}, {72, "P-I-HH"},
    {80, "INP-GA"}, {81, "INP-GB"}, {82, "INP-FA"}, {83, "INP-FB"}, {84, "INP-EA"}, {85, "INP-EB"},   // the side-chain input
};
constexpr int kNumMachines = int(sizeof(kMachines) / sizeof(kMachines[0]));
inline int machineIndexOf(int id) { for (int i = 0; i < kNumMachines; ++i) if (kMachines[i].id == id) return i; return 0; }

// The kit a new instance starts with (machine ID and its descriptor defaults)
struct DefaultTrack { int id; std::array<int, 8> knobs; };
constexpr DefaultTrack kDefaultKit[kTracks] = {
    {16, {64, 64, 0, 0, 0, 0, 0, 0}},        // TRX-BD
    {17, {64, 32, 0, 64, 64, 64, 64, 0}},    // TRX-SD
    {18, {64, 48, 48, 102, 64, 0, 0, 0}},    // TRX-XT
    {19, {64, 64, 127, 64, 64, 32, 32, 64}}, // TRX-CP
    {20, {64, 32, 0, 0, 0, 0, 0, 0}},        // TRX-RS
    {21, {64, 64, 64, 0, 64, 64, 0, 0}},     // TRX-CB
    {22, {32, 32, 64, 64, 64, 0, 0, 0}},     // TRX-CH
    {23, {64, 64, 64, 64, 64, 0, 0, 0}},     // TRX-OH
    {24, {64, 64, 64, 64, 64, 64, 0, 0}},    // TRX-CY
    {25, {32, 32, 0, 0, 64, 64, 64, 64}},    // TRX-MA
    {26, {64, 32, 64, 0, 64, 0, 0, 0}},      // TRX-CL
    {27, {64, 32, 32, 96, 0, 0, 0, 0}},      // TRX-XC
    {32, {36, 76, 64, 55, 64, 64, 32, 127}}, // EFM-BD
    {33, {64, 55, 64, 64, 64, 32, 44, 16}},  // EFM-SD
    {52, {64, 127, 0, 0, 0, 0, 64, 64}},     // E12-CP
    {60, {64, 96, 0, 0, 0, 0, 64, 64}},      // E12-TA
};

// Track effects (DSP1 Y:0x200+0x40*t +0..7) and routing, as on the Machinedrum's EFFECTS and ROUTING pages
constexpr const char* kFxLabels[8] = {"AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR"};
constexpr int kFxDefaults[8] = {0, 0, 64, 64, 0, 127, 0, 0};
constexpr const char* kRouteLabels[6] = {"DIST", "VOL", "PAN", "DEL", "REV", "LEV"};
constexpr int kNumRoutes = 7;   // OUT: A B C D E F MAIN (the DSP1 route numbers 0..6)
constexpr const char* kRouteNames[kNumRoutes] = {"A", "B", "C", "D", "E", "F", "MAIN"};

// Master effects, in the ColdFire's section order (md::ControlCpu::MasterFx)
constexpr const char* kMasterFxNames[4] = {"REVERB", "DELAY", "EQ", "DYNAMIX"};
constexpr const char* kMasterFxLabels[4][8] = {
    {"DVOL", "PRED", "DEC", "DAMP", "HP", "LP", "GATE", "LEV"},
    {"TIME", "MOD", "MFRQ", "FB", "FLTF", "FLTW", "MONO", "LEV"},
    {"LF", "LG", "HF", "HG", "PF", "PG", "PQ", "GAIN"},
    {"ATCK", "REL", "TRHD", "RTIO", "KNEE", "HP", "OUTG", "MIX"},
};
constexpr int kMasterFxDefaults[4][8] = {
    {127, 0, 64, 64, 0, 127, 0, 127},
    {24, 0, 0, 32, 0, 127, 0, 127},
    {64, 64, 64, 64, 64, 64, 64, 64},
    {0, 64, 127, 0, 0, 0, 64, 0},
};

inline juce::String tp(int t) { return "t" + juce::String(t + 1); }
inline juce::String machineId(int t) { return tp(t) + "mach"; }
inline juce::String knobId(int t, int k) { return tp(t) + "p" + juce::String(k + 1); }
inline juce::String fxId(int t, int k) { return tp(t) + "fx" + juce::String(k + 1); }
inline juce::String distId(int t) { return tp(t) + "dist"; }
inline juce::String volId(int t) { return tp(t) + "vol"; }
inline juce::String panId(int t) { return tp(t) + "pan"; }
inline juce::String delId(int t) { return tp(t) + "del"; }
inline juce::String revId(int t) { return tp(t) + "rev"; }
inline juce::String levelId(int t) { return tp(t) + "lev"; }
inline juce::String routeId(int t) { return tp(t) + "out"; }
// LFO page: destination, shapes, type (the kit's LFO struct) and SPD DEP MIX (routing bytes 21-23)
constexpr const char* kLfoLabels[8] = {"TRK", "PARAM", "SHP1", "SHP2", "TYPE", "SPD", "DEP", "MIX"};
constexpr const char* kLfoTypes[3] = {"FREE", "TRIG", "HOLD"};
constexpr const char* kLfoParamNames[24] = {"SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8",
                                            "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR",
                                            "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
inline juce::String lfoId(int t, int k) { static const char* n[8] = {"lfotrk", "lfopar", "lfosh1", "lfosh2", "lfotyp", "lfospd", "lfodep", "lfomix"}; return tp(t) + n[k]; }
inline juce::String masterFxId(int fx, int k) { static const char* p[4] = {"rv", "dl", "eq", "dx"}; return juce::String(p[fx]) + juce::String(k + 1); }
inline juce::String masterId() { return "master"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    juce::StringArray names;
    for (const auto& m : kMachines) names.add(m.name);
    for (int t = 0; t < kTracks; ++t) {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>(tp(t), "T" + juce::String(t + 1), " ");
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{machineId(t), 1}, "MACHINE", names, machineIndexOf(kDefaultKit[t].id)));
        for (int k = 0; k < 8; ++k)
            g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{knobId(t, k), 1}, "SYN " + juce::String(k + 1), 0, 127, kDefaultKit[t].knobs[size_t(k)]));
        for (int k = 0; k < 8; ++k)
            g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{fxId(t, k), 1}, kFxLabels[k], 0, 127, kFxDefaults[k]));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{distId(t), 1}, "DIST", 0, 127, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{volId(t), 1}, "VOL", 0, 127, 100));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{panId(t), 2}, "PAN", -64, 63, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{delId(t), 1}, "DEL", 0, 127, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{revId(t), 1}, "REV", 0, 127, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{levelId(t), 2}, "LEVEL", 0, 127, 127));
        juce::StringArray routes;
        for (auto* r : kRouteNames) routes.add(r);
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{routeId(t), 1}, "OUT", routes, kNumRoutes - 1));
        juce::StringArray tracks, lfoParams, types;
        for (int i = 0; i < kTracks; ++i) tracks.add("T" + juce::String(i + 1));
        for (auto* n : kLfoParamNames) lfoParams.add(n);
        for (auto* n : kLfoTypes) types.add(n);
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{lfoId(t, 0), 1}, "LFO TRK", tracks, t));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{lfoId(t, 1), 1}, "LFO PARAM", lfoParams, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{lfoId(t, 2), 1}, "LFO SHP1", 0, 7, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{lfoId(t, 3), 1}, "LFO SHP2", 0, 7, 0));
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{lfoId(t, 4), 1}, "LFO TYPE", types, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{lfoId(t, 5), 1}, "LFO SPD", 0, 127, 64));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{lfoId(t, 6), 1}, "LFO DEP", 0, 127, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{lfoId(t, 7), 1}, "LFO MIX", 0, 127, 0));
        layout.add(std::move(g));
    }
    for (int fx = 0; fx < 4; ++fx) {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>(juce::String(kMasterFxNames[fx]).toLowerCase(), kMasterFxNames[fx], " ");
        for (int k = 0; k < 8; ++k)
            g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{masterFxId(fx, k), 1}, kMasterFxLabels[fx][k], 0, 127, kMasterFxDefaults[fx][k]));
        layout.add(std::move(g));
    }
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{masterId(), 1}, "VOLUME", 0, 127, 80));
    return layout;
}

} // namespace mnm::plugin::md
