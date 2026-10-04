// The Machinedrum sound engine, as Monomodule MD and the library previews run it: the OS's control CPU (knob
// slewing, LFOs, the machines' control handlers, the master effects' conversion), DSP2 (the 16 voices) and DSP1 (track
// effects, routing, mixer, master effects), one 32-frame pass at a time at 44.1 kHz. JUCE-free.
// Each pass: the targets set since the last pass go to the OS tick (which slews towards them), then whatever changed
// is converted and sent to the DSPs, then the voices and the mix render.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include "MdControl.h"
#include "MdFirmware.h"
#include "MdMixEngine.h"
#include "MdVoiceEngine.h"

namespace mnm::md {

class Engine {
public:
    static constexpr int kTracks = 16, kFrames = 32;
    static constexpr double kRate = 44100.0;
    struct Track {
        int machine = 0;                      // firmware machine id
        std::array<uint8_t, 24> params{};     // 8 synthesis, 8 effects, DIST VOL PAN DEL REV, LFO SPD DEP MIX
        uint8_t level = 127;
        std::array<uint8_t, 5> lfoConfig{};   // dest track, dest param, shape 1, shape 2, type
        int route = 6;                        // 0-5 = A..F, 6 = MAIN
    };

    explicit Engine(const Firmware& fw);   // throws when a DSP cannot boot
    const Firmware& firmware() const { return m_fw; }
    ControlCpu& cpu() { return *m_cpu; }
    VoiceEngine& voices() { return *m_voices; }
    MixEngine& mixer() { return *m_mixer; }

    void setTrack(int t, const Track& track) { m_tracks[size_t(t)].target = track; }
    void setLfoState(int t, const uint8_t* lfo36) { m_cpu->setLfo(t, lfo36); }   // a kit's whole LFO struct
    void setMasterFx(const std::array<std::array<uint8_t, 8>, 4>& fx) { m_masterFx = fx; }
    void setTempo(double bpm) { m_bpm = bpm; }
    void snap() { m_snap = true; }   // the next pass jumps to the targets (a kit load) instead of slewing
    // A trig: the voice starts, its LFO restarts, and the volume law gets the trig's accent factor (VOLUME mode: the
    // velocity; ACCENT mode: 0x80, accented 0x80 + 2 x ACCENT)
    void trig(int t, int machine, int accent);   // machine: the track's machine now (the targets follow at the next pass)
    void setInput(const int32_t* lr64) { m_voices->setInput(lr64); }   // 32 stereo frames for the INP machines
    void render();
    const VoiceEngine::Block& voiceBlock() const { return m_block; }
    const MixEngine::Output& output() const { return m_out; }
    // Per-track outputs (see MixEngine::setDirect): the masked tracks leave the hardware mix; after render(),
    // trackOut(t, 0|1) is each one's 32 frames (L / R, 1.0 = full scale) after its track effects, volume and pan, with
    // the master section's gain at neutral settings and its latency (measured: x5.762, 18 frames; correlation 1.0
    // against the track alone on MAIN), so a track on its own output sounds as it did in the main mix, in time
    static constexpr int kMasterLatency = 18;
    static constexpr float kMasterGain = 5.762f;
    void setDirect(uint32_t mask);
    uint32_t direct() const { return m_direct; }
    const float* trackOut(int t, int ch) const { return m_trackOut[size_t(t)][size_t(ch)].data(); }

private:
    void refresh();
    struct TrackState {
        Track target;
        int accent = -128, sentAccent = 0, sentMachine = -1, sentRoute = -1;
        std::array<int, 8> synSent{};
        std::array<int, 14> mixSent{};
    };
    const Firmware& m_fw;
    std::unique_ptr<ControlCpu> m_cpu;
    std::unique_ptr<VoiceEngine> m_voices;
    std::unique_ptr<MixEngine> m_mixer;
    std::array<TrackState, kTracks> m_tracks{};
    std::array<std::array<uint8_t, 8>, 4> m_masterFx{};
    std::array<std::array<int, 8>, 4> m_masterSent{};
    double m_bpm = 120.0, m_tempoSent = 0.0;
    bool m_snap = true;
    uint32_t m_blockCount = 0;
    VoiceEngine::Block m_block{};
    MixEngine::Output m_out{};
    std::array<int32_t, 64> m_masterReturn{};
    uint32_t m_direct = 0;
    // per track and channel: kMasterLatency frames of history, then the block (trackOut reads from the history start)
    std::array<std::array<std::array<float, kFrames + kMasterLatency>, 2>, kTracks> m_trackOut{};
};

} // namespace mnm::md
