// md-render: plays Machinedrum machines from the user's MD OS file, with the knob values converted by the OS's own
// control handlers.
//   md-render <os.syx> list
//   md-render <os.syx> dis <1|2> <hex addr> [count] | grep <1|2> <text>   (DSP2 / DSP1 program disassembly)
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
#include "MdPreview.h"
#include "MdEngine.h"
#include <cmath>
#include <map>
#include <memory>
#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/opcodes.h"

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
        if ((std::strcmp(argv[2], "dis") == 0 || std::strcmp(argv[2], "grep") == 0) && argc > 4) {
            // md-render <os.syx> dis <1|2> <hex addr> [count]: DSP2 (1) or DSP1 (2) program words
            // md-render <os.syx> grep <1|2> <text>: every instruction whose disassembly contains the text
            const auto& img = std::atoi(argv[3]) == 2 ? fw.mixDsp : fw.voiceDsp;
            std::map<uint32_t, uint32_t> p;
            for (const auto& r : img.records)
                if (r.space == decltype(r.space)::P || r.addr >= 0x100000)
                    for (size_t k = 0; k < r.words.size(); ++k) p[r.addr + uint32_t(k)] = r.words[k];
            dsp56k::Opcodes ops;
            dsp56k::Disassembler dis(ops);
            auto line = [&](uint32_t a) {
                std::string s;
                const auto b = p.count(a + 1) ? p[a + 1] : 0;
                const auto n = dis.disassemble(s, p[a], b, 0, 0, a);
                return std::make_pair(s, n ? n : 1u);
            };
            if (argv[2][0] == 'd') {
                uint32_t a = uint32_t(std::strtoul(argv[4], nullptr, 16));
                const int count = argc > 5 ? std::atoi(argv[5]) : 40;
                for (int i = 0; i < count; ++i) { const auto [s, n] = line(a); std::printf("%06x  %06x  %s\n", a, p[a], s.c_str()); a += n; }
            } else {
                for (const auto& [a, w] : p) { const auto s = line(a).first; if (s.find(argv[4]) != std::string::npos) std::printf("%06x  %s\n", a, s.c_str()); }
            }
            return 0;
        }
        if (std::strcmp(argv[2], "cfhex") == 0 && argc > 3) {   // md-render <os.syx> cfhex <hex addr> [bytes]: MainOS bytes
            const uint32_t a = uint32_t(std::strtoul(argv[3], nullptr, 16));
            const int count = argc > 4 ? std::atoi(argv[4]) : 64;
            for (int i = 0; i < count; ++i) {
                if (i % 16 == 0) std::printf("%s%06x ", i ? "\n" : "", a + uint32_t(i));
                const uint32_t off = a + uint32_t(i) - kMainOsBase;
                std::printf(" %02x", off < fw.mainOs.size() ? fw.mainOs[off] : 0);
            }
            std::printf("\n");
            return 0;
        }
        if ((std::strcmp(argv[2], "cfdis") == 0 || std::strcmp(argv[2], "cfgrep") == 0) && argc > 3) {
            // md-render <os.syx> cfdis <hex addr> [count]: MainOS (ColdFire) instructions from addr
            // md-render <os.syx> cfgrep <text> [from hex] [to hex]: every instruction (linear sweep) whose text contains it
            ControlCpu cpu(fw.mainOs);
            std::string s;
            if (argv[2][2] == 'd') {
                uint32_t a = uint32_t(std::strtoul(argv[3], nullptr, 16));
                const int count = argc > 4 ? std::atoi(argv[4]) : 40;
                for (int i = 0; i < count; ++i) { const int n = cpu.disassemble(a, s); std::printf("%06x  %s\n", a, s.c_str()); a += uint32_t(n > 0 ? n : 2); }
            } else {
                const uint32_t from = argc > 4 ? uint32_t(std::strtoul(argv[4], nullptr, 16)) : kMainOsBase;
                const uint32_t to = argc > 5 ? uint32_t(std::strtoul(argv[5], nullptr, 16)) : kMainOsBase + uint32_t(fw.mainOs.size());
                for (uint32_t a = from; a < to;) {
                    const int n = cpu.disassemble(a, s);
                    if (s.find(argv[3]) != std::string::npos) std::printf("%06x  %s\n", a, s.c_str());
                    a += uint32_t(n > 0 ? n : 2);
                }
            }
            return 0;
        }
        if (std::strcmp(argv[2], "ctrlfo") == 0) {
            // md-render <os.syx> ctrlfo: T1 a held GND-SN; T3 = a CTR-GB (silent, master reverb), its LFO on its own SYN1
            // (DVOL): the track's live word, the master word and the main level, pass by pass. CTRFX=f CTRK=k: master
            // effect f parameter k instead (CTRFX=2 CTRK=7: EQ GAIN); CTRHAND=v: that parameter fixed at v, no LFO
            Engine e(fw);
            for (int tr = 0; tr < 16; ++tr) {
                Engine::Track x;
                x.level = 0;
                if (tr == 0) { x.machine = 1; x.params[0] = 64; x.params[1] = 127; x.params[10] = 64; x.params[13] = 127; x.params[17] = 127; x.params[18] = 64; x.level = 127; }
                if (tr == 2) {
                    x.params[0] = 64; x.params[21] = 100; x.params[22] = 127; x.params[23] = 0;
                    x.lfoConfig = {2, 0, 0, 0, 0};
                    x.ctrMasterFx = 0;
                } else x.lfoConfig = {uint8_t(tr), 0, 0, 0, 0};
                e.setTrack(tr, x);
            }
            const int hand = std::getenv("CTRHAND") ? std::atoi(std::getenv("CTRHAND")) : -1;   // a static master DVOL, no LFO
            const int fxi = std::getenv("CTRFX") ? std::atoi(std::getenv("CTRFX")) : 0, kk = std::getenv("CTRK") ? std::atoi(std::getenv("CTRK")) : 0;
            std::array<std::array<uint8_t, 8>, 4> mfx{{{64, 0, 64, 64, 0, 127, 0, 127}, {24, 0, 0, 32, 0, 127, 0, 127}, {64, 64, 64, 64, 64, 64, 64, 64}, {0, 64, 127, 0, 0, 0, 64, 0}}};
            if (hand >= 0) mfx[size_t(fxi)][size_t(kk)] = uint8_t(hand);
            e.setMasterFx(mfx);
            if (hand >= 0 || fxi != 0 || kk != 0) {
                Engine::Track x; x.level = 0; x.params[0] = mfx[size_t(fxi)][size_t(kk)]; x.params[size_t(kk)] = mfx[size_t(fxi)][size_t(kk)];
                x.params[21] = 100; x.params[22] = uint8_t(hand >= 0 ? 0 : 127); x.lfoConfig = {2, uint8_t(kk), 0, 0, 0}; x.ctrMasterFx = fxi;
                e.setTrack(2, x);
            }
            e.snap();
            e.render();
            e.trig(0, 1, 127);
            double acc = 0;
            for (int pass = 0; pass < 64; ++pass) {
                e.render();
                for (int i = 0; i < 32; ++i) { const double v = e.output()[size_t(i)][2] / 8388608.0; acc += v * v; }
                if (pass % 4 == 3) {
                    std::printf("pass %2d  T3 SYN1 live %5u   master DVOL live %5u   main rms %.4f\n", pass, e.cpu().liveParam(2, 0), e.cpu().liveLevel(16), std::sqrt(acc / 128));
                    acc = 0;
                }
            }
            return 0;
        }
        if (std::strcmp(argv[2], "tails") == 0) {
            // md-render <os.syx> tails [knob=value ...]: every synthesis machine trigged once at its default knobs (plugin
            // track defaults elsewhere), the level left after 0.5 / 2 / 5 s; "k3=127" sets SYNTHESIS knob 3 for all
            std::vector<std::pair<int, int>> overrides;
            for (int a = 3; a < argc; ++a) { int k = 0, v = 0; if (std::sscanf(argv[a], "k%d=%d", &k, &v) == 2 && k >= 1 && k <= 8) overrides.push_back({k - 1, v}); }
            for (int id = 0; id < 80; ++id) {
                const auto* m = fw.byId(id);
                if (!m || m->labels[0].empty()) continue;
                Engine e(fw);
                Engine::Track x;
                x.machine = id;
                for (int k = 0; k < 8; ++k) x.params[size_t(k)] = m->defaults[size_t(k)];
                for (auto [k, v] : overrides) x.params[size_t(k)] = uint8_t(v);
                const int fxd[8] = {0, 0, 64, 64, 0, 127, 0, 0};
                for (int k = 0; k < 8; ++k) x.params[size_t(8 + k)] = uint8_t(fxd[k]);
                x.params[17] = 100; x.params[18] = 64; x.params[22] = 0;
                if (const char* lp = std::getenv("TAIL_LFO")) {   // an LFO on SYNTHESIS knob n (0-7) of the track itself
                    x.lfoConfig = {0, uint8_t(std::atoi(lp)), 0, 0, 0}; x.params[21] = 64; x.params[22] = 48;
                }
                x.level = 127;
                e.setTrack(0, x);
                e.setMasterFx({{{127, 0, 64, 64, 0, 127, 0, 127}, {24, 0, 0, 32, 0, 127, 0, 127}, {64, 64, 64, 64, 64, 64, 64, 64}, {0, 64, 127, 0, 0, 0, 64, 0}}});
                e.snap(); e.render();
                e.trig(0, id, 100);
                const int passes = int(6.0 * 44100 / 32);
                double w[4] = {}; int n[4] = {};
                for (int pass = 0; pass < passes; ++pass) {
                    e.render();
                    const double sec = pass * 32 / 44100.0;
                    const int b = sec < 0.5 ? 0 : (sec >= 1.9 && sec < 2.1) ? 1 : (sec >= 4.9 && sec < 5.1) ? 2 : (sec >= 5.8 ? 3 : -1);
                    if (b < 0) continue;
                    for (int i = 0; i < 32; ++i) for (int c : {2, 5}) { const double v = e.output()[size_t(i)][size_t(c)] / 8388608.0; w[b] += v * v; ++n[b]; }
                }
                auto db = [&](int b) { return n[b] ? 10 * std::log10(std::max(w[b] / n[b], 1e-20)) : -200.0; };
                std::string knobs;
                for (int k = 0; k < 8; ++k) if (!m->labels[size_t(k)].empty()) knobs += m->labels[size_t(k)] + "=" + std::to_string(x.params[size_t(k)]) + " ";
                const bool held = db(2) > -60.0;
                std::printf("%s%-7s  0-0.5s %6.1f dB  2s %6.1f  5s %6.1f  6s %6.1f   %s\n", held ? "HELD " : "     ", m->name().c_str(), db(0), db(1), db(2), db(3), knobs.c_str());
            }
            return 0;
        }
        if (std::strcmp(argv[2], "handler") == 0 && argc > 3) {
            // md-render <os.syx> handler <machine> [k1..k8]: the machine's control handler called as the OS does, with the
            // buffer prefilled with a marker (0xAAAAAA), slot 0 = 0 / 1 / the DSP type: which words it writes, what it returns
            const mnm::md::Machine* m = nullptr;
            for (int id = 0; id < 192 && !m; ++id) if (const auto* c = fw.byId(id); c && c->name() == argv[3]) m = c;
            if (!m) { std::printf("no machine\n"); return 1; }
            std::array<uint16_t, 8> raw{};
            for (int k = 0; k < 8; ++k) raw[size_t(k)] = ControlCpu::rawFromValue(4 + k < argc && argv[4 + k][0] != '-' ? std::atoi(argv[4 + k]) : m->defaults[size_t(k)]);
            ControlCpu cpu(fw.mainOs);
            for (uint32_t slot0 : {0u, 1u, uint32_t(m->dspType())}) {
                std::array<uint32_t, ControlCpu::kMaxPacket> buf{};
                buf.fill(0xAAAAAA);
                const int n = cpu.convertInPlace(m->handler, slot0, raw, buf);
                std::printf("slot0 %3u -> returns %2d:", slot0, n);
                for (int k = 0; k < 14; ++k) std::printf(" %06x", buf[size_t(k)] & 0xFFFFFF);
                std::printf("\n");
            }
            return 0;
        }
        if (std::strcmp(argv[2], "ctrpreview") == 0) {
            // md-render <os.syx> ctrpreview: a library preview of a kit with T1 a held GND-SN and T2 a CTR-EQ (or, with
            // "al", a CTR-AL on T1's VOL) whose trigs lock GAIN 0 at step 5 and 127 at step 9: the level by quarter bar
            const bool al = argc > 3 && std::strcmp(argv[3], "al") == 0;
            mnm::mddump::Kit kit;
            for (int tr = 0; tr < 16; ++tr) { kit.trigGroups[tr] = 127; kit.muteGroups[tr] = 127; kit.lfos[tr][0] = uint8_t(tr); kit.levels[tr] = 100; }
            kit.models[0] = 1;   // GND-SN, long decay, filter open
            kit.params[0][0] = 64; kit.params[0][1] = 127; kit.params[0][10] = 64; kit.params[0][13] = 127; kit.params[0][17] = 100; kit.params[0][18] = 64;
            kit.levels[0] = 127;
            kit.models[1] = al ? 112 : 122;
            for (int k = 0; k < 24; ++k) kit.params[1][k] = 64;
            const uint8_t rev[8] = {127, 0, 64, 64, 0, 127, 0, 127}, del[8] = {24, 0, 0, 32, 0, 127, 0, 127}, dyn[8] = {0, 64, 127, 0, 0, 0, 64, 0};
            std::memcpy(kit.reverb, rev, 8); std::memcpy(kit.delay, del, 8); std::memset(kit.eq, 64, 8); std::memcpy(kit.dynamics, dyn, 8);
            mnm::mddump::Pattern pat;
            pat.length = 16;
            pat.accentEditAll = pat.slideEditAll = pat.swingEditAll = 1;
            pat.trigs[0] = 1;                            // the sine, once
            pat.trigs[1] = (1ull << 4) | (1ull << 8);    // the CTR track, steps 5 and 9
            const int q = al ? 17 : 7;                   // CTR-AL: VOL (onto T1); CTR-EQ: GAIN
            pat.lockMasks[1] = 1u << q;
            pat.numLockedRows = 1;
            for (int st = 0; st < 64; ++st) pat.locks[0][st] = 255;
            pat.locks[0][4] = 0; pat.locks[0][8] = 127;
            mnm::mdpreview::Options opt;
            opt.minSeconds = 0; opt.maxLoops = 1; opt.tailSeconds = 0;
            const auto spec = mnm::mdpreview::patternPreview(kit, pat, opt);
            mnm::mdpreview::Renderer rnd(fw);
            std::vector<double> e(8, 0.0);
            const uint32_t quarter = spec.frames / 8;
            rnd.render(spec, 120.0, [&](uint32_t f0, const mnm::mdpreview::Block& b) {
                for (int i = 0; i < mnm::mdpreview::Block::kFrames; ++i) { const size_t k = std::min<size_t>(7, (f0 + uint32_t(i)) / quarter); e[k] += double(b.mixL[size_t(i)]) * b.mixL[size_t(i)]; }
            });
            std::printf("%s lock on step 5 (0) and step 9 (127): main level per 1/8 of the bar:", al ? "CTR-AL VOL" : "CTR-EQ GAIN");
            for (double v : e) std::printf(" %.4f", std::sqrt(v / quarter));
            std::printf("\n");
            return 0;
        }
        if (std::strcmp(argv[2], "songs") == 0 && argc > 3) {   // md-render <os.syx> songs <dump.syx>: every song's rows, raw
            std::FILE* f = std::fopen(argv[3], "rb");
            std::vector<uint8_t> bytes;
            for (int c; f && (c = std::fgetc(f)) != EOF;) bytes.push_back(uint8_t(c));
            if (f) std::fclose(f);
            const auto d = mnm::mddump::parseDump(bytes.data(), bytes.size(), "dump");
            for (const auto& s : d.songs) {
                std::printf("song %2d '%s' %d rows\n", s.position, s.name.c_str(), int(s.rows.size()));
                for (size_t r = 0; r < s.rows.size() && r < 12; ++r) {
                    std::printf("   ");
                    for (int b = 0; b < 10; ++b) std::printf(" %02x", s.rows[r].bytes[b]);
                    std::printf("\n");
                }
            }
            return 0;
        }
        if (std::strcmp(argv[2], "kitfx") == 0 && argc > 3) {   // md-render <os.syx> kitfx <dump.syx>: each kit's master effects
            std::FILE* f = std::fopen(argv[3], "rb");
            std::vector<uint8_t> bytes;
            for (int c; f && (c = std::fgetc(f)) != EOF;) bytes.push_back(uint8_t(c));
            if (f) std::fclose(f);
            const auto d = mnm::mddump::parseDump(bytes.data(), bytes.size(), "dump");
            for (int k = 0; k < 64; ++k)
                if (const auto* kit = d.kitAt(k)) {
                    int revSends = 0;
                    for (int tr = 0; tr < 16; ++tr) revSends += kit->params[tr][20] > 0 ? 1 : 0;
                    std::printf("kit %2d %-16s REV", k, kit->name.c_str());
                    for (int i = 0; i < 8; ++i) std::printf(" %3d", kit->reverb[i]);
                    std::printf("   tracks sending to reverb: %d\n", revSends);
                }
            return 0;
        }
        if (std::strcmp(argv[2], "fxcheck") == 0) {
            // md-render <os.syx> fxcheck: every effect off vs on through the whole engine, measured on the main output.
            // Master: reverb and delay (via the track's REV / DEL sends), EQ, dynamix. Track: AMD/AMF, EQF/EQG, the
            // filter, SRR, DIST. Each line: what was measured, off -> on, and whether it moved the way it should.
            struct Setup {
                int machine = 1;                               // GND-SN
                std::array<uint8_t, 24> p{};
                std::array<std::array<uint8_t, 8>, 4> fx{{{0, 0, 64, 64, 0, 127, 127, 127}, {24, 0, 0, 32, 0, 127, 0, 127}, {64, 64, 64, 64, 64, 64, 64, 64}, {0, 64, 127, 0, 0, 0, 64, 0}}};
                double seconds = 2.0;
                bool retrig = false;                           // a trig every 0.5 s (sustained material)
            };
            auto base = [](int machine) {
                Setup s; s.machine = machine;
                for (int k = 0; k < 8; ++k) s.p[size_t(k)] = 64;
                if (machine == 1) { s.p[0] = 64; s.p[1] = 127; s.p[2] = 0; s.p[3] = 0; }   // a long, steady sine (no pitch ramp)
                if (machine == 2) { s.p[0] = 127; }                    // long noise
                s.p[10] = 64; s.p[11] = 64; s.p[13] = 127;             // EQF 64, EQG 0, filter open
                s.p[17] = 100; s.p[18] = 64;
                return s;
            };
            auto render = [&](const Setup& sIn) {
                Setup s = sIn;
                if (const char* o = std::getenv("REVFX"))   // e.g. REVFX=6:127,0:64  (reverb knob:value)
                    for (const char* c = o; *c;) {
                        int k = 0, v = 0, n = 0;
                        if (std::sscanf(c, "%d:%d%n", &k, &v, &n) != 2) break;
                        if (k >= 0 && k < 8) s.fx[0][size_t(k)] = uint8_t(v);
                        c += n; if (*c == ',') ++c;
                    }
                Engine e(fw);
                for (int tr = 0; tr < 16; ++tr) {
                    Engine::Track x;
                    x.level = tr == 0 ? 120 : 0;
                    x.machine = tr == 0 ? s.machine : 0;
                    if (tr == 0) x.params = s.p;
                    x.lfoConfig = {uint8_t(tr), 0, 0, 0, 0};
                    e.setTrack(tr, x);
                }
                e.setMasterFx(s.fx);
                e.setTempo(120.0);
                e.snap(); e.render();
                std::vector<double> out;
                const int passes = int(s.seconds * 44100 / 32);
                for (int pass = 0; pass < passes; ++pass) {
                    if (pass == 0 || (s.retrig && pass % int(0.5 * 44100 / 32) == 0)) e.trig(0, s.machine, 127);
                    e.render();
                    for (int i = 0; i < 32; ++i) out.push_back(0.5 * (e.output()[size_t(i)][2] + e.output()[size_t(i)][5]) / 8388608.0);
                }
                return out;
            };
            auto rms = [](const std::vector<double>& x, double a, double b) {
                const size_t i0 = size_t(a * 44100), i1 = std::min(x.size(), size_t(b * 44100));
                double s = 0; for (size_t i = i0; i < i1; ++i) s += x[i] * x[i];
                return i1 > i0 ? std::sqrt(s / double(i1 - i0)) : 0.0;
            };
            auto band = [](const std::vector<double>& x, bool high) {   // energy share below ~200 Hz / above ~4 kHz
                double lp = 0, prev = 0, eb = 0, et = 0;
                const double a = std::exp(-2 * 3.14159265 * (high ? 4000.0 : 200.0) / 44100);
                for (double v : x) {
                    lp = a * lp + (1 - a) * v;
                    const double b = high ? v - lp : lp;
                    eb += b * b; et += v * v; prev = v;
                }
                (void)prev;
                return et > 0 ? eb / et : 0.0;
            };
            auto wobble = [](const std::vector<double>& x, double a, double b) {   // 10 ms RMS windows: max / min
                double lo = 1e9, hi = 0;
                for (double t0 = a; t0 + 0.01 <= b; t0 += 0.01) {
                    const size_t i0 = size_t(t0 * 44100);
                    double s = 0; for (size_t i = i0; i < i0 + 441; ++i) s += x[i] * x[i];
                    const double r = std::sqrt(s / 441); lo = std::min(lo, r); hi = std::max(hi, r);
                }
                return hi / std::max(lo, 1e-9);
            };
            int fails = 0;
            auto report = [&](const char* what, double off, double on, bool ok, const char* unit = "") {
                std::printf("  %-5s %-58s %10.4g -> %-10.4g %s\n", ok ? "ok" : "FAIL", what, off, on, unit);
                fails += ok ? 0 : 1;
            };
            const auto db = [](double r) { return 20 * std::log10(std::max(r, 1e-12)); };

            std::printf("MASTER EFFECTS\n");
            {   // reverb: a short click (TRX-RS, short decay), REV send 0 vs 127; tail after the dry sound
                auto s = base(20); s.p[1] = 20; s.seconds = 2.5;
                const auto dry = render(s);
                s.p[20] = 127; const auto wet = render(s);
                report("REVERB: tail 0.4-1.5 s, REV send 0 -> 127 (dB)", db(rms(dry, 0.4, 1.5)), db(rms(wet, 0.4, 1.5)), rms(wet, 0.4, 1.5) > 10 * rms(dry, 0.4, 1.5) + 1e-6, "dB");
                auto l = s; l.fx[0][7] = 0; const auto lev0 = render(l);
                report("REVERB: LEV 127 -> 0 with the send up (tail dB)", db(rms(wet, 0.4, 1.5)), db(rms(lev0, 0.4, 1.5)), rms(lev0, 0.4, 1.5) < 0.1 * rms(wet, 0.4, 1.5), "dB");
                auto d1 = s; d1.fx[0][2] = 20; auto d2 = s; d2.fx[0][2] = 120;
                const auto shortT = render(d1), longT = render(d2);
                report("REVERB: DEC 20 -> 120, tail 1.0-2.0 s (dB)", db(rms(shortT, 1.0, 2.0)), db(rms(longT, 1.0, 2.0)), rms(longT, 1.0, 2.0) > 2 * rms(shortT, 1.0, 2.0), "dB");
                // a gated reverb: held noise for 1 s, then nothing; wet minus dry while it plays and after, by GATE
                for (int gate : {0, 64, 127}) {
                    auto h = base(2); h.seconds = 2.0; h.retrig = false; h.p[1] = 127; h.fx[0][6] = uint8_t(gate);
                    const auto hd = render(h);
                    h.p[20] = 127; const auto hw = render(h);
                    std::vector<double> diff(hd.size());
                    for (size_t i = 0; i < hd.size(); ++i) diff[i] = hw[i] - hd[i];
                    std::printf("        GATE %3d, held noise: reverb while playing %6.1f dB, after it stops (1.05-1.6 s) %6.1f dB\n", gate,
                                db(rms(diff, 0.2, 0.9)), db(rms(diff, 1.05, 1.6)));
                }
                auto g0 = s; g0.fx[0][6] = 0; const auto gated = render(g0);
                report("REVERB: GATE 127 -> 0 (a gated reverb: the tail cut once the input stops)", db(rms(wet, 0.4, 1.5)), db(rms(gated, 0.4, 1.5)), rms(gated, 0.4, 1.5) < 0.1 * rms(wet, 0.4, 1.5), "dB");
            }
            {   // delay: the click, DEL send 0 -> 127, FB 0; echo energy after the click
                auto s = base(20); s.p[1] = 20; s.seconds = 2.0; s.fx[1][3] = 0;
                const auto dry = render(s);
                s.p[19] = 127; const auto wet = render(s);
                report("DELAY: echoes 0.1-1.0 s, DEL send 0 -> 127 (dB)", db(rms(dry, 0.1, 1.0)), db(rms(wet, 0.1, 1.0)), rms(wet, 0.1, 1.0) > 10 * rms(dry, 0.1, 1.0) + 1e-6, "dB");
                // where the first echo lands (TIME 24 at 120 BPM)
                double best = 0; size_t at = 0;
                for (size_t i = size_t(0.05 * 44100); i < size_t(1.5 * 44100); ++i) if (std::abs(wet[i]) > best) { best = std::abs(wet[i]); at = i; }
                std::printf("        first echo peak at %.3f s (TIME 24)\n", double(at) / 44100);
                auto f = s; f.fx[1][3] = 100; const auto fb = render(f);
                report("DELAY: FB 0 -> 100, echoes 1.0-2.0 s (dB)", db(rms(wet, 1.0, 2.0)), db(rms(fb, 1.0, 2.0)), rms(fb, 1.0, 2.0) > 3 * rms(wet, 1.0, 2.0) + 1e-6, "dB");
                auto l = s; l.fx[1][7] = 0; const auto lev0 = render(l);
                report("DELAY: LEV 127 -> 0 with the send up (dB)", db(rms(wet, 0.1, 1.0)), db(rms(lev0, 0.1, 1.0)), rms(lev0, 0.1, 1.0) < 0.1 * rms(wet, 0.1, 1.0), "dB");
            }
            {   // DVOL: the delay's output into the reverb? A click to the delay only (REV send 0), GATE 127
                auto s = base(20); s.p[1] = 20; s.seconds = 3.0; s.p[19] = 127; s.fx[1][3] = 0; s.fx[0][6] = 127;
                s.fx[0][0] = 0; const auto d0 = render(s);
                s.fx[0][0] = 127; const auto d1 = render(s);
                std::vector<double> diff(d0.size());
                for (size_t i = 0; i < d0.size(); ++i) diff[i] = d1[i] - d0[i];
                report("REVERB: DVOL 0 -> 127 with only the delay sent: reverb of the echoes (dB of the difference)", -138.5, db(rms(diff, 0.5, 2.5)), rms(diff, 0.5, 2.5) > 1e-4, "dB");
            }
            {   // EQ on noise: LG / HG / GAIN
                auto s = base(2); s.seconds = 1.0; s.retrig = true;
                const auto flat = render(s);
                auto lo = s; lo.fx[2][1] = 127; const auto lowUp = render(lo);
                report("EQ: LG 64 -> 127, low-band share", band(flat, false), band(lowUp, false), band(lowUp, false) > 1.3 * band(flat, false));
                auto hi = s; hi.fx[2][3] = 0; const auto highDown = render(hi);
                report("EQ: HG 64 -> 0, high-band share", band(flat, true), band(highDown, true), band(highDown, true) < 0.9 * band(flat, true));
                auto g = s; g.fx[2][7] = 100; const auto gainUp = render(g);
                report("EQ: GAIN 64 -> 100, level (dB)", db(rms(flat, 0.05, 0.95)), db(rms(gainUp, 0.05, 0.95)), rms(gainUp, 0.05, 0.95) > 1.3 * rms(flat, 0.05, 0.95), "dB");
            }
            {   // dynamix on loud noise: MIX 0 -> 127 with a low threshold and a high ratio
                auto s = base(2); s.seconds = 1.0; s.retrig = true; s.p[17] = 127;
                const auto off = render(s);
                auto c = s; c.fx[3] = {0, 64, 10, 127, 0, 0, 64, 127}; const auto comp = render(c);
                report("DYNAMIX: MIX 0 -> 127, TRHD 10, RTIO 127: level (dB)", db(rms(off, 0.1, 0.95)), db(rms(comp, 0.1, 0.95)), std::abs(db(rms(comp, 0.1, 0.95)) - db(rms(off, 0.1, 0.95))) > 1.0, "dB");
                auto o = c; o.fx[3][6] = 127; const auto outg = render(o);
                report("DYNAMIX: OUTG 64 -> 127 (dB)", db(rms(comp, 0.1, 0.95)), db(rms(outg, 0.1, 0.95)), rms(outg, 0.1, 0.95) > 1.05 * rms(comp, 0.1, 0.95), "dB");
            }
            std::printf("TRACK EFFECTS\n");
            {
                auto s = base(1); s.seconds = 1.0; s.p[0] = 16;   // a sustained low sine
                const auto sine = render(s);
                {
                    int zc = 0; for (size_t i = size_t(0.2 * 44100); i < size_t(0.8 * 44100); ++i) zc += (sine[i - 1] < 0) != (sine[i] < 0);
                    std::printf("        the test sine (GND-SN PTCH %d): about %.0f Hz, RMS %.1f dB\n", s.p[0], zc / 2.0 / 0.6, db(rms(sine, 0.2, 0.8)));
                }
                auto am = s; am.p[8] = 127; am.p[9] = 8; const auto amd = render(am);   // AMF low: a tremolo
                {
                    double dd = 0, ss = 0; for (size_t i = 0; i < sine.size(); ++i) { dd += (amd[i] - sine[i]) * (amd[i] - sine[i]); ss += sine[i] * sine[i]; }
                    report("AMD 0 -> 127 (AMF 8): how much the signal changed", 0.0, std::sqrt(dd / ss), std::sqrt(dd / ss) > 0.1);
                }
                auto am2 = s; am2.p[8] = 127; am2.p[9] = 100; const auto amd2 = render(am2);   // AMF high: ring-mod sidebands
                {
                    double dd = 0, ss = 0; for (size_t i = 0; i < sine.size(); ++i) { dd += (amd2[i] - sine[i]) * (amd2[i] - sine[i]); ss += sine[i] * sine[i]; }
                    report("AMD 0 -> 127 (AMF 100): how much the signal changed", 0.0, std::sqrt(dd / ss), std::sqrt(dd / ss) > 0.3);
                }
                auto dist = s; dist.p[16] = 127; const auto d = render(dist);
                report("DIST 0 -> 127: high-band share", band(sine, true), band(d, true), band(d, true) > 2 * band(sine, true) + 1e-6);
                auto srr = s; srr.p[15] = 127; const auto sr = render(srr);
                report("SRR 0 -> 127: high-band share", band(sine, true), band(sr, true), band(sr, true) > 2 * band(sine, true) + 1e-6);
                auto n = base(2); n.seconds = 1.0; n.retrig = true;
                const auto noise = render(n);
                auto lp = n; lp.p[12] = 0; lp.p[13] = 20; const auto filt = render(lp);   // FLTF 0, FLTW 20: a low pass
                report("FILTER FLTW 127 -> 20 (FLTF 0): high-band share", band(noise, true), band(filt, true), band(filt, true) < 0.5 * band(noise, true));
                auto hp = n; hp.p[12] = 100; hp.p[13] = 27; const auto filt2 = render(hp);   // FLTF 100: a high pass
                report("FILTER FLTF 0 -> 100 (FLTW 27): low-band share", band(noise, false), band(filt2, false), band(filt2, false) < 0.5 * band(noise, false));
                auto eq = n; eq.p[10] = 10; eq.p[11] = 127; const auto teq = render(eq);   // EQF low, EQG +63
                report("EQ EQF 10, EQG 0 -> +63: low-band share", band(noise, false), band(teq, false), band(teq, false) > 1.3 * band(noise, false));
            }
            std::printf(fails ? "FX CHECK: %d FAILED\n" : "FX CHECK: all effects respond\n", fails);
            return fails ? 1 : 0;
        }
        if (std::strcmp(argv[2], "directcheck") == 0 && argc > 4) {
            // md-render <os.syx> directcheck <dump.syx> <kit>: each track of the kit alone, once in the mix and once on its
            // own output (Engine::setDirect); its own output against the main, sample by sample (neutral master section)
            std::FILE* f = std::fopen(argv[3], "rb");
            std::vector<uint8_t> bytes;
            for (int c; f && (c = std::fgetc(f)) != EOF;) bytes.push_back(uint8_t(c));
            if (f) std::fclose(f);
            const auto d = mnm::mddump::parseDump(bytes.data(), bytes.size(), "dump");
            const auto* kit = d.kitAt(std::atoi(argv[4]));
            if (!kit) { std::printf("no kit\n"); return 1; }
            double worst = -1e9;   // the largest own-output error, dB below the main, over the tracks that sound
            for (int s = 0; s < 16; ++s) {
                auto make = [&](bool direct) {
                    auto e = std::make_unique<Engine>(fw);
                    e->setMasterFx({{{127, 0, 64, 64, 0, 127, 0, 127}, {24, 0, 0, 32, 0, 127, 0, 127}, {64, 64, 64, 64, 64, 64, 64, 64}, {0, 64, 127, 0, 0, 0, 64, 0}}});
                    for (int tr = 0; tr < 16; ++tr) {
                        Engine::Track x;
                        x.machine = kit->model(tr);
                        std::copy(kit->params[tr], kit->params[tr] + 24, x.params.begin());
                        x.params[19] = x.params[20] = 0;   // no sends: the main is then the track alone
                        x.level = tr == s ? kit->levels[tr] : 0;
                        x.route = 6;
                        e->setTrack(tr, x);
                    }
                    e->setDirect(direct ? 1u << s : 0u);
                    e->snap(); e->render();
                    e->trig(s, kit->model(s), 100);
                    return e;
                };
                auto mixed = make(false), direct = make(true);
                double err = 0, ref = 0, mainLeft = 0, mo = 0, oo = 0;
                for (int pass = 0; pass < 40; ++pass) {
                    mixed->render(); direct->render();
                    for (int i = 0; i < 32; ++i)
                        for (int c = 0; c < 2; ++c) {
                            const double m = mixed->output()[size_t(i)][size_t(c == 0 ? 2 : 5)] / 8388608.0, o = direct->trackOut(s, c)[i];
                            const double dm = direct->output()[size_t(i)][size_t(c == 0 ? 2 : 5)] / 8388608.0;
                            err += (m - o) * (m - o); ref += m * m; mainLeft += dm * dm; mo += m * o; oo += o * o;
                        }
                }
                const double snr = ref > 0 ? 10 * std::log10(ref / std::max(err, 1e-30)) : 0;
                if (ref > 1e-6) worst = std::max(worst, -snr);
                std::printf("T%-2d %-7s main rms %.4f  own-output error %6.1f dB below it (best gain x%.5f); main with it direct: rms %.2e\n", s + 1,
                            fw.byId(kit->model(s)) ? fw.byId(kit->model(s))->name().c_str() : "?", std::sqrt(ref / 2560), -snr, oo > 0 ? mo / oo : 0.0, std::sqrt(mainLeft / 2560));
            }
            std::printf("worst own-output error %.1f dB (re the main)\n", worst);
            return 0;
        }
        if (std::strcmp(argv[2], "preview") == 0 && argc > 5) {
            // md-render <os.syx> preview <dump.syx> <pattern slot 0-127 | kNN = kit NN's demo> <out.wav> [bpm]: a library preview
            std::FILE* f = std::fopen(argv[3], "rb");
            if (!f) { std::printf("cannot read %s\n", argv[3]); return 1; }
            std::vector<uint8_t> bytes;
            for (int c; (c = std::fgetc(f)) != EOF;) bytes.push_back(uint8_t(c));
            std::fclose(f);
            const auto d = mnm::mddump::parseDump(bytes.data(), bytes.size(), "dump");
            mnm::mdpreview::Options opt;
            if (argc > 6) opt.bpm = std::atof(argv[6]);
            mnm::mdpreview::Spec spec;
            if (argv[4][0] == 'k') {
                const auto* k = d.kitAt(std::atoi(argv[4] + 1));
                if (!k) { std::printf("no such kit\n"); return 1; }
                spec = mnm::mdpreview::patternPreview(*k, mnm::mdpreview::demoPattern(*k), opt);
            } else {
                const auto* p = d.patternAt(std::atoi(argv[4]));
                const auto* k = p ? d.kitAt(p->kit) : nullptr;
                if (!p || !k) { std::printf("no such pattern / kit\n"); return 1; }
                spec = mnm::mdpreview::patternPreview(*k, *p, opt);
                int locks = 0, accents = 0;
                for (const auto& e : spec.events) { locks += e.locks.empty() ? 0 : 1; accents += e.accent != -128 ? 1 : 0; }
                std::printf("pattern %s on kit %s: %zu trigs (%d locked, %d accented), %d loops, swing %d%%\n", mnm::mddump::patternSlotName(p->position).c_str(),
                            k->name.c_str(), spec.events.size(), locks, accents, spec.loops, p->swingPercent());
            }
            mnm::mdpreview::Renderer r(fw);
            std::vector<int32_t> out;
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = r.render(spec, opt.bpm, [&](uint32_t, const mnm::mdpreview::Block& b) {
                for (int i = 0; i < b.kFrames; ++i) { out.push_back(int32_t(b.mixL[size_t(i)] * 8388607.0f)); out.push_back(int32_t(b.mixR[size_t(i)] * 8388607.0f)); }
            });
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!ok) { std::printf("render failed: %s\n", r.error().c_str()); return 1; }
            writeWav(argv[5], out, 2);
            std::printf("rendered %.2f s in %.0f ms\n", spec.frames / 44100.0, ms);
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
        const int n = cpu.convert(m->handler, true, raw, packet);
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
