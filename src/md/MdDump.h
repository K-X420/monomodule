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

    // Monomodule's extras: the plugin's own, a real MD has no room for them. Per track and step:
    //   cond    the trig condition, 0 none (conditionName)
    //   micro   a nudge of -23..+23 24ths of a step (early / late)
    //   retrig  0 off, else bits 0-2 the rate (retrigHits) and bits 3-5 the steps it lasts, less one
    // encodePattern leaves them out; they travel in a message of their own (encodePatternExtras). copySteps and
    // clearSteps take them with the trig; a trig taken off clears its own (clearStepExtras).
    uint8_t cond[kTracks][64] = {};
    int8_t micro[kTracks][64] = {};
    uint8_t retrig[kTracks][64] = {};
    bool hasExtras() const;
    void clearStepExtras(int track, int step);
};

// Trig conditions (as Elektron's later machines): 0 none; 1-21 a probability; FILL, !FILL, PRE, !PRE (the track's
// last conditional trig played / didn't), NEI, !NEI (the same of the track before), 1ST, !1ST (the first pass); then
// A:B, the A-th pass of every B (1:2 .. 8:8)
constexpr int kConditions = 65;
enum : uint8_t { kCondFill = 22, kCondNotFill, kCondPre, kCondNotPre, kCondNei, kCondNotNei, kCondFirst, kCondNotFirst, kCondRatio };
std::string conditionName(int cond);               // "" for none
int conditionPercent(int cond);                    // a probability's percent, else -1
bool conditionRatio(int cond, int& a, int& b);     // an A:B condition's A and B
// Retrigs: the hits a step (rate 1..7: 2 3 4 6 8 12 16, i.e. 1/32 .. 1/256) and the steps they last (1..8)
inline int retrigRate(uint8_t r) { return r & 7; }
inline int retrigSteps(uint8_t r) { return ((r >> 3) & 7) + 1; }
inline uint8_t makeRetrig(int rate, int steps) { return rate < 1 ? 0 : uint8_t((rate > 7 ? 7 : rate) | (((steps < 1 ? 1 : steps > 8 ? 8 : steps) - 1) << 3)); }
int retrigHits(uint8_t r);                         // hits a step, 0 off
std::string retrigName(uint8_t r);                 // "1/32", "1/64x2" (two steps), "" off

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

// A global (message 0x50), read for its MIDI side (the message itself stays verbatim): the outputs of the tracks, the
// trig note map (note -> track, 127 = none), the base channel (0..15, 127 = off), the tempo, the program change mode.
// Layout (as the unit sends it): position, 16 raw routing bytes, the key map packed, then raw bytes: base channel, -,
// tempo (BPM x 24, 7-bit high / low), extended mode, clock / transport flags, local, levels and gates (10), program
// change, trig mode.
struct Global {
    int position = 0;
    uint8_t routing[kTracks] = {};
    uint8_t keyMap[128] = {};
    // keyMap: 0..15 a track, 16 + n pattern n, 0x90 START, 0x91 STOP, else none. trigMode = the pattern notes' mode
    // (MainOS 0x20D104): 0 GATE, 1 MOMENTARY (the pattern before comes back at the note-off), 2 QUEUE
    uint8_t baseChannel = 0, programChange = 0, trigMode = 0, flags = 0;
    int tempo = 120 * 24;
    int trackOfNote(int note) const { return note >= 0 && note < 128 && keyMap[note] < kTracks ? keyMap[note] : -1; }
};
bool decodeGlobal(const uint8_t* msg, size_t n, Global& g);

// One sysex message of the file, verbatim, with a link to its decoded form where there is one.
struct Message {
    uint8_t id = 0, version = 0, revision = 0;
    int position = -1;
    std::vector<uint8_t> raw;                          // F0 .. F7
    int kitIndex = -1, patternIndex = -1, songIndex = -1;
    bool damaged = false;                              // a known message that failed its checksum or layout (kept raw only)
    bool extras = false;                               // a pattern's extras (encodeDump writes them after their pattern)
};

struct Dump {
    std::string file;
    std::vector<Message> messages;                     // every message, in file order
    std::vector<Kit> kits;
    std::vector<Pattern> patterns;
    std::vector<Song> songs;
    std::vector<Global> globals;
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
// A pattern's extras as a message of their own (empty when it has none): F0 7D 'M' 'N' 'X' <version 1> <position>
// then an entry per step with any (track, step, cond, micro + 64, retrig) F7. 7D is MIDI's non-commercial ID: a real
// MD ignores the message. parseDump puts them back on the pattern of that position.
std::vector<uint8_t> encodePatternExtras(const Pattern& pat);
bool decodePatternExtras(const uint8_t* msg, size_t n, int& position, Pattern& pat);   // only the extras fields
bool isPatternExtras(const uint8_t* msg, size_t n);
// Re-encodes every decoded kit / pattern / song from its struct (so edits take effect) and copies every other message
// verbatim; for an unedited dump the result is the source file. A pattern's extras follow it (withExtras false: a
// pure MD dump, as one sent to the unit or exported).
std::vector<uint8_t> encodeDump(const Dump& dump, bool withExtras = true);

} // namespace mnm::mddump
