// Monomodule MD: the machine picker's words and logos. Each family has a logo, shown in the machine
// block and the picker's column header, and a blurb; a hovered machine shows its own description there. ASCII caps (the LCD faces have no lowercase), words of at
// most 8 characters (a column line holds about 8 at the picker's text size). Names follow the Machinedrum manual's
// machine reference (rev J, OS 1.53: E12-BC "BONGO CONGO", P-I-ML "METALLICA", INP-EA/EB play the input as a drum with
// envelopes, INP-FA/FB are filter followers); family words from its intros (EFM = Elektron's "Enhanced Feedback
// Modulation", P-I = "physically informed", TRX "inspired by classic analogue drum machine synthesis").
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>
#include "MdLogos.h"

namespace mnm::plugin::md::text {

struct Family { const char* name; const char* blurb; };
inline constexpr Family kFamilies[] = {
    {"GND", "BASIC TONES AND NOISE"},
    {"TRX", "ANALOG STYLE DRUMS"},
    {"EFM", "ENHANCED FEEDBACK FM"},
    {"E12", "12-BIT SAMPLED DRUMS"},
    {"P-I", "PHYSICAL MODELS OF DRUMS"},
    {"INP", "AUDIO INPUT AS A SOUND"},
    {"ROM", "YOUR OWN SAMPLES, LOADED INTO 48 SLOTS"},
    {"RAM", "RECORD AND PLAY LIVE"},
    {"MID", "PLAY SYNTHS OVER MIDI"},
    {"CTR", "CONTROL OTHER TRACKS"},
};
inline const char* familyBlurb(const char* family)
{
    for (const auto& f : kFamilies) if (std::strcmp(f.name, family) == 0) return f.blurb;
    return "";
}

struct Machine { int id; const char* text; };
inline constexpr Machine kMachines[] = {
    {0, "EMPTY TRACK"}, {1, "SINE WAVE"}, {2, "WHITE NOISE"}, {3, "IMPULSE"},
    {16, "ANALOG BASS DRUM"}, {17, "ANALOG SNARE"}, {18, "ANALOG TOM"}, {19, "ANALOG CLAP"}, {20, "ANALOG RIMSHOT"},
    {21, "ANALOG COWBELL"}, {22, "CLOSED HI-HAT"}, {23, "OPEN HI-HAT"}, {24, "ANALOG CYMBAL"}, {25, "MARACAS"},
    {26, "CLAVES"}, {27, "CONGAS"}, {28, "SECOND BASS DRUM"},
    {32, "FM BASS DRUM"}, {33, "FM SNARE"}, {34, "FM TOM"}, {35, "FM CLAP"}, {36, "FM RIMSHOT"}, {37, "FM COWBELL"},
    {38, "FM HI-HAT"}, {39, "FM CYMBAL"},
    {48, "SAMPLED BASS DRUM"}, {49, "SAMPLED SNARE"}, {50, "SAMPLED HIGH TOM"}, {51, "SAMPLED LOW TOM"},
    {52, "SAMPLED CLAP"}, {53, "SAMPLED RIMSHOT"}, {54, "SAMPLED COWBELL"}, {55, "SAMPLED CLOSED HAT"},
    {56, "SAMPLED OPEN HAT"}, {57, "SAMPLED RIDE"}, {58, "SAMPLED CRASH"}, {59, "SAMPLED BRUSHED SNARE"},
    {60, "SAMPLED TAMBOURINE"}, {61, "SAMPLED TRIANGLE"}, {62, "SAMPLED SHAKER"}, {63, "SAMPLED BONGO / CONGA"},
    {64, "MODELLED BASS DRUM"}, {65, "MODELLED SNARE"}, {66, "MODELLED TOM"}, {67, "MODELLED METALLIC STRIKE"},
    {68, "MODELLED MARACAS"}, {69, "MODELLED RIMSHOT"}, {70, "MODELLED RIDE"}, {71, "MODELLED CRASH"},
    {72, "MODELLED HI-HAT"},
    {80, "GATE ON INPUT A"}, {81, "GATE ON INPUT B"}, {82, "FILTER FOLLOWER, INPUT A"}, {83, "FILTER FOLLOWER, INPUT B"},
    {84, "INPUT A AS A DRUM"}, {85, "INPUT B AS A DRUM"},
    {160, "RECORD TO SLOT 1"}, {161, "RECORD TO SLOT 2"}, {165, "RECORD TO SLOT 3"}, {166, "RECORD TO SLOT 4"},
    {162, "PLAY SLOT 1"}, {163, "PLAY SLOT 2"}, {167, "PLAY SLOT 3"}, {168, "PLAY SLOT 4"},
    {112, "TURNS ALL TRACKS"}, {113, "8 ASSIGNED PARAMS"}, {120, "MASTER DELAY"}, {121, "MASTER REVERB"},
    {122, "MASTER EQ"}, {123, "MASTER DYNAMIX"},
};
// MID-01..16 and ROM slots are described by the caller (channel, sample name)
inline const char* machineText(int id)
{
    for (const auto& m : kMachines) if (m.id == id) return m.text;
    return nullptr;
}

// The Machinedrum's own family badges: five boxed 23x13 icons in the OS (E-12, EFM, P-I, TRX, UW), read from the
// user's OS file when it loads (nothing of Elektron's is in this source). Icon descriptors as the Monomachine's
// (20 bytes of big-endian u32: w, h, n, ->pixels, ->mask; pixels = w columns of u32, row r = bit 32-h+r), found as
// five such descriptors back to back. ROM and RAM (the UW machines) show UW.
struct RomBadges {
    std::array<LogoArt, 7> framed{}, bare{};   // E12 EFM P-I TRX ROM RAM, and UW
    std::atomic<bool> ready{false};
    std::atomic<int> style{1};                 // 1 bare (the frame cropped, as shown), 0 framed, -1 off (the drawn logos)
};
inline RomBadges& romBadges() { static RomBadges b; return b; }
inline void readRomBadges(const std::vector<uint8_t>& os, uint32_t base)
{
    auto& rb = romBadges();
    if (rb.ready.load()) return;
    const uint32_t end = base + uint32_t(os.size());
    auto u32 = [&](uint32_t a) -> uint32_t { const size_t o = a - base; return (uint32_t(os[o]) << 24) | (uint32_t(os[o + 1]) << 16) | (uint32_t(os[o + 2]) << 8) | os[o + 3]; };
    auto badge = [&](uint32_t a) {
        if (a + 20 > end) return false;
        const uint32_t w = u32(a), h = u32(a + 4), px = u32(a + 12), mask = u32(a + 16);
        return w == 23 && h == 13 && mask == px + 4 * w && px >= base && mask + 4 * w <= end;
    };
    for (uint32_t a = base; a + 100 <= end; a += 2) {
        bool all = true;
        for (uint32_t i = 0; i < 5 && all; ++i) all = badge(a + 20 * i);
        if (!all) continue;
        static const char* const names[5] = {"E12", "EFM", "P-I", "TRX", "UW"};
        for (int i = 0; i < 5; ++i) {
            const uint32_t d = a + 20 * uint32_t(i), px = u32(d + 12);
            LogoArt f{names[i], 23, 13, {}}, b{names[i], 17, 9, {}};
            for (int r = 0; r < 13; ++r)
                for (int c = 0; c < 23; ++c)
                    if ((u32(px + 4 * uint32_t(c)) >> (32 - 13 + r)) & 1u) {
                        f.rows[r] |= uint64_t(1) << (63 - c);
                        if (r >= 2 && r < 11 && c >= 3 && c < 20) b.rows[r - 2] |= uint64_t(1) << (63 - (c - 3));
                    }
            const int slot = i < 4 ? i : 6;
            rb.framed[size_t(slot)] = f; rb.bare[size_t(slot)] = b;
        }
        for (int s : {4, 5}) {   // ROM, RAM: the UW badge
            rb.framed[size_t(s)] = rb.framed[6]; rb.bare[size_t(s)] = rb.bare[6];
            rb.framed[size_t(s)].family = rb.bare[size_t(s)].family = s == 4 ? "ROM" : "RAM";
        }
        rb.ready.store(true);
        return;
    }
}

// A family's logo: the OS's badge when there is one, else MdLogos.h (made by make_logos.py: 1-bit art at the LCD's
// resolution, each family in a type of its own as the Monomachine's are, all in one box). GND has none, as on the
// Monomachine: its name is printed.
inline const LogoArt* logoArt(const char* family)
{
    auto& rb = romBadges();
    if (rb.ready.load() && rb.style.load() >= 0) {
        const auto& set = rb.style.load() == 0 ? rb.framed : rb.bare;
        for (int i = 0; i < 6; ++i) if (std::strcmp(set[size_t(i)].family, family) == 0) return &set[size_t(i)];
    }
    for (const auto& l : kLogoArt) if (std::strcmp(l.family, family) == 0) return &l;
    return nullptr;
}
inline bool logoLit(const LogoArt& l, int x, int y) { return (l.rows[y] >> (63 - x)) & 1u; }

} // namespace mnm::plugin::md::text
