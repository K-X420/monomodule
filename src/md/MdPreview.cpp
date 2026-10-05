#include "MdPreview.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mnm::mdpreview {

using namespace mnm::mddump;

namespace {
uint64_t maskOf(uint32_t editAll, uint64_t global, uint64_t perTrack) { return editAll ? global : perTrack; }

double stepFrames(const Pattern& p, double bpm)
{
    const double f = 60.0 / std::max(20.0, bpm) / 4.0 * kSampleRate;   // a 16th
    return p.doubleTempo ? f / 2.0 : f;
}
} // namespace

Spec patternPreview(const Kit& kit, const Pattern& p, const Options& opt)
{
    Spec s;
    s.kit = kit;
    const int len = std::clamp(int(p.length), 1, 64);
    const double step = stepFrames(p, opt.bpm);
    const double pass = len * step;
    s.loops = std::clamp(int(std::ceil(opt.minSeconds * kSampleRate / pass)), 1, std::max(1, opt.maxLoops));
    s.loopFrames = uint32_t(pass);
    const double swingDelay = (p.swingPercent() - 50) / 50.0 * step;   // the swung step's offset into its pair
    for (int loop = 0; loop < s.loops; ++loop)
        for (int st = 0; st < len; ++st)
            for (int t = 0; t < kTracks; ++t) {
                if (!((p.trigs[t] >> st) & 1)) continue;
                Event e;
                e.track = t;
                double at = (loop * len + st) * step;
                if ((maskOf(p.swingEditAll, p.swing, p.swingPerTrack[t]) >> st) & 1) at += swingDelay;
                e.frame = uint32_t(at);
                if ((maskOf(p.accentEditAll, p.accent, p.accentPerTrack[t]) >> st) & 1) e.accent = 0x80 + 2 * int(p.accentAmount);
                for (int q = 0; q < 24; ++q) {   // parameter locks of this step
                    const int row = p.lockRow(t, q);
                    if (row >= 0 && p.locks[row][st] <= 127) e.locks.push_back({q, p.locks[row][st]});
                }
                s.events.push_back(std::move(e));
            }
    std::stable_sort(s.events.begin(), s.events.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
    s.frames = uint32_t(s.loops * pass + opt.tailSeconds * kSampleRate);
    return s;
}

Spec soundPreview(const mdcatalog::Sound& sound, const Options& opt)
{
    Spec s;
    for (int t = 0; t < kTracks; ++t) { s.kit.trigGroups[t] = 127; s.kit.muteGroups[t] = 127; s.kit.lfos[t][0] = uint8_t(t); s.kit.levels[t] = 100; }
    sound.applyTo(s.kit, 0);
    s.kit.levels[0] = 127;
    // the master effects as a fresh Monomodule MD kit has them
    const uint8_t rev[8] = {127, 0, 64, 64, 0, 127, 0, 127}, del[8] = {24, 0, 0, 32, 0, 127, 0, 127}, dyn[8] = {0, 64, 127, 0, 0, 0, 64, 0};
    std::memcpy(s.kit.reverb, rev, 8); std::memcpy(s.kit.delay, del, 8); std::memset(s.kit.eq, 64, 8); std::memcpy(s.kit.dynamics, dyn, 8);
    Event e;
    e.track = 0;
    s.events.push_back(e);
    s.frames = uint32_t(opt.soundSeconds * kSampleRate);
    s.loopFrames = s.frames;
    s.stems = false;
    return s;
}

Pattern demoPattern(const Kit& kit)
{
    Pattern p;
    p.length = 16;
    p.accentEditAll = p.slideEditAll = p.swingEditAll = 1;
    std::vector<int> tracks;
    for (int t = 0; t < kTracks; ++t) if (kit.model(t) != 0) tracks.push_back(t);
    int step = 0;
    for (int t : tracks) { if (step >= 12) break; p.trigs[t] |= 1ull << step; ++step; }   // in turn ...
    for (int t : tracks) p.trigs[t] |= 1ull << 12;                                           // ... then together
    return p;
}

std::string choosePreviewPattern(const mdcatalog::Catalog& cat, const mdcatalog::KitItem& kit)
{
    std::string best;
    long score = -1;
    for (const auto& pid : kit.patternIds)
        if (const auto* p = cat.pattern(pid)) {
            int used = 0, trigs = 0;
            for (int t = 0; t < kTracks; ++t) { const int n = p->pattern.trigCount(t); trigs += n; used += n > 0 ? 1 : 0; }
            const long sc = long(used) * 100000 + trigs;
            if (sc > score) { score = sc; best = pid; }
        }
    return best;
}

// ---------------------------------------------------------------------------------------------- renderer

Renderer::Renderer(const md::Firmware& fw) : m_fw(fw) {}

md::Engine::Track Renderer::trackFor(const Kit& kit, int t) const
{
    md::Engine::Track tr;
    tr.machine = kit.model(t);
    std::copy(kit.params[t], kit.params[t] + 24, tr.params.begin());
    tr.level = kit.levels[t];
    std::copy(kit.lfos[t], kit.lfos[t] + 5, tr.lfoConfig.begin());
    tr.route = 6;   // MAIN: the outputs are a global setting of the unit, not the kit's
    if (md::isMidMachine(tr.machine)) {   // no voice (the plugin plays its MIDI; a preview has nowhere to send it)
        tr.machine = 0; tr.level = 0;
    } else if (md::isCtrMachine(tr.machine)) {   // no voice; CTR-RE..DX keep SYNTHESIS and the LFO knobs (an LFO on
        const bool lfo = tr.machine != md::kCtrAll && tr.machine != md::kCtr8p;   // one moves the master effect)
        const auto keep = tr.params;
        tr.params.fill(0);
        if (lfo) { for (int k = 0; k < 8; ++k) tr.params[size_t(k)] = keep[size_t(k)]; for (int k = 21; k < 24; ++k) tr.params[size_t(k)] = keep[size_t(k)]; }
        tr.ctrMasterFx = md::ctrMasterFx(tr.machine);
        tr.machine = 0; tr.level = 0;
    }
    return tr;
}

bool Renderer::control(Kit& kit, int t, int q, int v, std::array<bool, 16>& changed)
{
    const int id = kit.model(t);
    v = std::clamp(v, 0, 127);
    uint8_t* master[4] = {kit.reverb, kit.delay, kit.eq, kit.dynamics};
    if (const int fx = md::ctrMasterFx(id); fx >= 0) {
        if (q >= 8) return false;
        kit.params[t][q] = uint8_t(v);
        master[fx][q] = uint8_t(v);
        std::array<std::array<uint8_t, 8>, 4> m{};
        for (int f = 0; f < 4; ++f) std::copy(master[f], master[f] + 8, m[size_t(f)].begin());
        m_engine->setMasterFx(m);
        changed[size_t(t)] = true;   // its knob (an LFO's base)
        return false;
    }
    if (id == md::kCtrAll) {   // MainOS 0x207E0E: the change, counted within 1..126, onto every audio track
        const int now = std::clamp(v, 1, 126), d = now - std::clamp(int(kit.params[t][q]), 1, 126);
        kit.params[t][q] = uint8_t(now);
        if (d == 0) return false;
        bool any = false;
        for (int u = 0; u < kTracks; ++u) {
            const int uid = kit.model(u);
            if (u == t || md::isMidMachine(uid) || md::isCtrMachine(uid)) continue;
            if (q < 8 && md::isRamRecorder(uid)) continue;
            kit.params[u][q] = uint8_t(std::clamp(int(kit.params[u][q]) + d, 0, 127));
            changed[size_t(u)] = any = true;
        }
        return any;
    }
    if (id == md::kCtr8p) {    // MainOS 0x207D36: P knob q onto its TRK / PAR (the assignments cannot be locked)
        if (q >= 8) return false;
        const int tt = std::min(15, int(kit.params[t][8 + 2 * q])), tp = std::min(23, int(kit.params[t][9 + 2 * q]));
        kit.params[t][q] = uint8_t(v);
        const int tid = kit.model(tt);
        if (tid == md::kCtrAll || tid == md::kCtr8p) return false;
        if (md::isCtrMachine(tid)) return control(kit, tt, tp, v, changed);
        if (md::isMidMachine(tid)) return false;
        kit.params[tt][tp] = uint8_t(v);
        changed[size_t(tt)] = true;
        return true;
    }
    return false;   // MID: MIDI out (nothing to hear here)
}

void Renderer::loadKit(const Kit& kit, double bpm)
{
    m_engine = std::make_unique<md::Engine>(m_fw);   // a fresh engine per render: no tails of the last one
    for (int t = 0; t < kTracks; ++t) {
        m_engine->setTrack(t, trackFor(kit, t));
        m_engine->setLfoState(t, kit.lfos[t]);
    }
    std::array<std::array<uint8_t, 8>, 4> fx{};
    const uint8_t* src[4] = {kit.reverb, kit.delay, kit.eq, kit.dynamics};
    for (int f = 0; f < 4; ++f) std::copy(src[f], src[f] + 8, fx[size_t(f)].begin());
    m_engine->setMasterFx(fx);
    m_engine->setTempo(bpm);
    m_engine->snap();
    m_engine->render();   // a warm-up pass: the snapped targets reach the DSPs before the first trig
}

bool Renderer::render(const Spec& spec, double bpm, const std::function<void(uint32_t, const Block&)>& out, const std::atomic<bool>* cancel)
{
    m_error.clear();
    try { loadKit(spec.kit, bpm); }
    catch (const std::exception& e) { m_error = e.what(); return false; }
    constexpr int N = Block::kFrames;
    constexpr float kOut = 0.8f / 8388608.0f;   // the plugin's VOLUME 80
    size_t next = 0;
    std::array<bool, kTracks> locked{};
    Kit kit = spec.kit;   // what CTR locks change as the pattern plays
    Block b;
    for (uint32_t f0 = 0; f0 < spec.frames; f0 += N) {
        if (cancel && cancel->load()) { m_error = "cancelled"; return false; }
        bool snap = false;
        for (; next < spec.events.size() && spec.events[next].frame < f0 + N; ++next) {
            const auto& e = spec.events[next];
            const int t = e.track;
            const int id = kit.model(t);
            if (md::isCtrMachine(id) || md::isMidMachine(id)) {   // no voice: a CTR track's locks act on the kit
                std::array<bool, kTracks> changed{};
                for (const auto& [q, v] : e.locks) control(kit, t, q, v, changed);
                for (int u = 0; u < kTracks; ++u)
                    if (changed[size_t(u)] && !locked[size_t(u)]) m_engine->setTrack(u, trackFor(kit, u));
                continue;
            }
            if (!e.locks.empty() || locked[size_t(t)]) {   // this trig's locks, or the kit's values back after a locked trig
                auto tr = trackFor(kit, t);
                for (const auto& [q, v] : e.locks) tr.params[size_t(q)] = uint8_t(v);
                m_engine->setTrack(t, tr);
                locked[size_t(t)] = !e.locks.empty();
                snap = true;
            }
            m_engine->trig(t, id, e.accent);
        }
        if (snap) m_engine->snap();   // a lock is the step's value at once, not a slew
        try { m_engine->render(); }
        catch (const std::exception& ex) { m_error = ex.what(); return false; }
        const auto& o = m_engine->output();
        const auto& v = m_engine->voiceBlock();
        for (int i = 0; i < N; ++i) {
            b.mixL[size_t(i)] = float(o[size_t(i)][md::MixEngine::kMainLeft]) * kOut;
            b.mixR[size_t(i)] = float(o[size_t(i)][md::MixEngine::kMainRight]) * kOut;
            if (spec.stems)
                for (int t = 0; t < kTracks; ++t) b.stem[size_t(t)][size_t(i)] = float(v[size_t(t)][size_t(i)]) * kOut;
        }
        out(f0, b);
    }
    return true;
}

} // namespace mnm::mdpreview
