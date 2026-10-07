// Monomodule MD's editor, the parts its files share: the parameter pages' tables, the knob tooltips, small helpers
#pragma once
#include "MdEditor.h"
#include "MdMachineText.h"
#include "MdParamWords.h"
#include <map>

namespace mnm::plugin::md {

using one::kScale;
using one::LcdCanvas;
using one::KnobPage;
namespace lcd = one::lcd;
using one::drawLcdText;
using one::wrapLcdText;

namespace editor_detail {

// "TRX-BD" -> "TRX" / "BD"; "P-I-BD" -> "P-I" / "BD"; "GND---" -> "GND" / "---"
inline juce::String familyOf(int index)
{
    const juce::String n(kMachines[index].name);
    return n.startsWith("P-I-") ? juce::String("P-I") : n.substring(0, 3);
}
inline juce::String shortOf(int index)
{
    const juce::String n(kMachines[index].name);
    const auto s = n.substring(familyOf(index).length() + 1);
    return s.containsOnly("-") ? juce::String("---") : s;   // GND---: the empty machine
}

inline void dottedFrame(LcdCanvas& cv, int x, int y, int w, int h)
{
    cv.dotsH(x, x + w - 1, y); cv.dotsH(x, x + w - 1, y + h - 1);
    cv.dotsV(x, y, y + h - 1); cv.dotsV(x + w - 1, y, y + h - 1);
}

constexpr spec::Param numeric(const char* label, int def) { return {label, spec::Display::Numeric, false, uint8_t(def), 127, 128, spec::Icons::None, nullptr}; }
constexpr spec::Param bipolar(const char* label, int def = 64) { return {label, spec::Display::Bipolar, false, uint8_t(def), 127, 128, spec::Icons::None, nullptr}; }
constexpr spec::Param readout(const char* label, const char* const* names, int n, int def = 0)
{
    return {label, spec::Display::Readout, false, uint8_t(def), uint8_t(n - 1), uint8_t(n), spec::Icons::Switch, names};
}
// a two-state switch: a click flips it (the Toggle icon, as Monomodule One's on / off parameters)
constexpr spec::Param toggle(const char* label, const char* const* names, int def = 0)
{
    return {label, spec::Display::Readout, false, uint8_t(def), 1, 2, spec::Icons::Toggle, names};
}
constexpr spec::Param blank() { return {"", spec::Display::Blank, false, 0, 127, 128, spec::Icons::None, nullptr}; }

inline constexpr const char* kTrackNames[kTracks] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11", "T12", "T13", "T14", "T15", "T16"};
// The LFO shapes (manual: triangle, saw, square, linear decay, exponential decay, random; SHP2 = the inverted ones)
inline constexpr const char* kShape1Names[6] = {"TRI", "SAW", "SQR", "LIN", "EXP", "RND"};
inline constexpr const char* kShape2Names[6] = {"ITRI", "ISAW", "ISQR", "ILIN", "IEXP", "IRND"};
constexpr spec::Param withIcons(spec::Param p, spec::Icons i) { p.icons = i; return p; }
inline constexpr const char* kVelNames[2] = {"VOLUME", "ACCENT"};

inline const spec::Param kFxParams[8] = {numeric("AMD", 0), numeric("AMF", 0), numeric("EQF", 64), bipolar("EQG"),
                                  numeric("FLTF", 0), numeric("FLTW", 127), numeric("FLTQ", 0), numeric("SRR", 0)};
inline const spec::Param kRoutingParams[8] = {numeric("DIST", 0), numeric("VOL", 100), bipolar("PAN"), numeric("DEL", 0),
                                       numeric("REV", 0), readout("OUT", kRouteNames, kNumRoutes, kNumRoutes - 1), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
inline const spec::Param kLfoParams[8] = {readout("TRK", kTrackNames, kTracks), withIcons(readout("PARAM", kLfoParamNames, 24), spec::Icons::MdLfoParam),
                                   withIcons(readout("SHP1", kShape1Names, 6), spec::Icons::MdWave1),
                                   withIcons(readout("SHP2", kShape2Names, 6), spec::Icons::MdWave2), readout("TYPE", kLfoTypes, 3), numeric("SPD", 64), numeric("DEP", 0), numeric("MIX", 0)};
inline constexpr const char* kSeqNames[2] = {"OFF", "ON"};
inline constexpr const char* kModeNames[2] = {"PATTERN", "SONG"};
inline constexpr const char* kExtNames[2] = {"CLASSIC", "EXTENDED"};
inline const spec::Param kOutParams[8] = {numeric("VOL", 80), toggle("VEL", kVelNames), numeric("ACNT", 64), toggle("EXT", kExtNames, 1),
                                   toggle("SEQ", kSeqNames, 1), readout("PTN", kPatternNames, 128), toggle("MODE", kModeNames), readout("SONG", kSongNames, 32)};
inline constexpr const char* kMasterTabs[4] = {"REV", "DEL", "EQ", "DYN"};

// The pages of the MID and CTR machines (their parameters 8..23 are not track effects / routing)
inline const char* const* nameTable(int kind)   // 0 notes C-2..G8, 1 CC number (OFF, then 1 = CC 0), 2 program (OFF, 1..),
{                                        // 3 track T1..T16, 4 parameter (the 24 track parameters)
    static const auto tables = [] {
        std::array<std::array<std::string, 128>, 5> s;
        static const char* const notes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        for (int v = 0; v < 128; ++v) {
            s[0][size_t(v)] = std::string(notes[v % 12]) + std::to_string(v / 12 - 2);
            s[1][size_t(v)] = v == 0 ? "OFF" : std::to_string(v == 1 ? 0 : v);
            s[2][size_t(v)] = v == 0 ? "OFF" : std::to_string(v - 1);
            s[3][size_t(v)] = "T" + std::to_string(std::min(v, 15) + 1);
            s[4][size_t(v)] = kLfoParamNames[std::min(v, 23)];
        }
        return s;
    }();
    static const auto ptrs = [] {
        std::array<std::array<const char*, 128>, 5> p{};
        for (size_t k = 0; k < 5; ++k) for (size_t v = 0; v < 128; ++v) p[k][v] = tables[k][v].c_str();
        return p;
    }();
    return ptrs[size_t(kind)].data();
}
inline spec::Param named(const char* label, int kind, int def = 0) { return readout(label, nameTable(kind), 128, def); }
inline const spec::Param kMidFx[8] = {named("CC1D", 1), numeric("CC1V", 0), named("CC2D", 1), numeric("CC2V", 0),
                               named("CC3D", 1), numeric("CC3V", 0), named("CC4D", 1), numeric("CC4V", 0)};
inline const spec::Param kMidRouting[8] = {named("CC5D", 1), numeric("CC5V", 0), named("CC6D", 1), numeric("CC6V", 0),
                                    named("PCHG", 2), blank(), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
inline const spec::Param kCtr8pFx[8] = {named("P1T", 3), named("P1P", 4), named("P2T", 3), named("P2P", 4),
                                 named("P3T", 3), named("P3P", 4), named("P4T", 3), named("P4P", 4)};
inline const spec::Param kCtr8pRouting[8] = {named("P5T", 3), named("P5P", 4), named("P6T", 3), named("P6P", 4), named("P7T", 3), blank(), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
inline const spec::Param kCtr8pLfo[8] = {blank(), blank(), blank(), blank(), blank(), named("P7P", 4), named("P8T", 3), named("P8P", 4)};
inline const spec::Param kCtrAllRouting[8] = {numeric("DIST", 0), numeric("VOL", 100), bipolar("PAN"), numeric("DEL", 0),
                                       numeric("REV", 0), blank(), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
inline const spec::Param kGroupsOnly[8] = {blank(), blank(), blank(), blank(), blank(), blank(), readout("TRGG", kGroupNames, 17), readout("MUTG", kGroupNames, 17)};
inline const spec::Param kCtrAllLfo[8] = {blank(), blank(), blank(), blank(), blank(), numeric("SPD", 64), numeric("DEP", 0), numeric("MIX", 0)};
inline const spec::Param kBlankPage[8] = {blank(), blank(), blank(), blank(), blank(), blank(), blank(), blank()};

inline const spec::Param& masterParam(int fx, int k)
{
    static const auto table = [] {
        std::array<std::array<spec::Param, 8>, 4> t{};
        for (int f = 0; f < 4; ++f)
            for (int i = 0; i < 8; ++i) {
                const bool gain = f == 2 && (i == 1 || i == 3 || i == 5 || i == 7);   // EQ: LG HG PG GAIN are centred
                t[size_t(f)][size_t(i)] = gain ? bipolar(kMasterFxLabels[f][i], kMasterFxDefaults[f][i]) : numeric(kMasterFxLabels[f][i], kMasterFxDefaults[f][i]);
            }
        return t;
    }();
    return table[size_t(fx)][size_t(k)];
}

// What each knob does (the manual's words, short), by its label; the machine's own SYNTHESIS knobs get a general line
inline juce::String knobTip(const juce::String& label, int page, const juce::String& family)
{
    static const std::map<juce::String, const char*> words = {
        {"AMD", "Amplitude modulation depth"}, {"AMF", "Amplitude modulation rate"}, {"EQF", "EQ frequency"}, {"EQG", "EQ gain (centre = flat)"},
        {"FLTF", "Filter base frequency"}, {"FLTW", "Filter width (127 = open)"}, {"FLTQ", "Filter resonance"}, {"SRR", "Sample rate reduction"},
        {"DIST", "Distortion"}, {"VOL", "Track volume"}, {"PAN", "Pan"}, {"DEL", "Send to the master delay"}, {"REV", "Send to the master reverb"},
        {"OUT", "Output: MAIN, or A-F (no master effects there)"}, {"TRGG", "Trig group: a trig here also trigs that track"},
        {"MUTG", "Mute group: a trig here silences that track"}, {"TRK", "The track the LFO moves"}, {"PARAM", "The parameter the LFO moves"},
        {"SHP1", "LFO shape"}, {"SHP2", "Second LFO shape (inverted)"}, {"TYPE", "FREE runs on, TRIG restarts each trig, HOLD holds each trig"},
        {"SPD", "LFO speed (tempo synced)"}, {"DEP", "LFO depth"}, {"MIX", "Blend SHP1 to SHP2"}, {"DVOL", "Delay into the reverb"},
        {"PRED", "Pre-delay"}, {"DEC", "Decay"}, {"DAMP", "Damping"}, {"HP", "High-pass"}, {"LP", "Low-pass"}, {"GATE", "Gate (127 = open)"},
        {"LEV", "Output level"}, {"TIME", "Delay time (tempo synced)"}, {"FB", "Feedback"}, {"MOD", "Modulation depth"}, {"MFRQ", "Modulation rate"},
    };
    static const std::map<juce::String, const char*> out = {
        {"VOL", "Plugin output level"}, {"VEL", "Note velocity: VOLUME or ACCENT"}, {"EXT", "EXTENDED plays locks and pattern kits; CLASSIC plays neither"}, {"ACNT", "Accent amount for played notes"},
        {"SEQ", "Sequencer on / off"}, {"PTN", "The pattern"}, {"MODE", "PATTERN or SONG"}, {"SONG", "The song SONG mode plays"},
        {"LEN", "Pattern length"}, {"SPD", "Pattern speed"}, {"SWNG", "Swing amount"}, {"ACC", "Accent amount"}, {"KIT", "The pattern's kit"},
        {"GRID", "Keys as steps (G)"}, {"PAGE", "The page GRID shows"},
    };
    if (page == 0)
        if (const char* w = text::machineWords(family.toRawUTF8(), label.toRawUTF8())) return w;
    if (page >= 5)
        if (const auto it = out.find(label); it != out.end()) return it->second;
    if (const auto it = words.find(label); it != words.end()) return it->second;
    return label;
}

} // namespace editor_detail

using namespace editor_detail;

} // namespace mnm::plugin::md
