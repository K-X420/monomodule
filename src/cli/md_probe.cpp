// md-probe: feasibility check for a Machinedrum engine. Unpacks a Machinedrum OS .syx with the same pipeline as
// the Monomachine one (sysex -> flash container -> aPLib sections -> DSP records), loads one DSP image into the
// dsp56300 emulator, starts it at its entry point and reports where it settles (PC histogram), whether it
// faults, and what it touches on the host port.
//   md-probe <os.syx> [section=1] [millions of instructions=50]
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include "firmware/Firmware.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/jit.h"
#include "dsp56kEmu/disasm.h"

using namespace dsp56k;
using namespace mnm;

static fw::FlashImage parseAnyElektronSysex(const std::vector<uint8_t>& syx)
{
    fw::FlashImage img;
    bool haveBase = false;
    uint32_t expect = 0;
    for (size_t i = 0; i < syx.size();) {
        if (syx[i] != 0xF0) { ++i; continue; }
        size_t j = i;
        while (j < syx.size() && syx[j] != 0xF7) ++j;
        if (j >= syx.size()) break;
        const size_t len = j - i + 1;
        const uint8_t* m = &syx[i];
        if (len > 15 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x3C && m[5] == 0x00 && m[6] == 0x7E) {   // any Elektron device id
            uint32_t addr = 0;
            for (int k = 0; k < 6; ++k) addr = (addr << 4) | (m[9 + k] & 0xF);
            if (!haveBase) { img.base = expect = addr; haveBase = true; }
            if (addr != expect) throw fw::FirmwareError("non-contiguous block");
            for (size_t k = 15; k + 2 < len - 1; k += 3) {
                const uint32_t w = (uint32_t(m[k]) << 14) | (uint32_t(m[k + 1]) << 7) | m[k + 2];
                img.bytes.push_back(uint8_t(w >> 8));
                img.bytes.push_back(uint8_t(w & 0xFF));
            }
            expect += uint32_t((len - 16) / 3 * 2);
        }
        i = j + 1;
    }
    return img;
}

struct Rec { fw::Space space; uint32_t addr; std::vector<uint32_t> words; };

// Monomachine record format plus the 2-word marker type 4 the Machinedrum images carry after the start marker
static std::vector<Rec> parseRecords(const std::vector<uint8_t>& sec, uint32_t& start)
{
    std::vector<Rec> out;
    const size_t nw = sec.size() / 3;
    auto w = [&](size_t k) { return uint32_t(sec[3 * k]) | (uint32_t(sec[3 * k + 1]) << 8) | (uint32_t(sec[3 * k + 2]) << 16); };
    for (size_t p = 0; p < nw;) {
        const uint32_t t = w(p);
        if (t >= 3) { if (t == 3) start = w(p + 1); p += 2; continue; }
        Rec r{fw::Space(t), w(p + 1), {}};
        const uint32_t cnt = w(p + 2);
        if (p + 3 + cnt > nw) throw fw::FirmwareError("truncated record");
        r.words.resize(cnt);
        for (uint32_t k = 0; k < cnt; ++k) r.words[k] = w(p + 3 + k);
        out.push_back(std::move(r));
        p += 3 + cnt;
    }
    return out;
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { std::printf("usage: md-probe <os.syx> [section=1] [Minstr=50]\n"); return 2; }
    const int section = argc > 2 ? std::atoi(argv[2]) : 1;
    const uint64_t budget = uint64_t(argc > 3 && std::atoi(argv[3]) > 0 ? std::atoi(argv[3]) : 50) * 1'000'000ull;
    try {
        const auto c = fw::parseContainer(parseAnyElektronSysex(fw::readFile(argv[1])));
        std::printf("container: %zu sections\n", c.sections.size());
        if (section >= int(c.sections.size())) { std::printf("no section %d\n", section); return 1; }
        uint32_t start = 0;
        const auto recs = parseRecords(c.sections[size_t(section)].data, start);
        uint32_t maxAddr = 0; size_t words = 0;
        for (const auto& r : recs) { maxAddr = std::max<uint32_t>(maxAddr, r.addr + uint32_t(r.words.size())); words += r.words.size(); }
        std::printf("section %d: %zu records, %zu words, entry $%06x, highest address $%06x\n", section, recs.size(), words, start, maxAddr);

        DefaultMemoryValidator validator;
        const TWord bridge = 0x100000, size = std::max<TWord>(0x180000, (maxAddr + 0xFFFF) & ~0xFFFFu);
        Memory mem(validator, size, size, bridge);
        Peripherals56303 px;
        PeripheralsNop py, pxNop;
        const bool renderMode = argc > 6 && std::strcmp(argv[3], "render") == 0;
        // no DMA / ESSI / HI08 at all: the 56303 peripheral model stalls on the MD's DMA setup, and the render
        // harness drives the engine directly (as Monomodule's stub does) instead of through the audio ports
        const bool noPeriph = renderMode || std::getenv("MD_NOPERIPH") != nullptr;
        DSP dsp(mem, noPeriph ? static_cast<IPeripherals*>(&pxNop) : &px, &py);
        if (noPeriph) std::printf("peripherals disabled\n");
        auto cfg = dsp.getJit().getConfig();
        cfg.dynamicFastInterrupts = true;
        cfg.interruptRegionIsCode = true;
        dsp.getJit().setConfig(cfg);
        px.getHI08().setRXRateLimit(0);
        px.getHI08().setTransmitDataAlwaysEmpty(true);
        dsp.resetHW();
        for (const auto& r : recs)
            for (size_t k = 0; k < r.words.size(); ++k) {
                const TWord a = r.addr + TWord(k);
                if (r.space == fw::Space::P || a >= bridge) dsp.memWriteP(a, r.words[k]);
                else dsp.memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
            }
        dsp.setPC(start);
        if (argc > 5 && std::strcmp(argv[3], "disasm") == 0) {   // md-probe <os.syx> <section> disasm <hex addr> <count>
            Disassembler dis(dsp.opcodes());
            TWord pc = TWord(std::strtoul(argv[4], nullptr, 16));
            const int count = std::atoi(argv[5]);
            for (int i = 0; i < count; ++i) {
                std::string text;
                const TWord opA = mem.get(MemArea_P, pc), opB = mem.get(MemArea_P, pc + 1);
                const auto len = dis.disassemble(text, opA, opB, 0, 0, pc);
                std::printf("P:$%06x  %06x %s  %s\n", pc, opA, len > 1 ? (std::to_string(opB).empty() ? "" : "+") : " ", text.c_str());
                pc += std::max<uint32_t>(1, len);
            }
            return 0;
        }
        auto step = [&] {
            const TWord pc = dsp.getPC().toWord();
            // 'rep x0' memory clear at boot: memory starts zeroed, step over it and the repeated instruction
            if (mem.get(MemArea_P, pc) == 0x06C420) dsp.setPC(pc + 2);
            dsp.execInterpreter();
        };
        if (renderMode) {
            // md-probe <os.syx> 1 render <machine> <passes> <out.wav>
            // Main loop (P:$64..$E7): for track t = y:$142 (0..15), struct at y:$141 = $800 + $40*t. A non-zero word 0
            // is a trig with that machine: init routine from table $145AF5 when the machine changed, trig routine from
            // $145BB6, word cleared. Then the render routine from $145C77 fills 32 words at y:$140 ($100/$120 in turn).
            // At track 0 it waits for the ESSI frame (P:$BB..$BF); that wait is patched out.
            const int machine = int(std::strtol(argv[4], nullptr, 0));
            const int passes = std::atoi(argv[5]);
            uint64_t n = 0;
            while (dsp.getPC().toWord() != 0xBB && n++ < 50'000'000) step();
            if (dsp.getPC().toWord() != 0xBB) { std::printf("boot did not reach the main loop (PC $%06x)\n", dsp.getPC().toWord()); return 1; }
            std::printf("booted in %llu instructions; main loop reached\n", (unsigned long long)dsp.getInstructionCounter());
            dsp.memWriteP(0xBF, 0x000000);   // 'beq $bb' (frame wait) -> nop
            for (int a = 7; a < argc; ++a) {   // track 1 struct words: <index>=<value>, e.g. 1=0x51c0 (both may be hex)
                const char* eq = std::strchr(argv[a], '=');
                if (!eq) continue;
                const TWord idx = TWord(std::strtoul(argv[a], nullptr, 0)), val = TWord(std::strtoul(eq + 1, nullptr, 0)) & 0xFFFFFF;
                dsp.memWrite(MemArea_Y, 0x800 + idx, val);
                std::printf("  y:$%03x = $%06x\n", 0x800 + idx, val);
            }
            dsp.memWrite(MemArea_Y, 0x800, TWord(machine));   // trig track 1 with the machine
            std::vector<int32_t> capX, capY;
            uint64_t passStart = dsp.getInstructionCounter(), perPass = 0;
            for (int p = 0; p < passes;) {
                step();
                if (dsp.getPC().toWord() == 0xB5 && mem.get(MemArea_Y, 0x142) == 0) {   // track 1's render returned
                    const TWord base = mem.get(MemArea_Y, 0x140);
                    for (TWord k = 0; k < 32; ++k) {
                        capX.push_back(int32_t(mem.get(MemArea_X, base + k) << 8) >> 8);
                        capY.push_back(int32_t(mem.get(MemArea_Y, base + k) << 8) >> 8);
                    }
                    const auto now = dsp.getInstructionCounter();
                    if (p == 1) perPass = now - passStart;
                    passStart = now;
                    ++p;
                }
                if (dsp.getInstructionCounter() > 4'000'000'000ull) { std::printf("instruction cap hit\n"); break; }
            }
            auto energy = [](const std::vector<int32_t>& v) { double s = 0; for (auto x : v) s += double(x) * x; return v.empty() ? 0.0 : std::sqrt(s / double(v.size())) / 8388608.0; };
            const double eX = energy(capX), eY = energy(capY);
            std::printf("machine %d: %zu samples; RMS X %.5f  Y %.5f; ~%llu DSP instructions per 16-track pass\n", machine, capX.size(), eX, eY, (unsigned long long)perPass);
            const auto& cap = eY >= eX ? capY : capX;
            if (FILE* f = std::fopen(argv[6], "wb")) {   // 16-bit mono 44.1 kHz WAV
                const uint32_t dataBytes = uint32_t(cap.size() * 2), rate = 44100;
                auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
                auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
                std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
                u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
                std::fwrite("data", 1, 4, f); u32(dataBytes);
                for (auto v : cap) u16(uint16_t(int16_t(v >> 8)));
                std::fclose(f);
                std::printf("wrote %s (%s memory)\n", argv[6], eY >= eX ? "Y" : "X");
            }
            return 0;
        }
        std::printf("loaded; running from $%06x\n", start);
        std::atomic<bool> done{false};
        std::thread watchdog([&] {   // debug only: racy reads, enough to tell "running somewhere" from "stuck"
            for (int s = 1; !done; ++s) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                if (!done) std::printf("  [watchdog %ds] PC $%06x  instr %llu\n", s * 3, dsp.getPC().toWord(), (unsigned long long)dsp.getInstructionCounter());
            }
        });
        struct Join { std::thread& t; std::atomic<bool>& d; ~Join() { d = true; t.join(); } } join{watchdog, done};

        std::map<uint32_t, uint64_t> hist;   // PC samples (one per exec() call)
        const uint64_t t0 = dsp.getInstructionCounter();
        uint64_t calls = 0;
        const auto wall = std::chrono::steady_clock::now();
        // also capped by calls and wall time: a DSP sitting in WAIT (or a loop the counter skips) never reaches the budget
        while (dsp.getInstructionCounter() - t0 < budget && calls < budget && std::chrono::steady_clock::now() - wall < std::chrono::seconds(60)) {
            TWord pcBefore = dsp.getPC().toWord();
            // 'rep x0' over a 2M-word memory clear: the interpreter never finishes it, and the memory starts zeroed
            // anyway. Step over the rep and its repeated instruction (Monomodule replaces the MnM init the same way).
            if (mem.get(MemArea_P, pcBefore) == 0x06C420) {   // rep x0
                std::printf("  skipping rep at $%06x\n", pcBefore);
                dsp.setPC(pcBefore + 2);
                pcBefore += 2;
            }
            if (calls < 400 || (calls % 1'000'000) == 0)
                std::printf("  #%llu PC $%06x op $%06x\n", (unsigned long long)calls, pcBefore, mem.get(MemArea_P, pcBefore));
            dsp.execInterpreter();   // one instruction per call: a JIT block spinning on a peripheral would never return
            ++calls;
            if ((calls & 63) == 0) ++hist[dsp.getPC().toWord()];
            if (dsp.getJit().hasFailed()) { std::printf("JIT FAILED: %s\n", dsp.getJit().failReason().c_str()); break; }
        }
        std::printf("ran %llu instructions (%llu exec calls)\n", (unsigned long long)(dsp.getInstructionCounter() - t0), (unsigned long long)calls);
        std::vector<std::pair<uint64_t, uint32_t>> top;
        for (auto& [pc, n] : hist) top.emplace_back(n, pc);
        std::sort(top.rbegin(), top.rend());
        std::printf("distinct PCs sampled: %zu; hottest:\n", hist.size());
        for (size_t i = 0; i < std::min<size_t>(12, top.size()); ++i) std::printf("  $%06x  %llu\n", top[i].second, (unsigned long long)top[i].first);
        std::printf("final PC $%06x  SR $%06x  HI08 TX words waiting: %zu\n", dsp.getPC().toWord(), dsp.getSR().toWord(), px.getHI08().txData().size());
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}
