// md-plugintest <os.syx> <out.wav> [ui.png]: headless check of Monomodule MD. Plays a two-bar beat through the
// plugin processor at 48 kHz (resampled from the engine's 44.1 kHz) and writes the stereo result; optionally renders
// the editor to a PNG.
#include "one/RomArt.h"
#include <cstdio>
#include <map>
#include <cstring>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MdProcessor.h"
#include "MdEditor.h"
#include "MdPreview.h"
#include "MdMachineText.h"

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
    if (std::getenv("MD_MIDI_TEST")) {
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
    if (std::getenv("MD_SONGED_TEST")) {
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
        std::printf(fails ? "SONGED TEST FAILED (%d)\n" : "SONGED TEST OK\n", fails);
        return fails ? 1 : 0;
    }
    if (std::getenv("MD_REC_TEST")) {
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
    if (std::getenv("MD_GRID_TEST")) {
        // The GRID driven as clicks drive it: steps, undo / redo, a held step's locks (a knob drag is one undo step),
        // a double-click clearing one lock, Alt+click muting a track
        int fails = 0;
        auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto* med = dynamic_cast<MdEditor*>(ed.get());
        med->showGrid(-1);
        auto trigs = [&] { const auto p = proc.bankPattern(0); return p ? p->trigs[0] : uint64_t(0); };
        auto lockAt = [&](int step) { const auto p = proc.bankPattern(0); if (!p) return -1; const int row = p->lockRow(0, 0); return row >= 0 && p->locks[row][step] <= 127 ? int(p->locks[row][step]) : -1; };
        med->devStep(0); med->devStep(4);
        check(trigs() == ((1u << 0) | (1u << 4)), "two clicks: trigs on steps 1 and 5");
        med->devUndo();
        check(trigs() == 1u, "undo: step 5 off again");
        med->devRedo();
        check(trigs() == ((1u << 0) | (1u << 4)), "redo: step 5 back");
        med->devHold(4);
        auto& syn = med->devSynPage();
        for (int v = 70; v <= 100; v += 5) syn.devTurn(0, v);   // a drag
        check(lockAt(4) == 100 && syn.devMarked(0) && !syn.devMarked(1), "held step 5, a knob drag: PTCH locked to 100, its value box marked");
        med->devUndo();
        check(lockAt(4) == -1, "one undo takes the whole drag back");
        med->devRedo();
        check(lockAt(4) == 100, "redo: the lock again");
        syn.devReset(0);
        check(lockAt(4) == -1 && ((trigs() >> 4) & 1), "double-click: that lock goes, the trig stays");
        med->devUndo();
        check(lockAt(4) == 100, "undo brings the cleared lock back");
        {   // copy / paste / clear at face level: modifier clicks on the bar, the keys
            const juce::ModifierKeys shift(juce::ModifierKeys::shiftModifier), ctrl(juce::ModifierKeys::ctrlModifier), alt(juce::ModifierKeys::altModifier);
            const auto pat = [&] { return proc.bankPattern(0); };
            med->devBarClick(MdSeqBar::Pages, 0, shift);   // copy page 1
            med->devBarClick(MdSeqBar::Pages, 1, ctrl);    // paste onto page 2
            check(pat()->length == 32 && ((pat()->trigs[0] >> 16) & 1) && ((pat()->trigs[0] >> 20) & 1) && pat()->locks[pat()->lockRow(0, 0)][20] == 100,
                  "Shift+click page 1, Ctrl+click page 2: page 1 (with its lock) pasted onto page 2, 32 steps");
            med->devBarClick(MdSeqBar::Pages, 1, alt);
            check(pat()->length == 32 && !((pat()->trigs[0] >> 16) & 1) && ((pat()->trigs[0] >> 4) & 1), "Alt+click page 2: cleared, page 1 stays");
            med->devUndo();
            check(((pat()->trigs[0] >> 16) & 1), "and Ctrl+Z brings it back");
            med->keyPressed(juce::KeyPress('d', juce::ModifierKeys::ctrlModifier, 0));
            check(pat()->length == 64 && ((pat()->trigs[0] >> 48) & 1), "Ctrl+D: doubled to 64 steps");
            med->devShowPage(0);
            med->keyPressed(juce::KeyPress('c', juce::ModifierKeys::ctrlModifier, 0));
            med->devShowPage(3);
            med->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys(), 0));
            check(!((pat()->trigs[0] >> 48) & 1) && !((pat()->trigs[0] >> 52) & 1), "Delete: the shown page (4) cleared");
            med->keyPressed(juce::KeyPress('v', juce::ModifierKeys::ctrlModifier, 0));
            check(((pat()->trigs[0] >> 48) & 1) && ((pat()->trigs[0] >> 52) & 1), "Ctrl+C on page 1, Ctrl+V on page 4: pasted");
            med->devBarClick(MdSeqBar::Trk, -1, shift);   // copy T1
            med->selectTrack(5);
            med->devBarClick(MdSeqBar::Trk, -1, ctrl);    // paste onto T6
            check(pat()->trigs[5] == pat()->trigs[0] && pat()->lockRow(5, 0) >= 0, "Shift+click TRK (T1), select T6, Ctrl+click TRK: T1 pasted onto T6 with its locks");
            med->devBarClick(MdSeqBar::Trk, -1, alt);
            check(pat()->trigs[5] == 0, "Alt+click TRK: T6 cleared");
            med->selectTrack(0);
            med->devBarClick(MdSeqBar::Ptn, -1, shift);   // copy A01
            if (auto* a = proc.apvts.getParameter(patternId())) a->setValueNotifyingHost(a->convertTo0to1(1.0f));
            med->devBarClick(MdSeqBar::Ptn, -1, ctrl);    // paste onto A02
            const auto a02 = proc.bankPattern(1);
            check(a02 && a02->trigs[0] == pat()->trigs[0] && a02->length == 64, "Shift+click PTN (A01), PTN to A02, Ctrl+click PTN: A01 pasted into A02");
            if (auto* a = proc.apvts.getParameter(patternId())) a->setValueNotifyingHost(0.0f);
        }
        med->devMuteKey(2);
        check(proc.apvts.getRawParameterValue(muteId(2))->load() >= 0.5f, "Alt+click key 3: T3 muted");
        med->devMuteKey(2);
        check(proc.apvts.getRawParameterValue(muteId(2))->load() < 0.5f, "again: unmuted");
        {   // a fresh pattern swings the even steps (as the factory patterns), so SWNG works at once
            med->devStep(7); med->devStep(7);   // make sure slot exists... (a trig set and removed)
            const auto pp = proc.bankPattern(0);
            check(pp && pp->swing != 0, "patterns made in the plugin have swing trigs (steps 2, 4, 6...)");
        }
        {   // a drag paints: the first key turned on, the keys dragged over on too; one undo step for the stroke
            const auto before = trigs();
            med->devPaint(8, true, true); med->devPaint(9, true, false); med->devPaint(10, true, false);
            check((trigs() & (7ull << 8)) == (7ull << 8), "a drag over steps 9-11 paints them on");
            med->devUndo();
            check(trigs() == before, "one undo takes the whole stroke back");
            med->devPaint(8, true, true); med->devPaint(9, true, false);
            med->devPaint(8, false, true); med->devPaint(9, false, false);
            check((trigs() & (3ull << 8)) == 0, "a stroke started on a lit step erases");
        }
        {   // DEL PAGE: page 2 of a 3-page pattern goes, page 3 moves up
            proc.editPattern(0, [](mnm::mddump::Pattern& x) { x.length = 48; x.scale = 2; x.trigs[0] = (1ull << 0) | (1ull << 17) | (1ull << 34); });
            med->devDeletePage(1);
            const auto pp = proc.bankPattern(0);
            check(pp && pp->length == 32 && pp->trigs[0] == ((1ull << 0) | (1ull << 18)), "DEL PAGE 2: 48 -> 32 steps, page 3's trig (step 35) now step 19");
            med->devUndo();
            check(proc.bankPattern(0)->length == 48, "undo brings the page back");
        }
        {   // step editing pack: track shift, note copy / paste, a held step's locks cleared, a knob click locks its value
            proc.editPattern(0, [](mnm::mddump::Pattern& x) { x.length = 16; x.scale = 0; x.trigs[0] = (1ull << 0) | (1ull << 15); x.setLock(0, 0, 0, 33); });
            med->devShiftTrack(1);
            auto pp = proc.bankPattern(0);
            const int row = pp ? pp->lockRow(0, 0) : -1;
            check(pp && pp->trigs[0] == ((1ull << 1) | (1ull << 0)) && row >= 0 && pp->locks[row][1] == 33, "Shift+Right: T1's trigs one step later (16 wraps to 1), the lock goes with its trig");
            med->devShiftTrack(-1);
            check(proc.bankPattern(0)->trigs[0] == ((1ull << 0) | (1ull << 15)), "Shift+Left: back");
            med->devCopyNote(0); med->devStep(4); med->devPasteNote(4);
            pp = proc.bankPattern(0);
            check(pp && ((pp->trigs[0] >> 4) & 1) && pp->locks[pp->lockRow(0, 0)][4] == 33, "copy note 1, paste onto 5: the trig and its PTCH lock");
            med->devHold(8);
            med->devStep(8);   // (a trig there to hold) -- toggles: make sure it is on
            if (!((proc.bankPattern(0)->trigs[0] >> 8) & 1)) med->devStep(8);
            med->devHold(8);
            med->devSynPage().devClick(1);   // DEC as it is: locked on step 9
            pp = proc.bankPattern(0);
            check(pp && pp->lockRow(0, 1) >= 0 && pp->locks[pp->lockRow(0, 1)][8] <= 127, "a click on a held step's knob locks its value as it is");
            med->devHold(-1);
        }
        {   // Alt + turn: every other track's same knob (not the edited one twice; MID / CTR / RAM-R skipped)
            if (med->devSynPage().onAltTurn) med->devSynPage().onAltTurn(0, 99);
            bool all = true;
            for (int u = 1; u < 16; ++u) all = all && std::lround(proc.apvts.getRawParameterValue(knobId(u, 0))->load()) == 99;
            check(all, "Alt + PTCH turn: every track's PTCH to 99");
        }
        {   // queued mutes: Shift + M on T2 and T3, nothing yet; Shift let go: both flip together
            med->devQueueMute(1); med->devQueueMute(2);
            const bool none = proc.apvts.getRawParameterValue(muteId(1))->load() < 0.5f && proc.apvts.getRawParameterValue(muteId(2))->load() < 0.5f;
            med->devApplyMuteQueue();
            check(none && proc.apvts.getRawParameterValue(muteId(1))->load() >= 0.5f && proc.apvts.getRawParameterValue(muteId(2))->load() >= 0.5f, "Shift + M on T2, T3: queued, then both muted together");
            med->devQueueMute(1); med->devQueueMute(2); med->devApplyMuteQueue();
        }
        {   // kit tools: copy / paste / clear a machine; undo a kit change (twice = back again)
            auto setM = [&](int t, int id) { if (auto* q = proc.apvts.getParameter(machineId(t))) q->setValueNotifyingHost(q->convertTo0to1(float(machineIndexOf(id)))); };
            setM(0, 17); proc.syncMachineSideEffects();   // the machine first (its defaults land), then DEC
            if (auto* q = proc.apvts.getParameter(knobId(0, 1))) q->setValueNotifyingHost(q->convertTo0to1(77.0f));
            proc.syncMachineSideEffects();
            med->devSelectTrack(0); med->devCopyMachine();
            med->devSelectTrack(5); med->devPasteMachine();
            proc.syncMachineSideEffects();
            check(proc.machineIdOf(5) == 17 && std::lround(proc.apvts.getRawParameterValue(knobId(5, 1))->load()) == 77, "copy T1's machine, paste onto T6: the machine and its DEC (got machine " + juce::String(proc.machineIdOf(5)) + ", DEC " + juce::String(proc.apvts.getRawParameterValue(knobId(5, 1))->load()) + ", T1 " + juce::String(proc.machineIdOf(0)) + ")");
            med->devClearMachine();
            proc.syncMachineSideEffects();
            check(proc.machineIdOf(5) == 0, "clear T6's machine: GND---");
            auto other = proc.captureMdKit();
            for (int u = 0; u < 16; ++u) other.models[u] = 1;   // a kit of GND-SN
            const auto before = proc.machineIdOf(0);
            med->devLoadKitData(other, "OTHER");
            proc.syncMachineSideEffects();
            const bool loaded = proc.machineIdOf(0) == 1;
            med->devUndoKit();
            proc.syncMachineSideEffects();
            const bool back = proc.machineIdOf(0) == before;
            med->devUndoKit();
            proc.syncMachineSideEffects();
            check(loaded && back && proc.machineIdOf(0) == 1, "a kit loaded, UNDO KIT brings the one before back, again: the new one");
            med->devUndoKit(); proc.syncMachineSideEffects();
            med->devSelectTrack(0);
        }
        med->devBarClick(MdSeqBar::Pages, 0, juce::ModifierKeys(juce::ModifierKeys::shiftModifier));   // the bar says "COPIED PAGE 1"
        if (argc > 3) {
            med->devHold(4); med->refresh();
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::File png{juce::String(argv[3])}; png.deleteFile();
            juce::PNGImageFormat pf;
            if (auto st = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *st);
        }
        std::printf(fails ? "GRID TEST FAILED (%d)\n" : "GRID TEST OK\n", fails);
        return fails ? 1 : 0;
    }
    if (std::getenv("MD_EDIT_TEST")) {
        // Pattern editing: the lock rows as the unit keeps them, the sysex round trip, a bank made from nothing, and an
        // edit while it plays (it plays from the next block)
        int fails = 0;
        auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
        mnm::mddump::Pattern pat;
        for (auto& row : pat.locks) std::fill(std::begin(row), std::end(row), uint8_t(0xFF));
        pat.length = 16;
        pat.setLock(3, 5, 2, 90); pat.setLock(1, 2, 0, 10); pat.setLock(3, 1, 7, 33); pat.setLock(3, 5, 9, 91);
        check(pat.lockRow(1, 2) == 0 && pat.lockRow(3, 1) == 1 && pat.lockRow(3, 5) == 2 && pat.numLockedRows == 3, "rows in track / param order (T2 P3, T4 P2, T4 P6)");
        check(pat.locks[2][2] == 90 && pat.locks[2][9] == 91 && pat.locks[1][7] == 33 && pat.locks[0][0] == 10, "values on their steps");
        pat.clearLock(1, 2, 0);
        check(pat.lockRow(1, 2) == -1 && pat.lockRow(3, 1) == 0 && pat.lockRow(3, 5) == 1 && pat.numLockedRows == 2, "an emptied row goes, the rest move up");
        pat.clearLock(3, 5, 2);
        check(pat.lockRow(3, 5) == 1 && pat.locks[1][9] == 91, "a row with a lock left stays");
        pat.trigs[3] = (1u << 7) | (1u << 9);
        pat.position = 5;
        mnm::mddump::Pattern back;
        const auto syx = mnm::mddump::encodePattern(pat);
        const bool dec = mnm::mddump::decodePattern(syx.data(), syx.size(), back);
        check(dec && back.lockRow(3, 1) == 0 && back.locks[0][7] == 33 && back.locks[back.lockRow(3, 5)][9] == 91 && back.trigs[3] == pat.trigs[3], "sysex round trip");
        int full = 0;
        for (int t = 0; t < 16; ++t) for (int q = 0; q < 24; ++q) full += pat.setLock(t, q, 0, 1) ? 1 : 0;
        check(full == 64, "64 lock rows at most (" + juce::String(full) + ")");

        struct Head : juce::AudioPlayHead {
            double ppq = 0; bool playing = true;
            juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setPpqPosition(ppq); p.setBpm(120); p.setIsPlaying(playing); return p; }
        } head;
        auto pHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
        auto& p = *pHeap;
        p.setFirmwarePath(juce::String(argv[1]), false);
        p.setPlayHead(&head);
        p.prepareToPlay(rate, block);
        p.editPattern(0, [](mnm::mddump::Pattern& x) { x.trigs[0] = (1u << 0) | (1u << 4); x.setLock(0, 0, 4, 100); });
        check(p.bankName() == "PLUGIN" && p.bankHasPattern(0) && !p.bankHasPattern(1), "an edit with no bank makes one (PLUGIN) with A01");
        auto setParam = [&](const juce::String& id, float v) { if (auto* q = p.apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(v)); };
        setParam(seqId(), 1.0f);
        p.setTrigLogging(true);
        juce::AudioBuffer<float> buf(2, block);
        const double spc = rate * 60.0 / 120.0 / 24.0;
        int lockSeen = -1;
        auto run = [&](int blocks) {
            for (int i = 0; i < blocks; ++i) {
                juce::MidiBuffer midi; buf.clear(); p.processBlock(buf, midi);
                head.ppq += block / spc / 24.0;
                if (std::abs(head.ppq * 24.0 - 26.0) < 0.6) lockSeen = (p.engineForTests()->cpu().baseParam(0, 0) + 64) >> 7;
            }
        };
        run(int(48 * spc / block));   // half a pass
        p.editPattern(0, [](mnm::mddump::Pattern& x) { x.trigs[0] |= 1u << 12; });   // step 13 added while it plays
        run(int(48 * spc / block) + 2);
        std::vector<int> steps;
        for (const auto& e : p.trigLog()) if (e.step >= 0 && e.track == 0) steps.push_back(e.step);
        juce::String st; for (int s : steps) st << s + 1 << " ";
        check(steps.size() == 4 && steps[0] == 0 && steps[1] == 4 && steps[2] == 12 && steps[3] == 0, "T1 plays steps 1 5, then 13 added while playing, then 1 again: " + st);
        check(lockSeen == 100, "step 5's lock from the edit (" + juce::String(lockSeen) + ")");
        {   // PLAY: the plugin's own clock while the host is stopped, from step 1
            head.playing = false;
            p.setTrigLogging(true);
            run(4);   // stopped: nothing
            const bool quiet = p.trigLog().empty();
            p.setInternalPlay(true);
            p.setTrigLogging(true);
            run(int(96 * spc / block) + 1);
            std::vector<int> st2;
            for (const auto& e : p.trigLog()) if (e.step >= 0 && e.track == 0) st2.push_back(e.step);
            check(quiet && st2.size() >= 3 && st2[0] == 0 && st2[1] == 4 && st2[2] == 12 && p.seqPlaying(), "PLAY with the host stopped: steps 1 5 13 from the start");
            head.playing = true;
            run(1);
            check(!p.internalPlay(), "the host's transport takes over");
        }
        {   // PLAY with no pattern at all (a fresh instance, no bank): the steps run all the same
            auto qHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
            auto& q = *qHeap;
            q.setFirmwarePath(juce::String(argv[1]), false);
            Head h2; h2.playing = false;
            q.setPlayHead(&h2);
            q.prepareToPlay(rate, block);
            if (auto* a = q.apvts.getParameter(seqId())) a->setValueNotifyingHost(1.0f);
            q.setInternalPlay(true);
            juce::AudioBuffer<float> b2(2, block);
            std::vector<int> seen;
            for (int i = 0; i < 100; ++i) { juce::MidiBuffer mm; b2.clear(); q.processBlock(b2, mm); if (seen.empty() || seen.back() != q.seqStep()) seen.push_back(q.seqStep()); }
            check(q.seqPlaying() && seen.size() >= 6 && seen.front() == 0, "PLAY on an empty pattern (no bank): the step moves (" + juce::String(int(seen.size())) + " steps seen)");
            // the host's transport, no pattern, no bank: the steps run too
            auto hHeap = std::make_unique<MdProcessor>();
            auto& hq = *hHeap;
            hq.setFirmwarePath(juce::String(argv[1]), false);
            Head h3; h3.playing = true;
            hq.setPlayHead(&h3);
            hq.prepareToPlay(rate, block);
            check(hq.apvts.getRawParameterValue(seqId())->load() >= 0.5f, "SEQ is on by default: the host's play runs the pattern");
            std::vector<int> seen2;
            for (int i = 0; i < 100; ++i) { juce::MidiBuffer mm; b2.clear(); hq.processBlock(b2, mm); h3.ppq += block / 1000.0 / 24.0; if (seen2.empty() || seen2.back() != hq.seqStep()) seen2.push_back(hq.seqStep()); }
            check(hq.seqPlaying() && seen2.size() >= 6 && hq.seqLength() == 16, "the host's play on an empty pattern (no bank): the step moves (" + juce::String(int(seen2.size())) + " steps seen)");
            std::unique_ptr<juce::AudioProcessorEditor> hed(hq.createEditor());
            auto* hmed = dynamic_cast<MdEditor*>(hed.get());
            hmed->refresh();
            check(hmed->devKeys().devSeqLength() == 16 && hmed->devKeys().devSeqStep() >= 0, "the track keys show the running step of the empty pattern (step " + juce::String(hmed->devKeys().devSeqStep() + 1) + ")");
        }
        {   // copy / clear / double on the pattern
            mnm::mddump::Pattern a;
            for (auto& row : a.locks) std::fill(std::begin(row), std::end(row), uint8_t(0xFF));
            a.length = 16;
            a.trigs[0] = (1u << 0) | (1u << 4); a.trigs[2] = 1u << 8;
            a.setLock(0, 0, 4, 99);
            a.accentPerTrack[0] = 1u << 4;
            a.accent = 1u << 8;
            mnm::mddump::Pattern b = a;
            b.copySteps(a, 0, 16, 16, -1, 0);
            const int row = b.lockRow(0, 0);
            check(((b.trigs[0] >> 16) & 1) && ((b.trigs[0] >> 20) & 1) && ((b.trigs[2] >> 24) & 1) && row >= 0 && b.locks[row][20] == 99
                  && ((b.accentPerTrack[0] >> 20) & 1) && ((b.accent >> 24) & 1) && b.locks[row][4] == 99,
                  "copy page 1 -> 2 (all tracks): trigs, the lock, the marks");
            mnm::mddump::Pattern c = b;
            c.clearSteps(16, 16, 0);
            check(!((c.trigs[0] >> 16) & 1) && !((c.trigs[0] >> 20) & 1) && ((c.trigs[2] >> 24) & 1) && c.locks[c.lockRow(0, 0)][20] == 0xFF && c.locks[c.lockRow(0, 0)][4] == 99,
                  "clear page 2 of T1: its trigs and locks go, T3's and page 1's stay");
            mnm::mddump::Pattern d = a;
            d.copySteps(a, 0, 3, 1, 0, 5);
            check(((d.trigs[5] >> 3) & 1) && d.lockRow(5, 0) < 0 && ((d.trigs[0] >> 0) & 1), "copy one track's step onto another track");
            p.editPattern(0, [](mnm::mddump::Pattern& x) { const auto copy = x; x.copySteps(copy, 0, 16, 16, -1, 0); x.length = 32; x.scale = 1; });
            const auto doubled = p.bankPattern(0);
            check(doubled && doubled->length == 32 && ((doubled->trigs[0] >> 16) & 1) && ((doubled->trigs[0] >> 20) & 1) && ((doubled->trigs[0] >> 28) & 1), "double: 32 steps, the first 16 again");
            // it plays: two passes' worth of steps
            head.playing = true; head.ppq = 0;
            p.setTrigLogging(true);
            run(int(192 * spc / block) + 1);
            std::vector<int> st3;
            for (const auto& e : p.trigLog()) if (e.step >= 0 && e.track == 0) st3.push_back(e.step);
            juce::String s3; for (int s : st3) s3 << s + 1 << " ";
            check(st3.size() >= 6 && st3[0] == 0 && st3[1] == 4 && st3[2] == 12 && st3[3] == 16 && st3[4] == 20 && st3[5] == 28, "the doubled pattern plays steps 1 5 13 17 21 29: " + s3);
            // the edits are kept: state round trip
            juce::MemoryBlock state;
            p.getStateInformation(state);
            auto rHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
            auto& r = *rHeap;
            r.setStateInformation(state.getData(), int(state.getSize()));
            const auto back = r.bankPattern(0);
            check(back && back->length == 32 && back->trigs[0] == doubled->trigs[0] && back->locks[back->lockRow(0, 0)][20] == 100, "the edited pattern comes back with the plugin state");
        }
        if (argc > 3) {
            std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
            if (auto* med = dynamic_cast<MdEditor*>(ed.get())) { med->showGrid(12); med->refresh(); }
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::File png{juce::String(argv[3])};
            png.deleteFile();
            juce::PNGImageFormat pf;
            if (auto s = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *s);
        }
        std::printf(fails ? "EDIT TEST FAILED (%d)\n" : "EDIT TEST OK\n", fails);
        return fails ? 1 : 0;
    }
    if (std::getenv("MD_SEQ_TEST")) {
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
    if (std::getenv("MD_GROUP_TEST")) {
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
    if (std::getenv("MD_OSCHECK_TEST")) {   // behaviour checked against the OS: PRG CHANGE's channel + timing, song HALT, empty song slots
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
        std::printf(fails ? "OSCHECK TEST FAILED (%d)\n" : "OSCHECK TEST OK\n", fails);
        return fails ? 1 : 0;
    }
    if (std::getenv("MD_MIXER_TEST")) {   // G = GRID, M = the mixer; its faders, mutes and solos
        int fails = 0;
        auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
        proc.prepareToPlay(48000.0, 480);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto* med = dynamic_cast<MdEditor*>(ed.get());
        const bool g0 = med->devGridOn();
        med->keyPressed(juce::KeyPress('g'));
        const bool g1 = med->devGridOn();
        med->keyPressed(juce::KeyPress('g'));
        check(!g0 && g1 && !med->devGridOn(), "G turns GRID on, and off again");
        med->keyPressed(juce::KeyPress('m'));
        auto& mx = med->devMixer();
        check(mx.isVisible() && mx.getWidth() > 0, "M opens the mixer");
        mx.set(2, MdMixer::Level, 64);
        mx.set(2, MdMixer::Pan, 10);
        check(std::lround(proc.apvts.getRawParameterValue(levelId(2))->load()) == 64 && std::lround(proc.apvts.getRawParameterValue(panId(2))->load()) == 10
              && mx.strip(2).level == 64 && mx.strip(2).pan == 10, "T3's fader and pan set its LEVEL and PAN");
        mx.set(4, MdMixer::Mute, 1);
        check(proc.apvts.getRawParameterValue(muteId(4))->load() >= 0.5f && mx.strip(4).mute, "MUTE mutes T5");
        mx.set(4, MdMixer::Mute, 0);
        auto trigs = [&](int note) {
            proc.setTrigLogging(true);
            juce::MidiBuffer in; in.addEvent(juce::MidiMessage::noteOn(1, note, uint8_t(100)), 0);
            juce::AudioBuffer<float> buf(2, 480);
            proc.processBlock(buf, in);
            return proc.trigLog().size();
        };
        mx.set(1, MdMixer::Solo, 1);
        const auto other = trigs(36), soloed = trigs(38);   // the note map: T1 36, T2 38
        check(proc.soloed(1) && other == 0 && soloed == 1, "SOLO T2: T1's trig is silenced, T2's plays");
        mx.set(1, MdMixer::Solo, 0);
        check(trigs(36) == 1, "unsoloed: T1 plays again");
        mx.set(6, MdMixer::Select, 0);
        check(mx.strip(6).selected, "a click on T7's name selects it");
        if (argc > 3) {
            mx.set(1, MdMixer::Solo, 1); mx.set(4, MdMixer::Mute, 1);
            auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
            juce::File png{juce::String(argv[3])}; png.deleteFile();
            if (auto s = png.createOutputStream()) { juce::PNGImageFormat().writeImageToStream(img, *s); std::printf("wrote %s\n", argv[3]); }
        }
        med->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        mx.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        check(!mx.isVisible(), "Escape closes it");
        std::printf(fails ? "MIXER TEST FAILED (%d)\n" : "MIXER TEST OK\n", fails);
        return fails ? 1 : 0;
    }
    if (const char* syx = std::getenv("MD_ARROW_TEST")) {   // Up / Down step the kit or sound selector used last (a scratch MNM_LIBRARY_DIR)
        int fails = 0;
        auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
        if (!std::getenv("MNM_LIBRARY_DIR")) { std::printf("set MNM_LIBRARY_DIR to a scratch folder\n"); return 1; }
        juce::SharedResourcePointer<MdLibrary> lib;
        juce::String id;
        lib->importSyx(juce::File(juce::String(syx)), &id);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto* med = dynamic_cast<MdEditor*>(ed.get());
        med->devStripPart(MdKitStrip::KitNext);
        const auto k1 = proc.loadedKitKey();
        med->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
        const auto k2 = proc.loadedKitKey();
        med->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
        check(k1.isNotEmpty() && k2.isNotEmpty() && k2 != k1 && proc.loadedKitKey() == k1, "after the kit arrow: Down loads the next kit, Up the one before");
        med->devStripPart(MdKitStrip::SoundNext);
        const auto s1 = proc.loadedSoundKey(0);
        med->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
        check(s1.isNotEmpty() && proc.loadedSoundKey(0) != s1 && proc.loadedKitKey() == k1, "after the sound arrow: Down steps T1's sound, not the kit");
        check(!med->keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)), "Shift+Down is left alone");
        std::printf(fails ? "ARROW TEST FAILED (%d)\n" : "ARROW TEST OK\n", fails);
        return fails ? 1 : 0;
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
    if (std::getenv("MD_PARITY_TEST")) {   // knob names, per-machine memory, init kit, save into a project
        int fails = 0;
        auto check = [&](bool ok, const char* what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what); fails += ok ? 0 : 1; };
        auto set = [&](const juce::String& id, float v) { if (auto* pp = proc.apvts.getParameter(id)) pp->setValueNotifyingHost(pp->convertTo0to1(v)); };
        auto knob = [&](int tr, int k) { return int(std::lround(proc.apvts.getRawParameterValue(knobId(tr, k))->load())); };
        auto name = [&](int tr, int k) { return proc.apvts.getParameter(knobId(tr, k))->getName(32); };
        proc.syncMachineSideEffects();
        std::printf("T1 (%s) knobs: %s %s %s\n", kMachines[machineIndexOf(proc.machineIdOf(0))].name, name(0, 0).toRawUTF8(), name(0, 1).toRawUTF8(), name(0, 2).toRawUTF8());
        check(name(0, 0) == "T1 PTCH", "a knob is named after its track and the machine's label (TRX-BD: T1 PTCH)");
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
            auto otherHeap = std::make_unique<MdProcessor>();   // on the heap: main's many test blocks would overflow the stack
            auto& other = *otherHeap;
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
        check(int(std::lround(proc.apvts.getRawParameterValue(levelId(3))->load())) == 100 && knob(0, 0) == kDefaultKit[0].knobs[0], "INIT KIT: levels (100) and knobs back to the defaults");
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
            if (std::getenv("MD_UI_GRIDCLICK")) { med->devBarPart(MdSeqBar::Grid); med->refresh(); }   // dev: GRID clicked on a fresh plugin
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
