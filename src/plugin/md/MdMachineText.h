// Monomodule MD: the machine picker's words and logos. Each family column has a logo (original pixel art in the
// LCD's style: the Machinedrum's OS has no family artwork to read, unlike the Monomachine's boot-splash logos) and a
// blurb; a hovered machine shows its own description there. ASCII caps (the LCD faces have no lowercase), words of at
// most 8 characters (a column line holds about 8 at the picker's text size). Names follow the Machinedrum manual's
// machine reference (rev J, OS 1.53: E12-BC "BONGO CONGO", P-I-ML "METALLICA", INP-EA/EB play the input as a drum with
// envelopes, INP-FA/FB are filter followers); family words from its intros (EFM = Elektron's "Enhanced Feedback
// Modulation", P-I = "physically informed", TRX "inspired by classic analogue drum machine synthesis").
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

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

// A family logo: 24 x 12 LCD pixels, bit (23 - x) of row y lit. Drawn from a few primitives so the art stays legible.
struct Logo {
    static constexpr int kW = 24, kH = 12;
    std::array<uint32_t, kH> rows{};
    void set(int x, int y) { if (x >= 0 && x < kW && y >= 0 && y < kH) rows[size_t(y)] |= 1u << (kW - 1 - x); }
    bool lit(int x, int y) const { return (rows[size_t(y)] >> (kW - 1 - x)) & 1u; }
    void rect(int x, int y, int w, int h) { for (int j = y; j < y + h; ++j) for (int i = x; i < x + w; ++i) set(i, j); }
    void line(int x0, int y0, int x1, int y1)
    {
        const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int e = dx + dy;
        for (;;) {
            set(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * e;
            if (e2 >= dy) { e += dy; x0 += sx; }
            if (e2 <= dx) { e += dx; y0 += sy; }
        }
    }
    void ring(double cx, double cy, double r, bool fill = false)
    {
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
                if (fill ? d <= r : std::abs(d - r) <= 0.6) set(x, y);
            }
    }
    template <class F> void wave(F yOf)   // a curve y(x) across the width, joined up
    {
        int py = int(std::lround(yOf(0.0)));
        for (int x = 0; x < kW; ++x) { const int y = int(std::lround(yOf(double(x)))); line(x > 0 ? x - 1 : 0, py, x, y); py = y; }
    }
};

inline Logo familyLogo(const char* family)
{
    Logo l;
    const double pi = 3.14159265358979;
    auto is = [&](const char* f) { return std::strcmp(family, f) == 0; };
    if (is("GND")) {          // the ground symbol
        l.rect(11, 0, 2, 4); l.rect(2, 4, 20, 2); l.rect(6, 7, 12, 2); l.rect(10, 10, 4, 2);
    } else if (is("TRX")) {   // a struck drum: a damped oscillation
        l.wave([&](double x) { return 5.5 - 5.0 * std::exp(-x / 9.0) * std::sin(2 * pi * x / 7.0); });
    } else if (is("EFM")) {   // frequency modulation: a sine whose phase is swung by another
        l.wave([&](double x) { return 5.5 - 4.5 * std::sin(2 * pi * x / 12.0 + 1.8 * std::sin(2 * pi * x / 4.0)); });
    } else if (is("E12")) {   // a sampled wave: held steps
        int py = -1;
        for (int x = 0; x < Logo::kW; ++x) {
            const int y = int(std::lround(5.5 - 5.0 * std::sin(2 * pi * double(x / 3 * 3) / 24.0)));
            if (py >= 0 && y != py) l.line(x, py, x, y);
            l.set(x, y); py = y;
        }
    } else if (is("P-I")) {   // a mallet striking a bar
        l.rect(1, 9, 22, 2); l.ring(14.5, 4.0, 2.6, true); l.line(13, 4, 3, 0); l.line(14, 4, 4, 0);
        l.set(19, 7); l.set(20, 6); l.set(9, 7); l.set(8, 6);
    } else if (is("INP")) {   // a signal into a jack
        l.ring(17.0, 6.0, 4.6); l.ring(17.0, 6.0, 1.6, true); l.rect(0, 5, 10, 2); l.line(7, 2, 10, 5); l.line(7, 9, 10, 6);
    } else if (is("ROM")) {   // a memory chip
        l.rect(4, 3, 16, 6);
        for (int x = 5; x < 20; x += 3) { l.rect(x, 0, 1, 3); l.rect(x, 9, 1, 3); }
    } else if (is("RAM")) {   // record
        l.ring(12.0, 6.0, 5.4); l.ring(12.0, 6.0, 2.8, true);
    } else if (is("MID")) {   // a five-pin DIN socket
        l.ring(12.0, 6.0, 5.6);
        for (int k = 0; k < 5; ++k) {
            const double a = pi + k * pi / 4.0;   // 180..360 degrees: the pins' half circle
            l.set(int(std::lround(12.0 + 3.2 * std::cos(a) - 0.5)), int(std::lround(6.5 + 3.2 * std::sin(a) + 2.0)));
        }
        l.rect(11, 0, 2, 1);
    } else if (is("CTR")) {   // three faders
        for (int k = 0; k < 3; ++k) { const int x = 4 + 8 * k; l.rect(x, 0, 1, 12); }
        l.rect(2, 7, 5, 3); l.rect(10, 2, 5, 3); l.rect(18, 5, 5, 3);
    }
    return l;
}

} // namespace mnm::plugin::md::text
