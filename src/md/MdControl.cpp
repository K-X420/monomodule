#include "MdControl.h"
#include <cstring>
#include <mutex>
#include "MdFirmware.h"

extern "C" {
#include "m68k.h"
}

// Musashi keeps one CPU in globals: one call at a time across every ControlCpu (and plugin instance).
namespace {
std::mutex g_cpuLock;
std::vector<uint8_t>* g_ram = nullptr;
uint32_t g_tempo = 0;
bool g_returned = false;

constexpr uint32_t kRamSize   = 0x800000;     // 8 MB flat: MainOS image, its globals and RAM, our frame
constexpr uint32_t kReturnPc  = 0x7F0000;     // the handler returns here (a NOP)
constexpr uint32_t kRawAddr   = 0x7F0100;     // u16 raw[8]
constexpr uint32_t kPacketAddr= 0x7F0200;     // u32 packet[kMaxPacket]
constexpr uint32_t kStackTop  = 0x7FFFF0;
constexpr uint32_t kSramBase  = 0x1000000;    // ColdFire internal SRAM (8 KB): copied code, DSP parameter mirrors
constexpr uint32_t kSramSize  = 0x2000;
constexpr uint32_t kSramShadow= 0x7E0000;     // where the SRAM lives inside m_ram
constexpr uint32_t kTempoAddr = 0x100150C;    // in SRAM: tempo = BPM x 24
constexpr int kMaxCycles = 2'000'000;
uint32_t g_stopPc = 0;

void ensureCpu()   // under g_cpuLock
{
    static bool initialised = false;
    if (!initialised) { m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68020); initialised = true; }
}

uint8_t rd8(uint32_t a)
{
    a &= 0xFFFFFFFF;
    if (a >= kTempoAddr && a < kTempoAddr + 4) return uint8_t(g_tempo >> (8 * (3 - (a - kTempoAddr))));
    if (a >= kSramBase && a < kSramBase + kSramSize) return (*g_ram)[kSramShadow + (a - kSramBase)];
    if (a < kRamSize) return (*g_ram)[a];
    return 0;   // unmapped (peripherals, flash aliases): reads as zero
}
void wr8(uint32_t a, uint8_t v)
{
    if (a >= kSramBase && a < kSramBase + kSramSize) { (*g_ram)[kSramShadow + (a - kSramBase)] = v; return; }
    if (a < kRamSize && a >= mnm::md::kMainOsBase) (*g_ram)[a] = v;   // vectors below the OS stay ours
}
} // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return rd8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return (unsigned(rd8(a)) << 8) | rd8(a + 1); }
unsigned int m68k_read_memory_32(unsigned int a) { return (unsigned(rd8(a)) << 24) | (unsigned(rd8(a + 1)) << 16) | (unsigned(rd8(a + 2)) << 8) | rd8(a + 3); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return m68k_read_memory_8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { wr8(a, uint8_t(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { wr8(a, uint8_t(v >> 8)); wr8(a + 1, uint8_t(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { wr8(a, uint8_t(v >> 24)); wr8(a + 1, uint8_t(v >> 16)); wr8(a + 2, uint8_t(v >> 8)); wr8(a + 3, uint8_t(v)); }
void md_m68k_instruction_hook(unsigned int pc)
{
    if (pc == kReturnPc || (g_stopPc && pc == g_stopPc)) { g_returned = true; m68k_end_timeslice(); }
}
}

namespace mnm::md {

ControlCpu::ControlCpu(const std::vector<uint8_t>& mainOs) : m_ram(kRamSize, 0)
{
    if (mainOs.size() > kReturnPc - kMainOsBase - 0x100000) throw std::runtime_error("MainOS image too large");
    std::memcpy(m_ram.data() + kMainOsBase, mainOs.data(), mainOs.size());
    // reset vectors (SSP, PC) and the return trampoline: NOP
    auto put32 = [&](uint32_t a, uint32_t v) { for (int k = 0; k < 4; ++k) m_ram[a + uint32_t(k)] = uint8_t(v >> (8 * (3 - k))); };
    put32(0, kStackTop);
    put32(4, kReturnPc);
    m_ram[kReturnPc] = 0x4E; m_ram[kReturnPc + 1] = 0x71;
    // the boot copy: MainOS 0x26237C.. -> SRAM 0x1000088..0x1000A2A (hot routines and initialised data)
    constexpr uint32_t kCopySrc = 0x26237C, kCopyDst = 0x1000088, kCopyEnd = 0x1000A2A;
    if (kCopySrc - kMainOsBase + (kCopyEnd - kCopyDst) <= mainOs.size())
        std::memcpy(m_ram.data() + kSramShadow + (kCopyDst - kSramBase), mainOs.data() + (kCopySrc - kMainOsBase), kCopyEnd - kCopyDst);
}

const ControlCpu::MasterFxSection& ControlCpu::masterFxSection(MasterFx fx)
{
    static const MasterFxSection sections[kNumMasterFx] = {
        {0x20B940, 0x1000D9C, 0x1001B74, 0x185, 8},    // reverb (Gate Box)
        {0x20B4AE, 0x1000DAC, 0x1001AF4, 0x150, 9},    // delay (Rhythm Echo)
        {0x20B74E, 0x1000DBC, 0x1001B44, 0x170, 10},   // EQ
        {0x20B604, 0x1000DCC, 0x1001B18, 0x17A, 11},   // dynamics
    };
    return sections[int(fx)];
}

bool ControlCpu::call(uint32_t fn, uint32_t stopPc)
{
    // caller holds g_cpuLock; a C call with no arguments returning to the trampoline (or stopping at stopPc)
    g_ram = &m_ram;
    g_tempo = m_tempo;
    auto put32 = [&](uint32_t a, uint32_t v) { for (int k = 0; k < 4; ++k) m_ram[a + uint32_t(k)] = uint8_t(v >> (8 * (3 - k))); };
    const uint32_t sp = kStackTop - 0x100;
    put32(sp, kReturnPc);
    ensureCpu();
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, fn);
    g_returned = false;
    g_stopPc = stopPc;
    int cycles = 0;
    while (!g_returned && cycles < kMaxCycles) cycles += m68k_execute(10000);
    g_stopPc = 0;
    m_lastPc = m68k_get_reg(nullptr, M68K_REG_PC);
    g_ram = nullptr;
    return g_returned;
}

bool ControlCpu::convertMasterFx(MasterFx fx, const std::array<uint16_t, 8>& raw, std::array<uint32_t, 16>& words)
{
    const auto& s = masterFxSection(fx);
    for (int k = 0; k < 8; ++k) { sram(s.rawAddr)[2 * k] = uint8_t(raw[size_t(k)] >> 8); sram(s.rawAddr)[2 * k + 1] = uint8_t(raw[size_t(k)]); }
    return convertMasterFxFromTick(fx, words);
}

bool ControlCpu::convertMasterFxFromTick(MasterFx fx, std::array<uint32_t, 16>& words)
{
    constexpr uint32_t kJoin = 0x20BA4C;
    const auto& s = masterFxSection(fx);
    bool ok;
    {
        std::lock_guard<std::mutex> lock(g_cpuLock);
        ok = call(s.entry, kJoin);
    }
    for (int k = 0; k < s.words && k < 16; ++k) {
        const uint8_t* p = sram(s.mirror + 4 * uint32_t(k));
        words[size_t(k)] = ((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]) & 0xFFFFFF;
    }
    m_error = ok ? "" : "master FX section did not finish";
    return ok;
}

// ---- control tick ------------------------------------------------------------------------------------------

namespace {
constexpr uint32_t kTargets = 0x1000DDC, kLevelTargets = 0x1000F5C, kLfos = 0x1000F8C;
constexpr uint32_t kBase = 0x1000A4C, kLive = 0x10011CC, kLevelLive = 0x1000D7C;
constexpr uint32_t kFnLfoUpdate = 0x1000088, kFnSlewParams = 0x10002E0, kFnApplyLfos = 0x1000332, kFnSlewLevels = 0x100029E;
}

void ControlCpu::setTargets(const TrackParams& params, const std::array<uint8_t, 16>& levels, const std::array<std::array<uint8_t, 8>, 4>& masterFx)
{
    uint8_t* t = sram(kTargets);
    for (int tr = 0; tr < 16; ++tr)
        for (int k = 0; k < 24; ++k) t[24 * tr + k] = params[size_t(tr)][size_t(k)];
    uint8_t* l = sram(kLevelTargets);
    for (int tr = 0; tr < 16; ++tr) l[tr] = levels[size_t(tr)];
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) l[16 + 8 * fx + k] = masterFx[size_t(fx)][size_t(k)];
}

void ControlCpu::snapToTargets()
{
    auto put16 = [](uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); };
    const uint8_t* t = sram(kTargets);
    for (int i = 0; i < 16 * 24; ++i) { put16(sram(kBase) + 2 * i, uint16_t(t[i] << 7)); put16(sram(kLive) + 2 * i, uint16_t(t[i] << 7)); }
    const uint8_t* l = sram(kLevelTargets);
    for (int i = 0; i < 48; ++i) put16(sram(kLevelLive) + 2 * i, uint16_t(l[i] << 7));
}

void ControlCpu::setLfo(int track, const uint8_t* lfo36)
{
    if (track < 0 || track > 15) return;
    std::memcpy(sram(kLfos + 36 * uint32_t(track)), lfo36, 36);
}

void ControlCpu::setLfoConfig(int track, const uint8_t* first5)
{
    if (track < 0 || track > 15) return;
    std::memcpy(sram(kLfos + 36 * uint32_t(track)), first5, 5);
}

void ControlCpu::lfoTrig(int track)
{
    if (track >= 0 && track <= 15) sram(kLfos + 36 * uint32_t(track))[5] = 1;   // the OS's trig flag (0x20CE1C)
}

bool ControlCpu::tick(bool lfoUpdate)
{
    std::lock_guard<std::mutex> lock(g_cpuLock);
    bool ok = true;
    if (lfoUpdate) ok = call(kFnLfoUpdate) && ok;
    ok = call(kFnSlewParams) && ok;
    ok = call(kFnApplyLfos) && ok;
    ok = call(kFnSlewLevels) && ok;
    if (!ok) m_error = "control tick did not finish";
    return ok;
}

uint16_t ControlCpu::liveParam(int track, int k) const
{
    const uint8_t* p = sram(kLive + 2 * uint32_t(24 * track + k));
    return uint16_t((p[0] << 8) | p[1]);
}

uint16_t ControlCpu::liveLevel(int track) const
{
    const uint8_t* p = sram(kLevelLive + 2 * uint32_t(track));
    return uint16_t((p[0] << 8) | p[1]);
}

ControlCpu::~ControlCpu() = default;

int ControlCpu::convert(uint32_t handler, int dspType, const std::array<uint16_t, 8>& raw, std::array<uint32_t, kMaxPacket>& packet)
{
    if (handler < kMainOsBase || handler >= kReturnPc) return 0;
    std::lock_guard<std::mutex> lock(g_cpuLock);
    g_ram = &m_ram;
    g_tempo = m_tempo;
    auto put16 = [&](uint32_t a, uint16_t v) { m_ram[a] = uint8_t(v >> 8); m_ram[a + 1] = uint8_t(v); };
    auto put32 = [&](uint32_t a, uint32_t v) { for (int k = 0; k < 4; ++k) m_ram[a + uint32_t(k)] = uint8_t(v >> (8 * (3 - k))); };
    auto get32 = [&](uint32_t a) { return (uint32_t(m_ram[a]) << 24) | (uint32_t(m_ram[a + 1]) << 16) | (uint32_t(m_ram[a + 2]) << 8) | m_ram[a + 3]; };
    for (int k = 0; k < 8; ++k) put16(kRawAddr + 2 * uint32_t(k), raw[size_t(k)]);
    for (int k = 0; k < kMaxPacket; ++k) put32(kPacketAddr + 4 * uint32_t(k), 0);
    put32(kPacketAddr, uint32_t(dspType));
    // C call frame: return address, packet, raw
    const uint32_t sp = kStackTop - 12;
    put32(sp, kReturnPc);
    put32(sp + 4, kPacketAddr);
    put32(sp + 8, kRawAddr);

    ensureCpu();
    m68k_pulse_reset();   // SSP/PC from our vectors; supervisor mode
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, handler);
    g_returned = false;
    int cycles = 0;
    while (!g_returned && cycles < kMaxCycles) cycles += m68k_execute(10000);
    g_ram = nullptr;
    m_lastPc = m68k_get_reg(nullptr, M68K_REG_PC);
    if (!g_returned) { m_error = "handler did not return"; return 0; }
    const int n = int(m68k_get_reg(nullptr, M68K_REG_D0));
    if (n < 1 || n > kMaxPacket) { m_error = "bad packet word count"; m_lastPc = uint32_t(n); return 0; }
    m_error = "";
    for (int k = 0; k < n; ++k) packet[size_t(k)] = get32(kPacketAddr + 4 * uint32_t(k)) & 0xFFFFFF;
    return n;
}

} // namespace mnm::md
