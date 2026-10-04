// Monomodule MD: the Machinedrum's sound engine from the user's MD OS file.
//   DSP2 renders the 16 voices (md/MdVoiceEngine), DSP1 runs the track effects, routing, mixer and master effects
//   (md/MdMixEngine); the OS's own ColdFire code converts the synthesis knobs and the master effects (md/MdControl),
//   and its routing law converts level, volume, pan and the sends. Knobs are slewed as raw words, as the OS does.
// MIDI (any channel for trigs; base channel 1 for CCs, as the hardware's default map):
//   trigs  notes 36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62 = tracks 1-16
//   CCs    channel 1 + t/4: [16, 40, 72, 96][t%4] + 0..7 synthesis, +8..15 effects, +16..20 DIST VOL PAN DEL REV;
//          CC 8 + t%4 = level
// Outputs: main = the hardware's A/B (MAIN and A, B routes), optional C/D and E/F buses.
// As on the hardware, a new machine starts playing at its track's next trig.
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MdParams.h"
#include "MdControl.h"
#include "MdFirmware.h"
#include "MdKit.h"
#include "MdMixEngine.h"
#include "MdVoiceEngine.h"

namespace mnm::plugin::md {

class MdProcessor : public juce::AudioProcessor,
                    private juce::AudioProcessorValueTreeState::Listener,
                    private juce::AsyncUpdater {
public:
    MdProcessor();
    ~MdProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Monomodule MD"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // OS file (message thread). The path is shared by every Monomodule MD instance.
    juce::String firmwarePath() const { return m_firmwarePath; }
    void setFirmwarePath(const juce::String& path, bool persist = true);
    bool engineReady() const { return m_engineReady.load(); }
    juce::String statusText() const { return m_status; }
    const mnm::md::Machine* machineInfo(int id) const { return m_fw ? m_fw->byId(id) : nullptr; }

    int machineIdOf(int t) const;
    // Kit loading (message thread): every track's machine and knobs, levels and the master effects. Machines the
    // plugin does not run (ROM/RAM, MIDI, controller, input) become GND---. Returns how many tracks were emptied.
    int applyKit(const mnm::md::Kit& kit);
    juce::String kitName() const { return m_kitName; }
    void auditionTrack(int t) { m_audition[size_t(t)].store(true); }   // UI: trig as from MIDI
    float trackActivity(int t) const { return m_activity[size_t(t)].load(); }   // decays between UI polls

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kMixRaw = 14;   // AMD AMF EQF EQG FLTF FLTW FLTQ SRR, DIST, VOL, PAN, DEL, REV, LEVEL
    struct Track {
        std::atomic<float>* machine = nullptr;
        std::atomic<float>* knobs[8] = {};
        std::atomic<float>* mix[kMixRaw] = {};
        std::atomic<float>* route = nullptr;
        int sentMachine = -1;
        std::array<int, 8> synRaw{}, synSent{};      // slewed raw words and the ones the packet was built from
        std::array<int, kMixRaw> mixRaw{}, mixSent{};
        int sentRoute = -1;
        bool snap = true;                            // jump to the targets (load, state restore)
    };

    void loadEngine();
    void refreshParameters();   // audio thread, before each pass: slew, convert, send
    void runPass();             // one 32-frame block through DSP2 and DSP1 into the FIFO
    void handleCc(int channel, int cc, int value);
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;

    juce::String m_firmwarePath, m_status, m_kitName;
    std::unique_ptr<mnm::md::Firmware> m_fw;
    std::unique_ptr<mnm::md::ControlCpu> m_cpu;
    std::unique_ptr<mnm::md::VoiceEngine> m_voices;
    std::unique_ptr<mnm::md::MixEngine> m_mixer;
    juce::CriticalSection m_engineLock;
    std::atomic<bool> m_engineReady{false};

    std::array<Track, kTracks> m_tracks;
    std::array<std::array<std::atomic<float>*, 8>, 4> m_masterFx{};
    std::array<std::array<int, 8>, 4> m_masterSent{};
    bool m_masterSnap = true;
    double m_tempoSent = 0.0;
    std::atomic<float>* m_master = nullptr;
    std::atomic<double> m_hostBpm{120.0};
    std::array<std::atomic<bool>, kTracks> m_audition{};
    std::array<std::atomic<float>, kTracks> m_activity{};
    std::array<std::atomic<bool>, kTracks> m_machineChanged{};   // message thread: load that machine's defaults

    // engine-rate (44.1 kHz) output FIFO: the six DAC channels, and their host-rate resamplers
    static constexpr int kDac = mnm::md::MixEngine::kChannels;
    std::array<std::vector<float>, kDac> m_fifo;
    int m_fifoLen = 0;
    double m_hostRate = 44100.0;
    std::array<juce::LagrangeInterpolator, kDac> m_interp;
    // side-chain input for the INP machines, resampled to the engine rate
    std::array<std::vector<float>, 2> m_inFifo;
    int m_inLen = 0;
    std::array<juce::LagrangeInterpolator, 2> m_inInterp;
    std::array<int32_t, 64> m_inBlock{};
    struct PendingTrig { int track; double enginePos; };   // engine frames from the current FIFO read point
    std::vector<PendingTrig> m_pending;
    mnm::md::VoiceEngine::Block m_block{};
    mnm::md::MixEngine::Output m_out{};
};

} // namespace mnm::plugin::md
