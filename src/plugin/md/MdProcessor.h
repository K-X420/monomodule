// Monomodule MD: the Machinedrum's 16 synthesis voices from the user's MD OS file.
//   DSP2 renders the voices (md/MdVoiceEngine), the OS's own ColdFire control handlers convert the knobs
//   (md/MdControl), and a plain stereo mixer applies level and pan (DSP1's track effects, routing and master
//   effects are not run yet).
// MIDI: the Machinedrum's trig notes (36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62 = tracks 1-16), any channel.
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
    double getTailLengthSeconds() const override { return 5.0; }
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
    void auditionTrack(int t) { m_audition[size_t(t)].store(true); }   // UI: trig as from MIDI
    float trackActivity(int t) const { return m_activity[size_t(t)].load(); }   // decays between UI polls

    juce::AudioProcessorValueTreeState apvts;

private:
    struct Track {
        std::atomic<float>* machine = nullptr;
        std::atomic<float>* knobs[8] = {};
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        int sentMachine = -1;                 // machine ID whose packet the DSP holds
        std::array<int, 8> sentKnobs{};       // knob values that packet was built from
        float gainL = 0.0f, gainR = 0.0f;
    };

    void loadEngine();
    void refreshPackets();                    // audio thread, between passes
    void runPass();                           // one 32-frame DSP2 block, mixed into the FIFO
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;

    juce::String m_firmwarePath, m_status;
    std::unique_ptr<mnm::md::Firmware> m_fw;
    std::unique_ptr<mnm::md::ControlCpu> m_cpu;
    std::unique_ptr<mnm::md::VoiceEngine> m_engine;
    juce::CriticalSection m_engineLock;
    std::atomic<bool> m_engineReady{false};

    std::array<Track, kTracks> m_tracks;
    std::atomic<float>* m_master = nullptr;
    std::array<std::atomic<bool>, kTracks> m_audition{};
    std::array<std::atomic<float>, kTracks> m_activity{};
    std::array<std::atomic<bool>, kTracks> m_machineChanged{};   // message thread: load that machine's defaults

    // engine-rate (44.1 kHz) output FIFO and the host-rate resampler
    std::vector<float> m_fifoL, m_fifoR;
    int m_fifoLen = 0;
    double m_hostRate = 44100.0;
    juce::LagrangeInterpolator m_interpL, m_interpR;
    struct PendingTrig { int track; double enginePos; };   // engine frames from the current FIFO read point
    std::vector<PendingTrig> m_pending;
    mnm::md::VoiceEngine::Block m_block{};
};

} // namespace mnm::plugin::md
