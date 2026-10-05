#include "MdEngine.h"
#include <algorithm>
#include <cmath>

namespace mnm::md {

Engine::Engine(const Firmware& fw) : m_fw(fw)
{
    m_cpu = std::make_unique<ControlCpu>(fw.mainOs);
    m_voices = std::make_unique<VoiceEngine>(fw);
    m_mixer = std::make_unique<MixEngine>(fw);
}

void Engine::trig(int t, int machine, int accent)
{
    m_voices->trig(t, machine + 1);
    m_cpu->lfoTrig(t);
    m_tracks[size_t(t)].trigPending = true;   // the next conversion is the trig tick's (the whole packet)
    m_tracks[size_t(t)].accent = accent;
    groupTrig(t);
}

void Engine::groupTrig(int t)
{
    m_tracks[size_t(t)].groupMuted = false;
    const int m = m_tracks[size_t(t)].target.muteGroup;
    if (m >= 0 && m < kTracks && m != t) m_tracks[size_t(m)].groupMuted = true;
}

void Engine::refresh()
{
    ControlCpu::TrackParams params{};
    std::array<uint8_t, 16> levels{};
    for (int t = 0; t < kTracks; ++t) {
        const auto& tr = m_tracks[size_t(t)].target;
        for (int k = 0; k < 24; ++k) params[size_t(t)][size_t(k)] = tr.params[size_t(k)];
        levels[size_t(t)] = tr.level;
        m_cpu->setLfoConfig(t, tr.lfoConfig.data());
    }
    m_cpu->setTargets(params, levels, m_masterFx);
    const bool snap = m_snap;
    m_snap = false;
    if (snap) m_cpu->snapToTargets();
    m_cpu->setTempo(m_bpm);
    m_cpu->tick((m_blockCount++ & 3) == 0);   // the LFOs advance every 4th block, as on the hardware
    // An LFO on a CTR-RE / GB / EQ / DX track's SYNTHESIS knob k: the master effect's parameter k takes that knob's live
    // word (knob + LFO): its live word and its target (MainOS 0x23AEAC -> 0x237C54, which also writes the kit). Here only
    // the control CPU's copies move (the plugin's master parameter keeps the knob's value: no automation at LFO rate), so
    // they are put back after every tick, as setTargets restores the target each pass.
    for (int t = 0; t < kTracks; ++t) {
        const int fx = m_tracks[size_t(t)].target.ctrMasterFx;
        if (fx < 0 || fx > 3) continue;
        for (int l = 0; l < kTracks; ++l) {
            const auto& cfg = m_tracks[size_t(l)].target.lfoConfig;
            const int k = cfg[1];
            if (cfg[0] != t || k > 7) continue;
            const uint16_t raw = m_cpu->liveParam(t, k);
            m_cpu->setLiveLevel(16 + 8 * fx + k, raw);
            m_cpu->setLevelTarget(16 + 8 * fx + k, uint8_t(std::min(127, raw >> 7)));   // the sections read some targets
        }
    }

    for (int t = 0; t < kTracks; ++t) {
        auto& st = m_tracks[size_t(t)];
        // synthesis: the machine's control handler on the live raw words
        const int id = st.target.machine;
        std::array<int, 8> syn{};
        for (int k = 0; k < 8; ++k) syn[size_t(k)] = m_cpu->liveParam(t, k);
        const bool trigTick = st.trigPending;
        st.trigPending = false;
        if (trigTick || snap || id != st.sentMachine || syn != st.synSent) {
            st.sentMachine = id; st.synSent = syn;
            if (const auto* m = m_fw.byId(id)) {
                std::array<uint16_t, 8> raw{};
                for (int k = 0; k < 8; ++k) raw[size_t(k)] = uint16_t(syn[size_t(k)]);
                std::array<uint32_t, ControlCpu::kMaxPacket> packet{};
                const int n = m_cpu->convert(m->handler, trigTick, raw, packet);
                if (n > 0) m_voices->setPacket(t, packet.data(), n, trigTick);
            }
        }
        // DSP1: track effects (AMD..SRR, DIST) and routing (level, VOL, PAN, sends)
        std::array<int, 14> mix{};
        for (int k = 0; k < 13; ++k) mix[size_t(k)] = m_cpu->liveParam(t, 8 + k);   // AMD..SRR DIST, VOL PAN DEL REV
        mix[13] = m_cpu->liveLevel(t);
        const int route = st.target.route < 0 ? 0 : st.target.route > 6 ? 6 : st.target.route;
        if (snap || mix != st.mixSent || route != st.sentRoute || st.accent != st.sentAccent || st.groupMuted != st.sentGroupMuted) {
            std::array<uint16_t, 9> fx{};
            for (int k = 0; k < 9; ++k) fx[size_t(k)] = uint16_t(mix[size_t(k)]);
            m_mixer->setTrackFx(t, fx);
            auto words = MixEngine::routingWords(uint32_t(mix[13]), uint32_t(mix[9]), uint32_t(mix[10]), uint32_t(mix[12]), uint32_t(mix[11]), route, st.accent);
            if (st.groupMuted) std::fill(words.begin() + 1, words.end(), 0u);   // muted by its mute group: the route stays
            m_mixer->setRouting(t, words);
            st.mixSent = mix; st.sentRoute = route; st.sentAccent = st.accent; st.sentGroupMuted = st.groupMuted;
        }
    }
    // master effects (the delay also follows the tempo)
    for (int fx = 0; fx < ControlCpu::kNumMasterFx; ++fx) {
        const auto id = ControlCpu::MasterFx(fx);
        std::array<int, 8> raw{};
        for (int k = 0; k < 8; ++k) raw[size_t(k)] = m_cpu->liveLevel(16 + 8 * fx + k);
        const bool tempoChanged = id == ControlCpu::MasterFx::Delay && std::abs(m_bpm - m_tempoSent) > 0.01;
        if (!snap && !tempoChanged && raw == m_masterSent[size_t(fx)]) continue;
        m_masterSent[size_t(fx)] = raw;
        std::array<uint32_t, 16> words{};
        if (m_cpu->convertMasterFxFromTick(id, words)) {
            const auto& s = ControlCpu::masterFxSection(id);
            m_mixer->setY(s.dspAddr, words.data(), s.words);
        }
    }
    m_tempoSent = m_bpm;
}

void Engine::setDirect(uint32_t mask)
{
    for (int t = 0; t < kTracks; ++t)   // a track that just went direct starts from silence
        if (((mask & ~m_direct) >> t) & 1) for (auto& c : m_trackOut[size_t(t)]) c.fill(0.0f);
    m_direct = mask;
    m_mixer->setDirect(mask);
}

void Engine::render()
{
    refresh();
    m_voices->renderPass(m_block);
    m_mixer->renderBlock(m_block, m_out);
    for (int t = 0; t < kTracks; ++t) {   // the meters
        float L[kFrames], R[kFrames], pk = 0.0f;
        m_mixer->trackOutput(t, L, R);
        for (int i = 0; i < kFrames; ++i) pk = std::max({pk, std::abs(L[i]), std::abs(R[i])});
        m_trackPeak[size_t(t)] = pk * kMasterGain;
    }
    for (int t = 0; t < kTracks; ++t) {
        if (!((m_direct >> t) & 1)) continue;
        auto& o = m_trackOut[size_t(t)];
        for (auto& c : o) std::copy(c.begin() + kFrames, c.end(), c.begin());   // the last kMasterLatency frames move to the front
        float L[kFrames], R[kFrames];
        m_mixer->trackOutput(t, L, R);
        for (int i = 0; i < kFrames; ++i) { o[0][size_t(kMasterLatency + i)] = L[i] * kMasterGain; o[1][size_t(kMasterLatency + i)] = R[i] * kMasterGain; }
    }
    m_mixer->masterReturn(m_masterReturn.data());   // the main mix for the RAM recorders, next block
    m_voices->setMasterReturn(m_masterReturn.data());
}

} // namespace mnm::md
