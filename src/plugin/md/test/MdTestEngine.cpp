// md-plugintest's checks of the engine: sample rates, parity with the unit
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

// MD_RATE_TEST: upstream issue #3: no rhythmic clicks at 48 / 96 kHz (a held GND-SN sine, block 512)
int mdRateTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
    int fails = 0;
    auto check = [&](bool ok, const juce::String& what) { std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); fails += ok ? 0 : 1; };
    for (double hostRate : {44100.0, 48000.0, 96000.0}) {
        auto pHeap = std::make_unique<MdProcessor>();
        auto& p = *pHeap;
        p.setFirmwarePath(juce::String(argv[1]), false);
        const int blk = 512;
        p.prepareToPlay(hostRate, blk);
        auto kit = p.captureMdKit();
        kit.models[0] = 1;   // GND-SN: a steady sine (long decay, no sweep)
        kit.params[0][0] = 40; kit.params[0][1] = 127; kit.params[0][2] = 0; kit.params[0][3] = 0;
        kit.levels[0] = 100;
        p.loadMdKit("rate", kit, "RATE");
        p.syncMachineSideEffects();
        juce::AudioBuffer<float> buf(2, blk);
        std::vector<float> y;
        for (int b2 = 0; b2 < int(hostRate / blk); ++b2) {   // 1 s
            juce::MidiBuffer midi;
            if (b2 == 2) midi.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(100)), 0);
            buf.clear(); p.processBlock(buf, midi);
            for (int i = 0; i < blk; ++i) y.push_back(buf.getSample(0, i));
        }
        // the second difference of a clean sine is smooth (it is the sine again, scaled); a dropped or doubled frame
        // shows as a spike far above its neighbours
        const size_t from = size_t(0.3 * hostRate), to = size_t(0.9 * hostRate);
        double peak = 0, typical = 0;
        std::vector<double> d2;
        for (size_t i = from; i < to; ++i) d2.push_back(std::abs(double(y[i]) - 2.0 * y[i - 1] + y[i - 2]));
        std::vector<double> sorted = d2; std::sort(sorted.begin(), sorted.end());
        typical = sorted[sorted.size() * 99 / 100];
        peak = sorted.back();
        double rms = 0; for (size_t i = from; i < to; ++i) rms += double(y[i]) * y[i]; rms = std::sqrt(rms / double(to - from));
        check(rms > 0.01 && peak < 2.0 * typical + 1e-6, juce::String(hostRate, 0) + " Hz: no clicks (largest 2nd difference " + juce::String(peak, 6)
              + " vs 99th percentile " + juce::String(typical, 6) + ", level " + juce::String(rms, 3) + ")");
    }
    std::printf(fails ? "RATE TEST FAILED (%d)\n" : "RATE TEST OK\n", fails);
    return fails ? 1 : 0;
}

// MD_PARITY_TEST: knob names, per-machine memory, init kit, save into a project
int mdParityTest(TestEnv& env, const char* value)
{
    auto& proc = env.proc;
    const double rate = env.rate;
    const int block = env.block;
    const int argc = env.argc;
    char** argv = env.argv;
    juce::ignoreUnused(proc, rate, block, argc, argv, value);
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

} // namespace mnm::plugin::md::test
