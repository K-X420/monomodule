// Machinedrum sysex codec (src/md/MdDump): every message type round-trips byte for byte, edits re-encode in the
// hardware's format, and a real dump ($MD_DUMP, a .syx from a Machinedrum) re-encodes to the file itself.
#include "mini_test.h"
#include "MdDump.h"
#include <fstream>
#include <iterator>

using namespace mnm::mddump;

namespace {
std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}
Kit sampleKit()
{
    Kit k;
    k.position = 5;
    const char* name = "TEST KIT";
    for (int i = 0; name[i]; ++i) k.nameRaw[i] = uint8_t(name[i]);
    for (int t = 0; t < kTracks; ++t) {
        for (int p = 0; p < 24; ++p) k.params[t][p] = uint8_t((t * 7 + p * 3) & 0x7F);
        k.levels[t] = uint8_t(100 + t);
        k.models[t] = uint32_t(16 + t);
        for (int b = 0; b < 36; ++b) k.lfos[t][b] = uint8_t((t * 31 + b * 13) & 0xFF);   // state bytes use bit 7
        k.trigGroups[t] = 127; k.muteGroups[t] = 127;
    }
    for (int i = 0; i < 8; ++i) { k.reverb[i] = uint8_t(i); k.delay[i] = uint8_t(10 + i); k.eq[i] = 64; k.dynamics[i] = uint8_t(i * 9); }
    return k;
}
} // namespace

TEST_CASE(mddump_kit_roundtrip)
{
    const auto k = sampleKit();
    const auto bytes = encodeKit(k);
    Kit back;
    CHECK(decodeKit(bytes.data(), bytes.size(), back));
    CHECK_EQ(back.name, std::string("TEST KIT"));
    CHECK_EQ(back.position, 5);
    CHECK_EQ(int(back.models[3]), 19);
    CHECK_EQ(int(back.lfos[7][20]), int(k.lfos[7][20]));
    CHECK(encodeKit(back) == bytes);
    auto bad = bytes;
    bad[20] ^= 1;   // a damaged byte fails the checksum
    CHECK(!decodeKit(bad.data(), bad.size(), back));
}

TEST_CASE(mddump_pattern_roundtrip)
{
    for (const bool extended : {false, true}) {
        Pattern p;
        p.position = 17; p.extended = extended; p.length = extended ? 64 : 32;
        p.trigs[0] = extended ? 0x8000000100010001ull : 0x10001ull;
        p.lockMasks[2] = 0x5; p.numLockedRows = 2;
        p.locks[0][3] = 99; p.locks[1][40] = 12;
        p.accent = extended ? 0x100000000ull : 0x2; p.swingAmount = 7; p.kit = 9;
        p.accentPerTrack[4] = 0xF0;
        const auto bytes = encodePattern(p);
        Pattern back;
        CHECK(decodePattern(bytes.data(), bytes.size(), back));
        CHECK_EQ(back.extended, extended);
        CHECK(back.trigs[0] == p.trigs[0]);
        CHECK_EQ(back.trigCount(0), extended ? 4 : 2);
        CHECK_EQ(back.lockRow(2, 2), 1);
        CHECK_EQ(int(back.locks[1][40]), extended ? 12 : 0);
        CHECK(encodePattern(back) == bytes);
    }
    CHECK_EQ(patternSlotName(0), std::string("A01"));
    CHECK_EQ(patternSlotName(127), std::string("H16"));
}

TEST_CASE(mddump_song_roundtrip)
{
    Song s;
    s.position = 3;
    s.nameRaw[0] = 'S'; s.nameRaw[1] = 'G';
    for (int i = 0; i < 5; ++i) { SongRow r; r.bytes[0] = uint8_t(i); r.bytes[1] = uint8_t(i * 2); r.bytes[6] = 0x83; s.rows.push_back(r); }
    const auto bytes = encodeSong(s);
    Song back;
    CHECK(decodeSong(bytes.data(), bytes.size(), back));
    CHECK_EQ(back.rows.size(), size_t(5));
    CHECK_EQ(back.name, std::string("SG"));
    CHECK(encodeSong(back) == bytes);
}

TEST_CASE(mddump_real_dump)
{
    const char* env = std::getenv("MD_DUMP");
    if (!env || !*env || !std::filesystem::exists(env)) throw mt::Skip{"no Machinedrum dump (set MD_DUMP=<file.syx>)"};
    const auto data = readFile(env);
    CHECK(isMachinedrumSysex(data.data(), data.size()));
    const auto d = parseDump(data.data(), data.size(), "dump.syx");
    CHECK_MSG(d.numDamaged == 0, d.numDamaged << " damaged messages");
    CHECK(!d.kits.empty());
    CHECK_MSG(encodeDump(d) == data, "re-encoded dump differs from the file (" << d.kits.size() << " kits, " << d.patterns.size()
              << " patterns, " << d.songs.size() << " songs, " << d.numGlobals << " globals, " << d.numUnknown << " unknown)");
    for (const auto& m : d.messages) {   // and every decoded message on its own
        std::vector<uint8_t> e;
        if (m.kitIndex >= 0) e = encodeKit(d.kits[size_t(m.kitIndex)]);
        else if (m.patternIndex >= 0) e = encodePattern(d.patterns[size_t(m.patternIndex)]);
        else if (m.songIndex >= 0) e = encodeSong(d.songs[size_t(m.songIndex)]);
        else continue;
        CHECK_MSG(e == m.raw, "message id " << int(m.id) << " position " << m.position << " differs");
    }
    std::printf("    MD dump: %zu kits, %zu patterns, %zu songs, %d globals, %d unknown\n", d.kits.size(), d.patterns.size(), d.songs.size(), d.numGlobals, d.numUnknown);
}
