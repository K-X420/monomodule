// md-plugintest's checks of the sequencer: playing, recording, songs, groups
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

// MD_SEQ_TEST
int mdSeqTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
    // Pattern playback against a simulated host transport (120 BPM, 48 kHz, 480-sample blocks), on a bank made here:
    //   A01 (16 steps, 1x): T1 GND-SN on steps 0 4 8 12; PTCH locked to 100 on step 4, none on 8 (back to the kit's
    //       64), locked to 20 on step 12 with a slide (to the next trig: step 0 of the next pass, no lock = the kit's)
    //       T2 on the odd steps, swung (66%: a 6-clock step's delay is 6 x 5243 / 16384 clocks)
    //   A02 (8 steps, 3/4x = 8 clocks a step), kit 1 (T1 PTCH 30): T3 on step 0
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
    struct Head : juce::AudioPlayHead {
        double ppq = 0, bpm = 120; bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo p; p.setPpqPosition(ppq); p.setBpm(bpm); p.setIsPlaying(playing); return p;
        }
    } head;
    mnm::mddump::Dump d;
    for (int k = 0; k < 2; ++k) {
        mnm::mddump::Kit kit;
        kit.position = k;
        for (int tr = 0; tr < 16; ++tr) { kit.trigGroups[tr] = 127; kit.muteGroups[tr] = 127; kit.lfos[tr][0] = uint8_t(tr); kit.levels[tr] = 100; kit.params[tr][17] = 100; }
        for (int tr = 0; tr < 3; ++tr) { kit.models[tr] = 1; kit.params[tr][0] = uint8_t(k == 0 ? 64 : 30); kit.params[tr][1] = 60; }
        const uint8_t rev[8] = {0, 0, 64, 64, 0, 127, 127, 127}, del[8] = {24, 0, 0, 32, 0, 127, 0, 127}, dyn[8] = {0, 64, 127, 0, 0, 0, 64, 0};
        std::memcpy(kit.reverb, rev, 8); std::memcpy(kit.delay, del, 8); std::memset(kit.eq, 64, 8); std::memcpy(kit.dynamics, dyn, 8);
        d.kits.push_back(kit);
    }
    mnm::mddump::Pattern a;
    a.position = 0; a.length = 16; a.kit = 0; a.accentEditAll = a.slideEditAll = a.swingEditAll = 1;
    a.trigs[0] = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 12);
    for (int s = 1; s < 16; s += 2) a.trigs[1] |= 1ull << s;
    a.swing = a.trigs[1];
    a.swingAmount = 16 * 16384 / 50;   // 66%
    a.slide = 1u << 12;
    a.lockMasks[0] = 1;                // T1 PTCH: lock row 0
    for (int s = 0; s < 64; ++s) a.locks[0][s] = 0xFF;
    a.locks[0][4] = 100; a.locks[0][12] = 20;
    a.numLockedRows = 1;
    mnm::mddump::Pattern b;
    b.position = 1; b.length = 8; b.kit = 1; b.doubleTempo = 2; b.accentEditAll = b.slideEditAll = b.swingEditAll = 1;
    b.trigs[2] = 1;
    d.patterns.push_back(a); d.patterns.push_back(b);

    auto pHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack

    auto& p = *pHeap;
    p.setFirmwarePath(juce::String(argv[1]), false);
    p.setPlayHead(&head);
    p.prepareToPlay(rate, block);
    p.loadMdKit("seqtest", d.kits[0], "SEQ TEST");
    p.setPatternBank("test", "SEQ TEST", d, 0);
    auto setParam = [&](const juce::String& id, float v) { if (auto* q = p.apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(v)); };
    setParam(seqId(), 1.0f);
    setParam(patternId(), 0.0f);
    p.setTrigLogging(true);
    juce::AudioBuffer<float> buf(2, block);
    const double spc = rate * 60.0 / 120.0 / 24.0;   // samples per clock: 1000
    std::vector<int> base;   // T1 PTCH's base word (>> 7, rounded) at each block's end
    auto run = [&](int blocks) {
        for (int i = 0; i < blocks; ++i) {
            juce::MidiBuffer midi;
            buf.clear();
            p.processBlock(buf, midi);
            head.ppq += block / spc / 24.0;
            p.syncMachineSideEffects();   // the message thread's part (a pattern's kit)
            base.push_back((p.engineForTests()->cpu().baseParam(0, 0) + 64) >> 7);
        }
    };
    run(int(96 * spc / block) + 1);   // one pass of A01 (16 x 6 clocks)
    // trig times: T1 at 0, 24, 48, 72 clocks; T2 at 6k + 6 x 5243 / 16384 for odd k
    const double swing = 6.0 * 5243.0 / 16384.0;
    int t1 = 0, t2 = 0, badTime = 0;
    for (const auto& e : p.trigLog()) {
        if (e.step < 0 || e.sample >= int64_t(96 * spc) - 1) continue;   // the first pass
        const double want = (e.track == 0 ? 6.0 * e.step : 6.0 * e.step + swing) * spc;
        if (std::abs(double(e.sample) - want) > 1.0) { ++badTime; std::printf("    T%d step %d at %lld, want %.1f\n", e.track + 1, e.step, (long long)e.sample, want); }
        (e.track == 0 ? t1 : t2)++;
    }
    check(t1 == 4 && t2 == 8, "one pass: T1 4 trigs, T2 8 (" + juce::String(t1) + ", " + juce::String(t2) + ")");
    check(badTime == 0, "every trig on its clock, the swung ones " + juce::String(swing, 3) + " clocks late");
    auto baseAt = [&](double clock) { return base[size_t(std::min(base.size() - 1, size_t(clock * spc / block)))]; };
    check(baseAt(25) == 100, "step 4's lock: PTCH jumps to 100 (" + juce::String(baseAt(25)) + ")");
    check(baseAt(49) == 64, "step 8, no lock: back to the kit's 64 (" + juce::String(baseAt(49)) + ")");
    const int s1 = baseAt(73), s2 = baseAt(84), s3 = baseAt(95);
    check(s1 >= 20 && s1 < 24 && s2 > s1 && s3 > s2 && s3 < 64, "step 12's slide: 20 toward 64 over 24 clocks (" + juce::String(s1) + " " + juce::String(s2) + " " + juce::String(s3) + ")");
    // A02 queued: it starts where A01 ends (clock 192 = 2 passes), with kit 1
    setParam(patternId(), 1.0f);
    p.setTrigLogging(true);
    const int64_t from = int64_t(head.ppq * 24.0 * spc);
    run(int((192 + 64) * spc / block) - int(base.size()) + 1);
    std::vector<int64_t> t3;
    int t1After = 0;
    for (const auto& e : p.trigLog()) {
        if (e.step < 0) continue;
        if (e.track == 2) t3.push_back(from + e.sample);
        if (e.track == 0 && from + e.sample >= int64_t(192 * spc)) ++t1After;
    }
    check(!t3.empty() && std::abs(double(t3[0]) - 192 * spc) <= 1.0, "A02 starts at the end of A01's pass (clock 192): " + juce::String(t3.empty() ? -1 : t3[0]));
    check(t3.size() >= 2 && std::abs(double(t3[1] - t3[0]) - 64 * spc) <= 1.0, "A02 at 3/4x: 8 steps x 8 clocks a pass");
    check(t1After == 0, "A01's trigs stop at the change");
    check(p.seqPattern() == 1 && p.kitName() == "", "kit 1 loaded by the pattern change (kit name '" + p.kitName() + "')");
    check(base.back() == 30, "kit 1's T1 PTCH in place (" + juce::String(base.back()) + ")");
    // SONG 01: A01 steps 1-8 x2 with T2 muted; LOOP to row 1 once; A02 x1; END. From clock 0: row 1 at 0-96, again
    // at 96-192, A02 at 192-256, then nothing.
    {
        mnm::mddump::Song s;
        s.position = 0;
        mnm::mddump::SongRow r0, loop, r2, end;
        r0.bytes[0] = 0; r0.bytes[2] = 1; r0.bytes[4] = 0; r0.bytes[5] = 0x02; r0.bytes[6] = r0.bytes[7] = 0xFF; r0.bytes[8] = 0; r0.bytes[9] = 8;
        loop.bytes[0] = 0xFE; loop.bytes[2] = 1; loop.bytes[3] = 0;
        r2.bytes[0] = 1; r2.bytes[6] = r2.bytes[7] = 0xFF; r2.bytes[9] = 8;
        end.bytes[0] = 0xFF;
        s.rows = {r0, loop, r2, end};
        d.songs.push_back(s);
        p.setPatternBank("test", "SEQ TEST", d, -1);
        setParam(seqModeId(), 1.0f);
        setParam(songId(), 0.0f);
        head.ppq = 0;
        auto songRun = [&](double fromClock, double toClock) {
            head.ppq = fromClock / 24.0;
            p.setTrigLogging(true);
            run(int((toClock - fromClock) * spc / block) + 1);
            std::vector<std::pair<int, double>> got;   // track, clock
            for (const auto& e : p.trigLog()) if (e.step >= 0) got.push_back({e.track, fromClock + double(e.sample) / spc});
            return got;
        };
        auto got = songRun(0, 300);
        std::vector<double> t1, t3;
        int t2 = 0;
        for (const auto& [tr, c] : got) { if (tr == 0) t1.push_back(c); else if (tr == 1) ++t2; else if (tr == 2) t3.push_back(c); }
        const std::vector<double> want1 = {0, 24, 48, 72, 96, 120, 144, 168};
        bool ok1 = t1.size() == want1.size();
        for (size_t i = 0; ok1 && i < t1.size(); ++i) ok1 = std::abs(t1[i] - want1[i]) < 0.01;
        juce::String s1; for (auto c : t1) s1 << juce::String(c, 1) << " ";
        check(ok1, "row 1 (steps 1-8, x2) twice through the LOOP: T1 at " + s1);
        check(t2 == 0, "row 1's mute: T2 silent (" + juce::String(t2) + " trigs)");
        check(t3.size() == 1 && std::abs(t3[0] - 192) < 0.01, "row 3: A02 once at clock 192, then END (" + juce::String(int(t3.size())) + " trigs)");
        // a jump into the middle: clock 150 = the LOOP's second time through row 1, step 10 of its 16
        got = songRun(150, 260);
        t1.clear(); t3.clear();
        for (const auto& [tr, c] : got) { if (tr == 0) t1.push_back(c); else if (tr == 2) t3.push_back(c); }
        check(t1.size() == 1 && std::abs(t1[0] - 168) < 0.01 && t3.size() == 1 && std::abs(t3[0] - 192) < 0.01,
              "a locate to clock 150 picks the song up there: T1 at 168, A02 at 192");
    }
    if (argc > 3) {   // the editor mid-pattern, the keys showing the steps (T1 selected)
        setParam(seqModeId(), 0.0f); setParam(patternId(), 0.0f);
        head.ppq = 0; run(2);
        head.ppq = 50.0 / 24.0; run(3);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        if (auto* med = dynamic_cast<MdEditor*>(ed.get())) med->refresh();
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File png{juce::String(argv[3])};
        png.deleteFile();
        juce::PNGImageFormat pf;
        if (auto st = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *st);
    }
    std::printf(fails ? "SEQ TEST FAILED (%d)\n" : "SEQ TEST OK\n", fails);
    return fails ? 1 : 0;
}

// MD_REC_TEST
int mdRecTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
    // Live recording against a simulated transport (120 BPM, 48 kHz: 1000 samples a clock, a step = 6 clocks)
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
    struct Head : juce::AudioPlayHead {
        double ppq = 0; bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setPpqPosition(ppq); p.setBpm(120); p.setIsPlaying(playing); return p; }
    } head;
    auto pHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
    auto& p = *pHeap;
    p.setFirmwarePath(juce::String(argv[1]), false);
    p.setPlayHead(&head);
    p.prepareToPlay(rate, block);
    p.editPattern(0, [](mnm::mddump::Pattern& x) { x.trigs[0] = (1u << 8) | (1u << 12); });   // T1 on steps 9 and 13
    const auto before = p.bankPattern(0);
    if (auto* a = p.apvts.getParameter(seqId())) a->setValueNotifyingHost(1.0f);
    p.setRecord(true);
    p.setTrigLogging(true);
    const double spc = 1000.0;
    juce::AudioBuffer<float> buf(2, block);
    auto runTo = [&](double clock, const std::vector<std::pair<double, int>>& notes, std::function<void(double)> each = nullptr) {
        while (head.ppq * 24.0 < clock) {
            const double c0 = head.ppq * 24.0, c1 = c0 + block / spc;
            juce::MidiBuffer midi;
            for (const auto& [c, note] : notes)
                if (c >= c0 && c < c1) midi.addEvent(juce::MidiMessage::noteOn(1, note, uint8_t(100)), int((c - c0) * spc));
            if (each) each(c0);
            buf.clear();
            p.processBlock(buf, midi);
            head.ppq += block / spc / 24.0;
            p.syncMachineSideEffects();
        }
    };
    // T2 (note 38) at 1x: a hit records onto the step playing (MainOS 0x2379AC): clock 23.5 = step 4, 26 and 28 = step 5
    runTo(96, {{23.5, 38}, {26.0, 38}, {28.0, 38}});
    auto pat = p.bankPattern(0);
    check(pat && ((pat->trigs[1] >> 3) & 1) && ((pat->trigs[1] >> 4) & 1) && (pat->trigs[1] & ~((1ull << 3) | (1ull << 4))) == 0,
          "notes at clocks 23.5, 26, 28: T2 trigs on steps 4 and 5 (the step playing), none accented");
    check(pat && pat->accent == 0 && pat->accentPerTrack[1] == 0, "recorded trigs are never accented");
    int step6pass1 = 0;
    for (const auto& e : p.trigLog()) if (e.track == 1 && e.step == 4) ++step6pass1;
    check(step6pass1 == 0, "step 5 was played live: the sequencer does not play it again in that pass");
    p.setTrigLogging(true);
    // pass 2: PTCH of T1 turned at clock 140 (during step 8): locked onto step 9 (144, a trig); forgotten by step 13 (168)
    auto* ptch = p.apvts.getParameter(knobId(0, 0));
    runTo(192, {}, [&](double c0) { if (c0 <= 140.0 && c0 + block / spc > 140.0) ptch->setValueNotifyingHost(ptch->convertTo0to1(90.0f)); });
    int step6pass2 = 0;
    for (const auto& e : p.trigLog()) if (e.track == 1 && e.step == 4) ++step6pass2;
    check(step6pass2 == 1, "the next pass plays the recorded step 5");
    pat = p.bankPattern(0);
    const int row = pat ? pat->lockRow(0, 0) : -1;
    check(row >= 0 && pat->locks[row][8] == 90 && pat->locks[row][12] == 0xFF, "a knob turned during step 8: its value locked on step 9 (the next, a trig), not on step 13");
    head.playing = false;
    runTo(head.ppq * 24.0 + 4, {});
    MdProcessor::RecordedEdit run;
    const bool got = p.takeRecordedEdit(run);
    MdProcessor::RecordedEdit extra;
    check(got && run.slot == 0 && run.before == before && run.after && ((run.after->trigs[1] >> 4) & 1) && !p.takeRecordedEdit(extra),
          "the stop ends the run: one undo step from before the recording to after it");
    std::printf(fails ? "REC TEST FAILED (%d)\n" : "REC TEST OK\n", fails);
    return fails ? 1 : 0;
}

// MD_SONGED_TEST
int mdSongedTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
    // Song editing through the editor's own edit path (undo included), the song playing, and the library save
    // (only into a library under MNM_LIBRARY_DIR: the user's own is never touched)
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
    struct Head : juce::AudioPlayHead {
        double ppq = 0; bool playing = false;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setPpqPosition(ppq); p.setBpm(120); p.setIsPlaying(playing); return p; }
    } head;
    auto pHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
    auto& p = *pHeap;
    p.setFirmwarePath(juce::String(argv[1]), false);
    p.setPlayHead(&head);
    p.prepareToPlay(rate, block);
    p.editPattern(0, [](mnm::mddump::Pattern& x) { x.trigs[0] = 1u; });   // A01: T1 on step 1
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* med = dynamic_cast<MdEditor*>(ed.get());
    med->devOpenSong(0);
    auto& se = med->devSongEditor();
    auto row = [](int b0, int b2, int b3, int b8, int b9) { mnm::mddump::SongRow r; r.bytes[0] = uint8_t(b0); r.bytes[2] = uint8_t(b2); r.bytes[3] = uint8_t(b3); r.bytes[6] = r.bytes[7] = 0xFF; r.bytes[8] = uint8_t(b8); r.bytes[9] = uint8_t(b9); return r; };
    se.edit("row", [&](mnm::mddump::Song& s) { s.rows.push_back(row(0, 1, 0, 0, 16)); }, -1);      // A01 x2
    se.edit("loop", [&](mnm::mddump::Song& s) { s.rows.push_back(row(0xFE, 1, 0, 0, 0)); }, -1);  // LOOP to row 1, once
    se.edit("end", [&](mnm::mddump::Song& s) { s.rows.push_back(row(0xFF, 0, 0, 0, 0)); }, -1);
    auto song = p.bankSong(0);
    check(song && song->rows.size() == 3, "three rows made in the editor");
    med->devUndo();
    check(p.bankSong(0) && p.bankSong(0)->rows.size() == 2, "undo: the END row goes");
    med->devRedo();
    check(p.bankSong(0) && p.bankSong(0)->rows.size() == 3, "redo: back");
    if (auto* a = p.apvts.getParameter(seqId())) a->setValueNotifyingHost(1.0f);
    if (auto* a = p.apvts.getParameter(seqModeId())) a->setValueNotifyingHost(1.0f);
    head.playing = true;
    p.setTrigLogging(true);
    juce::AudioBuffer<float> buf(2, block);
    for (int i = 0; i < int(96 * 6 * 1000.0 / block); ++i) { juce::MidiBuffer mm; buf.clear(); p.processBlock(buf, mm); head.ppq += block / 1000.0 / 24.0; }
    int t1 = 0;
    for (const auto& e : p.trigLog()) if (e.track == 0 && e.step == 0) ++t1;
    check(t1 == 4, "the song plays A01 four times (x2, looped once), then ends (" + juce::String(t1) + ")");
    if (std::getenv("MNM_LIBRARY_DIR")) {
        MdLibrary lib;
        juce::String id; juce::StringArray changes;
        auto r = lib.model().saveMdPatterns({}, p.bankDump(), "SAVE TEST", "test", &id, &changes);
        check(r.wasOk() && id.isNotEmpty() && lib.model().mdState(id) && lib.model().mdState(id)->patternAt(0), "a new project from the plugin's bank");
        p.editPattern(0, [](mnm::mddump::Pattern& x) { x.trigs[1] = 1u << 3; });
        r = lib.model().saveMdPatterns(id, p.bankDump(), "SAVE TEST", "test", nullptr, &changes);
        const auto* st = lib.model().mdState(id);
        check(r.wasOk() && changes.size() == 1 && st && st->patternAt(0) && st->patternAt(0)->trigs[1] == (1u << 3), "an edit saved: a new version with that pattern (" + changes.joinIntoString("; ") + ")");
        r = lib.model().saveMdPatterns(id, p.bankDump(), "SAVE TEST", "test", nullptr, &changes);
        check(r.wasOk() && changes.isEmpty(), "saved again unchanged: no new version");
    } else {
        std::printf("  skip the library save (set MNM_LIBRARY_DIR to a scratch folder)\n");
    }
    if (argc > 3) {
        se.edit("tempo", [&](mnm::mddump::Song& s) { s.rows[0].bytes[6] = uint8_t((128 * 24) >> 8); s.rows[0].bytes[7] = uint8_t(128 * 24); s.rows[0].bytes[5] = 0x05; }, -1);
        med->refresh();
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File png{juce::String(argv[3])}; png.deleteFile();
        juce::PNGImageFormat pf;
        if (auto st = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *st);
    }
    if (argc > 3) {   // a snapshot: a pattern row, a LOOP, a JUMP, a HALT; the start row and a cued row marked
        p.editSong(0, [](mnm::mddump::Song& s) {
            auto row = [](int ptn) { mnm::mddump::SongRow r; r.bytes[0] = uint8_t(ptn); r.bytes[9] = 16; r.bytes[6] = r.bytes[7] = 0xFF; return r; };
            mnm::mddump::SongRow loop; loop.bytes[0] = 0xFE; loop.bytes[2] = 2; loop.bytes[3] = 0;
            mnm::mddump::SongRow jump; jump.bytes[0] = 0xFE; jump.bytes[3] = 5;
            mnm::mddump::SongRow halt; halt.bytes[0] = 0xFE; halt.bytes[3] = 4;
            s.rows = {row(0), row(1), loop, jump, halt, row(2)};
        });
        p.setSongStartRow(1); p.cueSongRow(5);
        med->devOpenSong(0); med->refresh();
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File png{juce::String(argv[3])}; png.deleteFile();
        juce::PNGImageFormat pf;
        if (auto st = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *st);
    }
    std::printf(fails ? "SONGED TEST FAILED (%d)\n" : "SONGED TEST OK\n", fails);
    return fails ? 1 : 0;
}

// MD_GROUP_TEST
int mdGroupTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
    // Trig and mute groups. T1-T3 are GND-SN sines with a long decay, each on its own output (PER TRACK).
    // Mute group: T2 rings, T1 is trigged at 0.25 s with MUTE GROUP T2: T2 must go silent (vs the same run without it).
    // Trig group: only T1 is trigged, with TRIG GROUP T3: T3 must sound (vs silent without it).
    int fails = 0;
    auto run = [&](int trigGroup, int muteGroup, bool trigT2, double& t2After, double& t3, bool viaParams = false) {
        auto pHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
        auto& p = *pHeap;
        p.setFirmwarePath(juce::String(argv[1]), false);
        p.enableAllBuses();
        if (auto* q = p.apvts.getParameter(outputModeId())) q->setValueNotifyingHost(1.0f);
        p.prepareToPlay(rate, block);
        auto kit = p.captureMdKit();
        for (int tr = 0; tr < 3; ++tr) {
            kit.models[tr] = 1;   // GND-SN: pitch, a long decay, no pitch sweep
            kit.params[tr][0] = uint8_t(40 + 12 * tr); kit.params[tr][1] = 127; kit.params[tr][2] = 0; kit.params[tr][3] = 0;
            kit.levels[tr] = 100;
        }
        if (!viaParams) { kit.trigGroups[0] = uint8_t(trigGroup); kit.muteGroups[0] = uint8_t(muteGroup); }
        p.loadMdKit("grouptest", kit, "GROUPS");
        if (viaParams) {   // edited on the ROUTING page after the load
            if (auto* q = p.apvts.getParameter(trigGroupId(0))) q->setValueNotifyingHost(q->convertTo0to1(float(groupParam(uint8_t(trigGroup)))));
            if (auto* q = p.apvts.getParameter(muteGroupId(0))) q->setValueNotifyingHost(q->convertTo0to1(float(groupParam(uint8_t(muteGroup)))));
        }
        p.syncMachineSideEffects();
        juce::AudioBuffer<float> buf(p.getTotalNumOutputChannels(), block);
        t2After = t3 = 0;
        for (int blk = 0; blk < 100; ++blk) {   // 1 s
            juce::MidiBuffer midi;
            if (blk == 0 && trigT2) midi.addEvent(juce::MidiMessage::noteOn(1, kTrackNotes[1], uint8_t(100)), 0);
            if (blk == 25) midi.addEvent(juce::MidiMessage::noteOn(1, kTrackNotes[0], uint8_t(100)), 0);
            buf.clear();
            p.processBlock(buf, midi);
            auto e = [&](int tr) {
                auto b2 = p.getBusBuffer(buf, false, 3 + tr);
                double s = 0;
                for (int c = 0; c < b2.getNumChannels(); ++c) for (int i = 0; i < block; ++i) s += double(b2.getSample(c, i)) * b2.getSample(c, i);
                return s;
            };
            if (blk >= 30) t2After += e(1);
            t3 += e(2);
        }
    };
    auto check = [&](bool ok, const char* what, double a2, double b2) { std::printf("  %s %s (%.4g vs %.4g)\n", ok ? "ok  " : "FAIL", what, a2, b2); fails += ok ? 0 : 1; };
    double plainT2, plainT3, mutedT2, x, y, groupT3;
    run(127, 127, true, plainT2, plainT3);
    run(127, 1, true, mutedT2, x);
    check(plainT2 > 1.0 && mutedT2 < plainT2 * 1e-4, "MUTE GROUP T2: T1's trig silences the ringing T2", mutedT2, plainT2);
    check(plainT3 < 1e-6, "no trig group: T3 stays silent", plainT3, 0.0);
    run(2, 127, false, y, groupT3);
    check(groupT3 > 1.0, "TRIG GROUP T3: T1's trig plays T3", groupT3, plainT3);
    double editedMuteT2, editedT3, z;
    run(127, 1, true, editedMuteT2, z, true);
    run(2, 127, false, z, editedT3, true);
    check(editedMuteT2 < plainT2 * 1e-4 && editedT3 > 1.0, "the same groups set on the ROUTING page (MUTG / TRGG) work", editedMuteT2, editedT3);
    {   // the kit loads into the parameters, a save of the kit carries the edits, the state keeps them, old sessions take the base kit's
        auto pHeap = std::make_unique<MdProcessor>();
        auto& p = *pHeap;
        auto kit = p.captureMdKit();
        kit.trigGroups[4] = 6; kit.muteGroups[8] = 9;
        p.loadMdKit("g", kit, "G");
        const bool loaded = std::lround(p.apvts.getRawParameterValue(trigGroupId(4))->load()) == 7 && std::lround(p.apvts.getRawParameterValue(muteGroupId(8))->load()) == 10;
        if (auto* q = p.apvts.getParameter(muteGroupId(2))) q->setValueNotifyingHost(q->convertTo0to1(16.0f));
        const auto cap = p.captureMdKit();
        check(loaded && cap.trigGroups[4] == 6 && cap.muteGroups[8] == 9 && cap.muteGroups[2] == 15 && cap.trigGroups[0] == 127 && p.kitModified(),
              "a kit's groups load as T5 TRGG T7 / T9 MUTG T10; an edit (T3 MUTG T16) goes into the saved kit and marks it modified", 0, 0);
        juce::MemoryBlock st; p.getStateInformation(st);
        auto rHeap = std::make_unique<MdProcessor>();
        rHeap->setStateInformation(st.getData(), int(st.getSize()));
        const bool kept = std::lround(rHeap->apvts.getRawParameterValue(muteGroupId(2))->load()) == 16 && std::lround(rHeap->apvts.getRawParameterValue(trigGroupId(4))->load()) == 7;
        auto xml = juce::AudioProcessor::getXmlFromBinary(st.getData(), int(st.getSize()));
        for (int i = xml->getNumChildElements(); --i >= 0;)   // a session saved before the group parameters existed
            if (xml->getChildElement(i)->getStringAttribute("id").endsWith("grp")) xml->removeChildElement(xml->getChildElement(i), true);
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary(*xml, old);
        auto oHeap = std::make_unique<MdProcessor>();
        oHeap->setStateInformation(old.getData(), int(old.getSize()));
        const bool migrated = std::lround(oHeap->apvts.getRawParameterValue(trigGroupId(4))->load()) == 7 && std::lround(oHeap->apvts.getRawParameterValue(muteGroupId(8))->load()) == 10;
        check(kept && migrated, "the groups come back with the state; an older session takes them from its saved kit", 0, 0);
    }
    std::printf(fails ? "GROUP TEST FAILED (%d)\n" : "GROUP TEST OK\n", fails);
    return fails ? 1 : 0;
}

} // namespace mnm::plugin::md::test
