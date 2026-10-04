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
    bool renderPass(Block& out);         // one 32-sample block for all 16 tracks
    bool faulted() const { return m_faulted; }
    const std::string& faultReason() const { return m_fault; }
    uint64_t lastPassInstructions() const { return m_lastInstr; }

private:
    bool runToPark(uint64_t maxExec);
    void installPatches();

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
