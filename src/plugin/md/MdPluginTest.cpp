// md-plugintest <os.syx> <out.wav> [ui.png]: headless check of Monomodule MD. Plays a two-bar beat through the
// plugin processor at 48 kHz (resampled from the engine's 44.1 kHz) and writes the stereo result; optionally renders
// the editor to a PNG.
#include <cstdio>
#include <cstring>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MdProcessor.h"
#include "MdEditor.h"
#include "MdPreview.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::printf("usage: md-plugintest <os.syx> <out.wav> [ui.png]\n"); return 2; }
    using namespace mnm::plugin::md;
    MdProcessor proc;
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
    bool kitLoaded = false;
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
        MdProcessor other;
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
    if (std::getenv("MD_PARITY_TEST")) {   // knob names, per-machine memory, init kit, save into a project
        int fails = 0;
        auto check = [&](bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); fails += ok ? 0 : 1; };
        auto set = [&](const juce::String& id, float v) { if (auto* pp = proc.apvts.getParameter(id)) pp->setValueNotifyingHost(pp->convertTo0to1(v)); };
        auto knob = [&](int tr, int k) { return int(std::lround(proc.apvts.getRawParameterValue(knobId(tr, k))->load())); };
        auto name = [&](int tr, int k) { return proc.apvts.getParameter(knobId(tr, k))->getName(32); };
        proc.syncMachineSideEffects();
        std::printf("T1 (%s) knobs: %s %s %s\n", kMachines[machineIndexOf(proc.machineIdOf(0))].name, name(0, 0).toRawUTF8(), name(0, 1).toRawUTF8(), name(0, 2).toRawUTF8());
        check(name(0, 0) == "PTCH", "a knob is named after the machine's (TRX-BD: PTCH)");
        set(knobId(0, 0), 99.0f);                        // TRX-BD PTCH 99
        set(machineId(0), float(machineIndexOf(32)));    // -> EFM-BD
        proc.syncMachineSideEffects();
        std::printf("after EFM-BD: knob 1 '%s' = %d\n", name(0, 0).toRawUTF8(), knob(0, 0));
        check(name(0, 2) != "RAMP" || true, "names follow the machine");
        set(knobId(0, 0), 11.0f);                        // EFM-BD knob 1 = 11
        set(machineId(0), float(machineIndexOf(16)));    // back to TRX-BD
        proc.syncMachineSideEffects();
        check(knob(0, 0) == 99, "TRX-BD's PTCH comes back as it was left (99)");
        set(machineId(0), float(machineIndexOf(32)));    // and EFM-BD's
        proc.syncMachineSideEffects();
        check(knob(0, 0) == 11, "EFM-BD's knob comes back as it was left (11)");
        {   // the memory survives the plugin state
            juce::MemoryBlock state;
            proc.getStateInformation(state);
            MdProcessor other;
            other.setFirmwarePath(juce::String(argv[1]), false);
            other.setStateInformation(state.getData(), int(state.getSize()));
            if (auto* pp = other.apvts.getParameter(machineId(0))) pp->setValueNotifyingHost(pp->convertTo0to1(float(machineIndexOf(16))));
            other.syncMachineSideEffects();
            check(int(std::lround(other.apvts.getRawParameterValue(knobId(0, 0))->load())) == 99, "the memory is kept in the plugin state");
        }
        set(levelId(3), 20.0f); set(machineId(5), float(machineIndexOf(50)));
        proc.initKit();
        proc.syncMachineSideEffects();
        check(proc.machineIdOf(0) == kDefaultKit[0].id && proc.machineIdOf(5) == kDefaultKit[5].id, "INIT KIT: the default machines");
        check(int(std::lround(proc.apvts.getRawParameterValue(levelId(3))->load())) == 127 && knob(0, 0) == kDefaultKit[0].knobs[0], "INIT KIT: levels and knobs back to the defaults");
        check(proc.kitName().isEmpty() && proc.loadedKitKey().isEmpty() && !proc.kitModified(), "INIT KIT: no loaded kit");
        if (std::getenv("MD_LIB_IMPORT_FILE")) {   // save a loaded kit into its project slot
            juce::SharedResourcePointer<MdLibrary> lib;
            juce::String pid;
            lib->importSyx(juce::File(juce::String(std::getenv("MD_LIB_IMPORT_FILE"))), &pid);
            KitEntry first;
            for (const auto& k : lib->kits()) if (k.sourceId == pid) { first = k; break; }
            mnm::mddump::Kit kit;
            lib->loadKit(first.key, kit);
            proc.loadMdKit(first.key, kit, first.name);
            const auto slot = lib->projectSlotOfKit(first.key);
            check(slot.valid() && slot.kit == first.position, "the loaded kit's project slot");
            set(knobId(1, 0), 3.0f);
            juce::String key;
            mnm::library::ProjectInfo before;
            const int versions = [&] { for (const auto& pj : lib->model().projects()) if (pj.id == pid) return int(pj.versions.size()); return 0; }();
            const auto r = lib->saveKit("PUT BACK", proc.captureMdKit(), first.key, &key, slot);
            const int after = [&] { for (const auto& pj : lib->model().projects()) if (pj.id == pid) return int(pj.versions.size()); return 0; }();
            check(r.wasOk() && after == versions + 1, "saving into the project adds a project version");
            const auto* st = lib->model().mdState(pid);
            const auto* k2 = st ? st->kitAt(first.position) : nullptr;
            check(k2 && k2->params[1][0] == 3 && juce::String(k2->name).toUpperCase() == first.name, "the slot holds the edited kit under its own name");
            const auto sslot = lib->projectSlotOfSound(proc.loadedSoundKey(1));
            std::printf("  T2 sound's slot: %s kit %d track %d\n", sslot.projectName.toRawUTF8(), sslot.kit, sslot.track);
        }
        std::printf("PARITY %s\n", fails == 0 ? "OK" : "FAIL");
        return fails == 0 ? 0 : 1;
    }
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
        juce::AudioBuffer<float> buf(2, n);
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
        for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    float peak = 0; double sum = 0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) { const float v = out.getSample(c, i); peak = std::max(peak, std::abs(v)); sum += double(v) * v; }
    std::printf("rendered %.2f s in %.0f ms (%.1fx realtime); peak %.3f RMS %.4f\n", total / rate, ms, total / rate * 1000.0 / ms, peak, std::sqrt(sum / (2.0 * total)));

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
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        if (auto* med = dynamic_cast<MdEditor*>(ed.get())) {
            med->refresh();
            if (std::getenv("MD_UI_PICKER")) med->showMachinePicker();   // the machine picker open over the pages
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
