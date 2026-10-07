// md-plugintest's checks of the EXTRAS: trig conditions, micro-timing and retrigs, from the pattern codec through the
// sequencer and the processor to GRID's edit windows
#include <cstdio>
#include <cstring>
#include <map>
#include "MdProcessor.h"
#include "MdEditor.h"
#include "MdSequencer.h"
#include "MdTests.h"

namespace mnm::plugin::md::test {

namespace {
mnm::mddump::Pattern blankPattern(int position, int length)
{
    mnm::mddump::Pattern p;
    p.position = position; p.length = uint8_t(length); p.accentEditAll = p.slideEditAll = p.swingEditAll = 1;
    for (auto& row : p.locks) std::memset(row, 0xFF, sizeof(row));
    return p;
}
}

// MD_XTRA_TEST
int mdXtraTest(TestEnv& env, const char* value)
{
    juce::ignoreUnused(value);
    namespace dd = mnm::mddump;
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };

    // ---- the codec: the extras ride in a message of their own; the MD's own message stays the same bytes
    {
        auto x = blankPattern(5, 16);
        x.trigs[0] = 0x1111; x.trigs[1] = 0x4; x.trigs[2] = 0x1;
        x.cond[0][4] = 11; x.micro[1][2] = -6; x.retrig[2][0] = dd::makeRetrig(3, 2);
        auto plain = x;
        for (int t = 0; t < 16; ++t) for (int s = 0; s < 64; ++s) plain.clearStepExtras(t, s);
        check(dd::encodePattern(x) == dd::encodePattern(plain) && !plain.hasExtras() && x.hasExtras(), "the pattern's own message carries no extras");
        auto bytes = dd::encodePattern(x);
        const auto ex = dd::encodePatternExtras(x);
        bytes.insert(bytes.end(), ex.begin(), ex.end());
        const auto d = dd::parseDump(bytes.data(), bytes.size(), "x");
        const bool back = d.patterns.size() == 1 && d.patterns[0].cond[0][4] == 11 && d.patterns[0].micro[1][2] == -6 && d.patterns[0].retrig[2][0] == dd::makeRetrig(3, 2);
        check(back && d.numUnknown == 0, "a dump with extras parses back onto its pattern (unknown messages: " + juce::String(d.numUnknown) + ")");
        check(dd::encodeDump(d) == bytes, "the dump re-encodes byte for byte");
        check(dd::encodeDump(d, false) == dd::encodePattern(x), "exported (pure MD): the extras left out");
        check(dd::encodePatternExtras(plain).empty(), "no extras: no message");
        check(dd::conditionName(11) == "50%" && dd::conditionName(dd::kCondFill) == "FILL" && dd::conditionName(dd::kCondRatio) == "1:2"
              && dd::conditionName(dd::kConditions - 1) == "8:8" && dd::retrigName(dd::makeRetrig(3, 2)) == "1/64x2",
              "names: 50%, FILL, 1:2, 8:8, 1/64x2");
        auto c = blankPattern(0, 16);
        c.copySteps(x, 0, 0, 16, -1, -1);
        check(c.cond[0][4] == 11 && c.micro[1][2] == -6, "copied steps take their extras");
        c.clearSteps(0, 16, -1);
        check(!c.hasExtras(), "cleared steps lose them");
    }

    // ---- the sequencer: conditions, micro-timing, retrigs (4 steps of 6 clocks a pass)
    {
        auto x = blankPattern(0, 4);
        x.trigs[0] = 1; x.cond[0][0] = dd::kCondRatio;                         // 1:2
        x.trigs[1] = 1 | 4; x.cond[1][0] = 11; x.cond[1][2] = dd::kCondPre;     // 50%, then PRE
        x.trigs[2] = 1; x.cond[2][0] = dd::kCondNei;                            // NEI (track 2's step 0)
        x.trigs[3] = 1; x.cond[3][0] = dd::kCondFill;
        x.trigs[4] = 2; x.micro[4][1] = 12;                                     // half a step late
        x.trigs[5] = 4; x.retrig[5][2] = dd::makeRetrig(3, 1);                  // 4 hits
        x.trigs[6] = 1; x.cond[6][0] = dd::kCondFirst;
        x.trigs[7] = 1; x.cond[7][0] = 11;                                      // 50% again: its own dice
        const mnm::md::PatternPlayer pl(x);
        const int passes = 200;
        auto play = [&](bool extras, bool fill) {
            std::vector<mnm::md::SeqTrig> out;
            mnm::md::TrigContext ctx; ctx.extras = extras; ctx.fill = fill; ctx.seed = 1234;
            pl.trigs(0, 24.0 * passes, out, -1, ctx);
            return out;
        };
        const auto on = play(true, false), off = play(false, false), filled = play(true, true);
        std::map<int, int> n;
        std::vector<std::array<bool, 3>> pass(passes, {false, false, false});   // T2 step 0, T2 step 2, T3
        int same = 0;
        bool microOk = false, retrigOk = true;
        int hits = 0;
        for (const auto& e : on) {
            ++n[e.track];
            const int64_t ps = e.stepIndex / 4;
            if (e.track == 1 && e.step == 0) pass[size_t(ps)][0] = true;
            if (e.track == 1 && e.step == 2) pass[size_t(ps)][1] = true;
            if (e.track == 2) pass[size_t(ps)][2] = true;
            if (e.track == 4 && e.stepIndex == 1) microOk = std::abs(e.clock - 9.0) < 1e-9;
            if (e.track == 5 && e.stepIndex == 2) { retrigOk = retrigOk && std::abs(e.clock - (12.0 + 1.5 * hits)) < 1e-9 && e.retrig == (hits > 0); ++hits; }
            if (e.track == 7 && e.step == 0) same += pass[size_t(ps)][0] ? 1 : 0;
        }
        int preSame = 0, neiSame = 0;
        for (const auto& q : pass) { preSame += q[0] == q[1]; neiSame += q[0] == q[2]; }
        check(n[0] == passes / 2, "1:2: every other pass (" + juce::String(n[0]) + " of " + juce::String(passes) + ")");
        check(n[1] > 0 && pass.size() == size_t(passes) && std::abs(n[1] / 2 - passes / 2) < passes / 6, "50%: about half (" + juce::String(n[1] / 2) + ")");
        check(preSame == passes, "PRE plays exactly when the track's 50% trig did");
        check(neiSame == passes, "NEI plays exactly when the track before's did");
        check(n[3] == 0 && std::count_if(filled.begin(), filled.end(), [](const auto& e) { return e.track == 3; }) == passes, "FILL: only in fill mode");
        check(n[6] == 1, "1ST: the first pass only");
        check(same > 0 && same < n[7], "two 50% trigs roll their own dice");
        check(microOk, "micro +12/24: half a step late (clock 9)");
        check(hits == 4 && retrigOk, "retrig 1/64: 4 hits 1.5 clocks apart, the later ones marked (" + juce::String(hits) + ")");
        check(n[5] == passes * 4, "every pass's retrigs (" + juce::String(n[5]) + ")");
        std::map<int, int> m;
        for (const auto& e : off) ++m[e.track];
        bool offOk = true;
        for (int t = 0; t < 8; ++t) offOk = offOk && m[t] == passes * (t == 1 ? 2 : 1);
        check(offOk, "EXTRAS off: every trig plays as written, once");
        const auto again = play(true, false);
        check(again.size() == on.size(), "the same seed plays the same way");
    }

    // ---- the processor: XTRA switches them; the state keeps them
    {
        struct Head : juce::AudioPlayHead {
            double ppq = 0; bool playing = true;
            juce::Optional<PositionInfo> getPosition() const override { PositionInfo q; q.setPpqPosition(ppq); q.setBpm(120); q.setIsPlaying(playing); return q; }
        } head;
        dd::Dump d;
        dd::Kit kit;
        for (int tr = 0; tr < 16; ++tr) { kit.trigGroups[tr] = 127; kit.muteGroups[tr] = 127; kit.levels[tr] = 100; kit.params[tr][17] = 100; }
        kit.models[0] = 1; kit.params[0][0] = 64; kit.params[0][1] = 30;
        d.kits.push_back(kit);
        auto x = blankPattern(0, 16);
        x.trigs[0] = 1;
        x.retrig[0][0] = dd::makeRetrig(3, 1);   // 4 hits on step 1
        d.patterns.push_back(x);
        auto pHeap = std::make_unique<MdProcessor>();
        auto& p = *pHeap;
        p.setFirmwarePath(juce::String(env.argv[1]), false);
        p.setPlayHead(&head);
        p.prepareToPlay(env.rate, env.block);
        p.loadMdKit("xtra", d.kits[0], "XTRA");
        p.setPatternBank("test", "XTRA", d, 0);
        auto setParam = [&](const juce::String& id, float v) { if (auto* q = p.apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(v)); };
        setParam(seqId(), 1.0f);
        setParam(patternId(), 0.0f);
        const double spc = env.rate * 60.0 / 120.0 / 24.0;
        juce::AudioBuffer<float> buf(2, env.block);
        auto blocks = [&](int k) { for (int i = 0; i < k; ++i) { juce::MidiBuffer midi; buf.clear(); p.processBlock(buf, midi); head.ppq += env.block / spc / 24.0; } };
        auto onePass = [&] {   // a pass counted from half a pass in (the engine renders a little ahead of the log)
            blocks(int(48 * spc / env.block));
            p.setTrigLogging(true);
            blocks(int(96 * spc / env.block));
            int c = 0;
            for (const auto& e : p.trigLog()) c += e.track == 0;
            return c;
        };
        check(!p.extrasOn(), "EXTRAS is off at first");
        const int offHits = onePass();
        setParam(extrasId(), 1.0f);
        const int onHits = onePass();
        check(offHits == 1 && onHits == 4, "the retrig plays only with EXTRAS on (" + juce::String(offHits) + ", " + juce::String(onHits) + " hits)");
        juce::MemoryBlock state;
        p.getStateInformation(state);
        auto qHeap = std::make_unique<MdProcessor>();
        qHeap->setStateInformation(state.getData(), int(state.getSize()));
        const auto back = qHeap->bankPattern(0);
        check(back && back->retrig[0][0] == dd::makeRetrig(3, 1) && qHeap->extrasOn(), "the state keeps the extras and the switch");
        const auto bank = p.bankDump();
        check(!bank.patterns.empty() && bank.patterns[0].retrig[0][0] == dd::makeRetrig(3, 1), "the bank as the library gets it keeps them");

        // ---- GRID: C T R open the windows only with EXTRAS on; a click sets the last value, the wheel changes it
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        auto* med = dynamic_cast<MdEditor*>(ed.get());
        med->showGrid(-1);
        setParam(extrasId(), 0.0f);
        med->keyPressed(juce::KeyPress('c'));
        const int offMode = med->devMarkModeNow();
        setParam(extrasId(), 1.0f);
        med->keyPressed(juce::KeyPress('c'));
        const int condMode = med->devMarkModeNow();
        check(offMode == 0 && condMode == 4, "C: the CONDITION window, only with EXTRAS on (" + juce::String(offMode) + ", " + juce::String(condMode) + ")");
        med->devExtraPaint(0, 0, true, true);
        const int c1 = p.bankPattern(0)->cond[0][0];
        med->devExtraWheel(0, 0, 1, false);
        const int c2 = p.bankPattern(0)->cond[0][0];
        med->devExtraPaint(3, 0, true, true);   // no trig there
        const int c3 = p.bankPattern(0)->cond[0][3];
        check(c1 == 11 && c2 == 12 && c3 == 0, "click: 50%; wheel up: 59%; a step without a trig: nothing (" + juce::String(c1) + " " + juce::String(c2) + " " + juce::String(c3) + ")");
        med->devExtraWheel(0, 2, 1, true);   // RETRIG, Shift: 2 steps long
        check(dd::retrigSteps(p.bankPattern(0)->retrig[0][0]) == 2, "Shift+wheel: the retrig's length");
        med->devUndo();
        check(dd::retrigSteps(p.bankPattern(0)->retrig[0][0]) == 1, "undo");
        med->devStep(0);   // the trig off: its extras go with it
        check(!p.bankPattern(0)->hasExtras(), "a trig taken off takes its extras");
        med->keyPressed(juce::KeyPress('c'));
        setParam(extrasId(), 0.0f);
        med->keyPressed(juce::KeyPress('t'));
        check(med->devMarkModeNow() != 5, "EXTRAS off: T does nothing");
    }
    std::printf(fails ? "XTRA TEST FAILED (%d)\n" : "XTRA TEST OK\n", fails);
    return fails ? 1 : 0;
}

} // namespace mnm::plugin::md::test
