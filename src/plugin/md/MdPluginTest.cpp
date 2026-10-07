// md-plugintest <os.syx> <out.wav> [ui.png]: headless check of Monomodule MD. Plays a two-bar beat through the
// plugin processor at 48 kHz (resampled from the engine's 44.1 kHz) and writes the stereo result; optionally renders
// the editor to a PNG.
#include "one/RomArt.h"
#include <cstdio>
#include <algorithm>
#include <map>
#include <cstring>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MdProcessor.h"
#include "MdEditor.h"
#include "MdPreview.h"
#include "MdMidiExport.h"
#include "MdMachineText.h"
#include "test/MdTests.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (const char* mos = std::getenv("MD_DUMP_MNM_LOGOS")) {   // dev: the Monomachine's group logos as text (MD_DUMP_MNM_LOGOS=<mnm os.syx>)
        std::string err;
        if (!mnm::uispec::ensureRomArt(std::filesystem::path(mos), &err)) { std::printf("%s\n", err.c_str()); return 1; }
        for (const char* gname : {"GND", "SID", "SWAVE", "DPRO", "FM+", "VO", "FX"}) {
            const auto* b = mnm::uispec::groupLogo(gname);
            if (!b) { std::printf("%s: none\n", gname); continue; }
            const auto lb = mnm::uispec::litBounds(*b);
            std::printf("%s %dx%d (lit %dx%d)\n", gname, b->w, b->h, lb.w, lb.h);
            for (int y = lb.y; y < lb.y + lb.h; ++y) { std::printf("    "); for (int x = lb.x; x < lb.x + lb.w; ++x) std::printf("%s", b->lit(x, y) ? "#" : "."); std::printf("\n"); }
        }
        return 0;
    }
    if (const char* mdos = std::getenv("MD_SCAN_ICONS")) {   // dev: every icon descriptor in the MD OS (the Monomachine's format), as text
        const auto fw = mnm::md::loadFirmware(std::filesystem::path(mdos));
        const auto& os = fw.mainOs;
        const uint32_t base = mnm::md::kMainOsBase, end = base + uint32_t(os.size());
        auto u32 = [&](uint32_t a) -> uint32_t { const size_t o = a - base; return (uint32_t(os[o]) << 24) | (uint32_t(os[o + 1]) << 16) | (uint32_t(os[o + 2]) << 8) | os[o + 3]; };
        const int minW = std::getenv("MD_SCAN_MINW") ? std::atoi(std::getenv("MD_SCAN_MINW")) : 12;
        int found = 0;
        for (uint32_t a = base; a + 20 <= end; a += 2) {
            const uint32_t w = u32(a), h = u32(a + 4), n = u32(a + 8), px = u32(a + 12), mask = u32(a + 16);
            if (w < 1 || w > 128 || h < 1 || h > 32 || n < 1 || n > 64 || (mask != px + 4 * w && mask != 0) || px < base || px + 4 * w > end || (mask && mask + 4 * w > end)) continue;
            ++found;
            if (int(w) < minW) continue;
            std::printf("@%06X w=%u h=%u n=%u\n", a, w, h, n);
            for (uint32_t r = 0; r < h; ++r) {
                std::printf("    ");
                for (uint32_t c = 0; c < w; ++c) std::printf("%s", (u32(px + 4 * c) >> (32 - h + r)) & 1 ? "#" : ".");
                std::printf("\n");
            }
        }
        if (const char* out = std::getenv("MD_SCAN_DUMP")) { if (FILE* f = std::fopen(out, "wb")) { std::fwrite(os.data(), 1, os.size(), f); std::fclose(f); } }   // the decoded MainOS
        std::printf("%d descriptors\n", found);
        return 0;
    }
    if (std::getenv("MD_PRINT_SIZE")) { std::printf("sizeof(MdProcessor) = %zu\n", sizeof(mnm::plugin::md::MdProcessor)); return 0; }
    if (argc < 3) { std::printf("usage: md-plugintest <os.syx> <out.wav> [ui.png]\n"); return 2; }
    using namespace mnm::plugin::md;
    auto procHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
    auto& proc = *procHeap;
    proc.setFirmwarePath(juce::String(argv[1]), false);
    std::printf("%s\n", proc.statusText().toRawUTF8());
    if (!proc.engineReady()) {   // without an OS: the editor's first-run screen (md-plugintest <bad path> x.wav ui.png)
        if (argc > 3) {
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            if (auto* med = dynamic_cast<MdEditor*>(ed.get())) med->refresh();
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::File png{juce::String(argv[3])};
            png.deleteFile();
            juce::PNGImageFormat pf;
            if (auto s = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *s);
            std::printf("wrote %s\n", argv[3]);
        }
        return 1;
    }
    const double rate = 48000.0;
    const int block = 480;
    proc.setPlayConfigDetails(0, 2, rate, block);
    proc.prepareToPlay(rate, block);
    test::TestEnv env{proc, rate, block, argc, argv};
    if (const char* v = std::getenv("MD_MIDI_TEST")) return test::mdMidiTest(env, v);
    if (const char* v = std::getenv("MD_SONGED_TEST")) return test::mdSongedTest(env, v);
    if (const char* v = std::getenv("MD_REC_TEST")) return test::mdRecTest(env, v);
    if (const char* v = std::getenv("MD_GRID_TEST")) return test::mdGridTest(env, v);
    if (const char* v = std::getenv("MD_EDIT_TEST")) return test::mdEditTest(env, v);
    if (const char* v = std::getenv("MD_SEQ_TEST")) return test::mdSeqTest(env, v);
    if (const char* v = std::getenv("MD_GROUP_TEST")) return test::mdGroupTest(env, v);
    if (const char* v = std::getenv("MD_XTRA_TEST")) return test::mdXtraTest(env, v);
    bool kitLoaded = false;
    // MD_OUTS_TEST: every output bus live, PER TRACK outputs (MD_OUTS_OFF=n leaves track n's bus off: it stays on
    // the main); prints each bus's level. MD_OUTS_TEST=hw: the same buses in HARDWARE mode
    const char* outsTest = std::getenv("MD_OUTS_TEST");
    if (outsTest) {
        proc.enableAllBuses();
        if (const char* off = std::getenv("MD_OUTS_OFF")) {
            auto layout = proc.getBusesLayout();
            layout.outputBuses.getReference(3 + std::atoi(off) - 1) = juce::AudioChannelSet::disabled();
            proc.setBusesLayout(layout);
        }
        if (std::strcmp(outsTest, "hw") != 0)
            if (auto* p = proc.apvts.getParameter(outputModeId())) p->setValueNotifyingHost(1.0f);
        proc.prepareToPlay(rate, block);
    }
    std::vector<double> busEnergy(size_t(proc.getBusCount(false)), 0.0);
    std::array<float, kTracks> meterMax{};   // MD_METER: the LEV meter's highest reading per track over the render
    if (std::getenv("MD_ROM_TEST")) {   // a 220 Hz WAV into ROM-01 on track 1, then the state round trip into a 2nd instance
        juce::File wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-rom-test.wav");
        wav.deleteFile();
        {
            juce::AudioBuffer<float> b(1, 44100);
            for (int i = 0; i < 44100; ++i) b.setSample(0, i, 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * float(i) / 44100.0f) * (1.0f - float(i) / 44100.0f));
            juce::WavAudioFormat fmt;
            auto stream = std::unique_ptr<juce::FileOutputStream>(wav.createOutputStream());
            if (auto w = std::unique_ptr<juce::AudioFormatWriter>(fmt.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0))) { stream.release(); w->writeFromAudioSampleBuffer(b, 0, 44100); }
        }
        if (auto* p = proc.apvts.getParameter(machineId(0))) p->setValueNotifyingHost(p->convertTo0to1(float(machineIndexOf(128))));
        static const int romDefaults[8] = {64, 64, 127, 0, 0, 127, 0, 64};   // no message loop here: set the ROM knob defaults
        for (int k = 0; k < 8; ++k) if (auto* p = proc.apvts.getParameter(knobId(0, k))) p->setValueNotifyingHost(p->convertTo0to1(float(romDefaults[k])));
        const auto err = proc.loadSample(0, wav);
        std::printf("ROM test: load '%s' -> %s (%.2f s, memory %.1f%%)\n", wav.getFileName().toRawUTF8(), err.isEmpty() ? "ok" : err.toRawUTF8(),
                    proc.sampleSeconds(0), proc.sampleMemoryUsed() * 100.0);
        juce::MemoryBlock state;
        proc.getStateInformation(state);
        auto otherHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
        auto& other = *otherHeap;
        other.setFirmwarePath(juce::String(argv[1]), false);
        other.setStateInformation(state.getData(), int(state.getSize()));
        std::printf("state %zu bytes; restored instance has '%s' (%.2f s) in slot 0\n", state.getSize(), other.sampleName(0).toRawUTF8(), other.sampleSeconds(0));
    }
    const bool ramTest = std::getenv("MD_RAM_TEST") != nullptr;
    if (ramTest) {   // T1 RAM-R1 records the main mix (MLEV 32, MD_RAM_MLEV) in bar 1, T2 RAM-P1 plays it in bar 2, T3 TRX-BD kicks in bar 1
        auto set = [&](const juce::String& id, float v) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
        set(machineId(0), float(machineIndexOf(160)));
        int rec[8] = {32, 64, 0, 64, 0, 0, 127, 127};   // MLEV 32: the full mix is hot, higher levels clip
        if (const char* m = std::getenv("MD_RAM_MLEV")) rec[0] = std::atoi(m);
        for (int k = 0; k < 8; ++k) set(knobId(0, k), float(rec[k]));
        set(machineId(1), float(machineIndexOf(162)));
        const int play[8] = {64, 64, 127, 0, 0, 127, 0, 64};
        for (int k = 0; k < 8; ++k) set(knobId(1, k), float(play[k]));
        set(machineId(2), float(machineIndexOf(16)));
        std::printf("RAM test: RAM-R1 records the kicks of bar 1, RAM-P1 plays them in bar 2\n");
    }
    if (const char* v = std::getenv("MD_OSCHECK_TEST")) return test::mdOscheckTest(env, v);
    if (const char* v = std::getenv("MD_RATE_TEST")) return test::mdRateTest(env, v);
    if (const char* v = std::getenv("MD_FOUR_TEST")) return test::mdFourTest(env, v);
    if (const char* v = std::getenv("MD_MIXER_TEST")) return test::mdMixerTest(env, v);
    if (const char* v = std::getenv("MD_ARROW_TEST")) return test::mdArrowTest(env, v);
    if (const char* syx = std::getenv("MD_LIB_IMPORT")) {   // the shared library: import, load a kit, a sound, save both
        juce::SharedResourcePointer<MdLibrary> lib;
        juce::String id;
        const auto r = lib->importSyx(juce::File(juce::String(syx)), &id);
        juce::String again;
        const auto r2 = lib->importSyx(juce::File(juce::String(syx)), &again);   // the same file twice is one project
        const auto kits = lib->kits();
        std::printf("library %s: import %s, again %s (%s), %zu kits, %zu sounds\n", lib->model().projects().empty() ? "?" : "ok",
                    r.wasOk() ? "ok" : r.getErrorMessage().toRawUTF8(), r2.wasOk() ? "ok" : "failed", again == id ? "same project" : "ANOTHER PROJECT",
                    kits.size(), lib->sounds(-1).size());
        proc.setTrackLocked(15, true);   // LOCK: track 16 keeps its machine through the kit load
        const auto lockedBefore = proc.machineIdOf(15);
        for (const auto& k : kits) if (k.sourceId == id) {
            mnm::mddump::Kit kit;
            const bool ok = lib->loadKit(k.key, kit);
            const int emptied = proc.loadMdKit(k.key, kit, k.name);
            std::printf("locked track 16: machine %d before, %d after the load (%s)\n", lockedBefore, proc.machineIdOf(15), lockedBefore == proc.machineIdOf(15) ? "kept" : "CHANGED");
            std::printf("loaded '%s' (%s, %d emptied), modified=%d; the capture re-encodes to the kit: %s\n", k.name.toRawUTF8(), ok ? "ok" : "FAILED", emptied, int(proc.kitModified()),
                        [&] {
                auto c = proc.captureMdKit();
                for (int t = 0; t < 15; ++t) {
                    for (int i = 0; i < 24; ++i) if (c.params[t][i] != kit.params[t][i]) { std::printf("  T%d param %d: %d vs %d\n", t + 1, i, c.params[t][i], kit.params[t][i]); return "NO"; }
                    for (int i = 0; i < 36; ++i) if (c.lfos[t][i] != kit.lfos[t][i]) { std::printf("  T%d lfo byte %d: %d vs %d\n", t + 1, i, c.lfos[t][i], kit.lfos[t][i]); return "NO"; }
                    if (c.levels[t] != kit.levels[t]) { std::printf("  T%d level\n", t + 1); return "NO"; }
                }
                return "yes"; }());
            if (auto* p = proc.apvts.getParameter(knobId(0, 0))) p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.1f : 0.9f);
            std::printf("after a knob turn: kit modified=%d, T1 sound modified=%d\n", int(proc.kitModified()), int(proc.soundModified(0)));
            juce::String kitKey, soundKey;
            const auto s1 = lib->saveKit("MY " + k.name, proc.captureMdKit(), k.key, &kitKey);
            proc.setLoadedKit(kitKey, "MY " + k.name);
            const auto s2 = lib->saveSound("MY SOUND", proc.captureSound(0), proc.loadedSoundKey(0), &soundKey);
            proc.setLoadedSound(0, soundKey, "MY SOUND");
            mnm::mdcatalog::Sound back;
            std::printf("saved kit %s, sound %s; the sound reloads: %s; modified=%d/%d\n", s1.wasOk() ? "ok" : "FAILED", s2.wasOk() ? "ok" : "FAILED",
                        lib->loadSound(soundKey, back) ? "yes" : "NO", int(proc.kitModified()), int(proc.soundModified(0)));
            // a sound from another kit onto track 2
            const auto sounds = lib->sounds(-1);
            if (!sounds.empty()) { mnm::mdcatalog::Sound s; lib->loadSound(sounds.back().key, s); std::printf("sound '%s' onto T2: %s\n", sounds.back().name.toRawUTF8(), proc.loadSound(1, sounds.back().key, s, sounds.back().name) ? "ok" : "machine not available"); }
            kitLoaded = true;
            break;
        }
    }
    if (const char* v = std::getenv("MD_CTR_TEST")) return test::mdCtrTest(env, v);
    if (const char* v = std::getenv("MD_PARITY_TEST")) return test::mdParityTest(env, v);
    const bool auditionTest = std::getenv("MD_AUDITION") != nullptr;
    if (auditionTest) {   // a library kit auditioned (no notes): the output is the preview alone
        juce::SharedResourcePointer<MdLibrary> lib;
        const auto kits = lib->kits();
        if (kits.empty()) { std::printf("audition: the library has no MD kits\n"); return 1; }
        const auto& cat = lib->model().mdCatalog();
        const auto* k = cat.kit(kits.back().key.toStdString());
        const auto best = mnm::mdpreview::choosePreviewPattern(cat, *k);
        const auto* pat = cat.pattern(best);
        const auto kitData = k->kit;
        mnm::mdpreview::Options opt;
        if (pat) { const auto pd = pat->pattern; proc.previewPlay(kits.back().key, [kitData, pd, opt] { return mnm::mdpreview::patternPreview(kitData, pd, opt); }); }
        else proc.previewPlay(kits.back().key, [kitData, opt] { return mnm::mdpreview::patternPreview(kitData, mnm::mdpreview::demoPattern(kitData), opt); });
        std::printf("audition '%s' (%s): key %s, status '%s'\n", kits.back().name.toRawUTF8(), pat ? "its pattern" : "demo", proc.previewKey().isNotEmpty() ? "set" : "EMPTY", proc.previewStatus().toRawUTF8());
        juce::Thread::sleep(1500);   // let the render thread get ahead (the harness runs faster than real time)
    }
    if (std::getenv("MD_MUTE_TEST"))   // MUTE: the kick (track 1) ignores its trigs
        if (auto* p = proc.apvts.getParameter(muteId(0))) p->setValueNotifyingHost(1.0f);
    const bool lfoTest = std::getenv("MD_LFO_TEST") != nullptr;
    const bool velTest = std::getenv("MD_VEL_TEST") != nullptr;
    if (lfoTest) {   // track 1: GND-SN, long decay, LFO on its own PTCH (SYN1), depth 127, SPD 32
        auto set = [&](const juce::String& id, float v) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
        set(machineId(0), float(machineIndexOf(1)));
        set(knobId(0, 0), 64.0f); set(knobId(0, 1), 127.0f);
        set(lfoId(0, 0), 0.0f); set(lfoId(0, 1), 0.0f); set(lfoId(0, 2), 0.0f); set(lfoId(0, 3), 0.0f);
        set(lfoId(0, 5), 32.0f); set(lfoId(0, 6), 127.0f); set(lfoId(0, 7), 0.0f);
        std::printf("LFO test: GND-SN with an LFO on PTCH\n");
    }
    if (argc > 5) {   // md-plugintest <os> <out.wav> <ui.png> <dump.syx> <kit position>
        const auto kits = mnm::md::loadKits(argv[4]);
        const int pos = std::atoi(argv[5]);
        for (const auto& k : kits)
            if (k.position == pos) {
                const int emptied = proc.applyKit(k);
                std::printf("kit %d '%s' loaded (%d tracks emptied)\n", pos, k.name.c_str(), emptied);
                kitLoaded = true;
            }
    }

    // 120 BPM, 16th notes: kick 1, snare 2, closed hat 7, clap 4, open hat 8 on offbeats
    const double bpm = 120.0, stepSec = 60.0 / bpm / 4.0;
    const int steps = 32;
    const int total = int(steps * stepSec * rate) + int(rate);
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        juce::AudioBuffer<float> buf(std::max(2, proc.getTotalNumOutputChannels()), n);
        juce::MidiBuffer midi;
        for (int s = 0; s < steps; ++s) {
            const int at = int(s * stepSec * rate);
            if (at < pos || at >= pos + n) continue;
            auto note = [&](int nn, int vel = 100) { midi.addEvent(juce::MidiMessage::noteOn(1, nn, uint8_t(vel)), at - pos); };
            if (velTest) {   // kick at 127, 64, 20 (VOLUME mode), then 100 and 120 (ACCENT mode)
                static const int vels[5] = {127, 64, 20, 100, 120};
                if (s % 6 == 0 && s / 6 < 5) {
                    if (s / 6 == 3) if (auto* p = proc.apvts.getParameter(velModeId())) p->setValueNotifyingHost(1.0f);
                    note(36, vels[s / 6]);
                }
                continue;
            }
            if (auditionTest) continue;
            if (lfoTest || std::getenv("MD_ROM_TEST")) { if (s == 0) note(36); continue; }
            if (ramTest) {
                if (s == 0) note(36);                       // T1 RAM-R1: start recording
                if (s < 16 && s % 4 == 0) note(40);         // T3 kicks, bar 1 only
                if (s == 16) note(38);                      // T2 RAM-P1: play the recording
                continue;
            }
            if (kitLoaded) {   // a busier pattern over all 16 tracks
                for (int t = 0; t < kTracks; ++t)
                    if ((s * (t + 3) + t) % (t < 2 ? 4 : 7) == 0) note(kTrackNotes[t]);
                continue;
            }
            if (s % 4 == 0) note(36);                 // kick
            if (s % 8 == 4) note(38);                 // snare
            if (s % 2 == 0) note(48);                 // closed hat (track 8 = TRX-CH... track 7)
            if (s % 4 == 2) note(50);                 // open hat
            if (s == 14 || s == 30) note(41);         // clap
            if (s % 8 == 7) note(53);                 // claves
        }
        proc.processBlock(buf, midi);
        if (std::getenv("MD_METER")) for (int t = 0; t < kTracks; ++t) meterMax[size_t(t)] = std::max(meterMax[size_t(t)], proc.trackPeak(t));
        for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
        for (int b = 0; outsTest && b < proc.getBusCount(false); ++b) {
            if (!proc.getBus(false, b)->isEnabled()) continue;
            auto bb = proc.getBusBuffer(buf, false, b);
            for (int c = 0; c < bb.getNumChannels(); ++c) for (int i = 0; i < n; ++i) busEnergy[size_t(b)] += double(bb.getSample(c, i)) * bb.getSample(c, i);
        }
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    float peak = 0; double sum = 0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) { const float v = out.getSample(c, i); peak = std::max(peak, std::abs(v)); sum += double(v) * v; }
    std::printf("rendered %.2f s in %.0f ms (%.1fx realtime); peak %.3f RMS %.4f\n", total / rate, ms, total / rate * 1000.0 / ms, peak, std::sqrt(sum / (2.0 * total)));
    if (ramTest) {   // the sample manager's RAM > ROM: RAM-R1's recording into ROM-11, and the RAM machines empty after
        const double recSec = proc.ramSeconds(0);
        const auto err = proc.copyRamToRom(0, 10);
        double e = 0, r = 0;
        const auto back = proc.engineForTests()->voices().readSlot(10, &r);
        for (float v : back) e += double(v) * v;
        std::printf("RAM > ROM: RAM 1 held %.2f s; copy %s; ROM-11 now %.2f s (%s, RMS %.4f, %.0f Hz); RAM 1 after: %.2f s\n", recSec,
                    err.isEmpty() ? "ok" : err.toRawUTF8(), proc.sampleSeconds(10), proc.sampleName(10).toRawUTF8(),
                    back.empty() ? 0.0 : std::sqrt(e / double(back.size())), r, proc.ramSeconds(0));
        std::printf(recSec > 0.1 && err.isEmpty() && std::abs(proc.sampleSeconds(10) - recSec) < 0.05 && e > 0 && proc.ramSeconds(0) == 0.0 ? "RAM TO ROM OK\n" : "RAM TO ROM FAILED\n");
    }

    if (std::getenv("MD_METER")) {
        std::printf("meter peaks:");
        for (int t = 0; t < kTracks; ++t) std::printf(" T%d %.3f", t + 1, meterMax[size_t(t)]);
        std::printf("\n");
    }
    if (outsTest) {
        std::printf("bus levels (RMS):");
        for (int b = 0; b < proc.getBusCount(false); ++b)
            std::printf("%s %s %.4f", b % 5 == 0 ? "\n " : "", proc.getBus(false, b)->getName().toRawUTF8(),
                        proc.getBus(false, b)->isEnabled() ? std::sqrt(busEnergy[size_t(b)] / (2.0 * total)) : -1.0);
        std::printf("\n");
    }
    juce::File wav{juce::String(argv[2])};
    wav.deleteFile();
    juce::WavAudioFormat fmt;
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(wav.createOutputStream())) {
        if (auto w = std::unique_ptr<juce::AudioFormatWriter>(fmt.createWriterFor(stream.get(), rate, 2, 16, {}, 0))) {
            stream.release();
            w->writeFromAudioSampleBuffer(out, 0, total);
            std::printf("wrote %s\n", argv[2]);
        }
    }
    if (argc > 3) {
        if (std::getenv("MD_DROP_TEST")) {   // samples dropped on the editor: onto a non-ROM track, then two at once
            int fails = 0;
            auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
            auto writeWav = [](const juce::File& f, double hz) {
                f.deleteFile();
                juce::WavAudioFormat fmt;
                std::unique_ptr<juce::FileOutputStream> s(f.createOutputStream());
                std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(s.get(), 44100.0, 1, 16, {}, 0));
                if (!w) return;
                s.release();
                juce::AudioBuffer<float> b(1, 22050);
                for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * float(hz) * float(i) / 44100.0f));
                w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
            };
            const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
            const auto a = dir.getChildFile("md_drop_a.wav"), b = dir.getChildFile("md_drop_b.wav"), c = dir.getChildFile("md_drop_c.wav");
            writeWav(a, 220.0); writeWav(b, 330.0); writeWav(c, 440.0);
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            auto* med = dynamic_cast<MdEditor*>(ed.get());
            check(med->isInterestedInFileDrag({a.getFullPathName()}), "a .wav is accepted for dropping");
            check(!med->isInterestedInFileDrag({dir.getChildFile("notes.txt").getFullPathName()}), "a .txt is not");
            med->filesDropped({a.getFullPathName()}, 0, 0);   // onto the selected track (T1, TRX-BD)
            proc.syncMachineSideEffects();
            check(proc.machineIdOf(0) == 128 && proc.sampleName(0) == "md_drop_a", "T1 became ROM-01 holding md_drop_a (machine " + juce::String(proc.machineIdOf(0)) + ", slot 1 '" + proc.sampleName(0) + "')");
            med->filesDropped({b.getFullPathName(), c.getFullPathName()}, 0, 0);   // onto the ROM track: replace, then the next empty slot
            proc.syncMachineSideEffects();
            check(proc.sampleName(0) == "md_drop_b" && proc.sampleName(1) == "md_drop_c", "two files: ROM-01 replaced ('" + proc.sampleName(0) + "'), the second into ROM-02 ('" + proc.sampleName(1) + "')");
            // it plays: a trig of T1
            juce::AudioBuffer<float> buf(2, 4800);
            double e = 0;
            for (int blk = 0; blk < 10; ++blk) {
                juce::MidiBuffer midi;
                if (blk == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(110)), 0);
                buf.clear();
                proc.processBlock(buf, midi);
                for (int i = 0; i < buf.getNumSamples(); ++i) e += double(buf.getSample(0, i)) * buf.getSample(0, i);
            }
            check(e > 1.0, "T1 plays the dropped sample (energy " + juce::String(e, 2) + ")");
            a.deleteFile(); b.deleteFile(); c.deleteFile();
            std::printf(fails ? "DROP TEST FAILED (%d)\n" : "DROP TEST OK\n", fails);
            return fails ? 1 : 0;
        }
        if (const char* st = std::getenv("MD_UI_BADGES")) mnm::plugin::md::text::romBadges().style.store(std::atoi(st));   // 0 framed, 1 bare, -1 the drawn logos
        if (const char* m = std::getenv("MD_UI_MACHINE"))   // track 1's machine for the snapshot (an MD machine ID)
            if (auto* pp = proc.apvts.getParameter(machineId(0))) { pp->setValueNotifyingHost(pp->convertTo0to1(float(machineIndexOf(std::atoi(m))))); proc.syncMachineSideEffects(); }
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        if (auto* med = dynamic_cast<MdEditor*>(ed.get())) {
            med->refresh();
            if (const char* pk = std::getenv("MD_UI_PICKER")) med->showMachinePicker(std::atoi(pk) > 1 ? machineIndexOf(std::atoi(pk)) : -1);   // the picker open (=<id>: that machine hovered)
            if (const char* keys = std::getenv("MD_UI_PICKKEYS")) {   // arrow keys into the open picker: L R U D, E = Return
                for (const char* c = keys; *c; ++c) {
                    const int code = *c == 'L' ? juce::KeyPress::leftKey : *c == 'R' ? juce::KeyPress::rightKey : *c == 'U' ? juce::KeyPress::upKey
                                   : *c == 'D' ? juce::KeyPress::downKey : *c == 'E' ? juce::KeyPress::returnKey : 0;
                    if (code) med->pickerKey(juce::KeyPress(code));
                }
                med->refresh();
                std::printf("after keys %s: track 1 machine %s\n", keys, kMachines[size_t(std::lround(proc.apvts.getRawParameterValue(machineId(0))->load()))].name);
            }
            if (std::getenv("MD_UI_SAMPLES")) { proc.renameSample(0, "X"); med->devOpenSamples(); med->refresh(); }   // dev: the sample manager
            if (std::getenv("MD_UI_GRIDCLICK")) { med->devBarPart(MdSeqBar::Grid); med->refresh(); }   // dev: GRID clicked on a fresh plugin
            if (const char* xm = std::getenv("MD_UI_XTRA")) {   // dev: an extras window (4 condition, 5 micro, 6 retrig) with some values
                if (auto* a = proc.apvts.getParameter(extrasId())) a->setValueNotifyingHost(1.0f);
                med->showGrid(-1);
                for (int s : {0, 3, 4, 8, 10, 12, 14}) med->devStep(s);
                const int kind = juce::jlimit(4, 6, std::atoi(xm)) - 4;
                for (int s : {0, 4, 8, 12}) {
                    med->devExtraPaint(s, kind, true, true);
                    for (int k = 0; k < s / 4; ++k) med->devExtraWheel(s, kind, kind == 1 ? -5 : 3, false);
                }
                med->devMarkMode(kind + 4);
                med->refresh();
            }
            if (std::getenv("MD_UI_KITS")) med->showKitList();
            if (std::getenv("MD_UI_ABOUT")) med->showAbout();
            if (std::getenv("MD_UI_SKIN")) med->showSkinDialog();
            if (const char* tab = std::getenv("MD_UI_PANEL")) med->showLibrary(std::atoi(tab));   // 0 sounds, 1 kits, 2 patterns          // the kit list open under the header
        }
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File png{juce::String(argv[3])};
        png.deleteFile();
        juce::PNGImageFormat pf;
        if (auto s = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *s);
        std::printf("wrote %s\n", argv[3]);
    }
    return 0;
}
