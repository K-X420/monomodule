// Runs the Machinedrum's DSP2 program (section 1 of the OS file): the 16 machine voices.
// No peripherals are emulated (the 56303 DMA/ESSI model stalls on the MD's link setup, and nothing here needs
// them). The resident dispatcher (P:0x64..0xE7, see janne808/machinedrum-kit docs/08-dsp2-voice-abi.md) is kept and
// patched in three places:
//   P:0xBF  the ESSI frame wait at track 0 -> NOP
//   P:0xD1  the DMA0 start that sends a finished voice to DSP1 -> JSR to a copy stub: the 32 words of the bank in
//           Y:0x140 go to Y:kCapture + 32*t
//   P:0xE2  the end of the 16-track pass -> JMP to a park loop; the host sees PC = kParkPc, reads the 16 voices and
//           restarts the next pass at P:0x64
// Control packets and trigs are written straight into the track state blocks Y:0x800 + 0x40*t (what the ColdFire's
// HI08 transfers do on the hardware).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include "MdFirmware.h"

namespace dsp56k { class DSP; class Memory; class PeripheralsNop; class DefaultMemoryValidator; }

namespace mnm::md {

class VoiceEngine {
public:
    static constexpr int kTracks = 16;
    static constexpr int kBlockFrames = 32;
    using Block = std::array<std::array<int32_t, kBlockFrames>, kTracks>;   // 24-bit signed

    explicit VoiceEngine(const Firmware& fw);
    ~VoiceEngine();

    void reset();   // reload, boot to the first pass; throws on failure
    // Control words for Y:S+1.. (packet[1..n-1] from ControlCpu::convert)
    void setPacket(int track, const uint32_t* words, int count);
    void trig(int track, int dspType);   // takes effect at the next pass (init on a type change, then update)
    // Audio input for the INP machines: 32 stereo frames (L/R interleaved, 24-bit) for the next pass. Each INP
    // voice reads 32 frames per block from the ADC ring X:0x100..0x1FF at its own pointer (+0x40 per block), so
    // the block goes into all four ring slots.
    void setInput(const int32_t* lr64);

    // UW sample slots (ROM-01..32 = slots 0..31, ROM-33..48 = 48..63; RAM-R/P use 32..40). The OS's loader layout:
    // 12-bit codes, two per word (first sample in the high 12 bits), in sample memory 0x150000..0x1FFA00; the player
    // decodes a code through the companding table at Y:0x146000 (4096 entries, code 2048 = 0). The slot record at
    // 0x147E00 + 4 slot is { base, length (samples), loop start (-1 = none), (0x16250000 / period in ns) << 4 }, and a ROM slot
    // only runs the player while it holds a sample (its dispatch entries are switched like the OS does).
    static constexpr int kSlots = 64;
    static constexpr uint32_t kSampleCapacity = (0x1FFA00 - 0x150000) * 2;   // 12-bit samples (~32 s at 44.1 kHz)
    // Replaces every slot's sample (index = slot; empty = no sample). Returns false when they do not fit.
    // samples: -1..1 floats per slot, encoded to the nearest code of the DSP's own table.
    bool setSamples(const std::array<std::vector<float>, kSlots>& samples, const std::array<double, kSlots>& rates,
                    const std::array<int, kSlots>& loopStarts);
    bool renderPass(Block& out);         // one 32-sample block for all 16 tracks
    bool faulted() const { return m_faulted; }
    const std::string& faultReason() const { return m_fault; }
    uint64_t lastPassInstructions() const { return m_lastInstr; }
    uint32_t peek(int space, uint32_t addr) const;   // debug: 0 = P, 1 = X, 2 = Y

private:
    bool runToPark(uint64_t maxExec);
    void installPatches();
    // The latest packet of each track, rewritten before every pass as the OS keeps refreshing it: some machines'
    // init routines clear packet words (P-I-ML clears HARD at Y:S+3) that the next refresh restores.
    std::array<std::array<uint32_t, 0x40>, kTracks> m_packet{};
    std::array<int, kTracks> m_packetLen{};

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
