// md-probe: feasibility check for a Machinedrum engine. Unpacks a Machinedrum OS .syx with the same pipeline as
// the Monomachine one (sysex -> flash container -> aPLib sections -> DSP records), loads one DSP image into the
// dsp56300 emulator, starts it at its entry point and reports where it settles (PC histogram), whether it
// faults, and what it touches on the host port.
//   md-probe <os.syx> [section=1] [millions of instructions=50]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include "firmware/Firmware.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/jit.h"

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
    const uint64_t budget = uint64_t(argc > 3 ? std::atoi(argv[3]) : 50) * 1'000'000ull;
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
        PeripheralsNop py;
        DSP dsp(mem, &px, &py);
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
        std::printf("loaded; running from $%06x\n", start);

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
