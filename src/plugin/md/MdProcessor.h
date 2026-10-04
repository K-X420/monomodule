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
#include "MdLibrary.h"
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
    int applyKit(const mnm::md::Kit& kit, const std::array<int, 16>* routes = nullptr);
    juce::String kitName() const { return m_kitName; }
    // The kit library (MdLibrary): the current sound as a kit, loading one by key, and which one is loaded
    LibraryKit captureKit() const;
    int loadLibraryKit(const juce::String& key, const LibraryKit& kit, const juce::String& name);   // returns emptied tracks
    void setLoadedKit(const juce::String& key, const juce::String& name);   // after a save: the new kit is the loaded one
    juce::String loadedKitKey() const { return m_kitKey; }
    bool kitModified() const;   // the sound differs from the loaded kit
    // LOCK: a locked track keeps its sound when a kit is loaded (kept in the plugin state)
    bool trackLocked(int t) const { return m_locked[size_t(t)].load(); }
    void setTrackLocked(int t, bool on) { m_locked[size_t(t)].store(on); }

    // UW samples (message thread): any audio file into a ROM slot (mixed to mono, kept at its own rate; the DSP
    // resamples). Kept in the plugin state. Returns an error text, empty on success.
    juce::String loadSample(int slot, const juce::File& file);
    void clearSample(int slot);
    juce::String sampleName(int slot) const { return m_samples[size_t(slot)].name; }
    double sampleSeconds(int slot) const { const auto& s = m_samples[size_t(slot)]; return s.rate > 0 ? double(s.data.size()) / s.rate : 0.0; }
    double sampleMemoryUsed() const;   // 0..1 of the UW sample memory
    void auditionTrack(int t) { m_audition[size_t(t)].store(true); }   // UI: trig as from MIDI
    float trackActivity(int t) const { return m_activity[size_t(t)].load(); }   // decays between UI polls

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kMixRaw = 14;   // AMD AMF EQF EQG FLTF FLTW FLTQ SRR, DIST, VOL, PAN, DEL, REV, LEVEL
    struct Track {
        std::atomic<float>* machine = nullptr;
        std::atomic<float>* knobs[8] = {};
        std::atomic<float>* mix[kMixRaw] = {};
        std::atomic<float>* lfo[8] = {};             // TRK PARAM SHP1 SHP2 TYPE SPD DEP MIX
        std::atomic<float>* route = nullptr;
        std::atomic<float>* mute = nullptr;
        int sentMachine = -1;
        std::array<int, 8> synSent{};                // live raw words the DSP2 packet was built from
        std::array<int, kMixRaw> mixSent{};          // and the DSP1 words
        int sentRoute = -1;
        int accent = -128;                           // the volume law's accent factor: velocity, or 0x80 (+2 x ACCENT)
        int sentAccent = 0;
    };

    void loadEngine();
    bool pushSamples();   // every slot into DSP2 (under the engine lock)
    struct Sample { juce::String name; std::vector<float> data; double rate = 44100.0; };
    std::array<Sample, mnm::md::VoiceEngine::kSlots> m_samples;
    juce::ValueTree samplesToTree() const;
    void samplesFromTree(const juce::ValueTree& t);
    void refreshParameters();   // audio thread, before each pass: slew, convert, send
    void runPass();             // one 32-frame block through DSP2 and DSP1 into the FIFO
    void handleCc(int channel, int cc, int value);
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;

    juce::String m_firmwarePath, m_status, m_kitName, m_kitKey;
    juce::var m_kitSnapshot;   // the loaded kit as captured right after loading (kitModified compares with it)
    std::unique_ptr<mnm::md::Firmware> m_fw;
    std::unique_ptr<mnm::md::ControlCpu> m_cpu;
    std::unique_ptr<mnm::md::VoiceEngine> m_voices;
    std::unique_ptr<mnm::md::MixEngine> m_mixer;
    juce::CriticalSection m_engineLock;
    std::atomic<bool> m_engineReady{false};

    std::array<Track, kTracks> m_tracks;
    std::array<std::array<std::atomic<float>*, 8>, 4> m_masterFx{};
    std::array<std::array<int, 8>, 4> m_masterSent{};
    std::atomic<bool> m_snap{true};   // the next tick jumps every knob to its target (load, state restore, kit)
    uint32_t m_blockCount = 0;
    std::array<std::array<uint8_t, 36>, kTracks> m_kitLfos{};   // a loaded kit's LFO structs, for the audio thread
    std::array<std::atomic<bool>, kTracks> m_kitLfoPending{};
    double m_tempoSent = 0.0;
    std::atomic<float>* m_master = nullptr;
    std::atomic<float>* m_velMode = nullptr;
    std::atomic<float>* m_accent = nullptr;
    std::atomic<double> m_hostBpm{120.0};
    std::array<std::atomic<bool>, kTracks> m_audition{};
    std::array<std::atomic<bool>, kTracks> m_locked{};
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
    std::array<int32_t, 64> m_masterReturn{};
    struct PendingTrig { int track; double enginePos; int velocity; };   // engine frames from the current FIFO read point
    std::vector<PendingTrig> m_pending;
    mnm::md::VoiceEngine::Block m_block{};
    mnm::md::MixEngine::Output m_out{};
};

} // namespace mnm::plugin::md
