// Monomodule MD parameters: 16 tracks, each with a machine, the machine's eight synthesis knobs (raw 0..127, as on
// the hardware), level and pan; a master volume. The machine list is the stock synthesis machines of Machinedrum
// OS 1.63 (the IDs the OS offers; the names and knob labels shown come from the user's OS file at run time).
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

inline juce::String tp(int t) { return "t" + juce::String(t + 1); }
inline juce::String machineId(int t) { return tp(t) + "mach"; }
inline juce::String knobId(int t, int k) { return tp(t) + "p" + juce::String(k + 1); }
inline juce::String levelId(int t) { return tp(t) + "lev"; }
inline juce::String panId(int t) { return tp(t) + "pan"; }
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
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{levelId(t), 1}, "LEVEL", 0, 127, 100));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{panId(t), 1}, "PAN", -64, 63, 0));
        layout.add(std::move(g));
    }
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{masterId(), 1}, "MASTER", 0, 127, 100));
    return layout;
}

} // namespace mnm::plugin::md
