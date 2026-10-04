// Runs the Machinedrum's DSP1 program (section 2 of the OS file): per-track effects (amplitude modulation, EQ,
// filter, sample-rate reduction, distortion), routing, the mixer and the master delay, reverb, EQ and dynamics.
// As for DSP2 no peripherals are emulated; the block scheduler (janne808/machinedrum-kit docs/09-dsp1-audio-path.md)
// is patched:
//   P:0x39  boot wait for the ADC DMA -> NOP
//   P:0x79  the wait for each voice block from DSP2 -> NOP (the host puts all 16 at Y:0x600.. before the block)
//   P:0x9A8 'bra 0x3C' at the end of a block -> JMP to a park loop; the host restarts at P:0x44 (DAC half X:0x400)
// Output: 32 frames of the six-channel DAC stream (X:0x400..0x4BF, 6 words per frame; MAIN = offsets 2 and 5).
// Parameters are Y words written by the host (what the ColdFire's HI08 transfers do):
//   Y:0x200+0x40*t +0..8  AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST (raw knob words, value << 7)
//   Y:0x100+5*t   +0..4   route (6 = MAIN), volume, pan, reverb send, delay send
//   Y:0x150 delay (9 words), Y:0x170 EQ (10), Y:0x17A dynamics (11), Y:0x185 reverb (8)
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include "MdFirmware.h"
#include "MdVoiceEngine.h"

namespace dsp56k { class DSP; class Memory; class PeripheralsNop; class DefaultMemoryValidator; }

namespace mnm::md {

class MixEngine {
public:
    static constexpr int kFrames = 32;
    static constexpr int kChannels = 6;
    using Output = std::array<std::array<int32_t, kChannels>, kFrames>;   // 24-bit signed, DAC frame order
    static constexpr int kMainLeft = 2, kMainRight = 5;

    explicit MixEngine(const Firmware& fw);
    ~MixEngine();

    void reset();
    void setY(uint32_t addr, const uint32_t* words, int count);

    // The ColdFire's routing conversion (MainOS 0x20B252..0x20B2FE) from raw knob words (value << 7, slewed).
    // accent = 127 unaccented.
    //   volume = ((L^2 >> 8) * accent >> 17) * (V^2 >> 17)   L = kit level, V = VOL
    //   pan    = PAN << 9,   sends = S^2 >> 5
    static std::array<uint32_t, 5> routingWords(uint32_t level, uint32_t vol, uint32_t pan, uint32_t reverbSend, uint32_t delaySend, int route = 6, int accent = 127);
    void setTrackFx(int track, const std::array<uint16_t, 9>& raw);   // AMD AMF EQF EQG FLTF FLTW FLTQ SRR DIST
    void setRouting(int track, const std::array<uint32_t, 5>& words) { setY(0x100 + 5 * uint32_t(track), words.data(), 5); }
    bool renderBlock(const VoiceEngine::Block& voices, Output& out);
    bool faulted() const { return m_faulted; }
    const std::string& faultReason() const { return m_fault; }
    uint64_t lastBlockInstructions() const { return m_lastInstr; }

private:
    bool runToPark(uint64_t maxExec);

    const Firmware& m_fw;
    std::unique_ptr<dsp56k::DefaultMemoryValidator> m_validator;
    std::unique_ptr<dsp56k::Memory> m_mem;
    std::unique_ptr<dsp56k::PeripheralsNop> m_periphX, m_periphY;
    std::unique_ptr<dsp56k::DSP> m_dsp;
    bool m_faulted = false;
    std::string m_fault;
    uint64_t m_lastInstr = 0;
};

} // namespace mnm::md
