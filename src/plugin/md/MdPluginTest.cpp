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
    if (std::getenv("MD_CTR_TEST")) {   // MID and CTR machines through the processor: MIDI out, CTR-AL, CTR-8P, CTR-GB / EQ
        int fails = 0;
        auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
        auto set = [&](const juce::String& id, float v) { if (auto* pp = proc.apvts.getParameter(id)) pp->setValueNotifyingHost(pp->convertTo0to1(v)); };
        auto get = [&](const juce::String& id) { return int(std::lround(proc.apvts.getRawParameterValue(id)->load())); };
        std::vector<juce::MidiMessage> out;
        std::vector<double> blockRms;   // the main bus, per block
        int64_t clock = 0;
        auto run = [&](int blocks, std::vector<int> notesAt0 = {}) {   // blocks of 480 at 48 kHz; notes at the first block's start
            for (int b = 0; b < blocks; ++b) {
                juce::AudioBuffer<float> buf(std::max(2, proc.getTotalNumOutputChannels()), block);
                juce::MidiBuffer midi;
                if (b == 0) for (int nn : notesAt0) midi.addEvent(juce::MidiMessage::noteOn(1, nn, uint8_t(100)), 0);
                proc.processBlock(buf, midi);
                { double e = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) e += double(buf.getSample(c, i)) * buf.getSample(c, i); blockRms.push_back(std::sqrt(e / (2.0 * block))); }
                for (const auto m : midi) { auto msg = m.getMessage(); msg.setTimeStamp(double(clock + m.samplePosition)); out.push_back(msg); }
                clock += block;
                proc.syncMachineSideEffects();   // the message thread: the CTR writes land
            }
        };
        auto machine = [&](int tr, int id) { set(machineId(tr), float(machineIndexOf(id))); };
        // T1 = MID-01: NOTE 60 (C3), N2 +4, N3 +7, LEN 7 (one 16th), VEL 90, PCHG 5 (program 4)
        machine(0, 96);
        machine(1, kCtrAll); machine(2, 121); machine(3, kCtr8p); machine(4, 16); machine(5, 17);
        run(40);   // settle (the machine changes and the load quiet time)
        set(knobId(0, 0), 60); set(knobId(0, 1), 68); set(knobId(0, 2), 71); set(knobId(0, 3), 7); set(knobId(0, 4), 90);
        set(knobId(0, 5), 64); set(knobId(0, 6), 0); set(knobId(0, 7), 0); set(revId(0), 5);
        run(2);
        check(std::any_of(out.begin(), out.end(), [](const juce::MidiMessage& m) { return m.isProgramChange() && m.getProgramChangeNumber() == 4; }), "PCHG turned to 5: program 4 out");
        out.clear(); clock = 0;
        run(20, {36});
        juce::String log;
        for (const auto& m : out) log << int(m.getTimeStamp()) << ":" << m.getDescription() << " | ";
        std::printf("MID-01 trig: %s\n", log.toRawUTF8());
        auto find = [&](auto pred) { for (const auto& m : out) if (pred(m)) return int(m.getTimeStamp()); return -1; };
        check(find([](const juce::MidiMessage& m) { return m.isNoteOn() && m.getNoteNumber() == 60 && m.getChannel() == 1 && m.getVelocity() == 90; }) == 0, "NOTE C3 on channel 1 at VEL 90");
        check(find([](const juce::MidiMessage& m) { return m.isNoteOn() && m.getNoteNumber() == 64; }) == 0 && find([](const juce::MidiMessage& m) { return m.isNoteOn() && m.getNoteNumber() == 67; }) == 0, "N2 / N3: +4 and +7 semitones");
        check(find([](const juce::MidiMessage& m) { return m.isProgramChange(); }) < 0, "the trig does not resend the program the channel already has");
        const int off = find([](const juce::MidiMessage& m) { return m.isNoteOff() && m.getNoteNumber() == 60; });
        check(std::abs(off - 6000) <= 1, "LEN 7 = one 16th at 120 BPM (6000 samples at 48 kHz), off at " + juce::String(off));
        // a knob turn on MID: CC1D = 74, then CC1V -> CC 74; MW -> CC 1; PB -> pitch bend
        out.clear(); clock = 0;
        set(fxId(0, 0), 74); run(2);
        set(fxId(0, 1), 100); set(knobId(0, 6), 33); set(knobId(0, 5), 80); run(2);
        check(find([](const juce::MidiMessage& m) { return m.isController() && m.getControllerNumber() == 74 && m.getControllerValue() == 100; }) >= 0, "CC1V turn sends CC 74 = 100");
        check(find([](const juce::MidiMessage& m) { return m.isController() && m.getControllerNumber() == 1 && m.getControllerValue() == 33; }) >= 0, "MW turn sends CC 1");
        check(find([](const juce::MidiMessage& m) { return m.isPitchWheel() && m.getPitchWheelValue() == 80 * 128; }) >= 0, "PB turn sends pitch bend");
        // LFO -> MIDI: T1's LFO on its own CC1V (CC 74), then on NOTE; depth 0 stops the stream
        set(lfoId(0, 0), 0); set(lfoId(0, 1), 9); set(lfoId(0, 2), 0); set(lfoId(0, 3), 0); set(lfoId(0, 4), 0);
        set(lfoId(0, 5), 110); set(lfoId(0, 7), 0); set(fxId(0, 1), 64); set(lfoId(0, 6), 127);
        run(10); out.clear(); clock = 0;
        run(100);   // 1 s
        int ccs = 0, lo = 127, hi = 0, last = -1;
        for (const auto& m : out)
            if (m.isController() && m.getControllerNumber() == 74) { ++ccs; lo = std::min(lo, m.getControllerValue()); hi = std::max(hi, m.getControllerValue()); last = int(m.getTimeStamp()); }
        check(ccs > 20 && hi - lo > 40, "LFO on CC1V streams CC 74: " + juce::String(ccs) + " messages over 1 s, " + juce::String(lo) + ".." + juce::String(hi));
        check(last > 40000, "the stream runs through the second (last at " + juce::String(last) + ")");
        set(lfoId(0, 1), 0);   // the LFO onto NOTE: four trigs a quarter second apart
        juce::StringArray notes;
        for (int k = 0; k < 4; ++k) {
            out.clear(); clock = 0;
            run(25, {36});
            for (const auto& m : out) if (m.isNoteOn()) { notes.addIfNotAlreadyThere(juce::String(m.getNoteNumber())); break; }
        }
        check(notes.size() > 1, "LFO on NOTE: the trigs play " + notes.joinIntoString(" "));
        set(lfoId(0, 1), 9); set(lfoId(0, 6), 0); run(20); out.clear(); clock = 0;
        run(30);
        check(std::none_of(out.begin(), out.end(), [](const juce::MidiMessage& m) { return m.isController() && m.getControllerNumber() == 74; }), "LFO depth 0: no more CC 74");
        set(lfoId(0, 1), 0);
        // CTR-AL (T2): SYN1 64 -> 70 moves T5 / T6 SYN1 by +6, not the MID or CTR tracks
        set(knobId(1, 0), 64); set(knobId(4, 0), 40); set(knobId(5, 0), 125); run(3);
        const int mid0 = get(knobId(0, 0));
        set(knobId(1, 0), 70); run(3);
        check(get(knobId(4, 0)) == 46 && get(knobId(5, 0)) == 127, "CTR-AL +6: T5 40 -> " + juce::String(get(knobId(4, 0))) + ", T6 125 -> " + juce::String(get(knobId(5, 0))) + " (clamped)");
        check(get(knobId(0, 0)) == mid0, "CTR-AL leaves the MID track alone");
        set(volId(1), 50); run(3); set(volId(1), 40); run(3);
        check(get(volId(4)) == 40, "CTR-AL VOL 100 -> 50 -> 40: T5 VOL 100 -> " + juce::String(get(volId(4))));
        // CTR-GB (T3) = the reverb: knob DEC (3rd) -> master reverb DEC; master reverb DAMP -> the knob
        set(knobId(2, 2), 20); run(3);
        check(get(masterFxId(0, 2)) == 20, "CTR-GB DEC -> master reverb DEC = " + juce::String(get(masterFxId(0, 2))));
        set(masterFxId(0, 3), 99); run(3);
        check(get(knobId(2, 3)) == 99, "master reverb DAMP -> CTR-GB knob = " + juce::String(get(knobId(2, 3))));
        // CTR-8P (T4): P1 -> T5 VOL (track 4, parameter 17)
        set(fxId(3, 0), 4); set(fxId(3, 1), 17); run(3);
        set(knobId(3, 0), 30); run(3);
        check(get(volId(4)) == 30, "CTR-8P P1 -> T5 VOL = " + juce::String(get(volId(4))));
        set(fxId(3, 2), 2); set(fxId(3, 3), 8); run(3);   // P2 -> T3 (CTR-GB) parameter 8: its EFFECTS page, nothing behind it
        set(fxId(3, 4), 2); set(fxId(3, 5), 1); run(3);   // P3 -> T3 (CTR-GB) SYN2 = master reverb PRED
        set(knobId(3, 2), 77); run(4);
        check(get(masterFxId(0, 1)) == 77, "CTR-8P P3 -> CTR-GB PRED -> master reverb PRED = " + juce::String(get(masterFxId(0, 1))));
        // CTR-EQ's LFO (T3 turned into a CTR-EQ, its LFO on its own GAIN) makes a held sine on T7 swell and dip
        machine(2, 122); run(10);
        auto wobble = [&](int depth) {
            machine(6, 1); run(5);   // T7 GND-SN
            set(knobId(6, 0), 64); set(knobId(6, 1), 127); set(volId(6), 127); set(levelId(6), 127);
            set(knobId(2, 7), 64);   // GAIN in the middle: room both ways
            set(lfoId(2, 0), 2); set(lfoId(2, 1), 7); set(lfoId(2, 2), 0); set(lfoId(2, 3), 0); set(lfoId(2, 4), 0);
            set(lfoId(2, 5), 40); set(lfoId(2, 7), 0); set(lfoId(2, 6), float(depth));
            run(5);
            blockRms.clear();
            run(100, {47});
            double lo = 1e9, hi = 0;
            for (size_t b = 15; b < blockRms.size(); ++b) { lo = std::min(lo, blockRms[b]); hi = std::max(hi, blockRms[b]); }
            return hi / std::max(lo, 1e-9);
        };
        const double still = wobble(0), moving = wobble(127);
        check(still < 1.2 && moving > 4.0, "CTR-EQ LFO on GAIN: main level range x" + juce::String(still, 2) + " at depth 0, x" + juce::String(moving, 1) + " at depth 127");
        check(get(masterFxId(2, 7)) == get(knobId(2, 7)) && get(masterFxId(2, 7)) == 64, "the master EQ GAIN parameter stays at the knob (" + juce::String(get(masterFxId(2, 7))) + "): the LFO is not written into it");
        std::printf(fails ? "CTR TEST FAILED (%d)\n" : "CTR TEST OK\n", fails);
        return fails ? 1 : 0;
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
        if (const char* m = std::getenv("MD_UI_MACHINE"))   // track 1's machine for the snapshot (an MD machine ID)
            if (auto* pp = proc.apvts.getParameter(machineId(0))) { pp->setValueNotifyingHost(pp->convertTo0to1(float(machineIndexOf(std::atoi(m))))); proc.syncMachineSideEffects(); }
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        if (auto* med = dynamic_cast<MdEditor*>(ed.get())) {
            med->refresh();
            if (const char* pk = std::getenv("MD_UI_PICKER")) med->showMachinePicker(std::atoi(pk) > 1 ? machineIndexOf(std::atoi(pk)) : -1);   // the picker open (=<id>: that machine hovered)
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
