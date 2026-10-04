// md-render: plays Machinedrum machines from the user's MD OS file, with the knob values converted by the OS's own
// control handlers.
//   md-render <os.syx> list
//   md-render <os.syx> <machine name or id> <out.wav> [seconds=1] [k1..k8 knob values, '-' = default] [--track N]
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "MdControl.h"
#include "MdFirmware.h"
#include "MdVoiceEngine.h"
#include "MdMixEngine.h"
#include "MdKit.h"
#include <memory>

using namespace mnm::md;

static void writeWav(const char* path, const std::vector<int32_t>& s, int channels = 1)
{
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::printf("cannot write %s\n", path); return; }
    const uint32_t bytes = uint32_t(s.size() * 2), rate = 44100;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(uint16_t(channels)); u32(rate); u32(rate * 2 * uint32_t(channels)); u16(uint16_t(2 * channels)); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (auto v : s) u16(uint16_t(int16_t(std::max(-32768, std::min(32767, v >> 8)))));
    std::fclose(f);
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { std::printf("usage: md-render <os.syx> list | <machine> <out.wav> [seconds] [knobs...] [--track N]\n"); return 2; }
    try {
        const auto fw = loadFirmware(argv[1]);
        if (std::strcmp(argv[2], "list") == 0) {
            for (const auto& m : fw.machines) {
                std::printf("%3d %-7s handler %06x  ", m.id, m.name().c_str(), m.handler);
                for (int k = 0; k < 8; ++k) if (!m.labels[size_t(k)].empty()) std::printf("%s=%d ", m.labels[size_t(k)].c_str(), m.defaults[size_t(k)]);
                std::printf("\n");
            }
            return 0;
        }
        if (std::strcmp(argv[2], "kits") == 0 && argc > 3) {   // md-render <os.syx> kits <dump.syx>
            for (const auto& k : loadKits(argv[3])) {
                std::printf("kit %2d %-16s", k.position, k.name.c_str());
                for (int t = 0; t < 16; ++t) {
                    const auto* mm = fw.byId(int(k.machines[size_t(t)]));
                    std::printf(" %s", mm ? mm->name().c_str() : ("#" + std::to_string(k.machines[size_t(t)])).c_str());
                }
                std::printf("\n");
            }
            return 0;
        }
        if (argc < 4) { std::printf("missing output file\n"); return 2; }
        const Machine* m = nullptr;
        for (const auto& x : fw.machines) if (x.name() == argv[2] || std::to_string(x.id) == argv[2]) m = &x;
        if (!m) { std::printf("no machine '%s' (try list)\n", argv[2]); return 1; }
        const double seconds = argc > 4 ? std::atof(argv[4]) : 1.0;
        int track = 0;
        std::array<uint8_t, 8> knobs = m->defaults;
        bool mix = false;
        std::array<int, 9> fx = {0, 0, 64, 64, 0, 127, 0, 0, 0};   // AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST
        int level = 127, vol = 100, pan = 64, rev = 0, del = 0;
        std::array<std::array<int, 8>, 4> master = {{{127, 0, 64, 64, 0, 127, 0, 127},       // reverb  DVOL PRED DEC DAMP HP LP GATE LEV
                                                     {24, 0, 0, 32, 0, 127, 0, 127},          // delay   TIME MOD MFRQ FB FLTF FLTW MONO LEV
                                                     {64, 64, 64, 64, 64, 64, 64, 64},        // EQ      LF LG HF HG PF PG PQ GAIN
                                                     {0, 64, 127, 0, 0, 0, 64, 0}}};          // dynamix ATCK REL TRHD RTIO KNEE HP OUTG MIX
        for (int a = 5, k = 0; a < argc; ++a) {
            if (std::strcmp(argv[a], "--track") == 0 && a + 1 < argc) { track = std::atoi(argv[++a]); continue; }
            if (std::strcmp(argv[a], "--mix") == 0) { mix = true; continue; }
            if (std::strcmp(argv[a], "--fx") == 0 && a + 1 < argc) {   // comma list of the 9 track effect values
                std::string s = argv[++a]; size_t p = 0;
                for (int i = 0; i < 9 && p != std::string::npos; ++i) { fx[size_t(i)] = std::atoi(s.c_str() + p); p = s.find(','); if (p != std::string::npos) s.erase(0, p + 1), p = 0; }
                mix = true; continue;
            }
            if (std::strcmp(argv[a], "--rev") == 0 && a + 1 < argc) { rev = std::atoi(argv[++a]); mix = true; continue; }
            if (std::strcmp(argv[a], "--del") == 0 && a + 1 < argc) { del = std::atoi(argv[++a]); mix = true; continue; }
            if (std::strcmp(argv[a], "--pan") == 0 && a + 1 < argc) { pan = std::atoi(argv[++a]); mix = true; continue; }
            if (k < 8) { if (std::strcmp(argv[a], "-") != 0) knobs[size_t(k)] = uint8_t(std::atoi(argv[a])); ++k; }
        }

        ControlCpu cpu(fw.mainOs);
        std::array<uint16_t, 8> raw{};
        for (int k = 0; k < 8; ++k) raw[size_t(k)] = ControlCpu::rawFromValue(knobs[size_t(k)]);
        std::array<uint32_t, ControlCpu::kMaxPacket> packet{};
        const int n = cpu.convert(m->handler, m->dspType(), raw, packet);
        std::printf("%s knobs", m->name().c_str());
        for (int k = 0; k < 8; ++k) if (!m->labels[size_t(k)].empty()) std::printf(" %s=%d", m->labels[size_t(k)].c_str(), knobs[size_t(k)]);
        std::printf("\npacket (%d words):", n);
        for (int k = 1; k < n; ++k) std::printf(" %06x", packet[size_t(k)]);
        std::printf("\n");
        if (n < 1) { std::printf("handler failed: %s (PC/D0 %08x)\n", cpu.lastError(), cpu.lastPc()); return 1; }

        const auto t0 = std::chrono::steady_clock::now();
        VoiceEngine eng(fw);
        const auto t1 = std::chrono::steady_clock::now();
        if (m->id >= 128) {   // ROM/RAM: a 1 s 220 Hz test tone in the machine's slot
            const int slot = m->id - 128;
            std::array<std::vector<float>, VoiceEngine::kSlots> samples;
            std::array<double, VoiceEngine::kSlots> rates{};
            std::array<int, VoiceEngine::kSlots> loops;
            loops.fill(-1);
            for (int i = 0; i < 44100; ++i) samples[size_t(slot)].push_back(float(0.5 * std::sin(2.0 * 3.14159265358979 * 220.0 * i / 44100.0)));
            rates[size_t(slot)] = 44100.0;
            std::printf("test tone in slot %d: %s\n", slot,
                        eng.setSamples(samples, rates, loops) ? "loaded" : "does not fit");
        }
        eng.setPacket(track, packet.data(), n);
        eng.trig(track, m->dspType());
        const int passes = int(seconds * 44100.0 / VoiceEngine::kBlockFrames);
        std::vector<int32_t> out;
        VoiceEngine::Block blk{};
        uint64_t instr = 0;
        std::unique_ptr<MixEngine> mixer;
        if (mix) {
            mixer = std::make_unique<MixEngine>(fw);
            for (int t = 0; t < VoiceEngine::kTracks; ++t) {
                { std::array<uint16_t, 9> raw{}; const std::array<int, 9> neutral{0, 0, 64, 64, 0, 127, 0, 0, 0}; for (int k = 0; k < 9; ++k) raw[size_t(k)] = ControlCpu::rawFromValue(t == track ? fx[size_t(k)] : neutral[size_t(k)]); mixer->setTrackFx(t, raw); }
                mixer->setRouting(t, MixEngine::routingWords(ControlCpu::rawFromValue(level), ControlCpu::rawFromValue(vol), ControlCpu::rawFromValue(t == track ? pan : 64), ControlCpu::rawFromValue(t == track ? rev : 0), ControlCpu::rawFromValue(t == track ? del : 0)));
            }
            for (int e = 0; e < ControlCpu::kNumMasterFx; ++e) {
                std::array<uint16_t, 8> mr{};
                for (int k = 0; k < 8; ++k) mr[size_t(k)] = ControlCpu::rawFromValue(master[size_t(e)][size_t(k)]);
                std::array<uint32_t, 16> words{};
                const auto fxId = ControlCpu::MasterFx(e);
                if (!cpu.convertMasterFx(fxId, mr, words)) { std::printf("master FX %d: %s\n", e, cpu.lastError()); return 1; }
                const auto& s = ControlCpu::masterFxSection(fxId);
                mixer->setY(s.dspAddr, words.data(), s.words);
                std::printf("master FX %d -> Y:$%03x:", e, s.dspAddr);
                for (int k = 0; k < s.words; ++k) std::printf(" %06x", words[size_t(k)]);
                std::printf("\n");
            }
        }
        MixEngine::Output mixed{};
        uint64_t mixInstr = 0;
        double phase = 0.0;
        const bool input = m->id >= 80 && m->id <= 85;   // INP machines: a 220 Hz tone at half scale on both inputs
        for (int p = 0; p < passes; ++p) {
            if (input) {
                std::array<int32_t, 64> in{};
                for (int i = 0; i < 32; ++i) {
                    const auto v = int32_t(0.5 * 8388607.0 * std::sin(phase));
                    in[size_t(2 * i)] = v; in[size_t(2 * i + 1)] = v;
                    phase += 2.0 * 3.14159265358979 * 220.0 / 44100.0;
                }
                eng.setInput(in.data());
            }
            if (!eng.renderPass(blk)) { std::printf("render failed: %s\n", eng.faultReason().c_str()); return 1; }
            instr += eng.lastPassInstructions();
            if (mixer) {
                if (!mixer->renderBlock(blk, mixed)) { std::printf("mix failed: %s\n", mixer->faultReason().c_str()); return 1; }
                mixInstr += mixer->lastBlockInstructions();
                for (const auto& f : mixed) { out.push_back(f[MixEngine::kMainLeft]); out.push_back(f[MixEngine::kMainRight]); }
            } else {
                out.insert(out.end(), blk[size_t(track)].begin(), blk[size_t(track)].end());
            }
        }
        if (mixer) std::printf("DSP1: %.1f M instr/s of audio\n", double(mixInstr) / seconds / 1e6);
        if (std::getenv("MD_DUMP")) {   // debug: the track's state block and the first samples of its last block
            std::printf("state Y:$%03x..:", 0x800 + 0x40 * track);
            for (int k = 0; k < 0x30; ++k) std::printf("%s%06x", k % 8 == 0 ? "\n  " : " ", eng.peek(2, 0x800 + 0x40 * uint32_t(track) + uint32_t(k)));
            std::printf("\nactive type y:$%03x = %u; slot record X:$147e00..: %06x %06x %06x %06x; P:$13d = %06x",
                        0x153 + track, eng.peek(2, 0x153 + uint32_t(track)), eng.peek(1, 0x147E00), eng.peek(1, 0x147E01),
                        eng.peek(1, 0x147E02), eng.peek(1, 0x147E03), eng.peek(0, 0x13D));
            const uint32_t type = uint32_t(m->dspType());
            std::printf("\ntables for type %u: init Y:%06x P:%06x  update Y:%06x P:%06x  render Y:%06x P:%06x", type,
                        eng.peek(2, 0x145AF5 + type), eng.peek(0, 0x145AF5 + type), eng.peek(2, 0x145BB6 + type), eng.peek(0, 0x145BB6 + type),
                        eng.peek(2, 0x145C77 + type), eng.peek(0, 0x145C77 + type));
            std::printf("\nbanks Y:$100..: ");
            for (uint32_t k = 0; k < 8; ++k) std::printf(" %06x", eng.peek(2, 0x100 + k));
            std::printf("\n      Y:$120..: ");
            for (uint32_t k = 0; k < 8; ++k) std::printf(" %06x", eng.peek(2, 0x120 + k));
            std::printf("\nsample mem Y:$150000..:");
            for (uint32_t k = 0; k < 8; ++k) std::printf(" %06x", eng.peek(2, 0x150000 + k));
            std::printf("\nX:$000.. (decode ptrs):");
            for (uint32_t k = 0; k < 12; ++k) std::printf(" %06x", eng.peek(1, k));
            std::printf("\nX:$092.. (decoded ring):");
            for (uint32_t k = 0; k < 12; ++k) std::printf(" %06x", eng.peek(1, 0x92 + k));
            std::printf("\nX:$0f8..$0ff:");
            for (uint32_t k = 0xF8; k < 0x100; ++k) std::printf(" %06x", eng.peek(1, k));
            std::printf("\nY:$000.. (filter out):");
            for (uint32_t k = 0; k < 12; ++k) std::printf(" %06x", eng.peek(2, k));
            std::printf("\nY:$146000 table (every 256th):");
            for (uint32_t k = 0; k < 4096; k += 256) std::printf(" %06x", eng.peek(2, 0x146000 + k));
            std::printf("\nY:$146000 around 0/2048/4095: %06x %06x %06x | %06x %06x %06x | %06x", eng.peek(2, 0x146000), eng.peek(2, 0x146001), eng.peek(2, 0x146002),
                        eng.peek(2, 0x1467FF), eng.peek(2, 0x146800), eng.peek(2, 0x146801), eng.peek(2, 0x146FFF));
            std::printf("\nlast block:");
            for (int i = 0; i < 8; ++i) std::printf(" %d", blk[size_t(track)][size_t(i)]);
            std::printf("\n");
        }
        const auto t2 = std::chrono::steady_clock::now();
        double peak = 0, sum = 0;
        for (auto v : out) { peak = std::max(peak, std::abs(double(v))); sum += double(v) * v; }
        const double renderSec = std::chrono::duration<double>(t2 - t1).count();
        std::printf("boot %.0f ms; rendered %.2f s in %.3f s (%.1fx realtime, %.1f M DSP instr/s of audio); peak %.3f RMS %.4f\n",
                    std::chrono::duration<double, std::milli>(t1 - t0).count(), seconds, renderSec, seconds / renderSec,
                    double(instr) / seconds / 1e6, peak / 8388608.0, std::sqrt(sum / double(out.size())) / 8388608.0);
        writeWav(argv[3], out, mixer ? 2 : 1);
        std::printf("wrote %s\n", argv[3]);
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}
