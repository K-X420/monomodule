// md-plugintest's checks of MIDI: the settings, the MID / CTR machines, behaviour against the OS
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
#include "MdTests.h"

namespace mnm::plugin::md::test {

// MD_MIDI_TEST
int mdMidiTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
    // The MIDI settings (the unit's globals): base channel, note map, program change, MIDI out of trigs and CCs
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
    struct Head : juce::AudioPlayHead {
        double ppq = 0; bool playing = false;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setPpqPosition(ppq); p.setBpm(120); p.setIsPlaying(playing); return p; }
    } head;
    auto pHeap = std::make_unique<MdProcessor>();
    auto& p = *pHeap;
    p.setFirmwarePath(juce::String(argv[1]), false);
    p.setPlayHead(&head);
    p.prepareToPlay(rate, block);
    juce::AudioBuffer<float> buf(2, block);
    auto blockWith = [&](juce::MidiBuffer in) { buf.clear(); p.processBlock(buf, in); head.ppq += head.playing ? block / 1000.0 / 24.0 : 0.0; return in; };
    auto trigsOf = [&](const juce::MidiMessage& m) {
        p.setTrigLogging(true);
        juce::MidiBuffer in; in.addEvent(m, 0);
        blockWith(in);
        std::vector<int> ts; for (const auto& e : p.trigLog()) ts.push_back(e.track);
        return ts;
    };
    auto ts = trigsOf(juce::MidiMessage::noteOn(1, 36, uint8_t(100)));
    check(ts.size() == 1 && ts[0] == 0, "default: note 36 on channel 1 plays T1");
    ts = trigsOf(juce::MidiMessage::noteOn(2, 36, uint8_t(100)));
    check(ts.empty(), "on channel 2 it does not (base channel 1)");
    auto ms = MdProcessor::defaultMidiSettings();
    ms.baseChannel = 2; ms.noteTrack.fill(-1); ms.noteTrack[60] = 4;
    p.setMidiSettings(ms);
    ts = trigsOf(juce::MidiMessage::noteOn(3, 60, uint8_t(100)));
    check(ts.size() == 1 && ts[0] == 4, "base channel 3, note 60 mapped to T5: it plays T5");
    ts = trigsOf(juce::MidiMessage::noteOn(3, 36, uint8_t(100)));
    check(ts.empty(), "note 36 is no longer mapped");
    ms.programChange = 0; p.setMidiSettings(ms);
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::programChange(3, 5), 0); blockWith(in); }
    p.syncMachineSideEffects();
    const bool off = int(std::lround(p.apvts.getRawParameterValue(patternId())->load())) == 0;
    ms.programChange = 1; p.setMidiSettings(ms);
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::programChange(3, 5), 0); blockWith(in); }
    p.syncMachineSideEffects();
    check(off && int(std::lround(p.apvts.getRawParameterValue(patternId())->load())) == 5, "PRG CHANGE OFF ignores a program change; IN takes it as the pattern (A06)");
    if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(0.0f);
    ms = MdProcessor::defaultMidiSettings(); ms.midiOut = 2; p.setMidiSettings(ms);
    p.editPattern(0, [](mnm::mddump::Pattern& x) { x.trigs[0] = (1u << 0) | (1u << 4); x.accent = 1u; x.setLock(0, 0, 4, 90); });
    if (auto* a = p.apvts.getParameter(seqId())) a->setValueNotifyingHost(1.0f);
    head.playing = true; head.ppq = 0;
    std::vector<juce::MidiMessage> out;
    for (int i = 0; i < int(48 * 1000.0 / block); ++i) { auto o = blockWith({}); for (const auto m : o) out.push_back(m.getMessage()); }
    int on36 = 0, off36 = 0, vel127 = 0; bool lockCc = false, releaseCc = false;
    for (const auto& m : out) {
        if (m.isNoteOn() && m.getChannel() == 1 && m.getNoteNumber() == 36) { ++on36; if (m.getVelocity() == 127) ++vel127; }
        if (m.isNoteOff() && m.getChannel() == 1 && m.getNoteNumber() == 36) ++off36;
        if (m.isController() && m.getChannel() == 1 && m.getControllerNumber() == 16 && m.getControllerValue() == 90) lockCc = true;
    }
    check(on36 == 2 && off36 >= 1 && vel127 == 1, "TRIGS out: T1's two trigs as note 36 on channel 1, the accented one at 127, note offs a step later");
    check(lockCc, "CCs out: step 5's lock of PTCH as CC 16 = 90 on channel 1");
    out.clear();
    for (int i = 0; i < int(50 * 1000.0 / block); ++i) { auto o = blockWith({}); for (const auto m : o) out.push_back(m.getMessage()); }
    for (const auto& m : out) if (m.isController() && m.getChannel() == 1 && m.getControllerNumber() == 16 && m.getControllerValue() != 90) releaseCc = true;
    check(releaseCc, "the lock released at the next trig: the kit value goes out");
    head.playing = false;
    out.clear();
    if (auto* a = p.apvts.getParameter(knobId(1, 2))) a->setValueNotifyingHost(a->convertTo0to1(33.0f));
    for (int i = 0; i < 2; ++i) { auto o = blockWith({}); for (const auto m : o) out.push_back(m.getMessage()); }
    bool turned = false; for (const auto& m : out) if (m.isController() && m.getControllerNumber() == 40 + 2 && m.getControllerValue() == 33) turned = true;
    out.clear();
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::controllerEvent(1, 40 + 3, 77), 0); auto o = blockWith(in); for (const auto m : o) out.push_back(m.getMessage()); }
    for (int i = 0; i < 2; ++i) { auto o = blockWith({}); for (const auto m : o) out.push_back(m.getMessage()); }
    bool echoed = false; for (const auto& m : out) if (m.isController() && m.getControllerNumber() == 40 + 3) echoed = true;
    check(turned && !echoed, "a knob turn (T2 SYN3) goes out as CC 42; CC 43 that came in is not sent back");
    if (const char* home = std::getenv("USERPROFILE")) {
        juce::File syx(juce::String(home) + "/Documents/Elektron OS/Kits/Elektron Factory/MD_presets_1_0.syx");
        juce::MemoryBlock mb;
        if (syx.loadFileAsData(mb)) {
            const auto d = mnm::mddump::parseDump(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), "f");
            const bool okG = !d.globals.empty() && d.globals[0].trackOfNote(36) == 0 && d.globals[0].trackOfNote(62) == 15 && d.globals[0].baseChannel == 0 && d.globals[0].tempo == 2400;
            const auto fromG = d.globals.empty() ? ms : MdProcessor::fromGlobal(d.globals[0], ms);
            check(okG && fromG.noteTrack[36] == 0 && fromG.baseChannel == 0, "the factory pack global: notes 36..62 = T1..T16, channel 1, 100 BPM; loaded as settings");
            if (!d.globals.empty()) { int pn = 0; for (int i = 0; i < 128; ++i) if (d.globals[0].keyMap[i] >= 16) ++pn; std::printf("  (factory global: pattern-note mode %d, %d notes on patterns / start / stop, PRG CHANGE byte %d)\n", int(d.globals[0].trigMode), pn, int(d.globals[0].programChange)); } if (std::getenv("MD_SWINGMASK_DUMP")) { std::map<std::string,int> c; for (const auto& pt : d.patterns) { char b[64]; std::snprintf(b, sizeof b, "%016llX all=%u", (unsigned long long) pt.swing, unsigned(pt.swingEditAll)); ++c[b]; } for (const auto& [k, n] : c) std::printf("swing mask %s x%d\n", k.c_str(), n); } if (std::getenv("MD_KEYMAP_DUMP") && !d.globals.empty()) { for (int i = 0; i < 128; ++i) std::printf("%d:%d ", i, int(d.globals[0].keyMap[i])); std::printf("\n"); }
        }
    }
    ms = MdProcessor::defaultMidiSettings(); ms.baseChannel = 9; ms.midiOut = 1; ms.programChange = 3; ms.noteTrack[70] = 7;
    p.setMidiSettings(ms);
    juce::MemoryBlock state; p.getStateInformation(state);
    auto rHeap = std::make_unique<MdProcessor>();
    rHeap->setStateInformation(state.getData(), int(state.getSize()));
    const auto back = rHeap->midiSettings();
    check(back.baseChannel == 9 && back.midiOut == 1 && back.programChange == 3 && back.noteTrack[70] == 7 && back.noteTrack[36] == 0, "the settings come back with the plugin state");
    if (argc > 3) {
        auto snap = MdProcessor::defaultMidiSettings();   // the factory key map's pattern notes: A01..A16 on the white keys from E3
        for (int n = 64, k = 0; k < 16; ++n) if (!juce::MidiMessage::isMidiNoteBlack(n)) snap.noteAction[size_t(n)] = int16_t(k++);
        snap.noteAction[96] = MdProcessor::kStartNote; snap.noteAction[98] = MdProcessor::kStopNote;
        p.setMidiSettings(snap);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        auto* med = dynamic_cast<MdEditor*>(ed.get());
        med->devOpenMidi(); med->refresh();
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File png{juce::String(argv[3])}; png.deleteFile();
        juce::PNGImageFormat pf;
        if (auto st = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *st);
    }
    std::printf(fails ? "MIDI TEST FAILED (%d)\n" : "MIDI TEST OK\n", fails);
    return fails ? 1 : 0;
}

// MD_CTR_TEST: MID and CTR machines through the processor: MIDI out, CTR-AL, CTR-8P, CTR-GB / EQ
int mdCtrTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
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
    // T1 = MID-01: NOTE 60 (C3), N2 +4, N3 +7, LEN 1 (one 16th: 6 ticks at 24 per quarter), VEL 90, PCHG 5 (program 4)
    machine(0, 96);
    machine(1, kCtrAll); machine(2, 121); machine(3, kCtr8p); machine(4, 16); machine(5, 17);
    run(40);   // settle (the machine changes and the load quiet time)
    set(knobId(0, 0), 60); set(knobId(0, 1), 68); set(knobId(0, 2), 71); set(knobId(0, 3), 1); set(knobId(0, 4), 90);
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
    check(std::abs(off - 6000) <= 1, "LEN 1 = one 16th at 120 BPM (6000 samples at 48 kHz), off at " + juce::String(off));
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
    // free BPM: SYNC off, BPM 90 -> LEN 1 is a 16th at 90 BPM (8000 samples at 48 kHz)
    machine(0, 96); run(10);
    set(knobId(0, 0), 60); set(knobId(0, 1), 64); set(knobId(0, 2), 64); set(knobId(0, 3), 1); set(knobId(0, 4), 90);
    set(bpmSyncId(), 0.0f); set(bpmId(), 90.0f); run(3);
    out.clear(); clock = 0;
    run(30, {36});
    {
        const int on = find([](const juce::MidiMessage& m) { return m.isNoteOn() && m.getNoteNumber() == 60; });
        const int off = find([](const juce::MidiMessage& m) { return m.isNoteOff() && m.getNoteNumber() == 60; });
        check(on == 0 && std::abs(off - 8000) <= 1, "SYNC off, BPM 90: LEN 1 note off at " + juce::String(off) + " (a 16th = 8000)");
    }
    // all notes off: a held MID note ends at once
    set(knobId(0, 3), 127); run(2);   // LEN 127: a bar
    out.clear(); clock = 0;
    run(2, {36});
    {
        juce::AudioBuffer<float> buf(std::max(2, proc.getTotalNumOutputChannels()), block);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::allNotesOff(1), 100);
        proc.processBlock(buf, midi);
        bool ended = false;
        for (const auto m : midi) { const auto msg = m.getMessage(); if (msg.isNoteOff() && msg.getNoteNumber() == 60 && m.samplePosition == 100) ended = true; }
        check(ended, "all notes off ends the MID machine's held note");
    }
    set(bpmSyncId(), 1.0f);
    // automation names: the track (or master effect) first
    const auto pname = [&](const juce::String& id) { return proc.apvts.getParameter(id)->getName(32); };
    check(pname(levelId(0)) == "T1 LEVEL" && pname(fxId(3, 0)) == "T4 AMD" && pname(masterFxId(0, 7)) == "REV LEV" && pname(masterFxId(1, 7)) == "DEL LEV",
          "parameter names: " + pname(levelId(0)) + ", " + pname(fxId(3, 0)) + ", " + pname(masterFxId(0, 7)) + ", " + pname(masterFxId(1, 7)));
    machine(5, 16); proc.syncMachineSideEffects();
    check(pname(knobId(5, 0)) == "T6 PTCH", "a SYN knob: " + pname(knobId(5, 0)));
    std::printf("  status: %s\n", proc.statusText().toRawUTF8());
    check(proc.statusText().contains("instr/pass"), "the status shows the DSPs' load");
    std::printf(fails ? "CTR TEST FAILED (%d)\n" : "CTR TEST OK\n", fails);
    return fails ? 1 : 0;
}

// MD_OSCHECK_TEST: behaviour checked against the OS: PRG CHANGE's channel + timing, song HALT, empty song slots
int mdOscheckTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
    struct Head : juce::AudioPlayHead {
        double ppq = 0; bool playing = false;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo q; q.setPpqPosition(ppq); q.setBpm(120); q.setIsPlaying(playing); return q; }
    } head;
    auto pHeap = std::make_unique<MdProcessor>();
    auto& p = *pHeap;
    p.setFirmwarePath(juce::String(argv[1]), false);
    p.setPlayHead(&head);
    const int blk = 480;
    p.prepareToPlay(48000.0, blk);
    juce::AudioBuffer<float> buf(2, blk);
    int64_t clock = 0;
    std::vector<std::pair<int64_t, juce::MidiMessage>> out;
    auto run = [&](int blocks, juce::MidiBuffer in = {}) {
        for (int i = 0; i < blocks; ++i) {
            buf.clear(); juce::MidiBuffer m = i == 0 ? in : juce::MidiBuffer();
            p.processBlock(buf, m);
            for (const auto e : m) out.push_back({clock + e.samplePosition, e.getMessage()});
            clock += blk;
            if (head.playing) head.ppq += blk / 48000.0 * 2.0;   // 120 BPM
            p.syncMachineSideEffects();
        }
    };
    mnm::mddump::Global g; g.programChange = 0x05;   // the OS's byte: IN, channel 1
    const auto fromG = MdProcessor::fromGlobal(g, MdProcessor::defaultMidiSettings());
    check(fromG.programChange == 1 && fromG.pcChannel == 1, "global PRG CHANGE byte 0x05 = IN on channel 1 (was read as IN+OUT)");
    auto ms = MdProcessor::defaultMidiSettings(); ms.baseChannel = 0; ms.programChange = 3; ms.pcChannel = 0;
    p.setMidiSettings(ms);
    auto patNow = [&] { return int(std::lround(p.apvts.getRawParameterValue(patternId())->load())); };
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::programChange(4, 7), 0); run(1, in); }
    check(patNow() == 7, "AUTO: a program change on base channel + 3 is taken");
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::programChange(5, 9), 0); run(1, in); }
    check(patNow() == 7, "AUTO: base channel + 4 is not");
    ms.pcChannel = 10; p.setMidiSettings(ms);
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::programChange(10, 2), 0); run(1, in); }
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::programChange(1, 3), 0); run(1, in); }
    check(patNow() == 2, "channel 10: taken on 10, not on the base channel");
    // PRG CHANGE OUT: A01 plays (16 steps, 1x = 6000 samples a step); B01... queue A04 mid-pattern: PC 3 goes out at
    // the start of A01's last step (step 16 = 15 x 6000 = 90000), on channel 10
    if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(0.0f));
    p.editPattern(0, [](mnm::mddump::Pattern& x) { x.length = 16; x.trigs[0] = 1; });
    p.editPattern(3, [](mnm::mddump::Pattern& x) { x.length = 16; x.trigs[0] = 1; });
    run(2);
    out.clear(); clock = 0; head.ppq = 0; head.playing = true;
    run(20);   // 0.2 s in
    if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(3.0f));
    run(200);
    int64_t pcAt = -1; int pcCh = 0, pcNum = -1, pcs = 0;
    for (const auto& [at, m] : out) if (m.isProgramChange()) { ++pcs; if (pcAt < 0) { pcAt = at; pcCh = m.getChannel(); pcNum = m.getProgramChangeNumber(); } }
    check(pcNum == 3 && pcCh == 10 && std::abs(pcAt - 90000) <= blk, "PRG CHANGE OUT: program 3 on channel 10 at the last step's start (at " + juce::String(pcAt) + ", want 90000)");
    check(pcs == 1, "sent once (A04 repeating sends nothing more): " + juce::String(pcs));
    head.playing = false; run(2);
    // SONG: row 0 = A01 once, row 1 = LOOP onto itself (HALT), row 2 = A04: the song ends after A01
    p.editSong(0, [](mnm::mddump::Song& s) {
        s.rows.clear();
        mnm::mddump::SongRow r0; r0.bytes[0] = 0; r0.bytes[8] = 0; r0.bytes[9] = 16;
        mnm::mddump::SongRow r1; r1.bytes[0] = 0xFE; r1.bytes[2] = 3; r1.bytes[3] = 1;
        mnm::mddump::SongRow r2; r2.bytes[0] = 3; r2.bytes[9] = 16;
        s.rows = {r0, r1, r2};
    });
    if (auto* a = p.apvts.getParameter(seqModeId())) a->setValueNotifyingHost(1.0f);
    head.ppq = 0; head.playing = true; run(2);
    const int rowStart = p.seqSongRow();
    run(220);   // past A01's 96000 samples
    check(rowStart == 0 && p.seqSongRow() == -1 && p.seqStep() == -1, "a LOOP row onto itself is HALT: the song ends after A01 (row " + juce::String(p.seqSongRow()) + ")");
    head.playing = false; run(2);
    // an empty slot in a song plays (as an empty pattern) instead of being skipped: row 0 = empty slot H16, row 1 = A01
    p.editSong(0, [](mnm::mddump::Song& s) {
        mnm::mddump::SongRow r0; r0.bytes[0] = 127; r0.bytes[9] = 16;
        mnm::mddump::SongRow r1; r1.bytes[0] = 0; r1.bytes[9] = 16;
        s.rows = {r0, r1};
    });
    head.ppq = 0; head.playing = true; run(2);
    check(p.seqSongRow() == 0 && p.seqPattern() == 127, "an empty slot's row plays (row " + juce::String(p.seqSongRow()) + ", pattern " + juce::String(p.seqPattern()) + ")");
    head.playing = false; run(2);
    // CTR-AL locks set the value itself on every track (MainOS 0x237AE4), not a difference: T2 = CTR-AL (PTCH 64),
    // T5 / T6 = TRX-BD with PTCH 40 / 100; T2 trigs on step 1 with PTCH locked to 10 -> both 10
    if (auto* a = p.apvts.getParameter(seqModeId())) a->setValueNotifyingHost(0.0f);
    auto setP = [&](const juce::String& id, float v) { if (auto* q = p.apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(v)); };
    setP(machineId(1), float(machineIndexOf(kCtrAll))); setP(machineId(4), float(machineIndexOf(16))); setP(machineId(5), float(machineIndexOf(16)));
    run(40);
    setP(knobId(1, 0), 64); setP(knobId(4, 0), 40); setP(knobId(5, 0), 100);
    run(5);
    if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(5.0f));
    p.editPattern(5, [](mnm::mddump::Pattern& x) { x.length = 16; x.trigs[1] = 1; x.setLock(1, 0, 0, 10); });
    run(2);
    head.ppq = 0; head.playing = true; run(10);
    const int v5 = int(std::lround(p.apvts.getRawParameterValue(knobId(4, 0))->load())), v6 = int(std::lround(p.apvts.getRawParameterValue(knobId(5, 0))->load()));
    check(v5 == 10 && v6 == 10, "CTR-AL PTCH lock 10: T5 / T6 PTCH = " + juce::String(v5) + " / " + juce::String(v6) + " (want 10 / 10)");
    head.playing = false; run(2);
    // a muted CTR-AL applies no locks (MainOS 0x23B19C): PTCH back to 40 / 100, T2 muted, the pattern again
    setP(knobId(4, 0), 40); setP(knobId(5, 0), 100); setP(muteId(1), 1.0f);
    run(5);
    head.ppq = 0; head.playing = true; run(10);
    const int m5 = int(std::lround(p.apvts.getRawParameterValue(knobId(4, 0))->load())), m6 = int(std::lround(p.apvts.getRawParameterValue(knobId(5, 0))->load()));
    check(m5 == 40 && m6 == 100, "a muted CTR-AL's lock does nothing: T5 / T6 PTCH stay " + juce::String(m5) + " / " + juce::String(m6));
    head.playing = false; run(2);
    setP(muteId(1), 0.0f);
    // MIDI OUT of trigs (MainOS 0x23A91C): 95, or 127 on an accented step; the note-off (velocity 0) at once
    ms = MdProcessor::defaultMidiSettings(); ms.midiOut = 1; p.setMidiSettings(ms);
    setP(machineId(1), float(machineIndexOf(16)));
    run(40);
    if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(6.0f));
    p.editPattern(6, [](mnm::mddump::Pattern& x) { x.length = 16; x.trigs[0] = 1u | (1u << 4); x.accentEditAll = true; x.accent = 1u << 4; x.accentAmount = 0; });
    run(2);
    out.clear(); clock = 0; head.ppq = 0; head.playing = true;
    run(60);
    std::vector<std::pair<int64_t, int>> ons; int64_t offAt = -1;
    for (const auto& [at, m] : out) {
        if (m.isNoteOn() && m.getNoteNumber() == 36) ons.push_back({at, m.getVelocity()});
        if (m.isNoteOff() && m.getNoteNumber() == 36 && offAt < 0) offAt = at;
    }
    check(ons.size() >= 2 && ons[0].second == 95 && ons[1].second == 127, "MIDI OUT: velocity 95, then 127 on the accented step (accent amount 0)");
    check(!ons.empty() && offAt == ons[0].first, "the note-off goes out at once (at " + juce::String(offAt) + ")");
    head.playing = false; run(2);
    // PATTERN NOTES (the key map's pattern / START / STOP notes, global byte r[18] = their mode)
    {
        mnm::mddump::Global fg; fg.trigMode = 1;
        for (int i = 0; i < 128; ++i) fg.keyMap[i] = 255;
        fg.keyMap[36] = 0; fg.keyMap[64] = 16; fg.keyMap[65] = 17; fg.keyMap[100] = 0x90; fg.keyMap[101] = 0x91;
        const auto s = MdProcessor::fromGlobal(fg, MdProcessor::defaultMidiSettings());
        check(s.patternNoteMode == 1 && s.noteAction[64] == 0 && s.noteAction[65] == 1 && s.noteAction[100] == MdProcessor::kStartNote
              && s.noteAction[101] == MdProcessor::kStopNote && s.noteAction[36] == -1 && s.noteAction[70] == -1,
              "a project's key map: E3 = A01, F3 = A02, START / STOP notes, mode MOMENTARY");
    }
    if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(0.0f));
    ms = MdProcessor::defaultMidiSettings();
    ms.noteAction[80] = 3; ms.noteAction[90] = MdProcessor::kStartNote; ms.noteAction[91] = MdProcessor::kStopNote;
    auto note = [&](int nn, bool on, int blocks) {
        juce::MidiBuffer in;
        in.addEvent(on ? juce::MidiMessage::noteOn(1, nn, uint8_t(100)) : juce::MidiMessage::noteOff(1, nn), 0);
        run(blocks, in);
    };
    auto playFrom0 = [&] { head.playing = false; run(3); head.ppq = 0; head.playing = true; run(30); };   // 0.3 s into A01
    // QUEUE: A04 waits for the end of A01 (16 steps = 96000 samples)
    ms.patternNoteMode = 2; p.setMidiSettings(ms);
    playFrom0();
    note(80, true, 2);
    const int qNow = p.seqPattern();
    run(180);   // past 1.0 s... well past A01's end at 2 s? (each block 10 ms: to ~2.1 s)
    run(20);
    check(qNow == 0 && p.seqPattern() == 3, "QUEUE: A04 waits for A01's end (" + juce::String(qNow) + " then " + juce::String(p.seqPattern()) + ")");
    // GATE: A04 at once; the note-off stops
    ms.patternNoteMode = 0; p.setMidiSettings(ms);
    if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(0.0f));
    run(3);
    playFrom0();
    note(80, true, 3);
    const int gNow = p.seqPattern(), gStep = p.seqStep();
    note(80, false, 3);
    check(gNow == 3 && gStep >= 0 && gStep < 2 && p.seqStep() == -1, "GATE: A04 at once from its first step (step " + juce::String(gStep) + "); the note-off stops (" + juce::String(p.seqStep()) + ")");
    // MOMENTARY: A04 at once; the note-off queues A01 back
    ms.patternNoteMode = 1; p.setMidiSettings(ms);
    if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(0.0f));
    run(3);
    playFrom0();
    note(80, true, 3);
    const int mNow = p.seqPattern();
    note(80, false, 3);
    const int mHeld = p.seqPattern();
    run(200);
    check(mNow == 3 && mHeld == 3 && p.seqPattern() == 0, "MOMENTARY: A04 at once, A01 back after it (" + juce::String(mNow) + ", " + juce::String(mHeld) + ", " + juce::String(p.seqPattern()) + ")");
    // START / STOP notes with the host stopped: the plugin's own PLAY
    head.playing = false; run(3);
    note(90, true, 3);
    const bool started = p.internalPlay();
    note(91, true, 3);
    check(started && !p.internalPlay(), "the START note runs PLAY, the STOP note stops it");
    // live recording at 2x (3 clocks a step; a clock = 1000 samples here, a block 0.48 clock): a hit past 2/3 of
    // its step goes onto the next step, one at half stays; a knob turned during a step with no trig after it is forgotten
    {
        head.playing = false; run(3);
        ms = MdProcessor::defaultMidiSettings(); p.setMidiSettings(ms);
        if (auto* a = p.apvts.getParameter(seqModeId())) a->setValueNotifyingHost(0.0f);
        if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(8.0f));
        setP(machineId(0), float(machineIndexOf(16)));
        p.editPattern(8, [](mnm::mddump::Pattern& x) { x.length = 16; x.doubleTempo = 1; x.trigs[0] = 1ull << 10; });
        run(40);
        p.setRecord(true);
        head.ppq = 0; head.playing = true;
        run(11);                     // clock 5.28: step 2 (index 1), 76% through -> step 3 (index 2)
        note(38, true, 11);          // T2; now at clock 10.56: step 4 (index 3), 52% -> stays
        note(38, true, 23);          // now at clock 21.6: step 8 (index 7)
        setP(knobId(0, 0), 99);      // T1 PTCH turned during step 8; steps 9 and 10 have no T1 trig, step 11 does
        run(40);
        p.setRecord(false); run(3);
        head.playing = false; run(3);
        const auto pat = p.bankPattern(8);
        const uint64_t t2 = pat ? pat->trigs[1] : 0;
        check(((t2 >> 2) & 1) && ((t2 >> 3) & 1) && !((t2 >> 1) & 1), "2x: a hit 76% into step 2 records on step 3, one 52% into step 4 stays on 4 (T2 trigs " + juce::String::toHexString((juce::int64) t2) + ")");
        check(pat && pat->lockRow(0, 0) < 0, "a knob turned during a step with no trig after it is forgotten (no lock on step 11)");
    }
    // solo comes back with the session
    {
        p.setSolo(3, true);
        juce::MemoryBlock st; p.getStateInformation(st);
        auto rHeap = std::make_unique<MdProcessor>();
        rHeap->setStateInformation(st.getData(), int(st.getSize()));
        check(rHeap->soloed(3) && !rHeap->soloed(2), "SOLO is kept with the session");
        p.clearSolo();
    }
    // GRID: a step's tooltip lists its locks
    {
        if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(9.0f));
        p.editPattern(9, [](mnm::mddump::Pattern& x) { x.trigs[0] = 1ull << 2; x.setLock(0, 0, 2, 77); x.setLock(0, 17, 2, 40); });
        run(3);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        auto* med = dynamic_cast<MdEditor*>(ed.get());
        const auto tip = med->devKeys().stepLocks ? med->devKeys().stepLocks(2) : juce::String();
        check(tip.contains("77") && tip.contains("VOL 40") && med->devKeys().stepLocks(3).isEmpty(), "GRID step tooltip lists its locks: " + tip);
    }
    {   // the plugin's extra speeds: 1/2x, 1/4x, 1/8x, 3x after the unit's four
        mnm::mddump::Pattern sp; sp.length = 16; sp.trigs[0] = 1ull << 1;
        std::vector<mnm::md::SeqTrig> got;
        bool ok = true;
        const int want[8] = {6, 3, 8, 4, 12, 24, 48, 2};
        for (int k = 0; k < 8; ++k) {
            sp.doubleTempo = uint8_t(k);
            mnm::md::PatternPlayer pl; pl.set(sp);
            got.clear(); pl.trigs(0.0, 200.0, got, -1);
            ok = ok && !got.empty() && std::abs(got[0].clock - want[k]) < 1e-9;
        }
        check(ok, "speeds 1x 2x 3/4x 3/2x 1/2x 1/4x 1/8x 3x: step 2 at clocks 6 3 8 4 12 24 48 2");
    }
    {   // a chain A01 > A04: a pass each, looping; choosing A06 ends it
        head.playing = false; run(3);
        if (auto* a = p.apvts.getParameter(seqModeId())) a->setValueNotifyingHost(0.0f);
        if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(0.0f));
        p.editPattern(0, [](mnm::mddump::Pattern& x) { x.length = 16; x.doubleTempo = 0; });
        p.editPattern(3, [](mnm::mddump::Pattern& x) { x.length = 16; x.doubleTempo = 0; });
        run(3);
        p.setChain({0, 3});
        head.ppq = 0; head.playing = true;
        std::vector<int> seen;
        for (int i = 0; i < 640; ++i) { run(1); const int s = p.seqPattern(); if (seen.empty() || seen.back() != s) seen.push_back(s); }   // 6.4 s: 3+ passes of 2 s
        juce::String seq; for (int s : seen) seq << s << " ";
        check(seen.size() >= 4 && seen[0] == 0 && seen[1] == 3 && seen[2] == 0 && seen[3] == 3, "chain A01 > A04 plays them in turn, looping: " + seq);
        if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(5.0f));
        run(3);
        check(p.chain().empty(), "choosing A06 ends the chain");
        head.playing = false; run(3);
    }
    {   // CTRL IN OFF: Ableton's transport neither starts nor stops the sequencer; PLAY runs it
        head.playing = false; run(3);
        p.setChain({});
        if (auto* a = p.apvts.getParameter(seqModeId())) a->setValueNotifyingHost(0.0f);
        if (auto* a = p.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(0.0f));
        auto m2 = MdProcessor::defaultMidiSettings(); m2.ctrlIn = false; p.setMidiSettings(m2);
        run(3);
        head.ppq = 0; head.playing = true; run(30);
        const bool idle = !p.seqPlaying() || p.seqStep() < 0;
        p.setInternalPlay(true); run(30);
        const bool runs = p.seqStep() >= 0;
        head.playing = false; run(30);
        const bool keeps = p.seqStep() >= 0 && p.internalPlay();
        check(idle && runs && keeps, "CTRL IN OFF: Ableton's play does not start it, PLAY does, Ableton's stop does not stop it");
        p.setInternalPlay(false); run(3);
        m2.ctrlIn = true; p.setMidiSettings(m2); run(3);
    }
    {   // SONG transport: the start row, a cued row, a HALT restarted by a cue
        if (auto* a = p.apvts.getParameter(seqModeId())) a->setValueNotifyingHost(1.0f);
        auto row = [](int ptn) { mnm::mddump::SongRow r; r.bytes[0] = uint8_t(ptn); r.bytes[9] = 16; r.bytes[6] = r.bytes[7] = 0xFF; return r; };
        p.editSong(0, [&](mnm::mddump::Song& s) { s.rows = {row(0), row(0), row(3)}; });
        p.setSongStartRow(2);
        run(3);
        head.ppq = 0; head.playing = true; run(5);
        check(p.seqSongRow() == 2 && p.seqPattern() == 3, "start row 3: the song starts there (row " + juce::String(p.seqSongRow() + 1) + ")");
        head.playing = false; run(3);
        p.setSongStartRow(0); run(3);
        head.ppq = 0; head.playing = true; run(5);
        p.cueSongRow(2);   // while row 1 plays: row 3 next, row 2 skipped
        std::vector<int> rowsSeen;
        for (int i = 0; i < 220; ++i) { run(1); const int r = p.seqSongRow(); if (rowsSeen.empty() || rowsSeen.back() != r) rowsSeen.push_back(r); }
        check(rowsSeen.size() >= 2 && rowsSeen[0] == 0 && rowsSeen[1] == 2, "row 3 cued while row 1 plays: it plays next (rows " + juce::String(rowsSeen.size() > 1 ? rowsSeen[1] + 1 : -1) + ")");
        head.playing = false; run(3);
        mnm::mddump::SongRow halt; halt.bytes[0] = 0xFE; halt.bytes[3] = 1;
        p.editSong(0, [&](mnm::mddump::Song& s) { s.rows = {row(0), halt}; });
        run(3);
        head.ppq = 0; head.playing = true; run(220);
        const bool halted = p.seqSongRow() == -1;
        p.cueSongRow(0); run(10);
        check(halted && p.seqSongRow() == 0, "a HALTed song: a cued row starts it again");
        head.playing = false; run(3);
        if (auto* a = p.apvts.getParameter(seqModeId())) a->setValueNotifyingHost(0.0f);
        run(3);
    }
    std::printf(fails ? "OSCHECK TEST FAILED (%d)\n" : "OSCHECK TEST OK\n", fails);
    return fails ? 1 : 0;
}

} // namespace mnm::plugin::md::test
