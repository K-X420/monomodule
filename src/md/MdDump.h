#pragma once
// Lossless codec for Machinedrum sysex dumps: kits, patterns, songs and globals, decoded field by field, every other
// message kept verbatim. The wire format is the one documented by the open-source MCL project (github.com/jmamma/MCL,
// MDMessages / MDPattern); no MCL code is used.
// Framing: F0 00 20 3C 02 00 <id> <version> <revision> <position> <sections...> <cksumHi> <cksumLo> <lenHi> <lenLo> F7
// The body is a fixed sequence of sections per message type, each either raw (7-bit bytes as they are) or packed
// (Elektron's 7-bit packing: each group of up to 7 bytes is preceded by a byte holding their top bits, bit 6 = the
// first byte's; the grouping restarts with every section). Checksum = 14-bit sum of bytes [9 .. N-6], length = N - 10.
// Decoding keeps every byte of every section, so encoding an unedited message reproduces it byte for byte.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mnm::mddump {

constexpr uint8_t kGlobalId = 0x50, kKitId = 0x52, kPatternId = 0x67, kSongId = 0x69;
constexpr int kTracks = 16, kKitSlots = 64, kPatternSlots = 128, kSongSlots = 32, kGlobalSlots = 8;

// A kit (message 0x52, stock versions 1-4): the 16 tracks' machines and parameters, levels, LFOs, master effects.
struct Kit {
    int position = 0;                                  // 0..63
    uint8_t version = 4, revision = 1;
    uint8_t nameRaw[16] = {};                          // as on the wire (zero padded)
    std::string name;                                  // printable part of nameRaw
    uint8_t params[kTracks][24] = {};                  // 8 synthesis, 8 effects, DIST VOL PAN DEL REV, LFO SPD DEP MIX
    uint8_t levels[kTracks] = {};
    uint32_t models[kTracks] = {};                     // machine ids (bits beyond the id are flags: 0x200FF is the id)
    uint8_t lfos[kTracks][36] = {};                    // dest track, dest param, shape 1, shape 2, type, 31 bytes of state
    uint8_t reverb[8] = {}, delay[8] = {}, eq[8] = {}, dynamics[8] = {};
    uint8_t trigGroups[kTracks] = {}, muteGroups[kTracks] = {};   // 127 / 255 = off
    int model(int t) const { return int(models[t] & 0xFF); }      // the firmware machine id (0 = GND---)
    bool isEmptySlot() const;                          // no name and every track GND---
};

// A pattern (message 0x67): 32 steps, or 64 in the extended form. Bit j of a step mask = step j.
struct Pattern {
    int position = 0;                                  // 0..127 = A01..H16
    uint8_t version = 3, revision = 1;
    bool extended = false;                             // carries steps 33-64
    uint64_t trigs[kTracks] = {};
    uint32_t lockMasks[kTracks] = {};                  // bit j = param j of the track is locked
    uint64_t accent = 0, slide = 0, swing = 0;         // global step masks
    uint32_t swingAmount = 0;                          // (percent - 50) x 16384 / 50 (0 = 50%, no swing)
    uint8_t accentAmount = 0, length = 16, doubleTempo = 0, scale = 0, kit = 0, numLockedRows = 0;
    uint8_t locks[64][64] = {};                        // lock rows (row order = tracks, then params, of lockMasks) x steps
    uint32_t accentEditAll = 0, slideEditAll = 0, swingEditAll = 0;
    uint64_t accentPerTrack[kTracks] = {}, slidePerTrack[kTracks] = {}, swingPerTrack[kTracks] = {};
    int swingPercent() const { return 50 + int((uint64_t(swingAmount) * 50 + 8192) / 16384); }
    int trigCount(int track) const;                    // within the length
    bool empty() const;                                // no trig on any track within the length
    // The lock row of (track, param), -1 when that param is not locked
    int lockRow(int track, int param) const;
    // Editing (as the unit keeps them): a lock row per locked (track, param), in track then param order, 64 at most.
    // setLock puts a value (0..127) on a step, inserting the row if needed (false: all 64 rows in use); clearLock
    // takes it off and drops a row left empty. clearStepLocks: every lock of the track on that step (its trig gone).
    bool setLock(int track, int param, int step, int value);
    void clearLock(int track, int param, int step);
    void clearStepLocks(int track, int step);
    // Copy / clear across steps (pages, tracks): steps [from, from + count) of src's track `track` (-1: every track,
    // each onto itself) onto [to, to + count) of dstTrack here: the trigs, the accent / slide / swing marks (the
    // all-tracks marks too when every track is copied) and the locks. src must be another object than this one.
    void copySteps(const Pattern& src, int from, int to, int count, int track, int dstTrack);
    void clearSteps(int from, int count, int track);   // track -1: every track and the all-tracks marks
};

struct SongRow {
    uint8_t bytes[10] = {};                            // pattern, kit, loops, jump, mutes (2), tempo (2), start, end
    int pattern() const { return bytes[0]; }
    int kit() const { return bytes[1]; }
};

struct Song {
    int position = 0;                                  // 0..31
    uint8_t version = 2, revision = 2;
    uint8_t nameRaw[16] = {};
    std::string name;
    std::vector<SongRow> rows;
};

// One sysex message of the file, verbatim, with a link to its decoded form where there is one.
struct Message {
    uint8_t id = 0, version = 0, revision = 0;
    int position = -1;
    std::vector<uint8_t> raw;                          // F0 .. F7
    int kitIndex = -1, patternIndex = -1, songIndex = -1;
    bool damaged = false;                              // a known message that failed its checksum or layout (kept raw only)
};

struct Dump {
    std::string file;
    std::vector<Message> messages;                     // every message, in file order
    std::vector<Kit> kits;
    std::vector<Pattern> patterns;
    std::vector<Song> songs;
    int numGlobals = 0, numUnknown = 0, numDamaged = 0;
    const Kit* kitAt(int position) const { for (const auto& k : kits) if (k.position == position) return &k; return nullptr; }
    const Pattern* patternAt(int position) const { for (const auto& p : patterns) if (p.position == position) return &p; return nullptr; }
};

std::string patternSlotName(int position);             // 0 -> "A01" ... 127 -> "H16"

// Is this .syx data Machinedrum sysex (its first Elektron message carries the MD device byte)?
bool isMachinedrumSysex(const uint8_t* data, size_t size);
// Parses a whole .syx file. Never throws; malformed messages are kept raw and counted.
Dump parseDump(const uint8_t* data, size_t size, const std::string& filename);

bool decodeKit(const uint8_t* msg, size_t n, Kit& kit);
bool decodePattern(const uint8_t* msg, size_t n, Pattern& pat);
bool decodeSong(const uint8_t* msg, size_t n, Song& song);

std::vector<uint8_t> encodeKit(const Kit& kit);
std::vector<uint8_t> encodePattern(const Pattern& pat);
std::vector<uint8_t> encodeSong(const Song& song);
// Re-encodes every decoded kit / pattern / song from its struct (so edits take effect) and copies every other message
// verbatim; for an unedited dump the result is the source file.
std::vector<uint8_t> encodeDump(const Dump& dump);

} // namespace mnm::mddump
