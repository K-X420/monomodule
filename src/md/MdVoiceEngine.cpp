#include "MdVoiceEngine.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/jit.h"

namespace mnm::md {

using namespace dsp56k;

namespace {
constexpr TWord kBridge = 0x100000;
constexpr TWord kMemSize = 0x180000;
constexpr TWord kMainLoop = 0x64;
constexpr TWord kCopyStub = 0xF00;      // free internal P (the image uses P:0..0x3E1)
constexpr TWord kParkPc = 0xF40;
constexpr TWord kCapture = 0x170000;    // Y: 16 x 32 words, above the sine table and sample memory
constexpr uint64_t kMaxExecBoot = 20'000'000;
constexpr uint64_t kMaxExecPass = 2'000'000;
TWord trackBase(int t) { return 0x800 + 0x40 * TWord(t); }
}

VoiceEngine::VoiceEngine(const Firmware& fw) : m_fw(fw)
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

VoiceEngine::~VoiceEngine() = default;

void VoiceEngine::installPatches()
{
    Assembler as;
    TWord pc = kCopyStub;
    auto emit = [&](const char* text) {
        const auto r = as.assemble(text);
        if (!r.success()) throw std::runtime_error(std::string("MD harness: cannot assemble '") + text + "'");
        m_dsp->memWriteP(pc++, r.word[0]);
        if (r.wordCount > 1) m_dsp->memWriteP(pc++, r.word[1]);
    };
    // copy stub: Y:(Y:0x140) -> Y:kCapture + 32*Y:0x142
    emit("move y:>$140,r0");
    emit("move #>$ffffff,m0");
    emit("move y:>$142,b");
    emit("asl #5,b,b");
    emit("move #>$170000,x0");
    emit("add x0,b");
    emit("move b1,r1");
    emit("move #>$ffffff,m1");
    {
        std::ostringstream o; o << "do #32,$" << std::hex << (pc + 3);   // LA = last body word: two 1-word moves at pc+2, pc+3
        emit(o.str().c_str());
        emit("move y:(r0)+,x0");
        emit("move x0,y:(r1)+");
    }
    emit("rts");
    if (pc > kParkPc) throw std::runtime_error("MD harness: copy stub overflow");
    // park: spin until the host restarts the pass
    m_dsp->memWriteP(kParkPc, 0x0C0000 | kParkPc);   // jmp <kParkPc (short jump to itself)

    m_dsp->memWriteP(0xBF, 0x000000);                 // frame wait 'beq $bb' -> nop
    m_dsp->memWriteP(0xD1, 0x0BF080);                 // 'movep y:>$140,x:<<$ffffef' -> jsr >kCopyStub
    m_dsp->memWriteP(0xD2, kCopyStub);
    m_dsp->memWriteP(0xD3, 0x000000);                 // 'movep #>$8e5a51,x:<<$ffffec' (DMA0 start) -> nop nop
    m_dsp->memWriteP(0xD4, 0x000000);
    m_dsp->memWriteP(0xE2, 0x0C0000 | kParkPc);       // 'movep x:<<$ffffea,a' -> jmp <kParkPc
}

void VoiceEngine::reset()
{
    m_faulted = false; m_fault.clear();
    m_dsp->resetHW();
    for (const auto& r : m_fw.voiceDsp.records)
        for (size_t k = 0; k < r.words.size(); ++k) {
            const TWord a = r.addr + TWord(k);
            if (r.space == fw::Space::P || a >= kBridge) m_dsp->memWriteP(a, r.words[k]);
            else m_dsp->memWrite(r.space == fw::Space::X ? MemArea_X : MemArea_Y, a, r.words[k]);
        }
    installPatches();
    m_dsp->setPC(m_fw.voiceDsp.startAddr.value_or(0x24));
    if (!runToPark(kMaxExecBoot)) throw std::runtime_error("MD DSP2 boot did not reach the main loop: " + m_fault);
}

bool VoiceEngine::runToPark(uint64_t maxExec)
{
    const uint64_t start = m_dsp->getInstructionCounter();
    for (uint64_t n = 0; n < maxExec; ++n) {
        m_dsp->exec();
        if (m_dsp->getJit().hasFailed()) { m_faulted = true; m_fault = "DSP JIT failed: " + m_dsp->getJit().failReason(); return false; }
        if (m_dsp->getPC().toWord() == kParkPc) { m_lastInstr = m_dsp->getInstructionCounter() - start; return true; }
    }
    std::ostringstream o; o << "pass did not finish (PC=$" << std::hex << m_dsp->getPC().toWord() << ")";
    m_faulted = true; m_fault = o.str();
    return false;
}

void VoiceEngine::setPacket(int track, const uint32_t* words, int count)
{
    if (track < 0 || track >= kTracks) return;
    const int n = std::min(count, 0x40);
    for (int k = 1; k < n; ++k) {
        m_packet[size_t(track)][size_t(k)] = words[k] & 0xFFFFFF;
        m_dsp->memWrite(MemArea_Y, trackBase(track) + TWord(k), words[k] & 0xFFFFFF);
    }
    m_packetLen[size_t(track)] = n;
}

void VoiceEngine::trig(int track, int dspType)
{
    if (track < 0 || track >= kTracks || dspType <= 0 || dspType > 192) return;
    m_dsp->memWrite(MemArea_Y, trackBase(track), TWord(dspType));
}

void VoiceEngine::setInput(const int32_t* lr64)
{
    for (TWord slot = 0; slot < 4; ++slot)
        for (TWord k = 0; k < 64; ++k) m_dsp->memWrite(MemArea_X, 0x100 + slot * 0x40 + k, TWord(lr64[k]) & 0xFFFFFF);
}

uint32_t VoiceEngine::peek(int space, uint32_t addr) const
{
    return m_mem->get(space == 0 ? MemArea_P : space == 1 ? MemArea_X : MemArea_Y, addr);
}

bool VoiceEngine::renderPass(Block& out)
{
    if (m_faulted) { for (auto& t : out) t.fill(0); return false; }
    for (int t = 0; t < kTracks; ++t)
        for (int k = 1; k < m_packetLen[size_t(t)]; ++k) m_dsp->memWrite(MemArea_Y, trackBase(t) + TWord(k), m_packet[size_t(t)][size_t(k)]);
    m_dsp->setPC(kMainLoop);
    if (!runToPark(kMaxExecPass)) { for (auto& t : out) t.fill(0); return false; }
    for (int t = 0; t < kTracks; ++t)
        for (int i = 0; i < kBlockFrames; ++i) {
            const TWord w = m_mem->get(MemArea_Y, kCapture + TWord(t * kBlockFrames + i)) & 0xFFFFFF;
            out[size_t(t)][size_t(i)] = int32_t(w << 8) >> 8;
        }
    return true;
}

} // namespace mnm::md
