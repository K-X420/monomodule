// Machinedrum OS (.syx) decoder: the same transport, flash container and aPLib sections as the Monomachine OS
// (firmware/Firmware.h), with the Machinedrum's three sections:
//   0  MainOS for the ColdFire CPU, loaded at 0x200000 (control handlers, machine descriptors, tables)
//   1  DSP2 "producer": the 16 machine voices
//   2  DSP1 "mixer": track effects, mix, master effects
// DSP records are the Monomachine format plus a 2-word marker of type 4 after the start marker.
// The OS file is supplied by the user at run time (Elektron_SPS1-1UW_OS1.63.syx).
// Machine structures follow janne808/machinedrum-kit (docs/07-machine-catalogue.md, GPLv3).
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "firmware/Firmware.h"

namespace mnm::md {

constexpr uint32_t kMainOsBase = 0x200000;

// One 86-byte machine descriptor, reached through the ID table.
struct Machine {
    int id = 0;                          // firmware machine ID 0..191 (DSP type = id + 1)
    uint32_t handler = 0;                // ColdFire control handler: int handler(u32* packet, const u16* raw)
    std::string family, suffix;          // "TRX", "BD"
    std::array<std::string, 8> labels;   // knob labels ("PTCH", "DEC", ...), empty when unused
    std::array<uint8_t, 8> defaults{};   // knob defaults 0..127
    std::array<uint8_t, 4> format{};     // display format nibbles
    std::string name() const { return family + "-" + suffix; }
    int dspType() const { return id + 1; }
    // Machines that render a voice from DSP2 alone (no input, MIDI, controller or sample-slot machines)
    bool isSynth() const { return id < 80; }
};

struct Firmware {
    std::vector<uint8_t> mainOs;   // section 0, decompressed (ColdFire address kMainOsBase)
    fw::DspImage voiceDsp;         // section 1 (DSP2)
    fw::DspImage mixDsp;           // section 2 (DSP1)
    std::vector<Machine> machines; // every assigned ID, ascending
    std::filesystem::path source;
    const Machine* byId(int id) const;
};

// Throws fw::FirmwareError when the file is not a Machinedrum OS.
Firmware loadFirmware(const std::filesystem::path& syxPath);

} // namespace mnm::md
