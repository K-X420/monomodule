// md-plugintest's checks of the editor: GRID, pattern edits, the mixer, the selectors, the newer tools
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

// MD_GRID_TEST
int mdGridTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
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
    {   // the A / S / W buttons and keys: a step's marks, all tracks <-> per track
        proc.editPattern(0, [](mnm::mddump::Pattern& x) { x.accent = 0; x.slide = 0; x.accentEditAll = x.slideEditAll = x.swingEditAll = 1; });
        med->devFlag(3, 0); med->devFlag(3, 1);
        auto pp = proc.bankPattern(0);
        check(pp && ((pp->accent >> 3) & 1) && ((pp->slide >> 3) & 1), "A and S on step 4: accent and slide (all tracks)");
        med->devPerTrack(0);
        pp = proc.bankPattern(0);
        check(pp && pp->accentEditAll == 0 && ((pp->accentPerTrack[5] >> 3) & 1), "the A key: accent per track (every track keeps step 4's mark)");
        med->devFlag(3, 0);
        pp = proc.bankPattern(0);
        check(pp && !((pp->accentPerTrack[0] >> 3) & 1) && ((pp->accentPerTrack[5] >> 3) & 1), "A on step 4 now flips only the track's own mark");
        med->devPerTrack(0);
        // the ACCENT window: a stroke over steps 9-12 paints them, one undo takes it back
        proc.editPattern(0, [](mnm::mddump::Pattern& x) { x.accent = 0; });
        med->devMarkMode(1);
        med->devMarkPaint(8, 0, true, true); med->devMarkPaint(9, 0, true, false); med->devMarkPaint(10, 0, true, false); med->devMarkPaint(11, 0, true, false);
        const bool painted = (proc.bankPattern(0)->accent & (0xFull << 8)) == (0xFull << 8);
        med->devUndo();
        check(painted && (proc.bankPattern(0)->accent & (0xFull << 8)) == 0, "ACCENT window: a slide over steps 9-12 accents them; one undo takes the stroke back");
        med->devMarkMode(0);
    }
    {   // every knob has a tooltip
        bool all = true;
        for (int k = 0; k < 8; ++k) all = all && med->devSynPage().devTip(k).isNotEmpty();
        check(all && med->devSynPage().devTip(1) == "Decay time", "the SYNTHESIS knobs say what they do: " + med->devSynPage().devTip(1));
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

// MD_EDIT_TEST
int mdEditTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
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

// MD_MIXER_TEST: G = GRID, M = the mixer; its faders, mutes and solos
int mdMixerTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
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
    mx.set(8, MdMixer::MuteGroup, 9);
    check(std::lround(proc.apvts.getRawParameterValue(muteGroupId(8))->load()) == 10 && mx.strip(8).muteGroup == 9, "T9's mute group set to T10 from the mixer (the ROUTING page's MUTG)");
    mx.set(8, MdMixer::MuteGroup, -1);
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

// MD_ARROW_TEST: Up / Down step the kit or sound selector used last (a scratch MNM_LIBRARY_DIR)
int mdArrowTest(TestEnv& env, const char* syx)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, syx);
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

// MD_FOUR_TEST: CLASSIC / EXTENDED, note learn, the sample manager, the MIDI clip
int mdFourTest(TestEnv& env, const char* value)
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
    std::vector<juce::MidiMessage> out;
    auto run = [&](int blocks, juce::MidiBuffer in = {}) {
        for (int i = 0; i < blocks; ++i) {
            buf.clear(); juce::MidiBuffer m = i == 0 ? in : juce::MidiBuffer();
            p.processBlock(buf, m);
            for (const auto e : m) out.push_back(e.getMessage());
            if (head.playing) head.ppq += blk / 48000.0 * 2.0;
            p.syncMachineSideEffects();
        }
    };
    // CLASSIC: the pattern's lock (T1 PTCH 10 on step 1) is not played (its CC does not go out); EXTENDED: it is
    auto ms = MdProcessor::defaultMidiSettings(); ms.midiOut = 2; p.setMidiSettings(ms);
    p.editPattern(0, [](mnm::mddump::Pattern& x) { x.length = 16; x.trigs[0] = 1; x.setLock(0, 0, 0, 10); });
    auto lockOut = [&](bool extended) {
        if (auto* q = p.apvts.getParameter(extendedId())) q->setValueNotifyingHost(extended ? 1.0f : 0.0f);
        head.playing = false; run(3); out.clear();
        head.ppq = 0; head.playing = true; run(20);
        for (const auto& m : out) if (m.isController() && m.getControllerNumber() == 16 && m.getControllerValue() == 10) return true;
        return false;
    };
    const bool ext = lockOut(true), classic = lockOut(false);
    check(ext && !classic, "EXTENDED plays the lock (CC 16 = 10 goes out), CLASSIC does not");
    if (auto* q = p.apvts.getParameter(extendedId())) q->setValueNotifyingHost(1.0f);
    head.playing = false; run(3);
    // LEARN: the next note is caught, not played
    p.setTrigLogging(true);
    p.armLearn();
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(100)), 0); run(1, in); }
    const int learned = p.takeLearned();
    check(learned == 36 && p.trigLog().empty(), "LEARN: note 36 caught (" + juce::String(learned) + "), T1 not played");
    { juce::MidiBuffer in; in.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(100)), 0); run(1, in); }
    check(!p.trigLog().empty(), "after it, the note plays T1 again");
    // the sample manager: a ROM sample reads back from sample memory as loaded; rename; RAM copy with nothing recorded
    {
        const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md_four_test.wav");
        wav.deleteFile();
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::FileOutputStream> s(wav.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(s.get(), 44100.0, 1, 16, {}, 0));
            if (w) {
                s.release();
                juce::AudioBuffer<float> sb(1, 4410);
                for (int i = 0; i < sb.getNumSamples(); ++i) sb.setSample(0, i, 0.5f * std::sin(float(i) * 0.05f));
                w->writeFromAudioSampleBuffer(sb, 0, sb.getNumSamples());
            }
        }
        const auto err = p.loadSample(4, wav);
        double rate = 0;
        const auto back = p.engineForTests()->voices().readSlot(4, &rate);
        double maxErr = 0;
        for (size_t i = 0; i < back.size() && i < 4410; ++i) maxErr = std::max(maxErr, std::abs(double(back[i]) - 0.5 * std::sin(double(i) * 0.05)));
        check(err.isEmpty() && back.size() == 4410 && maxErr < 0.02 && std::abs(rate - 44100.0) < 50.0,
              "a ROM sample reads back from sample memory (" + juce::String(int(back.size())) + " samples, max error " + juce::String(maxErr, 4) + ", " + juce::String(rate, 0) + " Hz)");
        check(p.renameSample(4, "kick 1").isEmpty() && p.sampleName(4) == "KICK 1", "rename: ROM-05 is KICK 1");
        check(p.ramSeconds(0) == 0.0 && p.copyRamToRom(0, 6).isNotEmpty(), "RAM 1 with nothing recorded: no copy (and a reason)");
        wav.deleteFile();
    }
    // the MIDI clip: the pattern's trigs as notes on the tracks' trig notes
    {
        mnm::mddump::Pattern pat; pat.length = 16; pat.trigs[0] = 0x1111; pat.trigs[1] = 0x0101;
        const auto mf = mnm::library::buildMdPatternMidiFile(nullptr, pat);
        int on36 = 0, on38 = 0;
        for (int tr = 0; tr < mf.getNumTracks(); ++tr)
            for (const auto* ev : *mf.getTrack(tr)) if (ev->message.isNoteOn()) { on36 += ev->message.getNoteNumber() == 36; on38 += ev->message.getNoteNumber() == 38; }
        check(on36 == 4 && on38 == 2, "MIDI clip: T1's 4 trigs on note 36, T2's 2 on note 38");
    }
    {   // a MIDI clip in: the export of a pattern, dropped into an empty slot, comes back the same; onto a key: one track
        if (auto* q = p.apvts.getParameter(patternId())) q->setValueNotifyingHost(q->convertTo0to1(20.0f));
        run(2);
        mnm::mddump::Pattern src; src.length = 32; src.accentEditAll = src.slideEditAll = src.swingEditAll = 1;
        for (auto& row : src.locks) std::fill(std::begin(row), std::end(row), uint8_t(0xFF));
        src.trigs[0] = 0x0001000100010001ull & 0xFFFFFFFFull; src.trigs[2] = (1ull << 4) | (1ull << 20); src.accent = 1ull << 4;
        src.setLock(2, 1, 20, 33);
        const auto kit = p.captureMdKit();
        const auto mf = mnm::library::buildMdPatternMidiFile(&kit, src);
        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md_clip_test.mid");
        file.deleteFile();
        { juce::FileOutputStream o(file); mf.writeTo(o); }
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        auto* med = dynamic_cast<MdEditor*>(ed.get());
        const auto err = med->devImportClip(file, -1);
        const auto got = p.bankPattern(20);
        const int row = got ? got->lockRow(2, 1) : -1;
        check(err.isEmpty() && got && got->length == 32 && got->trigs[0] == src.trigs[0] && got->trigs[2] == src.trigs[2]
              && ((got->accent >> 4) & 1) && row >= 0 && got->locks[row][20] == 33,
              "a pattern's MIDI clip dropped back in: the same trigs, accent, length and lock (" + err + ")");
        const auto err2 = med->devImportClip(file, 9);   // onto key 10: every note on T10
        const auto got2 = p.bankPattern(20);
        check(err2.isEmpty() && got2 && got2->trigs[9] == (src.trigs[0] | src.trigs[2]) && got2->trigs[0] == src.trigs[0],
              "the clip onto key 10: all its notes on T10, the other tracks kept");
        file.deleteFile();
        // RESAMPLE: T1's sound rendered into ROM-08
        const auto rerr = p.resampleTrack(0, 7);
        check(rerr.isEmpty() && p.sampleSeconds(7) > 0.05 && p.sampleName(7).startsWith("T1 "),
              "RESAMPLE T1 > ROM-08: " + juce::String(p.sampleSeconds(7), 2) + " s, " + p.sampleName(7) + (rerr.isNotEmpty() ? " (" + rerr + ")" : juce::String()));
    }
    std::printf(fails ? "FOUR TEST FAILED (%d)\n" : "FOUR TEST OK\n", fails);
    return fails ? 1 : 0;
}

} // namespace mnm::plugin::md::test
