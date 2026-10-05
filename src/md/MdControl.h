// Runs the Machinedrum OS's own ColdFire control handlers (68020-compatible code, Musashi core) to turn a machine's
// eight raw knob words into the packet DSP2 receives at Y:0x800 + 0x40*t + 1.. . The handlers are pure functions
// of the raw words and a few globals (the tempo at 0x100150C), so they are called on demand, not per block.
//   int handler(u32* packet, const u16* raw)   -> packet word count including packet[0] (the type slot)
// The raw word of a knob is about value << 7 (0..16256); the OS slews it between knob positions.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mnm::md {

class ControlCpu {
public:
    static constexpr int kMaxPacket = 24;
    explicit ControlCpu(const std::vector<uint8_t>& mainOs);
    ~ControlCpu();

    // packet[1..n-1] are the DSP words (24 bits used). Returns n (0: nothing to send, or a failure: lastError()).
    // trig: what the OS puts in packet[0] before its per-tick call (MainOS 0x20B330..0x20B398): 1 on the tick of the
    // track's trig, else 0. On a trig tick a handler returns its whole packet; otherwise it may return fewer words:
    // the trailing ones are the voice's starting state, which the DSP then runs in place (TRX-XT 10 -> 8, TRX-CH
    // 11 -> 9, TRX-MA 12 -> 0), so a knob or LFO change between trigs never resets a sounding voice.
    int convert(uint32_t handler, bool trig, const std::array<uint16_t, 8>& raw, std::array<uint32_t, kMaxPacket>& packet);
    // As the OS calls it every control tick: the packet buffer is the track's own (kept between calls, in and out,
    // all kMaxPacket words) and slot 0 holds what the OS puts there (the trig flag). Returns the handler's D0.
    int convertInPlace(uint32_t handler, uint32_t slot0, const std::array<uint16_t, 8>& raw, std::array<uint32_t, kMaxPacket>& buffer);
    static uint16_t rawFromValue(int value) { return uint16_t(value < 0 ? 0 : value > 127 ? 127 << 7 : value << 7); }

    // Master effects: the OS converts the eight raw knob words of each effect inline in its per-tick task
    // (MainOS 0x20B440..0x20BAB2); each effect's section is run on its own, from its entry to the common join
    // 0x20BA4C, and its result read from the DSP1 parameter mirror in internal SRAM.
    enum class MasterFx { Reverb = 0, Delay, Eq, Dynamics };
    static constexpr int kNumMasterFx = 4;
    struct MasterFxSection { uint32_t entry, rawAddr, mirror, dspAddr; int words; };
    static const MasterFxSection& masterFxSection(MasterFx fx);
    // words: the DSP1 words for Y:dspAddr.. (count = section.words). Returns false on a runaway section.
    bool convertMasterFx(MasterFx fx, const std::array<uint16_t, 8>& raw, std::array<uint32_t, 16>& words);
    // As above, from the raw words the control tick left in SRAM (slewed, the OS's own path)
    bool convertMasterFxFromTick(MasterFx fx, std::array<uint32_t, 16>& words);

    // The OS's control tick, run once per audio block (32 frames) as the hardware's block interrupt does
    // (SRAM 0x1000088.. copied from MainOS). It slews every knob toward its target (cur = (3 cur + target) / 4),
    // evaluates the track LFOs (every 4th block) and adds each LFO to its destination knob:
    //   targets: track params 0x1000DDC (16 x 24 bytes), levels + master effects 0x1000F5C (16 + 32 bytes)
    //   LFOs:    0x1000F8C + 36 t (kit LFO struct: dest track, dest param, shape 1, shape 2, type, state)
    //   live:    track params 0x10011CC (16 x 24 raw words), levels 0x1000D7C, master effects 0x1000D9C
    using TrackParams = std::array<std::array<uint8_t, 24>, 16>;
    void setTargets(const TrackParams& params, const std::array<uint8_t, 16>& levels, const std::array<std::array<uint8_t, 8>, 4>& masterFx);
    void snapToTargets();   // jump every live word to its target (load, state restore)
    void setLfo(int track, const uint8_t* lfo36);       // the whole struct (kit load)
    void setLfoConfig(int track, const uint8_t* first5); // dest track, dest param, shape 1, shape 2, type (state kept)
    void lfoTrig(int track);                             // what a trig does to the track's LFO
    bool tick(bool lfoUpdate);
    uint16_t liveParam(int track, int k) const;          // raw word, 0..0x3FFF
    uint16_t baseParam(int track, int k) const;          // the slewed word before the LFOs (live - base = the LFO's part)
    uint16_t liveLevel(int track) const;
    void setLiveLevel(int index, uint16_t raw);          // 0-15 levels, 16 + 8 fx + k the master effects (the tick slews on from it)
    void setLevelTarget(int index, uint8_t value);       // the same index's target (the next setTargets puts the caller's back)

    // Developer aid: the instruction at pc (MainOS / SRAM addresses), Musashi's 68020 syntax. Returns its length.
    int disassemble(uint32_t pc, std::string& text);
    void setTempo(double bpm) { m_tempo = uint32_t(bpm * 24.0 + 0.5); }   // BPM x 24, as the OS keeps it
    const char* lastError() const { return m_error; }
    uint32_t lastPc() const { return m_lastPc; }

private:
    bool call(uint32_t fn, uint32_t stopPc = 0);   // under the CPU lock: run fn (or up to stopPc) to its return
    uint8_t* sram(uint32_t addr) { return m_ram.data() + 0x7E0000 + (addr - 0x1000000); }
    const uint8_t* sram(uint32_t addr) const { return m_ram.data() + 0x7E0000 + (addr - 0x1000000); }
    std::vector<uint8_t> m_ram;   // 0x000000.. : MainOS at 0x200000, globals after it, the call frame at the top
    uint32_t m_tempo = 120 * 24;
    const char* m_error = "";
    uint32_t m_lastPc = 0;
};

} // namespace mnm::md
