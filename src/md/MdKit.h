// Machinedrum kit sysex (message 0x52: F0 00 20 3C 02 00 52 version revision position ... F7). Layout of the stock
// kit message (versions 1-4), after the position byte:
//   name 16 | track params 16 x 24 (8 synthesis, 8 effects, 8 routing: DIST VOL PAN DEL REV LFOS LFOD LFOM)
//   | levels 16 | 7-bit: machine IDs 16 x u32 BE | 7-bit: LFOs 16 x 36 | reverb 8, delay 8, EQ 8, dynamics 8
//   | 7-bit: trig groups 16, mute groups 16 | checksum, length
// "7-bit" sections use Elektron's packing: each group of up to 7 bytes is preceded by a byte holding their top bits
// (bit 6 = the first byte's bit 7), the grouping restarting at each section.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mnm::md {

struct Kit {
    int position = 0;                                   // 0..63
    std::string name;
    std::array<std::array<uint8_t, 24>, 16> params{};   // per track: synthesis 0-7, effects 8-15, routing 16-23
    std::array<uint8_t, 16> levels{};
    std::array<uint32_t, 16> machines{};                // firmware machine IDs
    std::array<std::array<uint8_t, 8>, 4> masterFx{};   // reverb, delay, EQ, dynamics
};

// Parses one sysex message (F0..F7); false when it is not a stock MD kit message.
bool parseKit(const uint8_t* msg, size_t len, Kit& out);
// Every kit in a .syx file (a dump holds up to 64)
std::vector<Kit> loadKits(const std::filesystem::path& syxPath);

} // namespace mnm::md
