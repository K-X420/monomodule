// Monomodule MD parameters, as the Machinedrum's pages: per track the machine and its eight SYNTHESIS knobs, the eight
// EFFECTS knobs, ROUTING (DIST VOL PAN DEL REV), the kit LEVEL and the output; the four master effects; an output
// volume. Every knob is raw 0..127 as on the hardware (bipolar ones such as PAN shown -64..63). The machine list is the stock synthesis
// machines of OS 1.63 (names and knob labels shown come from the user's OS file at run time).
#pragma once
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamDisplay.h"
#include "MdMachines.h"

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
    // UW sample machines: ROM-01..32 = IDs 128..159 (slots 0..31), ROM-33..48 = IDs 176..191 (slots 48..63)
    {128, "ROM-01"}, {129, "ROM-02"}, {130, "ROM-03"}, {131, "ROM-04"}, {132, "ROM-05"}, {133, "ROM-06"}, {134, "ROM-07"},
    {135, "ROM-08"}, {136, "ROM-09"}, {137, "ROM-10"}, {138, "ROM-11"}, {139, "ROM-12"}, {140, "ROM-13"}, {141, "ROM-14"},
    {142, "ROM-15"}, {143, "ROM-16"}, {144, "ROM-17"}, {145, "ROM-18"}, {146, "ROM-19"}, {147, "ROM-20"}, {148, "ROM-21"},
    {149, "ROM-22"}, {150, "ROM-23"}, {151, "ROM-24"}, {152, "ROM-25"}, {153, "ROM-26"}, {154, "ROM-27"}, {155, "ROM-28"},
    {156, "ROM-29"}, {157, "ROM-30"}, {158, "ROM-31"}, {159, "ROM-32"},
    {176, "ROM-33"}, {177, "ROM-34"}, {178, "ROM-35"}, {179, "ROM-36"}, {180, "ROM-37"}, {181, "ROM-38"}, {182, "ROM-39"},
    {183, "ROM-40"}, {184, "ROM-41"}, {185, "ROM-42"}, {186, "ROM-43"}, {187, "ROM-44"}, {188, "ROM-45"}, {189, "ROM-46"},
    {190, "ROM-47"}, {191, "ROM-48"},
    // RAM machines: R1..R4 record the input and the main mix into slots 32..35, P1..P4 play them
    {160, "RAM-R1"}, {161, "RAM-R2"}, {165, "RAM-R3"}, {166, "RAM-R4"}, {162, "RAM-P1"}, {163, "RAM-P2"}, {167, "RAM-P3"}, {168, "RAM-P4"},
    // No sound: MID-01..16 play MIDI out on channel 1..16; the controller machines turn other parameters (CTR-AL every
    // track's, CTR-8P eight assigned ones, CTR-RE/GB/EQ/DX the master delay / reverb / EQ / dynamix)
    {96, "MID-01"}, {97, "MID-02"}, {98, "MID-03"}, {99, "MID-04"}, {100, "MID-05"}, {101, "MID-06"}, {102, "MID-07"},
    {103, "MID-08"}, {104, "MID-09"}, {105, "MID-10"}, {106, "MID-11"}, {107, "MID-12"}, {108, "MID-13"}, {109, "MID-14"},
    {110, "MID-15"}, {111, "MID-16"},
    {112, "CTR-AL"}, {113, "CTR-8P"}, {120, "CTR-RE"}, {121, "CTR-GB"}, {122, "CTR-EQ"}, {123, "CTR-DX"},
};
using mnm::md::isMidMachine;
using mnm::md::isCtrMachine;
using mnm::md::kCtrAll;
using mnm::md::kCtr8p;
// CTR-RE (Rhythm Echo) = the delay, CTR-GB (Gate Box) = the reverb, CTR-EQ, CTR-DX: index into the master effects
using mnm::md::ctrMasterFx;
inline bool isRomMachine(int id) { return (id >= 128 && id <= 159) || (id >= 176 && id <= 191); }
inline int romSlotOf(int id) { return id - 128; }   // the UW slot (0..31, 48..63)
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

// The knob labels and defaults of every machine, from the OS file (empty until one is loaded)
struct MachineKnobInfo {
    std::array<std::array<juce::String, 8>, 256> labels;
    std::array<std::array<uint8_t, 8>, 256> defaults{};
    std::array<bool, 256> known{};
};

// One synthesis knob of a track: a raw 0..127 kit byte whose name and default follow the track's machine (read live
// from the machine parameter), as Monomodule's SYN knobs. Hosts that cache names are told to re-read them when the
// machine changes (AudioProcessor::updateHostDisplay).
class MdSynParam : public juce::AudioParameterInt {
public:
    MdSynParam(const juce::String& id, int k, int def)
        : juce::AudioParameterInt(juce::ParameterID{id, 1}, "SYN " + juce::String(k + 1), 0, 127, def), m_k(k), m_default(def) {}
    void setSources(std::atomic<float>* machineIndex, const MachineKnobInfo* info, const int* idOfIndex)
    {
        m_machine = machineIndex; m_info = info; m_idOf = idOfIndex;
    }
    juce::String getName(int maximumStringLength) const override
    {
        const int id = machineIdNow();
        const juce::String label = id >= 0 && m_info->known[size_t(id)] ? m_info->labels[size_t(id)][size_t(m_k)] : juce::String();
        return (label.isNotEmpty() ? label : "SYN " + juce::String(m_k + 1)).substring(0, maximumStringLength);
    }
    float getDefaultValue() const override
    {
        const int id = machineIdNow();
        return convertTo0to1(float(id >= 0 && m_info->known[size_t(id)] ? int(m_info->defaults[size_t(id)][size_t(m_k)]) : m_default));
    }
private:
    int machineIdNow() const
    {
        if (!m_machine || !m_info || !m_idOf) return -1;
        return m_idOf[std::max(0, int(std::lround(m_machine->load())))];
    }
    int m_k, m_default;
    std::atomic<float>* m_machine = nullptr;
    const MachineKnobInfo* m_info = nullptr;
    const int* m_idOf = nullptr;
};
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
inline juce::String muteId(int t) { return tp(t) + "mute"; }   // a muted track ignores its trigs
// LFO page: destination, shapes, type (the kit's LFO struct) and SPD DEP MIX (routing bytes 21-23)
constexpr const char* kLfoLabels[8] = {"TRK", "PARAM", "SHP1", "SHP2", "TYPE", "SPD", "DEP", "MIX"};
constexpr const char* kLfoTypes[3] = {"FREE", "TRIG", "HOLD"};
constexpr const char* kLfoParamNames[24] = {"SYN1", "SYN2", "SYN3", "SYN4", "SYN5", "SYN6", "SYN7", "SYN8",
                                            "AMD", "AMF", "EQF", "EQG", "FLTF", "FLTW", "FLTQ", "SRR",
                                            "DIST", "VOL", "PAN", "DEL", "REV", "LFOS", "LFOD", "LFOM"};
inline juce::String lfoId(int t, int k) { static const char* n[8] = {"lfotrk", "lfopar", "lfosh1", "lfosh2", "lfotyp", "lfospd", "lfodep", "lfomix"}; return tp(t) + n[k]; }
// A track parameter by the Machinedrum's numbering: 0-7 SYNTHESIS, 8-15 EFFECTS, 16-20 DIST VOL PAN DEL REV, 21-23 LFOS LFOD LFOM
inline juce::String trackParamId(int t, int p)
{
    if (p < 8) return knobId(t, p);
    if (p < 16) return fxId(t, p - 8);
    switch (p) { case 16: return distId(t); case 17: return volId(t); case 18: return panId(t); case 19: return delId(t); case 20: return revId(t); default: break; }
    return lfoId(t, 5 + (p - 21));
}
inline juce::String masterFxId(int fx, int k) { static const char* p[4] = {"rv", "dl", "eq", "dx"}; return juce::String(p[fx]) + juce::String(k + 1); }
inline juce::String masterId() { return "master"; }
// MIDI velocity, as the OS's two trig paths (MainOS 0x20CD76): VOLUME = the velocity scales the track volume;
// ACCENT = velocity >= 112 is an accented trig (+2 x ACCENT in the volume law), others play at normal volume
inline juce::String velModeId() { return "velmode"; }
inline juce::String accentId() { return "accent"; }
// What the plugin's output buses carry. HARDWARE: Main A/B, Out C/D, Out E/F = the Machinedrum's six outputs (each
// track where its ROUTE puts it). PER TRACK: every track whose "Track n" bus the host has enabled plays there (after
// its track effects, volume and pan, level-matched to the main mix) and leaves the hardware outputs, its reverb and
// delay sends with it, as a track on an individual output does on the hardware; the rest stay on the hardware outputs.
enum class OutputMode : int { Hardware = 0, Tracks = 1 };
inline juce::String outputModeId() { return "outputs"; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    juce::StringArray names;
    for (const auto& m : kMachines) names.add(m.name);
    for (int t = 0; t < kTracks; ++t) {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>(tp(t), "T" + juce::String(t + 1), " ");
        g->addChild(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{machineId(t), 1}, "MACHINE", names, machineIndexOf(kDefaultKit[t].id)));
        for (int k = 0; k < 8; ++k)
            g->addChild(std::make_unique<MdSynParam>(knobId(t, k), k, kDefaultKit[t].knobs[size_t(k)]));
        for (int k = 0; k < 8; ++k)
            g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{fxId(t, k), 1}, kFxLabels[k], 0, 127, kFxDefaults[k]));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{distId(t), 1}, "DIST", 0, 127, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{volId(t), 1}, "VOL", 0, 127, 100));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{panId(t), 3}, "PAN", 0, 127, 64, hwDisplay(true)));   // raw, shown -64..63
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{delId(t), 1}, "DEL", 0, 127, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{revId(t), 1}, "REV", 0, 127, 0));
        g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{levelId(t), 2}, "LEVEL", 0, 127, 100));   // 100 as Monomodule: headroom above
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
        g->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{muteId(t), 1}, "MUTE", false));
        layout.add(std::move(g));
    }
    for (int fx = 0; fx < 4; ++fx) {
        auto g = std::make_unique<juce::AudioProcessorParameterGroup>(juce::String(kMasterFxNames[fx]).toLowerCase(), kMasterFxNames[fx], " ");
        for (int k = 0; k < 8; ++k)
            g->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{masterFxId(fx, k), 1}, kMasterFxLabels[fx][k], 0, 127, kMasterFxDefaults[fx][k]));
        layout.add(std::move(g));
    }
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{masterId(), 1}, "VOLUME", 0, 127, 80));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{velModeId(), 1}, "VEL", juce::StringArray{"VOLUME", "ACCENT"}, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{accentId(), 1}, "ACCENT", 0, 127, 64));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{outputModeId(), 1}, "OUTPUTS", juce::StringArray{"Hardware", "Per Track"}, 0));
    return layout;
}

} // namespace mnm::plugin::md
