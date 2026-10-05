#include "MdVoiceEngine.h"
#include <algorithm>
#include <cstdlib>
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
constexpr TWord kMemSize = 0x200000;    // UW sample memory runs 0x150000..0x1FFA00 (48-ROM layout)
constexpr TWord kMainLoop = 0x64;
constexpr TWord kCopyStub = 0xF00;      // free internal P (the image uses P:0..0x3E1)
constexpr TWord kParkPc = 0xF40;
constexpr TWord kCapture = 0x1000;      // Y: 16 x 32 words of plain RAM below the bridge (the image uses Y up to 0x7FF; tracks 0x800..0xBFF)
constexpr TWord kSampleBase = 0x150000, kSampleEnd = 0x1FFA00;   // the 48-ROM configuration
constexpr TWord kSlotTable = 0x147E00;  // 4 words per slot: base, length, loop start (-1 = none), rate
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
    emit("move #>$1000,x0");
    emit("add x0,b");
    emit("move b1,r1");
    emit("move #>$ffffff,m1");
    {
        // the operand is the address AFTER the loop (as the disassembler prints it): body = pc+2, pc+3
        std::ostringstream o; o << "do #32,$" << std::hex << (pc + 4);
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
    for (auto& w : m_packetWritten) w.fill(0xFFFFFFFF);   // a fresh image: every packet word goes in again
    m_packetTrig.fill(0);
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
    static const bool interp = std::getenv("MD_DSP_INTERP") != nullptr;   // debug: the interpreter instead of the JIT
    for (uint64_t n = 0; n < maxExec; ++n) {
        if (interp) {
            static const char* tr = std::getenv("MD_TRACE");   // "lo-hi" hex PC range: print registers there
            static TWord lo = tr ? TWord(std::strtoul(tr, nullptr, 16)) : 0, hi = tr && std::strchr(tr, '-') ? TWord(std::strtoul(std::strchr(tr, '-') + 1, nullptr, 16)) : 0;
            static int budget = 2000;
            const TWord pc = m_dsp->getPC().toWord();
            if (tr && pc >= lo && pc <= hi && budget > 0 && m_mem->get(MemArea_Y, 0x142) == 0) {
                --budget;
                auto& r = m_dsp->regs();
                std::fprintf(stderr, "PC=%04x x0=%06x x1=%06x y0=%06x y1=%06x a=%02x:%06x:%06x b=%02x:%06x:%06x r0=%06x r7=%06x m7=%06x\n", pc,
                             r.x.var & 0xFFFFFF, (r.x.var >> 24) & 0xFFFFFF, r.y.var & 0xFFFFFF, (r.y.var >> 24) & 0xFFFFFF,
                             unsigned((r.a.var >> 48) & 0xFF), unsigned((r.a.var >> 24) & 0xFFFFFF), unsigned(r.a.var & 0xFFFFFF),
                             unsigned((r.b.var >> 48) & 0xFF), unsigned((r.b.var >> 24) & 0xFFFFFF), unsigned(r.b.var & 0xFFFFFF),
                             r.r[0].var, r.r[7].var, r.m[7].var);
                if (pc == 0xF00 || pc == 0x3A1 || pc == 0xB5) {
                    const TWord bank = m_mem->get(MemArea_Y, 0x140);
                    std::fprintf(stderr, "   bank Y:%03x:", bank);
                    for (TWord k = 0; k < 10; ++k) std::fprintf(stderr, " %06x", m_mem->get(MemArea_Y, bank + k));
                    std::fprintf(stderr, "\n");
                }
            }
            m_dsp->execInterpreter();
        } else m_dsp->exec();
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
    for (int k = 1; k < n; ++k) m_packet[size_t(track)][size_t(k)] = words[k] & 0xFFFFFF;
    m_packetLen[size_t(track)] = n;
}

void VoiceEngine::trig(int track, int dspType)
{
    if (track < 0 || track >= kTracks || dspType <= 0 || dspType > 192) return;
    m_dsp->memWrite(MemArea_Y, trackBase(track), TWord(dspType));
    m_packetTrig[size_t(track)] = 2;
}

bool VoiceEngine::setSamples(const std::array<std::vector<float>, kSlots>& samples, const std::array<double, kSlots>& rates,
                             const std::array<int, kSlots>& loopStarts)
{
    size_t total = 0;
    for (int slot = 0; slot < kSlots; ++slot)
        if (slot < 32 || slot >= 48) total += (samples[size_t(slot)].size() + 1) & ~size_t(1);
    if (total > kRomCapacity) return false;
    // the decode table (built by the DSP at boot): monotonic, code 0 = -1.0 .. code 4095 = +1.0
    std::array<int32_t, 4096> table{};
    for (TWord k = 0; k < 4096; ++k) table[k] = int32_t(m_mem->get(MemArea_Y, 0x146000 + k) << 8) >> 8;
    auto encode = [&](float v) {
        const int32_t x = int32_t(std::clamp(v, -1.0f, 1.0f) * 8388607.0f);
        const auto it = std::lower_bound(table.begin(), table.end(), x);
        int code = int(it - table.begin());
        if (code >= 4096) code = 4095;
        else if (code > 0 && std::abs(int64_t(table[size_t(code - 1)]) - x) <= std::abs(int64_t(table[size_t(code)]) - x)) --code;
        return uint32_t(code);
    };
    TWord addr = kSampleBase;
    for (int slot = 0; slot < kSlots; ++slot) {
        const auto& s = samples[size_t(slot)];
        const TWord rec = kSlotTable + 4 * TWord(slot);
        // ROM slots only run the sample player while they hold a sample (the OS points their dispatch entries at
        // the player, P:0x13D / 0x15A / 0x16C, or back at the fallback)
        const bool romSlot = slot < 32 || slot >= 48;
        if (!romSlot) continue;   // RAM slots 32..35 are the recorders' (below); 36..47 are unused
        {
            const TWord type = TWord(slot) + 129;
            const bool on = !s.empty();
            m_dsp->memWriteP(0x145AF5 + type, on ? 0x00013D : 0x10008E);
            m_dsp->memWriteP(0x145BB6 + type, on ? 0x00015A : 0x10008E);
            m_dsp->memWriteP(0x145C77 + type, on ? 0x00016C : 0x10008F);
        }
        if (s.empty()) {
            for (TWord k = 0; k < 4; ++k) m_dsp->memWriteP(rec + k, 0);
            continue;
        }
        const TWord base = addr;
        for (size_t i = 0; i < s.size(); i += 2) {
            const uint32_t a = encode(s[i]), b = i + 1 < s.size() ? encode(s[i + 1]) : 2048;
            m_dsp->memWriteP(addr++, (a << 12) | b);
        }
        // the loader divides by the sample period in nanoseconds (as in a MIDI sample dump), not the rate
        const double rate = rates[size_t(slot)] > 0 ? rates[size_t(slot)] : 44100.0;
        const int64_t periodNs = std::max<int64_t>(1, int64_t(1e9 / rate + 0.5));
        const TWord rateWord = TWord((int64_t(0x16250000) / periodNs) << 4) & 0xFFFFFF;
        m_dsp->memWriteP(rec + 0, base);
        m_dsp->memWriteP(rec + 1, TWord(s.size()) & 0xFFFFFF);
        m_dsp->memWriteP(rec + 2, loopStarts[size_t(slot)] >= 0 ? TWord(loopStarts[size_t(slot)]) : 0xFFFFFF);
        m_dsp->memWriteP(rec + 3, rateWord);
    }

    // RAM machines (MainOS 0x20D5A0): the four recording slots 32..35 share what the ROM samples leave, as the OS
    // partitions it; record {base, length 0, no loop, 0}; the record limit (samples) at 0x147F00. The recorders
    // (RAM-R1..R4) and players (RAM-P1..P4) are switched on like the OS does once the slots exist.
    const TWord ramWords = (kSampleEnd - addr) / 4;
    for (int i = 0; i < 4; ++i) {
        const TWord rec = kSlotTable + 4 * TWord(32 + i);
        m_dsp->memWriteP(rec + 0, addr + TWord(i) * ramWords);
        m_dsp->memWriteP(rec + 1, 0);
        m_dsp->memWriteP(rec + 2, 0xFFFFFF);
        m_dsp->memWriteP(rec + 3, 0);
    }
    m_dsp->memWriteP(0x147F00, (ramWords * 2 - 32) & 0xFFFFFF);
    struct Entry { TWord type, init, update, render; };
    static const Entry ram[8] = {
        {161, 0x10351D, 0x103537, 0x103579}, {162, 0x103528, 0x10353A, 0x103579},   // RAM-R1, R2
        {163, 0x000137, 0x00015A, 0x00016C}, {164, 0x00013A, 0x00015A, 0x00016C},   // RAM-P1, P2
        {166, 0x10352D, 0x10353D, 0x103579}, {167, 0x103532, 0x103540, 0x103579},   // RAM-R3, R4
        {168, 0x10009B, 0x00015A, 0x00016C}, {169, 0x10009F, 0x00015A, 0x00016C},   // RAM-P3, P4
    };
    for (const auto& e : ram) {
        m_dsp->memWriteP(0x145AF5 + e.type, e.init);
        m_dsp->memWriteP(0x145BB6 + e.type, e.update);
        m_dsp->memWriteP(0x145C77 + e.type, e.render);
    }
    return true;
}

void VoiceEngine::setInput(const int32_t* lr64)
{
    for (TWord slot = 0; slot < 4; ++slot)
        for (TWord k = 0; k < 64; ++k) m_dsp->memWrite(MemArea_X, 0x100 + slot * 0x40 + k, TWord(lr64[k]) & 0xFFFFFF);
    m_dsp->memWrite(MemArea_X, 0x256, 0x100);   // the ADC ring position the RAM recorders read (P:0xE2 is patched out)
}

void VoiceEngine::setMasterReturn(const int32_t* lr64)
{
    for (TWord slot = 0; slot < 4; ++slot)
        for (TWord k = 0; k < 64; ++k) m_dsp->memWrite(MemArea_X, 0x700 + slot * 0x40 + k, TWord(lr64[k]) & 0xFFFFFF);
}

uint32_t VoiceEngine::peek(int space, uint32_t addr) const
{
    return m_mem->get(space == 0 ? MemArea_P : space == 1 ? MemArea_X : MemArea_Y, addr);
}

bool VoiceEngine::renderPass(Block& out)
{
    if (m_faulted) { for (auto& t : out) t.fill(0); return false; }
    for (int t = 0; t < kTracks; ++t) {
        const bool all = m_packetTrig[size_t(t)] > 0;
        if (all) --m_packetTrig[size_t(t)];
        auto& written = m_packetWritten[size_t(t)];
        for (int k = 1; k < m_packetLen[size_t(t)]; ++k) {
            const uint32_t w = m_packet[size_t(t)][size_t(k)];
            if (!all && w == written[size_t(k)]) continue;
            m_dsp->memWrite(MemArea_Y, trackBase(t) + TWord(k), w);
            written[size_t(k)] = w;
        }
    }
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
