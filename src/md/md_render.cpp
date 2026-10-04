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

using namespace mnm::md;

static void writeWav(const char* path, const std::vector<int32_t>& s)
{
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::printf("cannot write %s\n", path); return; }
    const uint32_t bytes = uint32_t(s.size() * 2), rate = 44100;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
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
        if (argc < 4) { std::printf("missing output file\n"); return 2; }
        const Machine* m = nullptr;
        for (const auto& x : fw.machines) if (x.name() == argv[2] || std::to_string(x.id) == argv[2]) m = &x;
        if (!m) { std::printf("no machine '%s' (try list)\n", argv[2]); return 1; }
        const double seconds = argc > 4 ? std::atof(argv[4]) : 1.0;
        int track = 0;
        std::array<uint8_t, 8> knobs = m->defaults;
        for (int a = 5, k = 0; a < argc; ++a) {
            if (std::strcmp(argv[a], "--track") == 0 && a + 1 < argc) { track = std::atoi(argv[++a]); continue; }
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
        eng.setPacket(track, packet.data(), n);
        eng.trig(track, m->dspType());
        const int passes = int(seconds * 44100.0 / VoiceEngine::kBlockFrames);
        std::vector<int32_t> out;
        VoiceEngine::Block blk{};
        uint64_t instr = 0;
        for (int p = 0; p < passes; ++p) {
            if (!eng.renderPass(blk)) { std::printf("render failed: %s\n", eng.faultReason().c_str()); return 1; }
            instr += eng.lastPassInstructions();
            out.insert(out.end(), blk[size_t(track)].begin(), blk[size_t(track)].end());
        }
        const auto t2 = std::chrono::steady_clock::now();
        double peak = 0, sum = 0;
        for (auto v : out) { peak = std::max(peak, std::abs(double(v))); sum += double(v) * v; }
        const double renderSec = std::chrono::duration<double>(t2 - t1).count();
        std::printf("boot %.0f ms; rendered %.2f s in %.3f s (%.1fx realtime, %.1f M DSP instr/s of audio); peak %.3f RMS %.4f\n",
                    std::chrono::duration<double, std::milli>(t1 - t0).count(), seconds, renderSec, seconds / renderSec,
                    double(instr) / seconds / 1e6, peak / 8388608.0, std::sqrt(sum / double(out.size())) / 8388608.0);
        writeWav(argv[3], out);
        std::printf("wrote %s\n", argv[3]);
    } catch (const std::exception& e) {
        std::printf("error: %s\n", e.what());
        return 1;
    }
    return 0;
}
