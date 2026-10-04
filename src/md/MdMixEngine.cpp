#include "MdMixEngine.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/jit.h"

namespace mnm::md {

using namespace dsp56k;

namespace {
constexpr TWord kBridge = 0x100000;
constexpr TWord kMemSize = 0x180000;   // delay lines reach 0x117C15 + 0x17AFC
constexpr TWord kBlockStart = 0x44;    // after the DAC-half poll: input X:0x100, output X:0x400
constexpr TWord kParkPc = 0xF40;       // free internal P (the image uses P:0..0xA07)
constexpr TWord kVoiceRing = 0x600;
constexpr TWord kDacHalf = 0x400;
constexpr uint64_t kMaxExecBoot = 20'000'000;
constexpr uint64_t kMaxExecBlock = 4'000'000;
}

MixEngine::MixEngine(const Firmware& fw) : m_fw(fw)
{
    m_validator = std::make_unique<DefaultMemoryValidator>();
    m_mem = std::make_unique<Memory>(*m_validator, kMemSize, kMemSize, kBridge);
    if (!m_mem->hasMmuSupport()) throw std::runtime_error("dsp56300: MMU-backed memory allocation failed");
    m_periphX = std::make_unique<PeripheralsNop>();
    m_periphY = std::make_unique<PeripheralsNop>();
    m_dsp = std::make_unique<DSP>(*m_mem, m_periphX.get(), m_periphY.get());
    auto cfg = m_dsp->getJit().getConfig();
    cfg.dynamicFastInterrupts = true;
    cfg.interruptRegionIsCode = true;
    m_dsp->getJit().setConfig(cfg);
    reset();
}

MixEngine::~MixEngine() = default;

void MixEngine::reset()
{
    m_faulted = false; m_fault.clear();
    m_dsp->resetHW();
    for (const auto& r : m_fw.mixDsp.records)
        for (size_t k = 0; k < r.words.size(); ++k) {
            const TWord a = r.addr + TWord(k);
            if (r.space == fw::Space::P || a >= kBridge) m_dsp->memWriteP(a, r.words[k]);
            else m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
        }
    m_dsp->memWriteP(kParkPc, 0x0C0000 | kParkPc);   // park: jmp <itself
    m_dsp->memWriteP(0x39, 0x000000);                 // boot ADC wait 'bne $36' -> nop
    m_dsp->memWriteP(0x79, 0x000000);                 // voice wait 'blt $73' -> nop
    m_dsp->memWriteP(0x9A8, 0x0C0000 | kParkPc);      // end of block 'bra $3c' -> jmp <park
    m_dsp->setPC(m_fw.mixDsp.startAddr.value_or(0x24));
    if (!runToPark(kMaxExecBoot)) throw std::runtime_error("MD DSP1 boot did not finish: " + m_fault);
}

bool MixEngine::runToPark(uint64_t maxExec)
{
    const uint64_t start = m_dsp->getInstructionCounter();
    for (uint64_t n = 0; n < maxExec; ++n) {
        m_dsp->exec();
        if (m_dsp->getJit().hasFailed()) { m_faulted = true; m_fault = "DSP JIT failed: " + m_dsp->getJit().failReason(); return false; }
        if (m_dsp->getPC().toWord() == kParkPc) { m_lastInstr = m_dsp->getInstructionCounter() - start; return true; }
    }
    std::ostringstream o; o << "block did not finish (PC=$" << std::hex << m_dsp->getPC().toWord() << ")";
    m_faulted = true; m_fault = o.str();
    return false;
}

void MixEngine::setY(uint32_t addr, const uint32_t* words, int count)
{
    for (int k = 0; k < count; ++k) m_dsp->memWrite(MemArea_Y, addr + TWord(k), words[k] & 0xFFFFFF);
}

std::array<uint32_t, 5> MixEngine::routingWords(uint32_t level, uint32_t vol, uint32_t pan, uint32_t reverbSend, uint32_t delaySend, int route, int accent)
{
    const int64_t L = level & 0xFFFF, V = vol & 0xFFFF;
    int64_t volume = ((L * L) >> 8) * accent >> 17;
    volume *= (V * V) >> 17;
    const uint32_t panWord = ((pan & 0xFFFF) << 9) & 0x1FFFE00;
    const uint32_t rev = uint32_t((int64_t(reverbSend & 0xFFFF) * (reverbSend & 0xFFFF)) >> 5);
    const uint32_t del = uint32_t((int64_t(delaySend & 0xFFFF) * (delaySend & 0xFFFF)) >> 5);
    return {uint32_t(route), uint32_t(volume) & 0xFFFFFF, panWord & 0xFFFFFF, rev & 0xFFFFFF, del & 0xFFFFFF};
}

void MixEngine::setTrackFx(int track, const std::array<uint16_t, 9>& raw)
{
    std::array<uint32_t, 9> w{};
    for (int k = 0; k < 9; ++k) w[size_t(k)] = raw[size_t(k)];
    setY(0x200 + 0x40 * uint32_t(track), w.data(), 9);
}

void MixEngine::masterReturn(int32_t* lr64) const
{
    for (TWord k = 0; k < 64; ++k) lr64[k] = int32_t(m_mem->get(MemArea_X, 0x688 + k) << 8) >> 8;
}

bool MixEngine::renderBlock(const VoiceEngine::Block& voices, Output& out)
{
    if (m_faulted) { for (auto& f : out) f.fill(0); return false; }
    for (int t = 0; t < VoiceEngine::kTracks; ++t)
        for (int i = 0; i < kFrames; ++i)
            m_dsp->memWrite(MemArea_Y, kVoiceRing + TWord(t * kFrames + i), TWord(voices[size_t(t)][size_t(i)]) & 0xFFFFFF);
    m_dsp->setPC(kBlockStart);
    if (!runToPark(kMaxExecBlock)) { for (auto& f : out) f.fill(0); return false; }
    for (int i = 0; i < kFrames; ++i)
        for (int c = 0; c < kChannels; ++c) {
            const TWord w = m_mem->get(MemArea_X, kDacHalf + TWord(i * kChannels + c)) & 0xFFFFFF;
            out[size_t(i)][size_t(c)] = int32_t(w << 8) >> 8;
        }
    return true;
}

} // namespace mnm::md
