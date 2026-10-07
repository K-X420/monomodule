#include "MdProcessor.h"
#include <cstring>
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MdEditor.h"
#include "SharedSettings.h"
#include "MdMachineText.h"

namespace mnm::plugin::md {

namespace {
using mnm::md::ControlCpu;
using mnm::md::MixEngine;
constexpr double kEngineRate = 44100.0;
constexpr int kBlock = mnm::md::VoiceEngine::kBlockFrames;
// DAC frame offsets of the outputs: A=2 B=5 C=1 D=4 E=0 F=3; the main bus is A/B (where MAIN lands)
constexpr int kBusChannels[3][2] = {{2, 5}, {1, 4}, {0, 3}};
constexpr int kHardwareBuses = 3;   // then "Track 1".."Track 16" (PER TRACK outputs)

juce::String loadOsPath() { return loadSharedSetting("mdOsPath"); }
}

MdProcessor::MdProcessor()
    : AudioProcessor([] {
          auto b = BusesProperties()
                       .withInput("Input (INP machines)", juce::AudioChannelSet::stereo(), false)
                       .withOutput("Main A/B", juce::AudioChannelSet::stereo(), true)
                       .withOutput("Out C/D", juce::AudioChannelSet::stereo(), false)
                       .withOutput("Out E/F", juce::AudioChannelSet::stereo(), false);
          for (int t = 0; t < kTracks; ++t) b = b.withOutput("Track " + juce::String(t + 1), juce::AudioChannelSet::stereo(), false);
          return b;
      }()),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    for (int t = 0; t < kTracks; ++t) {   // an empty kit's bytes: no trig / mute groups, every LFO on its own track
        m_baseKit.trigGroups[t] = 127; m_baseKit.muteGroups[t] = 127; m_baseKit.lfos[t][0] = uint8_t(t);
    }
    for (int i = 0; i < kNumMachines; ++i) m_idOfIndex[size_t(i)] = kMachines[i].id;
    for (int t = 0; t < kTracks; ++t) {
        m_shadowIdx[size_t(t)] = machineIndexOf(kDefaultKit[t].id);
        for (int k = 0; k < 8; ++k)
            if (auto* sp = dynamic_cast<MdSynParam*>(apvts.getParameter(knobId(t, k))))
                sp->setSources(apvts.getRawParameterValue(machineId(t)), &m_knobInfo, m_idOfIndex.data());
    }
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        tr.machine = apvts.getRawParameterValue(machineId(t));
        for (int k = 0; k < 8; ++k) tr.knobs[k] = apvts.getRawParameterValue(knobId(t, k));
        for (int k = 0; k < 8; ++k) tr.mix[k] = apvts.getRawParameterValue(fxId(t, k));
        tr.mix[8] = apvts.getRawParameterValue(distId(t));
        tr.mix[9] = apvts.getRawParameterValue(volId(t));
        tr.mix[10] = apvts.getRawParameterValue(panId(t));
        tr.mix[11] = apvts.getRawParameterValue(delId(t));
        tr.mix[12] = apvts.getRawParameterValue(revId(t));
        tr.mix[13] = apvts.getRawParameterValue(levelId(t));
        tr.route = apvts.getRawParameterValue(routeId(t));
        for (int k = 0; k < 8; ++k) tr.lfo[k] = apvts.getRawParameterValue(lfoId(t, k));
        tr.mute = apvts.getRawParameterValue(muteId(t));
        tr.trigGroup = apvts.getRawParameterValue(trigGroupId(t));
        tr.muteGroup = apvts.getRawParameterValue(muteGroupId(t));
        apvts.addParameterListener(machineId(t), this);
    }
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) m_masterFx[size_t(fx)][size_t(k)] = apvts.getRawParameterValue(masterFxId(fx, k));
    m_master = apvts.getRawParameterValue(masterId());
    m_velMode = apvts.getRawParameterValue(velModeId());
    m_outputMode = apvts.getRawParameterValue(outputModeId());
    m_bpmSync = apvts.getRawParameterValue(bpmSyncId());
    m_bpm = apvts.getRawParameterValue(bpmId());
    m_accent = apvts.getRawParameterValue(accentId());
    m_seqOn = apvts.getRawParameterValue(seqId());
    m_extended = apvts.getRawParameterValue(extendedId());
    m_patternParam = apvts.getRawParameterValue(patternId());
    m_seqMode = apvts.getRawParameterValue(seqModeId());
    m_songParam = apvts.getRawParameterValue(songId());
    for (auto& a : m_lockVal) a.fill(-1);
    for (auto& a : m_ctlWrite) a.fill(-1);
    m_recSkip.fill(-1);
    setMidiSettings(defaultMidiSettings());
    for (auto& a : m_outSeen) a.fill(-1);
    for (auto& a : m_recSeen) a.fill(-1);
    m_firmwarePath = loadOsPath();
    loadEngine();
}

MdProcessor::~MdProcessor()
{
    cancelPendingUpdate();
    for (int t = 0; t < kTracks; ++t) apvts.removeParameterListener(machineId(t), this);
}

int MdProcessor::machineIdOf(int t) const
{
    const int idx = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_tracks[size_t(t)].machine->load())));
    return kMachines[idx].id;
}

void MdProcessor::setFirmwarePath(const juce::String& path, bool persist)
{
    m_firmwarePath = path;
    loadEngine();
    if (persist && m_engineReady) saveSharedSetting("mdOsPath", path);   // a failed load must not reach the other instances
}

void MdProcessor::loadEngine()
{
    const juce::ScopedLock sl(m_engineLock);
    m_engineReady = false;
    m_engine.reset(); m_fw.reset();
    if (m_firmwarePath.isEmpty()) { m_status = "Select the Machinedrum OS file (Elektron_SPS1-1UW_OS1.63.syx)"; return; }
    try {
        auto fw = std::make_unique<mnm::md::Firmware>(mnm::md::loadFirmware(m_firmwarePath.toStdString()));
        auto engine = std::make_unique<mnm::md::Engine>(*fw);
        text::readRomBadges(fw->mainOs, mnm::md::kMainOsBase);   // the family badges, for the logos
        text::readMdLfoIcons(fw->mainOs, mnm::md::kMainOsBase);  // the LFO wave icons
        m_fw = std::move(fw); m_engine = std::move(engine);
        m_snap = true;
        m_status = "OS loaded: " + juce::File(m_firmwarePath).getFileName();
        for (const auto& m : m_fw->machines) {   // the knob names and defaults the parameters show
            if (m.id < 0 || m.id > 255) continue;
            for (int k = 0; k < 8; ++k) { m_knobInfo.labels[size_t(m.id)][size_t(k)] = juce::String(m.labels[size_t(k)]); m_knobInfo.defaults[size_t(m.id)][size_t(k)] = m.defaults[size_t(k)]; }
            m_knobInfo.known[size_t(m.id)] = true;
        }
        triggerAsyncUpdate();   // the hosts re-read the names (on the message thread)
        {   // the UW samples (inline: the engine lock is already held)
            std::array<std::vector<float>, mnm::md::VoiceEngine::kSlots> data;
            std::array<double, mnm::md::VoiceEngine::kSlots> rates{};
            std::array<int, mnm::md::VoiceEngine::kSlots> loops;
            loops.fill(-1);
            for (int i = 0; i < mnm::md::VoiceEngine::kSlots; ++i) { data[size_t(i)] = m_samples[size_t(i)].data; rates[size_t(i)] = m_samples[size_t(i)].rate; }
            m_engine->voices().setSamples(data, rates, loops);
        }
        m_engineReady = true;
    } catch (const std::exception& e) {
        m_status = juce::String("OS file error: ") + e.what();
    }
}

void MdProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    m_hostRate = sampleRate;
    const int cap = int(std::ceil(samplesPerBlock * kEngineRate / sampleRate)) + 2 * kBlock + 16;
    for (auto& f : m_fifo) f.assign(size_t(cap), 0.0f);
    m_fifoLen = 0;
    for (auto& i : m_interp) i.reset();
    for (auto& f : m_inFifo) f.assign(size_t(cap + 64), 0.0f);
    m_inLen = 0;
    for (auto& i : m_inInterp) i.reset();
    m_pending.clear();
    m_pending.reserve(1024);
    m_seqTrigs.clear();
    m_seqTrigs.reserve(2048);
    m_seqRunning = false;
    for (auto& v : m_midNotes) { v.clear(); v.reserve(8); }
    m_midLastPb.fill(-1); m_midLastMw.fill(-1); m_midLastPc.fill(-1);
    m_midiOut.ensureSize(4096);
    m_clock = 0;
    m_ctlStarted = false;
    for (auto& s : m_midSent) s.fill(-1);
    for (auto& o : m_lfoOffset) o.fill(0);
    m_midMachine.fill(-1);
}

bool MdProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto in = layouts.getMainInputChannelSet();
    if (!in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    for (int b = 1; b < layouts.outputBuses.size(); ++b) {
        const auto& s = layouts.outputBuses.getReference(b);
        if (!s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

// The OS's control path, once per block: the knob values become the tick's targets; the tick slews them and adds
// the LFOs; the live raw words then go through the machine handlers (DSP2), as raw words to DSP1's track effects,
// through the routing law, and through the master effect sections.
void MdProcessor::refreshParameters()
{
    auto val = [](std::atomic<float>* p) { return uint8_t(juce::jlimit(0, 127, int(std::lround(p->load())))); };
    // CTR writes that have reached their parameter (or never will) stop standing in for it
    for (int target = 0; target < kTracks + 4; ++target)
        for (int p = 0; p < 24; ++p) {
            auto& w = m_ctlWrite[size_t(target)][size_t(p)];
            if (w < 0) continue;
            const int now = target < kTracks ? trackParam(target, p) : p < 8 ? int(val(m_masterFx[size_t(target - kTracks)][size_t(p)])) : w;
            if (now == w || m_clock > m_ctlWriteUntil[size_t(target)]) w = -1;
        }
    const auto* ok = m_kitOverride.load();
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        mnm::md::Engine::Track e;
        e.machine = ok ? overrideMachine(*ok, t) : machineIdOf(t);
        auto& p = e.params;
        for (int k = 0; k < 24; ++k) p[size_t(k)] = uint8_t(seqParam(t, k));
        e.level = ok ? uint8_t(juce::jlimit(0, 127, int(ok->levels[t]))) : val(tr.mix[13]);
        static const int lfoMax[5] = {15, 23, 5, 5, 2};
        for (int k = 0; k < 5; ++k) e.lfoConfig[size_t(k)] = ok ? uint8_t(juce::jlimit(0, lfoMax[k], int(ok->lfos[t][k]))) : val(tr.lfo[k]);
        e.route = juce::jlimit(0, kNumRoutes - 1, int(std::lround(tr.route->load())));
        e.muteGroup = muteGroupOf(t);
        if (isMidMachine(e.machine)) {   // no voice; the parameters stay (the OS's LFOs move them: midStream)
            e.machine = 0; e.level = 0;
        } else if (isCtrMachine(e.machine)) {   // no voice; CTR-RE..DX keep SYNTHESIS (an LFO moves the master effect from it)
            const bool lfo = e.machine != kCtrAll && e.machine != kCtr8p;
            const auto keep = p;
            p.fill(0);
            if (lfo) { for (int k = 0; k < 8; ++k) p[size_t(k)] = keep[size_t(k)]; p[21] = keep[21]; p[22] = keep[22]; p[23] = keep[23]; }
            e.ctrMasterFx = ctrMasterFx(e.machine);
            e.machine = 0; e.level = 0;
        }
        if (m_kitLfoPending[size_t(t)].exchange(false)) m_engine->setLfoState(t, m_kitLfos[size_t(t)].data());
        m_engine->setTrack(t, e);
    }
    std::array<std::array<uint8_t, 8>, 4> master{};
    const uint8_t* kitFx[4] = {ok ? ok->reverb : nullptr, ok ? ok->delay : nullptr, ok ? ok->eq : nullptr, ok ? ok->dynamics : nullptr};
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) {
            const int w = m_ctlWrite[size_t(kTracks + fx)][size_t(k)];
            master[size_t(fx)][size_t(k)] = ok ? uint8_t(kitFx[fx][k] & 0x7F) : w >= 0 ? uint8_t(w) : val(m_masterFx[size_t(fx)][size_t(k)]);
        }
    m_engine->setMasterFx(master);
    if (m_snap.exchange(false)) { m_engine->snap(); m_ctlQuietUntil = m_clock + int64_t(0.2 * m_hostRate); }
    {   // switching sync off hands the current host tempo over to BPM, so nothing jumps
        const bool synced = m_bpmSync->load() >= 0.5f;
        if (m_wasSynced && !synced) {
            const float host = float(m_hostBpm.load());
            juce::MessageManager::callAsync([this, host] { if (auto* p = apvts.getParameter(bpmId())) p->setValueNotifyingHost(p->convertTo0to1(host)); });
        }
        m_wasSynced = synced;
    }
    m_engine->setTempo(tempo());
}

double MdProcessor::tempo() const
{
    const double t = m_bpmSync->load() >= 0.5f ? m_hostBpm.load() : double(m_bpm->load());
    return t >= 20.0 && t <= 400.0 ? t : 120.0;
}

juce::String MdProcessor::statusText() const
{
    const juce::ScopedLock sl(m_engineLock);
    juce::String s = m_status;
    if (m_engine) {
        if (m_engine->voices().faulted()) s += " | DSP2 (voices) FAULT: " + juce::String(m_engine->voices().faultReason());
        else if (m_engine->mixer().faulted()) s += " | DSP1 (mixer) FAULT: " + juce::String(m_engine->mixer().faultReason());
        else s += " | " + juce::String(int64_t(m_engine->voices().lastPassInstructions())) + " + " + juce::String(int64_t(m_engine->mixer().lastBlockInstructions())) + " instr/pass";
    }
    if (std::abs(m_hostRate - kEngineRate) > 0.5) s += " | host rate " + juce::String(m_hostRate, 0) + " Hz: resampling from 44100 (not 1:1)";
    return s;
}

void MdProcessor::refreshSharedOsPath()
{
    const auto p = loadOsPath();
    if (p.isNotEmpty() && p != m_firmwarePath && juce::File(p).existsAsFile()) setFirmwarePath(p, false);
}

void MdProcessor::runPass()
{
    refreshParameters();
    // 32 frames of side-chain input (silence when the FIFO runs short)
    for (int i = 0; i < kBlock; ++i)
        for (int c = 0; c < 2; ++c) {
            const float v = i < m_inLen ? m_inFifo[size_t(c)][size_t(i)] : 0.0f;
            m_inBlock[size_t(2 * i + c)] = int32_t(std::lround(juce::jlimit(-1.0f, 1.0f, v) * 8388607.0f));
        }
    const int take = std::min(kBlock, m_inLen);
    for (auto& f : m_inFifo) std::copy(f.begin() + take, f.begin() + m_inLen, f.begin());
    m_inLen -= take;
    m_engine->setInput(m_inBlock.data());
    m_engine->setDirect(m_directMask.load());
    m_engine->render();
    for (int t = 0; t < kTracks; ++t) {
        if (!isMidMachine(machineIdOf(t))) continue;
        for (int p = 0; p < 24; ++p) {
            const int d = int(m_engine->cpu().liveParam(t, p)) - int(m_engine->cpu().baseParam(t, p));
            m_lfoOffset[size_t(t)][size_t(p)] = int16_t(d >= 0 ? (d + 64) >> 7 : -((-d + 64) >> 7));
        }
    }
    midStream(int(double(m_fifoLen) * m_hostRate / kEngineRate));
    for (int t = 0; t < kTracks; ++t) {
        float pk = m_activity[size_t(t)].load() * 0.97f;
        for (auto v : m_engine->voiceBlock()[size_t(t)]) pk = std::max(pk, std::abs(float(v)) * (1.0f / 8388608.0f));
        m_activity[size_t(t)].store(pk);
        const float level = m_engine->trackPeak(t) * float(m_master->load()) / 100.0f;
        if (level > m_peak[size_t(t)].load()) m_peak[size_t(t)].store(level);
    }
    const auto& out = m_engine->output();
    const float gain = float(m_master->load()) / 100.0f * (1.0f / 8388608.0f);
    for (int c = 0; c < kDac; ++c) {
        float* dst = m_fifo[size_t(c)].data() + m_fifoLen;
        for (int i = 0; i < kBlock; ++i) dst[i] = float(out[size_t(i)][size_t(c)]) * gain;
    }
    const float trackGain = float(m_master->load()) / 100.0f;
    const uint32_t direct = m_engine->direct();
    for (int t = 0; t < kTracks; ++t)
        for (int c = 0; c < 2; ++c) {
            float* dst = m_fifo[size_t(kDac + 2 * t + c)].data() + m_fifoLen;
            if ((direct >> t) & 1) { const float* src = m_engine->trackOut(t, c); for (int i = 0; i < kBlock; ++i) dst[i] = src[i] * trackGain; }
            else std::fill_n(dst, kBlock, 0.0f);
        }
    m_fifoLen += kBlock;
}

// ---------------------------------------------------------------------------------------------- pattern playback

int MdProcessor::overrideMachine(const mnm::mddump::Kit& kit, int t)
{
    const int id = kit.model(t);
    return kMachines[machineIndexOf(id)].id == id ? id : 0;
}

int MdProcessor::kitParam(int t, int p) const
{
    if (const auto* k = m_kitOverride.load()) return juce::jlimit(0, 127, int(k->params[t][p]));
    const int w = m_ctlWrite[size_t(t)][size_t(p)];
    return w >= 0 ? w : trackParam(t, p);
}

bool MdProcessor::bankHasPattern(int slot) const
{
    const juce::SpinLock::ScopedLockType l(m_bankLock);
    return m_bank && slot >= 0 && slot < 128 && m_bank->hasPattern[size_t(slot)];
}

std::shared_ptr<const mnm::mddump::Song> MdProcessor::bankSong(int slot) const
{
    const juce::SpinLock::ScopedLockType l(m_bankLock);
    if (!m_bank || slot < 0 || slot >= 32 || !m_bank->hasSong[size_t(slot)]) return nullptr;
    return std::make_shared<const mnm::mddump::Song>(m_bank->songs[size_t(slot)]);
}

void MdProcessor::setBankSong(int slot, std::shared_ptr<const mnm::mddump::Song> s)
{
    if (slot < 0 || slot >= 32) return;
    std::shared_ptr<const SeqBank> cur;
    { const juce::SpinLock::ScopedLockType l(m_bankLock); cur = m_bank; }
    if (!cur && !s) return;
    auto bank = cur ? std::make_shared<SeqBank>(*cur) : std::make_shared<SeqBank>();
    if (!cur) { bank->patterns.resize(128); bank->kits.resize(64); bank->songs.resize(32); m_bankName = "PLUGIN"; }
    if (s) { bank->songs[size_t(slot)] = *s; bank->songs[size_t(slot)].position = slot; }
    else bank->songs[size_t(slot)] = {};
    bank->hasSong[size_t(slot)] = s && !s->rows.empty();
    m_songEdited.store(true);
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        m_bankOld = std::move(m_bank);
        m_bank = std::move(bank);
    }
    if (!cur) m_bankFresh.store(true);
}

void MdProcessor::editSong(int slot, const std::function<void(mnm::mddump::Song&)>& fn)
{
    if (slot < 0 || slot >= 32) return;
    mnm::mddump::Song s;
    if (const auto cur = bankSong(slot)) s = *cur;
    s.position = slot;
    fn(s);
    setBankSong(slot, std::make_shared<const mnm::mddump::Song>(s));
}

mnm::mddump::Dump MdProcessor::bankDump() const
{
    std::shared_ptr<const SeqBank> bank;
    { const juce::SpinLock::ScopedLockType l(m_bankLock); bank = m_bank; }
    std::vector<uint8_t> bytes;
    if (bank) {
        auto add = [&](const std::vector<uint8_t>& m) { bytes.insert(bytes.end(), m.begin(), m.end()); };
        for (int s = 0; s < 64; ++s) if (bank->hasKit[size_t(s)]) add(mnm::mddump::encodeKit(bank->kits[size_t(s)]));
        for (int s = 0; s < 128; ++s) if (bank->hasPattern[size_t(s)]) add(mnm::mddump::encodePattern(*bank->patterns[size_t(s)]));
        for (int s = 0; s < 32; ++s) if (bank->hasSong[size_t(s)]) add(mnm::mddump::encodeSong(bank->songs[size_t(s)]));
    }
    return mnm::mddump::parseDump(bytes.data(), bytes.size(), "bank");
}

std::shared_ptr<const mnm::mddump::Pattern> MdProcessor::bankPattern(int slot) const
{
    const juce::SpinLock::ScopedLockType l(m_bankLock);
    if (!m_bank || slot < 0 || slot >= 128 || !m_bank->hasPattern[size_t(slot)]) return nullptr;
    return m_bank->patterns[size_t(slot)];
}

void MdProcessor::editPattern(int slot, const std::function<void(mnm::mddump::Pattern&)>& fn)
{
    if (slot < 0 || slot >= 128) return;
    std::shared_ptr<const SeqBank> cur;
    { const juce::SpinLock::ScopedLockType l(m_bankLock); cur = m_bank; }
    auto bank = cur ? std::make_shared<SeqBank>(*cur) : std::make_shared<SeqBank>();
    if (!cur) {
        bank->patterns.resize(128);
        bank->kits.resize(64);
        bank->songs.resize(32);
        m_bankProjectId = {};
        m_bankName = "PLUGIN";
    }
    mnm::mddump::Pattern p;
    if (bank->hasPattern[size_t(slot)]) {
        p = *bank->patterns[size_t(slot)];
    } else {   // a fresh pattern: 16 steps at 1x, the accents / slides / swing of all tracks edited together
        p.position = slot;
        p.length = 16;
        p.kit = uint8_t(juce::jmax(0, m_seqKitSlot.load()));
        p.accentEditAll = p.slideEditAll = p.swingEditAll = 1;
        p.swing = 0xAAAAAAAAAAAAAAAAull;   // steps 2, 4, 6...: the swing trigs every factory pattern has
        p.accentAmount = 64;
        for (auto& row : p.locks) std::fill(std::begin(row), std::end(row), uint8_t(0xFF));
    }
    fn(p);
    p.position = slot;
    p.extended = p.extended || p.length > 32;
    bank->patterns[size_t(slot)] = std::make_shared<const mnm::mddump::Pattern>(p);
    bank->hasPattern[size_t(slot)] = true;
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        m_bankOld = std::move(m_bank);
        m_bank = std::move(bank);
    }
    const int was = m_patternEdited.exchange(slot);
    if (was >= 0 && was != slot) m_patternEdited.store(-2);
    if (!cur) m_bankFresh.store(true);
}

void MdProcessor::setBankPattern(int slot, std::shared_ptr<const mnm::mddump::Pattern> p)
{
    if (slot < 0 || slot >= 128) return;
    std::shared_ptr<const SeqBank> cur;
    { const juce::SpinLock::ScopedLockType l(m_bankLock); cur = m_bank; }
    if (!cur && !p) return;
    auto bank = cur ? std::make_shared<SeqBank>(*cur) : std::make_shared<SeqBank>();
    if (!cur) { bank->patterns.resize(128); bank->kits.resize(64); bank->songs.resize(32); m_bankName = "PLUGIN"; }
    bank->patterns[size_t(slot)] = p;
    bank->hasPattern[size_t(slot)] = p != nullptr;
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        m_bankOld = std::move(m_bank);
        m_bank = std::move(bank);
    }
    const int was = m_patternEdited.exchange(slot);
    if (was >= 0 && was != slot) m_patternEdited.store(-2);
    if (!cur) m_bankFresh.store(true);
}

void MdProcessor::setPatternBank(const juce::String& projectId, const juce::String& name, const mnm::mddump::Dump& dump, int kitSlot)
{
    m_seqKitSlot.store(kitSlot);
    auto bank = std::make_shared<SeqBank>();
    bank->patterns.resize(128);
    bank->kits.resize(64);
    for (const auto& p : dump.patterns)
        if (p.position >= 0 && p.position < 128) { bank->patterns[size_t(p.position)] = std::make_shared<const mnm::mddump::Pattern>(p); bank->hasPattern[size_t(p.position)] = true; }
    for (const auto& k : dump.kits)
        if (k.position >= 0 && k.position < 64) { bank->kits[size_t(k.position)] = k; bank->hasKit[size_t(k.position)] = true; }
    bank->songs.resize(32);
    for (const auto& s : dump.songs)
        if (s.position >= 0 && s.position < 32 && !s.rows.empty()) { bank->songs[size_t(s.position)] = s; bank->hasSong[size_t(s.position)] = true; }
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        m_bankOld = std::move(m_bank);   // the audio thread may still hold it this block
        m_bank = std::move(bank);
    }
    m_bankProjectId = projectId;
    m_bankName = name;
    m_bankGlobals = dump.globals;
    m_bankFresh.store(true);
}

// A trig's locks (MainOS 0x23B342..0x23B7D8, applied at 0x20B0D8): each parameter locked on the step jumps to the lock;
// one its last trig locked and this one does not jumps back to the kit value; every glide of the track ends, and the
// step's slides start. A MIDI / UI trig (s = null) releases the locks as a trig without any. MID and CTR tracks have
// no voice to jump: their values move (midStream / controlMachines read seqParam).
void MdProcessor::trigLocks(int t, const mnm::md::SeqTrig* s, int pos)
{
    const bool ccOut = m_midiOutMode.load() >= 2;
    const auto* ok = m_kitOverride.load();
    const int id = ok ? overrideMachine(*ok, t) : machineIdOf(t);
    const bool voice = !isMidMachine(id) && !isCtrMachine(id);
    auto& locks = m_lockVal[size_t(t)];
    const bool ext = extendedMode();   // CLASSIC: the pattern's locks (and so its slides) are not played
    for (int q = 0; q < 24; ++q) {
        m_glide[size_t(t)][size_t(q)].active = false;
        const int v = s && ext ? s->locks[size_t(q)] : -1;
        if (v >= 0) {
            if (ccOut && locks[size_t(q)] != v) sendCc(t, q, v, pos);
            locks[size_t(q)] = int16_t(v);
            if (voice) m_engine->jumpParam(t, q, uint8_t(v));
        } else if (locks[size_t(q)] >= 0) {
            locks[size_t(q)] = -1;
            if (voice) m_engine->jumpParam(t, q, uint8_t(kitParam(t, q)));
            if (ccOut) sendCc(t, q, kitParam(t, q), pos);
        }
    }
    if (s && ext && s->slideMask)
        for (int q = 0; q < 24; ++q)
            if ((s->slideMask >> q) & 1) {
                const int to = s->slideTo[size_t(q)] >= 0 ? s->slideTo[size_t(q)] : kitParam(t, q);
                m_glide[size_t(t)][size_t(q)].start(s->locks[size_t(q)], to, s->slideClocks[size_t(q)], s->clock);
            }
}

void MdProcessor::seqStop()
{
    m_seqPlayingUi.store(false);
    for (auto& tr : m_lockVal) tr.fill(-1);   // the knobs slew back to the kit
    for (auto& tr : m_glide) for (auto& g : tr) g.active = false;
    m_seqRunning = false;
    m_seqStepUi.store(-1);
}

void MdProcessor::seqStart(Segment& seg, int slot, int start, int end, int64_t steps, uint16_t mutes, double origin,
                           const SeqBank* bank, const std::shared_ptr<const SeqBank>& hold, double atEnginePos)
{
    seg.slot = slot;
    seg.origin = origin;
    seg.steps = steps;
    seg.mutes = mutes;
    // an empty slot (or no bank) plays as an empty pattern, as on the unit: 16 steps at 1x, nothing on them
    static const mnm::mddump::Pattern empty = [] {
        mnm::mddump::Pattern p;
        p.length = 16;
        p.accentEditAll = p.slideEditAll = p.swingEditAll = 1;
        p.swing = 0xAAAAAAAAAAAAAAAAull;
        for (auto& row : p.locks) std::fill(std::begin(row), std::end(row), uint8_t(0xFF));
        return p;
    }();
    seg.valid = slot >= 0 && slot < 128;
    m_seqPatternUi.store(slot);
    if (!seg.valid) { for (auto& u : m_seqTrigsUi) u.store(0); return; }
    const bool real = bank && bank->hasPattern[size_t(slot)];
    const auto& pat = real ? *bank->patterns[size_t(slot)] : empty;
    seg.player.set(pat);
    seg.player.setRange(start, end);
    seg.accentOn = 0x80 + 2 * int(pat.accentAmount);
    m_seqLenUi.store(seg.player.length());
    for (int t = 0; t < kTracks; ++t) m_seqTrigsUi[size_t(t)].store(pat.trigs[t]);
    if (!real || !extendedMode()) return;   // no kit to bring in (CLASSIC: patterns have no kit)
    const int k = pat.kit;
    if (k < 0 || k >= 64 || !bank->hasKit[size_t(k)] || k == m_seqKitSlot.load()) return;
    m_seqKitSlot.store(k);
    m_overrideBank = hold;
    m_kitSwitch = &bank->kits[size_t(k)];
    if (atEnginePos >= 0) m_pending.push_back({-2, atEnginePos, 0});
    else applyKitSwitch();
}

// SONG rows (MdDump SongRow): pattern 0..127 (0xFE LOOP, 0xFF END, 0xFD skipped), -, repeats - 1, the LOOP's target
// row, the muted tracks (16 bits, track 1 = bit 0), the tempo (the host's tempo rules here), start step, end step
// (exclusive). As the OS's song walker (MainOS 0x23D998): a LOOP row jumps back to its target as often as it says
// (0 = forever), a LOOP onto itself is HALT (the song ends), END or the rows running out end it; an empty pattern slot
// plays as an empty pattern.
void MdProcessor::seqNext(double origin, const SeqBank* bank, const std::shared_ptr<const SeqBank>& hold, double atEnginePos, bool song)
{
    if (!song) {   // PATTERN: the queued one, else the chain's next, else the same one on
        int slot = m_seqQueued >= 0 ? m_seqQueued : m_seg.slot;
        if (const int n = juce::jlimit(0, 16, m_chainLen.load()); n > 1 && m_seqQueued < 0 && m_seg.slot >= 0) {
            if (m_chain[size_t(m_chainPos % n)].load() != m_seg.slot)   // find where the playing pattern is in it
                for (int i = 0; i < n; ++i) if (m_chain[size_t(i)].load() == m_seg.slot) { m_chainPos = i; break; }
            m_chainPos = (m_chainPos + 1) % n;
            slot = m_chain[size_t(m_chainPos)].load();
            m_patternSeen = slot; m_ptnPending.store(slot); m_programChange.store(slot); triggerAsyncUpdate();   // the PTN follows
        }
        m_seqQueued = -1;
        seqStart(m_seg, slot, 0, 64, -1, 0, origin, bank, hold, atEnginePos);
        m_seqRowUi.store(-1);
        return;
    }
    const int sl = juce::jlimit(0, 31, int(std::lround(m_songParam->load())));
    const mnm::mddump::Song* s = bank && bank->hasSong[size_t(sl)] ? &bank->songs[size_t(sl)] : nullptr;
    if (const int cue = m_songCue.exchange(-1); cue >= 0) m_cursor.row = cue;   // a cued row plays next
    for (int guard = 0; s && guard < 4096; ++guard) {
        auto& cur = m_cursor;
        if (cur.row < 0 || cur.row >= int(s->rows.size()) || cur.row >= 256) break;
        const auto* r = s->rows[size_t(cur.row)].bytes;
        if (r[0] == 0xFF) break;   // END
        if (r[0] == 0xFE) {        // LOOP
            if (r[3] == cur.row) break;   // HALT
            auto& n = cur.loops[size_t(cur.row)];
            if (r[2] == 0 || n < r[2]) { ++n; cur.row = r[3]; }
            else { n = 0; ++cur.row; }
            continue;
        }
        const int row = cur.row++;
        if (r[0] >= 128) continue;
        seqStart(m_seg, r[0], r[8], r[9], 0, uint16_t((r[4] << 8) | r[5]), origin, bank, hold, atEnginePos);
        m_seg.steps = int64_t(m_seg.player.span()) * (int(r[2]) + 1);
        m_seg.row = row;
        m_seqRowUi.store(row);
        return;
    }
    m_seg.valid = false;   // the song has ended
    m_seg.steps = -1;
    m_seg.row = -1;
    m_seqRowUi.store(-1);
}

int MdProcessor::songPeekPattern(const SeqBank* bank) const
{
    const int sl = juce::jlimit(0, 31, int(std::lround(m_songParam->load())));
    if (!bank || !bank->hasSong[size_t(sl)]) return -1;
    const auto& s = bank->songs[size_t(sl)];
    auto cur = m_cursor;
    for (int guard = 0; guard < 4096; ++guard) {
        if (cur.row < 0 || cur.row >= int(s.rows.size()) || cur.row >= 256) return -1;
        const auto* r = s.rows[size_t(cur.row)].bytes;
        if (r[0] == 0xFF) return -1;
        if (r[0] == 0xFE) {
            if (r[3] == cur.row) return -1;
            auto& n = cur.loops[size_t(cur.row)];
            if (r[2] == 0 || n < r[2]) { ++n; cur.row = r[3]; } else { n = 0; ++cur.row; }
            continue;
        }
        if (r[0] >= 128) { ++cur.row; continue; }
        return r[0];
    }
    return -1;
}

void MdProcessor::applyKitSwitch()
{
    const auto* k = m_kitSwitch;
    if (!k) return;
    m_kitSwitch = nullptr;
    m_kitOverride.store(k);
    for (int t = 0; t < kTracks; ++t) m_engine->setLfoState(t, k->lfos[t]);
    for (auto& tr : m_lockVal) tr.fill(-1);
    for (auto& tr : m_glide) for (auto& g : tr) g.active = false;
    m_snap = true;   // a kit load: every knob at its value
    m_seqKitRequest.store(m_seqKitSlot.load());
    triggerAsyncUpdate();
}

void MdProcessor::seqGenerate(const Segment& seg, double from, double to, double ratio)
{
    if (!seg.valid || to <= from) return;
    const size_t first = m_seqTrigs.size();
    seg.player.trigs(from - seg.origin, to - seg.origin, m_seqTrigs, seg.steps);
    for (size_t i = first; i < m_seqTrigs.size(); ++i) {
        auto& s = m_seqTrigs[i];
        s.clock += seg.origin;
        if (silenced(s.track) || ((seg.mutes >> s.track) & 1)) continue;   // muted: no trig
        if (&seg == &m_seg && m_recSkip[size_t(s.track)] == s.stepIndex) { m_recSkip[size_t(s.track)] = -1; continue; }   // played live already
        const double host = (s.clock - m_seqClock0) / m_seqCps;
        m_pending.push_back({s.track, juce::jmax(0.0, host * ratio), s.accent ? seg.accentOn : -128, int(i)});
    }
}

// Once per host block, before the MIDI: the trigs in this block, timed from the host's position (clock = quarter
// notes x 24). Playback follows the transport; at a jump in the position (a loop, a locate), a start, or a new MODE /
// SONG / bank, it is placed again as if it had played from the start of the timeline: PATTERN = the pattern looping
// from there, SONG = the song's rows walked from there (LOOP counts and all).
void MdProcessor::scheduleSequencer(int n, double ratio)
{
    std::shared_ptr<const SeqBank> bank;
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        bank = m_bank;
    }
    m_seqTrigs.clear();
    const bool song = m_seqMode->load() >= 0.5f;
    const int songSlot = juce::jlimit(0, 31, int(std::lround(m_songParam->load())));
    bool relocate = false;
    if (const int ed = m_patternEdited.exchange(-1); ed != -1 && bank) {   // an edit: the new pattern in place
        for (auto* s : {&m_seg, &m_prevSeg}) {
            if (s->slot < 0 || (ed >= 0 && s->slot != ed) || !bank->hasPattern[size_t(s->slot)]) continue;
            const int start = s->player.rangeStart(), end = start + s->player.span();
            const bool whole = start == 0 && end == s->player.length();
            const auto& pat = *bank->patterns[size_t(s->slot)];
            s->player.set(pat);
            if (!whole) s->player.setRange(start, end);
            s->accentOn = 0x80 + 2 * int(pat.accentAmount);
            s->valid = true;
            if (s == &m_seg) {
                m_seqLenUi.store(s->player.length());
                for (int t = 0; t < kTracks; ++t) m_seqTrigsUi[size_t(t)].store(pat.trigs[t]);
            }
        }
    }
    if (m_bankFresh.exchange(false)) { m_patternSeen = -1; m_seg.slot = -1; relocate = true; }
    if (int(song) != m_modeSeen || songSlot != m_songSeen) { m_modeSeen = int(song); m_songSeen = songSlot; relocate = true; }
    if (m_songEdited.exchange(false) && song && !m_seg.valid) relocate = true;   // a song that had ended: rows added
    if (m_songRelocate.exchange(false) && song) relocate = true;                 // a new start row
    const int want = juce::jlimit(0, 127, int(std::lround(m_patternParam->load())));
    if (m_ptnPending.load() < 0 && want != m_patternSeen) {
        m_patternSeen = want; m_seqQueued = want == m_seg.slot ? -1 : want;
        bool inChain = false;   // a pattern chosen another way ends a chain
        for (int i = 0; i < juce::jlimit(0, 16, m_chainLen.load()); ++i) inChain = inChain || m_chain[size_t(i)].load() == want;
        if (!inChain) m_chainLen.store(0);
    }
    if (!m_hostPlaying) m_seqHalted = false;   // a STOP lasts until the transport stops (or a START / pattern note)
    const bool on = m_seqOn->load() >= 0.5f && !m_seqHalted;   // with no bank, the pattern is empty: the steps still run
    if (!on || !m_hostPlaying) {   // stopped: PATTERN shows the next pattern at once; SONG starts over
        if (m_seqRunning) seqStop();
        m_prevSeg.valid = false;
        if (!song && (m_seqQueued >= 0 || m_seg.slot < 0)) {
            if ((m_pcMode.load() & 2) && m_seqQueued >= 0 && m_seqQueued != m_pcLastSent && pcOutChannel() >= 0) {   // PRG CHANGE OUT: a select while stopped
                midSend(0, uint8_t(0xC0 | pcOutChannel()), uint8_t(m_seqQueued));
                m_pcLastSent = m_seqQueued;
            }
            seqNext(0.0, bank.get(), bank, -1.0, false);
        }
        return;
    }
    const double c0 = m_hostPpq * 24.0;
    const double cps = juce::jmax(1.0, m_seqBpm) * 24.0 / 60.0 / m_hostRate;
    m_seqClock0 = c0;
    m_seqCps = cps;
    const double c1 = c0 + n * cps;
    auto engineAt = [&](double clock) { return juce::jmax(0.0, (clock - c0) / cps * ratio); };
    if (!m_seqRunning || relocate || std::abs(c0 - m_seqExpect) > 0.25) {   // start, a jump, a new mode / song / bank
        if (m_seqRunning) seqStop();
        m_seqRunning = true;
        m_prevSeg.valid = false;
        if (!song) {
            if (m_seqQueued < 0) { const int pend = m_ptnPending.load(); m_seqQueued = pend >= 0 ? pend : want; }
            seqNext(0.0, bank.get(), bank, -1.0, false);
        } else {
            m_cursor = {};
            m_cursor.row = m_songStart.load();   // from the start row (ENTER on a row while stopped)
            seqNext(0.0, bank.get(), bank, -1.0, true);
            for (int guard = 0; guard < 100000 && m_seg.valid && m_seg.steps >= 0 && m_seg.endClock() <= c0; ++guard)
                seqNext(m_seg.endClock(), bank.get(), bank, -1.0, true);
        }
    }
    if (m_ptnJump >= 0 && !song) {   // a GATE / MOMENTARY pattern note: that pattern from its first step, now
        m_prevSeg.valid = false;   // the old pattern stops where it is
        m_seqQueued = m_ptnJump;
        seqNext(c0, bank.get(), bank, 0.0, false);
    }
    m_ptnJump = -1;
    m_seqExpect = c1;
    m_seqPlayingUi.store(true);
    // the segment that ended last: its swung steps' trigs after its end
    if (m_prevSeg.valid) {
        seqGenerate(m_prevSeg, c0, c1, ratio);
        if (c0 > m_prevSeg.endClock() + m_prevSeg.player.swingClocks() + 1.0) m_prevSeg.valid = false;
    }
    for (int guard = 0; guard < 256; ++guard) {
        if (!song) {
            if (m_seqQueued >= 0) {
                if (!m_seg.valid) {   // nothing playing: the new pattern at once, in step with the timeline
                    const double origin = std::floor(c0 / 96.0) * 96.0;
                    seqNext(origin, bank.get(), bank, engineAt(c0), false);
                } else if (m_seg.steps < 0) {   // ends at the end of its pass
                    const double len = m_seg.player.lengthClocks();
                    m_seg.steps = int64_t(std::floor((c0 - m_seg.origin) / len) + 1.0) * m_seg.player.span();
                }
            } else if (m_chainLen.load() > 1) {   // a chain: each pass ends (at least the pass that is starting)
                if (m_seg.valid && m_seg.steps < 0) {
                    const double len = m_seg.player.lengthClocks();
                    m_seg.steps = juce::jmax<int64_t>(1, int64_t(std::floor((c0 - m_seg.origin) / len) + 1.0)) * m_seg.player.span();
                }
            } else if (m_seg.steps >= 0 && m_seg.endClock() > c0) {
                m_seg.steps = -1;   // the queue was taken back: play on
            }
        }
        if (song && !m_seg.valid && m_songCue.load() >= 0) {   // a HALTed / ended song: the cued row starts at the next step
            const double origin = std::ceil(c0 / 6.0) * 6.0;
            if (origin < c1) seqNext(origin, bank.get(), bank, engineAt(origin), true);
        }
        seqGenerate(m_seg, c0, c1, ratio);
        if (m_seg.valid && m_seg.steps >= 0 && m_seg.endClock() < c1) {
            const double end = m_seg.endClock();
            m_prevSeg = m_seg;
            seqNext(end, bank.get(), bank, engineAt(end), song);
            continue;
        }
        break;
    }
    // PRG CHANGE OUT (MainOS 0x23BE6A): at the start of the playing pattern's last step, the next pattern, when it
    // differs from the program last sent
    if ((m_pcMode.load() & 2) && pcOutChannel() >= 0 && m_seg.valid && m_seg.steps >= 0) {
        const double last = m_seg.endClock() - m_seg.player.clocksPerStep();
        if (last >= c0 && last < c1) {
            const int next = song ? songPeekPattern(bank.get()) : (m_seqQueued >= 0 ? m_seqQueued : m_seg.slot);
            if (next >= 0 && next < 128 && next != m_pcLastSent) {
                midSend(juce::jlimit(0, juce::jmax(0, n - 1), int((last - c0) / cps)), uint8_t(0xC0 | pcOutChannel()), uint8_t(next));
                m_pcLastSent = next;
            }
        }
    }
    for (auto& sk : m_recSkip)   // a live-played step's mark ends once its step has gone by
        if (sk >= 0 && (!m_seg.valid || m_seg.origin + double(sk + 1) * m_seg.player.clocksPerStep() < c0)) sk = -1;
    if (m_seg.valid && c1 >= m_seg.origin) {
        int64_t k = int64_t(std::floor((c1 - m_seg.origin) / m_seg.player.clocksPerStep()));
        if (m_seg.steps >= 0 && k >= m_seg.steps) k = m_seg.steps - 1;
        m_seqStepUi.store(m_seg.player.stepOf(k));
    } else {
        m_seqStepUi.store(-1);
    }
}

// ---------------------------------------------------------------------------------------------- MIDI settings

MdProcessor::MidiSettings MdProcessor::defaultMidiSettings()
{
    // (no pattern notes: the factory map puts A01..A16 on the white keys from E3, FROM PROJECT brings it in)
    MidiSettings s;
    s.noteTrack.fill(-1);
    s.noteAction.fill(-1);
    for (int t = 0; t < kTracks; ++t) s.noteTrack[size_t(kTrackNotes[t])] = int8_t(t);
    return s;
}

MdProcessor::MidiSettings MdProcessor::midiSettings() const
{
    MidiSettings s;
    s.baseChannel = m_baseCh.load();
    for (int i = 0; i < 128; ++i) s.noteTrack[size_t(i)] = m_noteTrack[size_t(i)].load();
    s.programChange = m_pcMode.load();
    s.pcChannel = m_pcChannel.load();
    for (int i = 0; i < 128; ++i) s.noteAction[size_t(i)] = m_noteAction[size_t(i)].load();
    s.patternNoteMode = m_ptnNoteMode.load();
    s.ctrlIn = m_ctrlIn.load();
    s.midiOut = m_midiOutMode.load();
    return s;
}

void MdProcessor::setMidiSettings(const MidiSettings& s)
{
    m_baseCh.store(juce::jlimit(-1, 15, s.baseChannel));
    for (int i = 0; i < 128; ++i) m_noteTrack[size_t(i)].store(s.noteTrack[size_t(i)] >= 0 && s.noteTrack[size_t(i)] < kTracks ? s.noteTrack[size_t(i)] : int8_t(-1));
    for (int t = 0; t < kTracks; ++t) {   // the note a track sends: its first in the map
        int note = -1;
        for (int i = 0; i < 128 && note < 0; ++i) if (s.noteTrack[size_t(i)] == t) note = i;
        m_trackNote[size_t(t)].store(int8_t(note));
    }
    m_pcMode.store(juce::jlimit(0, 3, s.programChange));
    m_pcChannel.store(juce::jlimit(0, 16, s.pcChannel));
    m_ptnNoteMode.store(juce::jlimit(0, 2, s.patternNoteMode));
    m_ctrlIn.store(s.ctrlIn);
    for (int i = 0; i < 128; ++i) {
        const int a = s.noteAction[size_t(i)];
        const bool ok = s.noteTrack[size_t(i)] < 0 && ((a >= 0 && a < 128) || a == kStartNote || a == kStopNote);
        m_noteAction[size_t(i)].store(int16_t(ok ? a : -1));
    }
    m_midiOutMode.store(juce::jlimit(0, 2, s.midiOut));
}

MdProcessor::MidiSettings MdProcessor::fromGlobal(const mnm::mddump::Global& g, const MidiSettings& keep)
{
    MidiSettings s = keep;
    s.baseChannel = g.baseChannel < 16 ? g.baseChannel : -1;
    for (int i = 0; i < 128; ++i) s.noteTrack[size_t(i)] = int8_t(g.trackOfNote(i));
    s.programChange = g.programChange & 3;   // bits 0-1 the mode (IN, OUT), bits 2-6 the channel (0 AUTO)
    s.pcChannel = juce::jlimit(0, 16, (g.programChange >> 2) & 31);
    for (int i = 0; i < 128; ++i) {
        const int k = g.keyMap[i];
        s.noteAction[size_t(i)] = int16_t(k >= 16 && k < 16 + 128 ? k - 16 : k == 0x90 ? kStartNote : k == 0x91 ? kStopNote : -1);
    }
    s.patternNoteMode = juce::jlimit(0, 2, int(g.trigMode));
    return s;
}

// The CC map (relative to the base channel): channel + t / 4, CC [16, 40, 72, 96][t % 4] + p; level CC 8 + t % 4
void MdProcessor::sendCc(int t, int p, int value, int pos)
{
    const int base = m_baseCh.load();
    if (base < 0) return;
    const int ch = base + t / 4;
    if (ch > 15) return;
    static const int bases[4] = {16, 40, 72, 96};
    const int cc = p == 24 ? 8 + t % 4 : bases[t % 4] + p;
    midSend(pos, uint8_t(0xB0 | ch), uint8_t(cc), juce::jlimit(0, 127, value));
    m_outSeen[size_t(t)][size_t(p)] = int16_t(value);
}

// Knob turns (and automation) out as CCs; a value that came in as a CC is not sent back
void MdProcessor::scanKnobsOut(int pos)
{
    const bool quiet = m_clock < m_ctlQuietUntil;   // a kit load: not turns
    for (int t = 0; t < kTracks; ++t) {
        if (isMidMachine(machineIdOf(t))) continue;   // a MID track's values are its own MIDI
        for (int p = 0; p <= 24; ++p) {
            const int v = p == 24 ? juce::jlimit(0, 127, int(std::lround(m_tracks[size_t(t)].mix[13]->load()))) : trackParam(t, p);
            auto& seen = m_outSeen[size_t(t)][size_t(p)];
            if (v == seen) continue;
            if (seen < 0 || quiet) { seen = int16_t(v); continue; }
            sendCc(t, p, v, pos);
        }
    }
}

// ---------------------------------------------------------------------------------------------- live recording

void MdProcessor::recordPush(const RecEvent& e)
{
    const auto scope = m_recFifo.write(1);
    if (scope.blockSize1 > 0) m_recBuf[size_t(scope.startIndex1)] = e;
    else if (scope.blockSize2 > 0) m_recBuf[size_t(scope.startIndex2)] = e;
    else return;
    triggerAsyncUpdate();
}

// A trig played while recording (MainOS 0x2379AC): onto the step playing; at 2x, 3/4x and 3/2x a hit late in its step
// goes onto the next one (past 2/3, 5/8 and 1/2 of the step). Recorded trigs are never accented, whatever the velocity.
// A note the key map gives a pattern, START or STOP (MainOS 0x20D104 / 0x20D1B8), by the PATTERN NOTES mode:
// GATE plays the pattern at once and its note-off stops; MOMENTARY plays it at once and its note-off queues the pattern
// that was playing (or stops, if none was); QUEUE makes it the next pattern (at once when stopped).
void MdProcessor::patternNote(int note, bool on)
{
    const int act = m_noteAction[size_t(note)].load();
    const int mode = m_ptnNoteMode.load();
    const bool running = m_seqRunning && !m_seqHalted;
    auto choose = [&](int p) { m_patternSeen = p; m_ptnPending.store(p); m_programChange.store(p); triggerAsyncUpdate(); };
    auto stop = [&] { m_seqHalted = true; m_intPlay.store(false); };
    auto start = [&] { m_seqHalted = false; if (!m_hostPlaying) m_intPlay.store(true); };
    if (!on) {
        if (note != m_ptnHeldNote) return;
        m_ptnHeldNote = -1;
        if (mode == 0) stop();
        else if (mode == 1) {
            if (m_ptnMomentBack < 0) stop();
            else { m_seqQueued = m_ptnMomentBack == m_seg.slot ? -1 : m_ptnMomentBack; choose(m_ptnMomentBack); }
        }
        return;
    }
    if (act == kStartNote) { start(); return; }
    if (act == kStopNote) { stop(); return; }
    if (act < 0 || act > 127) return;
    if (mode == 2) {   // QUEUE
        if (running) { m_seqQueued = act == m_seg.slot ? -1 : act; choose(act); }
        else { m_seqQueued = act; choose(act); start(); }
        return;
    }
    if (m_ptnHeldNote < 0) m_ptnMomentBack = running ? m_seg.slot : -1;   // MOMENTARY: what comes back
    m_ptnHeldNote = note;
    choose(act);
    if (running) m_ptnJump = act;
    else { m_seqQueued = act; start(); }
}

void MdProcessor::setChain(const std::vector<int>& slots)
{
    const int n = juce::jmin(16, int(slots.size()));
    m_chainLen.store(0);
    for (int i = 0; i < n; ++i) m_chain[size_t(i)].store(int8_t(juce::jlimit(0, 127, slots[size_t(i)])));
    m_chainLen.store(n > 1 ? n : 0);
}

std::vector<int> MdProcessor::chain() const
{
    std::vector<int> v;
    for (int i = 0; i < juce::jlimit(0, 16, m_chainLen.load()); ++i) v.push_back(m_chain[size_t(i)].load());
    return v;
}

void MdProcessor::recordTrig(int t, double clock, int velocity)
{
    juce::ignoreUnused(velocity);
    if (!m_recordingUi.load() || !m_seg.valid || m_seg.slot < 0) return;
    const double T = m_seg.player.clocksPerStep();
    const double at = (clock - m_seg.origin) / T;
    int64_t k = int64_t(std::floor(at));
    const double late = T == 3.0 ? 2.0 / 3.0 : T == 8.0 ? 5.0 / 8.0 : T == 4.0 ? 0.5 : 2.0;   // clocks per step 6 3 8 4 = 1x 2x 3/4x 3/2x
    if (at - double(k) >= late - 1e-9) ++k;
    if (k < 0) k = 0;
    if (m_seg.steps >= 0 && k >= m_seg.steps) k = m_seg.steps - 1;
    const int step = m_seg.player.stepOf(k);
    recordPush({0, int8_t(t), 0, int8_t(step), 0, int16_t(m_seg.slot)});
    if (m_seg.origin + double(k) * T > clock) m_recSkip[size_t(t)] = k;   // its step is still to come in this pass: played already
}

void MdProcessor::recordScanKnobs()
{
    if (m_clock < m_ctlQuietUntil) {   // a kit load: not a turn
        for (int t = 0; t < kTracks; ++t) for (int q = 0; q < 24; ++q) m_recSeen[size_t(t)][size_t(q)] = int16_t(trackParam(t, q));
        return;
    }
    for (int t = 0; t < kTracks; ++t)
        for (int q = 0; q < 24; ++q) {
            const int v = trackParam(t, q);
            if (v == m_recSeen[size_t(t)][size_t(q)]) continue;
            m_recSeen[size_t(t)][size_t(q)] = int16_t(v);
            m_recTouched[size_t(t)][size_t(q)] = m_clock;
        }
}

void MdProcessor::recordApply()
{
    std::vector<RecEvent> evs;
    const auto scope = m_recFifo.read(m_recFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) evs.push_back(m_recBuf[size_t(scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) evs.push_back(m_recBuf[size_t(scope.startIndex2 + i)]);
    auto finish = [&] {
        if (m_recSession) { m_recRun.after = bankPattern(m_recRun.slot); m_recDone.push_back(m_recRun); }
        m_recSession = false;
    };
    for (size_t i = 0; i < evs.size();) {
        const int slot = evs[i].slot;
        size_t j = i;
        while (j < evs.size() && evs[j].slot == slot) ++j;
        if (m_recSession && m_recRun.slot != slot) finish();
        if (!m_recSession) { m_recSession = true; m_recRun = {slot, bankPattern(slot), nullptr}; }
        editPattern(slot, [&](mnm::mddump::Pattern& p) {
            for (size_t e = i; e < j; ++e) {
                const auto& ev = evs[e];
                if (ev.kind == 0) {
                    p.trigs[ev.track] |= 1ull << ev.step;
                    if (ev.value) (p.accentEditAll ? p.accent : p.accentPerTrack[ev.track]) |= 1ull << ev.step;
                } else if ((p.trigs[ev.track] >> ev.step) & 1) {
                    p.setLock(ev.track, ev.param, ev.step, ev.value);
                }
            }
        });
        i = j;
    }
    if (m_recEnded.exchange(false)) finish();
}

bool MdProcessor::takeRecordedEdit(RecordedEdit& out)
{
    if (m_recDone.empty()) return false;
    out = m_recDone.front();
    m_recDone.erase(m_recDone.begin());
    return true;
}

// ---------------------------------------------------------------------------------------------- CTR and MID machines

int MdProcessor::trackParam(int t, int p) const
{
    const auto& tr = m_tracks[size_t(t)];
    const std::atomic<float>* a = p < 8 ? tr.knobs[p] : p < 21 ? tr.mix[p - 8] : tr.lfo[5 + (p - 21)];
    return juce::jlimit(0, 127, int(std::lround(a->load())));
}

void MdProcessor::queueSet(int target, int p, int value, bool mirror)
{
    const auto scope = m_ctlFifo.write(1);
    if (scope.blockSize1 > 0) m_ctlBuf[size_t(scope.startIndex1)] = {int8_t(target), int8_t(p), int8_t(value)};
    else if (scope.blockSize2 > 0) m_ctlBuf[size_t(scope.startIndex2)] = {int8_t(target), int8_t(p), int8_t(value)};
    else return;
    m_ctlWrite[size_t(target)][size_t(p)] = int16_t(value);   // in effect from the next pass (the parameter follows)
    m_ctlWriteUntil[size_t(target)] = m_clock + int64_t(m_hostRate);
    if (mirror) {   // the echo is not a turn (a CTR-8P write to a CTR or MID track is: it acts on arrival)
        m_ctlPending[size_t(target)][size_t(p)] = int16_t(value);
        m_ctlPendingUntil[size_t(target)] = m_clock + int64_t(m_hostRate);   // a write that never arrives stops blocking after 1 s
    }
    triggerAsyncUpdate();
}

void MdProcessor::midSend(int pos, uint8_t a, uint8_t b, int c)
{
    const uint8_t bytes[3] = {a, b, uint8_t(juce::jlimit(0, 127, c))};
    m_midiOut.addEvent(bytes, c < 0 ? 2 : 3, juce::jlimit(0, juce::jmax(0, m_blockLen - 1), pos));
}

// The MID trig (MainOS 0x209914) on channel n of MID-n: the track's sounding notes end; PCHG (if set and not the program
// last sent on the channel); NOTE, and N2 / N3 when off centre (NOTE + N - 64), at VEL (0 plays as 1); PB and MW when
// they differ from what the channel last got, AT when not 0. The notes last 3 x (LEN + 1) sequencer ticks (24 per
// quarter note, MainOS 0x209DB2 / 0x23A68C; LEN 0 = 4 ticks), so LEN 1 is one 16th step, LEN 7 a quarter note and
// LEN 127 four bars.
void MdProcessor::midTrig(int t, int pos)
{
    const int ch = machineIdOf(t) - 96;
    auto& notes = m_midNotes[size_t(t)];
    for (const auto& nt : notes) midSend(pos, uint8_t(0x80 | ch), nt.note, 0);
    notes.clear();
    const int pc = midValue(t, 20);
    if (pc > 0 && pc - 1 != m_midLastPc[size_t(ch)]) { midSend(pos, uint8_t(0xC0 | ch), uint8_t(pc - 1)); m_midLastPc[size_t(ch)] = pc - 1; }
    const int note = midValue(t, 0), len = midValue(t, 3);   // the LFOs count (added after the knob, as the OS does)
    const int vel = juce::jmax(1, midValue(t, 4));
    const double bpm = tempo();
    const int ticks = len > 0 ? 3 + 3 * len : 4;
    const int64_t offAt = m_clock + pos + int64_t(std::lround(ticks * 60.0 / (bpm * 24.0) * m_hostRate));
    auto play = [&](int nn) {
        midSend(pos, uint8_t(0x90 | ch), uint8_t(nn), vel);
        notes.push_back({uint8_t(0x90 | ch), uint8_t(nn), offAt});
    };
    play(note);
    for (int k : {1, 2}) {
        const int n = midValue(t, k);
        if (n != 64) play(juce::jlimit(0, 127, note + n - 64));
    }
    const int pb = midValue(t, 5), mw = midValue(t, 6), at = midValue(t, 7);
    if (pb != m_midLastPb[size_t(ch)]) { midSend(pos, uint8_t(0xE0 | ch), 0, pb); m_midLastPb[size_t(ch)] = pb; }
    if (mw != m_midLastMw[size_t(ch)]) { midSend(pos, uint8_t(0xB0 | ch), 1, mw); m_midLastMw[size_t(ch)] = mw; }
    if (at != 0) midSend(pos, uint8_t(0xD0 | ch), uint8_t(at));
}

// After each pass, every MID track's continuous values, knob plus LFO ("LFOs applied to MIDI machines are always added
// after all locks and slides", OS 1.33): PB, MW, AT and the six CC values go out when they change, at the pass's place
// in the host block. A machine change or a load settles them without sending. (PCHG and the CC numbers are not
// streamed: they send on a turn only, in controlMachines.)
void MdProcessor::midStream(int pos)
{
    const bool quiet = !m_ctlStarted || m_clock < m_ctlQuietUntil;
    for (int t = 0; t < kTracks; ++t) {
        const int id = machineIdOf(t);
        if (!isMidMachine(id)) { m_midMachine[size_t(t)] = id; continue; }
        const bool settle = quiet || id != m_midMachine[size_t(t)];
        m_midMachine[size_t(t)] = id;
        const int ch = id - 96;
        for (int p : {5, 6, 7, 9, 11, 13, 15, 17, 19}) {
            const int v = midValue(t, p);
            auto& sent = m_midSent[size_t(t)][size_t(p)];
            if (settle || sent < 0) { sent = int16_t(v); continue; }
            if (v == sent) continue;
            sent = int16_t(v);
            if (p == 5) { midSend(pos, uint8_t(0xE0 | ch), 0, v); m_midLastPb[size_t(ch)] = v; }
            else if (p == 6) { midSend(pos, uint8_t(0xB0 | ch), 1, v); m_midLastMw[size_t(ch)] = v; }
            else if (p == 7) midSend(pos, uint8_t(0xD0 | ch), uint8_t(v));
            else if (const int cc = trackParam(t, p - 1); cc > 0) midSend(pos, uint8_t(0xB0 | ch), uint8_t(cc == 1 ? 0 : cc), v);
        }
    }
}

// Once per host block: every parameter of a CTR / MID track (and the master effects) against the value last seen. A
// change while the machine stays and no load is settling is a turn, as the OS's parameter-change routine sees it:
//   MID     PB / MW / AT / a CC value (on its CC number; 0 = off, 1 = CC 0) / PCHG -> MIDI out now
//   CTR-RE GB EQ DX   SYNTHESIS knob k -> that master effect's parameter k (and the knobs follow the master effect)
//   CTR-AL  parameter p moved by d (its own value counted within 1..126) -> p + d on every other track, except MID
//           and CTR tracks and the RAM recorders' SYNTHESIS knobs (MainOS 0x207E0E)
//   CTR-8P  P1..P8 -> the parameter assigned by its TRK / PAR pair (EFFECTS and ROUTING pages, then the LFO page's
//           last three), unless that track is a CTR-AL or CTR-8P (MainOS 0x207D36)
void MdProcessor::controlMachines(int n)
{
    juce::ignoreUnused(n);
    const bool quiet = !m_ctlStarted || m_clock < m_ctlQuietUntil;
    std::array<int, kTracks> ids{};
    for (int t = 0; t < kTracks; ++t) ids[size_t(t)] = machineIdOf(t);
    auto rebase = [&](int target) {
        for (int p = 0; p < 24; ++p) {
            m_ctlSeen[size_t(target)][size_t(p)] = int16_t(target < kTracks ? seqParam(target, p)
                                                           : p < 8 ? juce::jlimit(0, 127, int(std::lround(m_masterFx[size_t(target - kTracks)][size_t(p)]->load()))) : 0);
            m_ctlPending[size_t(target)][size_t(p)] = -1;
        }
    };
    // what moved since last block (a queued write counts once it has arrived, not before)
    auto moved = [&](int target, int p, int now) {
        auto& pend = m_ctlPending[size_t(target)][size_t(p)];
        auto& seen = m_ctlSeen[size_t(target)][size_t(p)];
        if (pend >= 0) {
            if (now == pend || m_clock > m_ctlPendingUntil[size_t(target)]) { pend = -1; seen = int16_t(now); }
            return false;
        }
        if (now == seen) return false;
        return true;
    };
    for (int t = 0; t < kTracks; ++t) {
        const int id = ids[size_t(t)];
        if (quiet || m_ctlRebase[size_t(t)].exchange(false) || id != m_ctlMachine[size_t(t)]) {
            m_ctlMachine[size_t(t)] = id;
            rebase(t);
            if (!isMidMachine(id))
                for (const auto& nt : m_midNotes[size_t(t)]) midSend(0, uint8_t(nt.status & 0xEF), nt.note, 0);
            if (!isMidMachine(id)) m_midNotes[size_t(t)].clear();
            continue;
        }
        if (!isMidMachine(id) && !isCtrMachine(id)) continue;
        for (int p = 0; p < 24; ++p) {
            const int now = seqParam(t, p);   // a pattern lock acts as a turn of the knob (CTR-AL: see below)
            const bool held = m_lockVal[size_t(t)][size_t(p)] >= 0;
            const bool lockMove = held || m_ctlLockHeld[size_t(t)][size_t(p)];   // a lock / slide, or its release
            m_ctlLockHeld[size_t(t)][size_t(p)] = held;
            if (!moved(t, p, now)) continue;
            // a muted CTR track (its mute or the song row's) applies no locks (MainOS 0x23B19C); knob turns still act
            const bool ctrMuted = isCtrMachine(id) && lockMove && (silenced(t) || (m_seg.valid && ((m_seg.mutes >> t) & 1)));
            const int before = m_ctlSeen[size_t(t)][size_t(p)];
            m_ctlSeen[size_t(t)][size_t(p)] = int16_t(now);
            if (isMidMachine(id)) {
                const int ch = id - 96;
                if (p == 20 && now > 0) { midSend(0, uint8_t(0xC0 | ch), uint8_t(now - 1)); m_midLastPc[size_t(ch)] = now - 1; }
                // PB MW AT and the CC values go out from midStream (knob + LFO)
            } else if (ctrMuted) {
                continue;
            } else if (const int fx = ctrMasterFx(id); fx >= 0) {
                if (p < 8) queueSet(kTracks + fx, p, now, true);
            } else if (id == kCtrAll) {
                // a knob turn moves every track's parameter by as much (MainOS 0x207E3A); a lock, and its release, set
                // them all to the value itself (0x237AE4); a muted CTR-AL's locks do nothing (0x23B17E)
                const int d = juce::jlimit(1, 126, now) - juce::jlimit(1, 126, before);
                if (!lockMove && d == 0) continue;
                for (int u = 0; u < kTracks; ++u) {
                    const int uid = ids[size_t(u)];
                    if (u == t || isMidMachine(uid) || isCtrMachine(uid)) continue;
                    if (p < 8 && (uid == 160 || uid == 161 || uid == 165 || uid == 166)) continue;   // RAM-R1..R4
                    const int cur = m_ctlPending[size_t(u)][size_t(p)] >= 0 ? m_ctlPending[size_t(u)][size_t(p)] : kitParam(u, p);
                    queueSet(u, p, juce::jlimit(0, 127, lockMove ? now : cur + d));
                }
            } else if (id == kCtr8p && p < 8) {
                const int tt = juce::jmin(15, trackParam(t, 8 + 2 * p)), tp = juce::jmin(23, trackParam(t, 9 + 2 * p));
                if (ids[size_t(tt)] == kCtrAll || ids[size_t(tt)] == kCtr8p) continue;
                queueSet(tt, tp, now);
            }
        }
    }
    // the master effects: a CTR-RE / GB / EQ / DX track's knobs show them
    for (int fx = 0; fx < 4; ++fx) {
        const int target = kTracks + fx;
        if (quiet) { rebase(target); continue; }
        for (int k = 0; k < 8; ++k) {
            const int now = juce::jlimit(0, 127, int(std::lround(m_masterFx[size_t(fx)][size_t(k)]->load())));
            if (!moved(target, k, now)) continue;
            m_ctlSeen[size_t(target)][size_t(k)] = int16_t(now);
            for (int t = 0; t < kTracks; ++t)
                if (ctrMasterFx(ids[size_t(t)]) == fx && trackParam(t, k) != now) queueSet(t, k, now, true);
        }
    }
    m_ctlStarted = true;
}

void MdProcessor::handleCc(int channel, int cc, int value)
{
    if (channel < 1 || channel > 4) return;
    auto set = [&](const juce::String& id, float v) {
        if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    if (cc >= 8 && cc <= 11) { const int t = (channel - 1) * 4 + cc - 8; set(levelId(t), float(value)); m_outSeen[size_t(t)][24] = int16_t(value); return; }
    static const int bases[4] = {16, 40, 72, 96};
    for (int i = 0; i < 4; ++i) {
        const int k = cc - bases[i];
        if (k < 0 || k > 23) continue;
        const int t = (channel - 1) * 4 + i;
        m_outSeen[size_t(t)][size_t(k)] = int16_t(value);
        if (k < 8) set(knobId(t, k), float(value));
        else if (k < 16) set(fxId(t, k - 8), float(value));
        else if (k < 21) {
            static juce::String (* const ids[5])(int) = {distId, volId, panId, delId, revId};
            set(ids[k - 16](t), float(value));
        } else {
            set(lfoId(t, 5 + (k - 21)), float(value));   // LFOS LFOD LFOM
        }
        return;
    }
}

void MdProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    m_blockLen = n;
    // side-chain input -> engine-rate FIFO (before the shared buffer is cleared for output)
    if (getBusCount(true) > 0 && getBus(true, 0)->isEnabled() && !m_inFifo[0].empty()) {
        auto in = getBusBuffer(buffer, true, 0);
        const double toEngine = kEngineRate / m_hostRate;
        const int frames = int(std::floor(n * toEngine));
        const int need = m_inLen + frames + 8;
        if (int(m_inFifo[0].size()) < need) for (auto& f : m_inFifo) f.resize(size_t(need));
        for (int c = 0; c < 2 && in.getNumChannels() > 0; ++c) {
            const float* src = in.getReadPointer(std::min(c, in.getNumChannels() - 1));
            float* dst = m_inFifo[size_t(c)].data() + m_inLen;
            if (std::abs(toEngine - 1.0) < 1e-9) std::copy_n(src, frames, dst);
            else m_inInterp[size_t(c)].process(1.0 / toEngine, src, dst, frames, n, 0);
        }
        m_inLen = std::min(m_inLen + frames, int(m_inFifo[0].size()));
        if (m_inLen > 4 * kBlock) {   // keep the input close to real time
            const int drop = m_inLen - 2 * kBlock;
            for (auto& f : m_inFifo) std::copy(f.begin() + drop, f.begin() + m_inLen, f.begin());
            m_inLen -= drop;
        }
    }
    buffer.clear();
    m_hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition()) {
            if (const auto b = pos->getBpm(); b && *b > 0.0) m_hostBpm.store(*b);
            m_hostPlaying = pos->getIsPlaying();
            if (const auto q = pos->getPpqPosition()) m_hostPpq = *q; else m_hostPlaying = false;
        }
    if (!m_ctrlIn.load()) m_hostPlaying = false;   // CTRL IN OFF: Ableton's transport is not ours (its tempo still is)
    m_hostPlayingUi.store(m_hostPlaying);
    m_beatUi.store(float(m_hostPpq - std::floor(m_hostPpq)));
    m_seqBpm = m_hostBpm.load();
    if (m_hostPlaying) {   // the host's transport: PLAY gives way
        m_intPlay.store(false);
        m_intWas = false;
    } else if (m_intPlay.load()) {   // PLAY: a timeline of the plugin's own, from its start
        if (!m_intWas) m_intPpq = 0;
        m_intWas = true;
        m_seqBpm = tempo();
        m_hostPlaying = true;
        m_hostPpq = m_intPpq;
        m_intPpq += buffer.getNumSamples() * m_seqBpm / 60.0 / m_hostRate;
    } else {
        m_intWas = false;
    }
    const juce::ScopedTryLock sl(m_engineLock);
    if (!sl.isLocked() || !m_engineReady) { midi.clear(); mixPreview(buffer); return; }
    const double ratio = kEngineRate / m_hostRate;   // engine frames per host frame
    {   // PER TRACK: the tracks whose own bus is live leave the hardware outputs
        uint32_t mask = 0;
        if (int(std::lround(m_outputMode->load())) == int(OutputMode::Tracks))
            for (int t = 0; t < kTracks; ++t)
                if (kHardwareBuses + t < getBusCount(false) && getBus(false, kHardwareBuses + t)->isEnabled()) mask |= 1u << t;
        m_directMask.store(mask);
    }

    m_lastRatio = ratio;
    scheduleSequencer(n, ratio);   // the pattern's trigs of this block
    {   // live recording: on while armed and the pattern plays (PATTERN mode)
        const bool rec = m_recArmed.load() && m_seqRunning && m_seqMode->load() < 0.5f;
        if (rec && !m_recWas)   // a run starts: the knobs as they are, none moved
            for (int t = 0; t < kTracks; ++t)
                for (int q = 0; q < 24; ++q) { m_recSeen[size_t(t)][size_t(q)] = int16_t(trackParam(t, q)); m_recTouched[size_t(t)][size_t(q)] = std::numeric_limits<int64_t>::min() / 2; }
        if (!rec && m_recWas) { m_recSkip.fill(-1); m_recEnded.store(true); triggerAsyncUpdate(); }
        m_recWas = rec;
        m_recordingUi.store(rec);
        if (rec) recordScanKnobs();
    }
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        const int base = m_baseCh.load();
        if (m.isNoteOn() && m_learnArm.exchange(false)) { m_learned.store(m.getNoteNumber()); continue; }   // MIDI SETTINGS learn
        if (m.isProgramChange()) {   // as on the unit (PRG CHANGE IN, base channel): the next pattern
            const int pcCh = m_pcChannel.load(), ch = m.getChannel() - 1;   // AUTO: the base channel and the next three
            if (!(m_pcMode.load() & 1) || (pcCh > 0 ? ch != pcCh - 1 : (base < 0 || ch < base || ch > base + 3))) continue;
            const int pc = m.getProgramChangeNumber();
            m_seqQueued = pc == m_seg.slot ? -1 : pc;
            m_patternSeen = pc;
            m_ptnPending.store(pc);
            m_programChange.store(pc);
            triggerAsyncUpdate();
        } else if (m.isNoteOff() && base >= 0 && m.getChannel() == base + 1) {   // a pattern note let go
            patternNote(m.getNoteNumber() & 127, false);
        } else if (m.isNoteOn()) {   // a trig: the note map, on the base channel
            if (base < 0 || m.getChannel() != base + 1) continue;
            if (m_noteAction[size_t(m.getNoteNumber() & 127)].load() >= 0) { patternNote(m.getNoteNumber() & 127, true); continue; }
            const int t = m_noteTrack[size_t(m.getNoteNumber() & 127)].load();
            if (t >= 0 && t < kTracks && !silenced(t)) {
                m_pending.push_back({t, meta.samplePosition * ratio, int(m.getVelocity()), -1, false});
                recordTrig(t, m_seqClock0 + meta.samplePosition * m_seqCps, int(m.getVelocity()));
            }
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {   // the host stopping: the MID machines' notes end
            for (auto& notes : m_midNotes) {
                for (const auto& nt : notes) midSend(meta.samplePosition, uint8_t(nt.status & 0xEF), nt.note, 0);
                notes.clear();
            }
        } else if (m.isController()) {   // the CC map spans the base channel and the next three
            if (base >= 0) handleCc(m.getChannel() - base, m.getControllerNumber(), m.getControllerValue());
        }
    }
    midi.clear();
    for (int t = 0; t < kTracks; ++t)
        if (m_audition[size_t(t)].exchange(false)) { m_pending.push_back({t, 0.0, 100}); recordTrig(t, m_seqClock0, 100); }
    controlMachines(n);
    for (auto& p : m_peak) p.store(p.load() * 0.8f);   // the meters' fall-off

    // render passes until the resampler has what it needs; each trig goes into the pass that covers its time
    const int needed = int(std::ceil(n * ratio)) + 4;
    if (int(m_fifo[0].size()) < needed + kBlock)
        for (auto& f : m_fifo) f.resize(size_t(needed + kBlock));
    // A trig as the OS's trig routine does it (MainOS 0x20CC48 / the sequencer): the track's locks (a pattern trig's) or
    // their release, the voice (MID: its notes; CTR: nothing), then the track in its TRIG GROUP with the same accent
    // (MainOS 0x20CE20 / 0x20ADF0: once, the group's own group does not follow; it releases its locks). The mute
    // group side is the engine's (Engine::groupTrig).
    bool ctrTrig = false;
    bool echo = true;   // the trig being fired goes out as MIDI (not a note that came in)
    auto fireOne = [&](int t, double enginePos, int accent, const mnm::md::SeqTrig* s) {
        const auto* ok = m_kitOverride.load();
        const int id = ok ? overrideMachine(*ok, t) : machineIdOf(t);
        const int pos = juce::jlimit(0, juce::jmax(0, n - 1), int(std::lround(enginePos / ratio)));
        trigLocks(t, s, pos);
        if (isMidMachine(id)) { midTrig(t, pos); m_engine->groupTrig(t); }
        else if (isCtrMachine(id)) { m_engine->groupTrig(t); ctrTrig = true; }
        else m_engine->trig(t, id, accent);
        m_activity[size_t(t)].store(1.0f);
        if (m_trigLogOn && m_trigLog.size() < m_trigLog.capacity()) m_trigLog.push_back({t, m_logClock + pos, s ? s->step : -1});
        if (echo && m_midiOutMode.load() >= 1 && m_baseCh.load() >= 0 && !isMidMachine(id)) {
            // MIDI OUT (MainOS 0x23A91C): the trig as its note, velocity 127 on an accented step else 95, and its note-off
            // (a note-on at velocity 0) straight after
            const int note = m_trackNote[size_t(t)].load(), ch = m_baseCh.load();
            if (note >= 0) {
                midSend(pos, uint8_t(0x90 | ch), uint8_t(note), s && s->accent ? 127 : 95);
                midSend(pos, uint8_t(0x90 | ch), uint8_t(note), 0);
            }
        }
    };
    auto fire = [&](const PendingTrig& pt) {
        if (pt.track == -2) {   // a pattern change brings its kit in here
            applyKitSwitch();
            return;
        }
        const mnm::md::SeqTrig* s = pt.seq >= 0 ? &m_seqTrigs[size_t(pt.seq)] : nullptr;
        echo = pt.echo;
        // the accent factor (MainOS 0x20CD76 / 0x20B26C): a pattern trig: 0x80 + 2 x the pattern's ACCENT when accented;
        // a note: VOLUME mode = velocity, ACCENT mode = 0x80 + 2 x ACCENT at velocity >= 112
        const int accent = s ? pt.velocity
                         : int(std::lround(m_velMode->load())) == 0 ? pt.velocity
                         : pt.velocity >= 112 ? 0x80 + 2 * int(std::lround(m_accent->load())) : -128;
        fireOne(pt.track, pt.enginePos, accent, s);
        if (s && m_recordingUi.load() && m_seg.valid && m_seg.slot >= 0) {   // knobs turned during the step before: locked on this trig
            // (MainOS 0x21D842: the turned parameters are locked onto the next step reached if the track trigs there,
            // and forgotten at every step either way)
            const int64_t at = m_clock + int64_t(pt.enginePos / ratio);
            const int64_t step = int64_t(std::ceil(m_seg.player.clocksPerStep() / juce::jmax(1e-12, m_seqCps))) + 1;
            for (int q = 0; q < 24; ++q) {
                const int64_t ago = at - m_recTouched[size_t(pt.track)][size_t(q)];
                if (ago >= 0 && ago <= step)
                    recordPush({1, int8_t(pt.track), int8_t(q), int8_t(s->step), int16_t(kitParam(pt.track, q)), int16_t(m_seg.slot)});
            }
        }
        const int g = trigGroupOf(pt.track);
        if (g >= 0 && g != pt.track && !silenced(g)) fireOne(g, pt.enginePos, accent, nullptr);
    };
    while (m_fifoLen < needed) {
        const double passEnd = double(m_fifoLen + kBlock);
        ctrTrig = false;
        // in time order (a pattern change before the trigs after it)
        std::stable_sort(m_pending.begin(), m_pending.end(), [](const PendingTrig& a, const PendingTrig& b) { return a.enginePos < b.enginePos; });
        size_t done = 0;
        while (done < m_pending.size() && m_pending[done].enginePos < passEnd) fire(m_pending[done++]);
        m_pending.erase(m_pending.begin(), m_pending.begin() + std::ptrdiff_t(done));
        if (ctrTrig) controlMachines(n);   // a CTR track's locks act in this pass
        if (m_seqRunning) {   // the slides step once per clock
            const double clk = m_seqClock0 + double(m_fifoLen) / ratio * m_seqCps;
            for (int t = 0; t < kTracks; ++t)
                for (int q = 0; q < 24; ++q) {
                    auto& gl = m_glide[size_t(t)][size_t(q)];
                    if (gl.active && gl.advance(clk)) m_lockVal[size_t(t)][size_t(q)] = int16_t(gl.target());
                }
        }
        runPass();
    }
    m_logClock += n;

    int used = 0;
    for (int bus = 0; bus < std::min(getBusCount(false), kHardwareBuses + kTracks); ++bus) {
        if (!getBus(false, bus)->isEnabled()) continue;
        auto out = getBusBuffer(buffer, false, bus);
        for (int ch = 0; ch < std::min(2, out.getNumChannels()); ++ch) {
            const int dac = bus < kHardwareBuses ? kBusChannels[bus][ch] : kDac + 2 * (bus - kHardwareBuses) + ch;
            float* dst = out.getWritePointer(ch);
            if (std::abs(ratio - 1.0) < 1e-9) { std::copy_n(m_fifo[size_t(dac)].data(), n, dst); used = n; }
            else used = m_interp[size_t(dac)].process(ratio, m_fifo[size_t(dac)].data(), dst, n);
        }
    }
    if (used == 0) used = int(std::lround(n * ratio));   // (no bus enabled: keep time moving)
    used = std::min(used, m_fifoLen);
    for (auto& f : m_fifo) std::copy(f.begin() + used, f.begin() + m_fifoLen, f.begin());
    m_fifoLen -= used;
    for (auto& p : m_pending) p.enginePos -= used;
    mixPreview(buffer);
    for (auto it = m_outNotes.begin(); it != m_outNotes.end();)   // the trig notes that end in this block
        if (it->offAt < m_clock + n) { midSend(int(juce::jmax<int64_t>(0, it->offAt - m_clock)), uint8_t(0x80 | it->channel), uint8_t(it->note), 0); it = m_outNotes.erase(it); }
        else ++it;
    if (m_midiOutMode.load() >= 2) scanKnobsOut(0);
    // the MID notes that end in this block, then the block's MIDI out
    for (auto& notes : m_midNotes)
        for (auto it = notes.begin(); it != notes.end();)
            if (it->offAt < m_clock + n) { midSend(int(juce::jmax<int64_t>(0, it->offAt - m_clock)), uint8_t(it->status & 0xEF), it->note, 0); it = notes.erase(it); }
            else ++it;
    midi.swapWith(m_midiOut);
    m_midiOut.clear();
    m_clock += n;
}

// A library preview plays over the main output, whether or not the engine does
void MdProcessor::mixPreview(juce::AudioBuffer<float>& buffer)
{
    if (m_previewKey.isEmpty() || getBusCount(false) == 0) return;
    auto main = getBusBuffer(buffer, false, 0);
    if (main.getNumChannels() > 0)
        m_previewVoice.process(main.getWritePointer(0), main.getNumChannels() > 1 ? main.getWritePointer(1) : nullptr, main.getNumSamples(), m_hostRate, false);
}

void MdProcessor::previewPlay(const juce::String& key, const std::function<mnm::mdpreview::Spec()>& build)
{
    if (!m_previewRenderer) m_previewRenderer = std::make_unique<mnm::library::PreviewRenderer>();
    m_previewRenderer->setMdFirmwarePath(m_firmwarePath);
    m_previewRenderer->clearStatus();
    double bpm = tempo();
    if (bpm < 30.0 || bpm > 300.0) bpm = 120.0;
    auto audio = m_previewRenderer->requestMd(key, bpm, build);
    if (!audio) { previewStop(); return; }
    m_previewAudio = audio;
    m_previewRenderer->keep(audio);
    m_previewVoice.start(audio, -1);
    m_previewKey = key;
}

void MdProcessor::previewStop()
{
    m_previewVoice.stop();
    m_previewKey.clear();
}

bool MdProcessor::previewPoll()
{
    if (m_previewKey.isEmpty()) return false;
    if (m_previewVoice.consumeFinished() || (m_previewAudio && m_previewAudio->failed.load()) || !m_previewVoice.active()) { previewStop(); return true; }
    return false;
}

// ---- UW samples ----------------------------------------------------------------------------------------------

bool MdProcessor::isAudioFile(const juce::String& path)
{
    static const juce::StringArray exts = [] {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::StringArray e;
        for (auto w : juce::StringArray::fromTokens(formats.getWildcardForAllFormats(), ";", ""))
            if (w.startsWith("*.")) e.add(w.substring(1).toLowerCase());
        return e;
    }();
    for (const auto& e : exts) if (path.endsWithIgnoreCase(e)) return true;
    return false;
}

int MdProcessor::firstEmptyRomSlot() const
{
    for (int i = 0; i < kNumMachines; ++i)
        if (isRomMachine(kMachines[i].id) && m_samples[size_t(romSlotOf(kMachines[i].id))].data.empty()) return romSlotOf(kMachines[i].id);
    return -1;
}

juce::String MdProcessor::loadSample(int slot, const juce::File& file)
{
    if (slot < 0 || slot >= mnm::md::VoiceEngine::kSlots) return "No such slot";
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader) return "Cannot read " + file.getFileName();
    const auto frames = int(std::min<juce::int64>(reader->lengthInSamples, juce::int64(mnm::md::VoiceEngine::kSampleCapacity)));
    juce::AudioBuffer<float> buf(int(reader->numChannels), frames);
    reader->read(&buf, 0, frames, 0, true, true);
    Sample s;
    s.name = file.getFileNameWithoutExtension();
    s.rate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    s.data.resize(size_t(frames));
    for (int i = 0; i < frames; ++i) {
        float v = 0.0f;
        for (int c = 0; c < buf.getNumChannels(); ++c) v += buf.getSample(c, i);
        s.data[size_t(i)] = v / float(std::max(1, buf.getNumChannels()));
    }
    auto previous = std::move(m_samples[size_t(slot)]);
    m_samples[size_t(slot)] = std::move(s);
    if (!pushSamples()) {
        m_samples[size_t(slot)] = std::move(previous);
        pushSamples();
        return "Not enough sample memory (about 32 seconds in all)";
    }
    return {};
}

juce::String MdProcessor::renameSample(int slot, const juce::String& name)
{
    if (slot < 0 || slot >= mnm::md::VoiceEngine::kSlots || m_samples[size_t(slot)].data.empty()) return "That slot holds no sample";
    const auto n = name.trim().toUpperCase().substring(0, 12);
    if (n.isEmpty()) return "A name, please";
    m_samples[size_t(slot)].name = n;
    return {};
}

double MdProcessor::ramSeconds(int ram) const
{
    const juce::ScopedLock sl(m_engineLock);
    if (!m_engine || ram < 0 || ram > 3) return 0.0;
    return double(m_engine->voices().slotLength(32 + ram)) / 44100.0;   // (the recorders run at 44.1 kHz)
}

juce::String MdProcessor::copyRamToRom(int ram, int slot)
{
    if (ram < 0 || ram > 3 || slot < 0 || slot >= mnm::md::VoiceEngine::kSlots || (slot >= 32 && slot < 48)) return "Not a ROM slot";
    Sample s;
    {
        const juce::ScopedLock sl(m_engineLock);
        if (!m_engine) return "No OS loaded";
        s.data = m_engine->voices().readSlot(32 + ram, &s.rate);
    }
    if (s.data.empty()) return "RAM " + juce::String(ram + 1) + " has no recording";
    s.name = "RAM" + juce::String(ram + 1) + " COPY";
    auto previous = std::move(m_samples[size_t(slot)]);
    m_samples[size_t(slot)] = std::move(s);
    if (!pushSamples()) {
        m_samples[size_t(slot)] = std::move(previous);
        pushSamples();
        return "Not enough sample memory";
    }
    return {};
}

void MdProcessor::clearSample(int slot)
{
    if (slot < 0 || slot >= mnm::md::VoiceEngine::kSlots) return;
    m_samples[size_t(slot)] = {};
    pushSamples();
}

double MdProcessor::sampleMemoryUsed() const
{
    size_t total = 0;
    for (const auto& s : m_samples) total += (s.data.size() + 1) & ~size_t(1);
    return double(total) / double(mnm::md::VoiceEngine::kSampleCapacity);
}

bool MdProcessor::pushSamples()
{
    const juce::ScopedLock sl(m_engineLock);
    if (!m_engine) return true;
    std::array<std::vector<float>, mnm::md::VoiceEngine::kSlots> data;
    std::array<double, mnm::md::VoiceEngine::kSlots> rates{};
    std::array<int, mnm::md::VoiceEngine::kSlots> loops;
    loops.fill(-1);
    for (int i = 0; i < mnm::md::VoiceEngine::kSlots; ++i) { data[size_t(i)] = m_samples[size_t(i)].data; rates[size_t(i)] = m_samples[size_t(i)].rate; }
    return m_engine->voices().setSamples(data, rates, loops);
}

// Samples in the plugin state: 16-bit PCM, base64
juce::ValueTree MdProcessor::samplesToTree() const
{
    juce::ValueTree t("SAMPLES");
    for (int i = 0; i < mnm::md::VoiceEngine::kSlots; ++i) {
        const auto& s = m_samples[size_t(i)];
        if (s.data.empty()) continue;
        juce::MemoryBlock pcm(s.data.size() * 2);
        auto* p = static_cast<int16_t*>(pcm.getData());
        for (size_t k = 0; k < s.data.size(); ++k) p[k] = int16_t(std::lround(juce::jlimit(-1.0f, 1.0f, s.data[k]) * 32767.0f));
        juce::ValueTree c("SAMPLE");
        c.setProperty("slot", i, nullptr);
        c.setProperty("name", s.name, nullptr);
        c.setProperty("rate", s.rate, nullptr);
        c.setProperty("pcm16", pcm.toBase64Encoding(), nullptr);
        t.appendChild(c, nullptr);
    }
    return t;
}

void MdProcessor::samplesFromTree(const juce::ValueTree& t)
{
    for (auto& s : m_samples) s = {};
    for (const auto& c : t) {
        const int slot = c.getProperty("slot", -1);
        if (slot < 0 || slot >= mnm::md::VoiceEngine::kSlots) continue;
        juce::MemoryBlock pcm;
        if (!pcm.fromBase64Encoding(c.getProperty("pcm16").toString())) continue;
        auto& s = m_samples[size_t(slot)];
        s.name = c.getProperty("name").toString();
        s.rate = double(c.getProperty("rate", 44100.0));
        const auto* p = static_cast<const int16_t*>(pcm.getData());
        s.data.resize(pcm.getSize() / 2);
        for (size_t k = 0; k < s.data.size(); ++k) s.data[k] = float(p[k]) / 32768.0f;
    }
    pushSamples();
}

int MdProcessor::applyKit(const mnm::md::Kit& kit)
{
    auto set = [&](const juce::String& id, float v) {
        if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    int emptied = 0;
    for (int t = 0; t < kTracks; ++t) {
        if (m_locked[size_t(t)].load()) continue;   // LOCK: the track keeps its sound
        const int id = int(kit.machines[size_t(t)] & 0xFF);
        int idx = machineIndexOf(id);
        if (kMachines[idx].id != id) { idx = 0; if (id != 0) ++emptied; }
        set(machineId(t), float(idx));
        const auto& p = kit.params[size_t(t)];
        for (int k = 0; k < 8; ++k) set(knobId(t, k), float(p[size_t(k)]));
        for (int k = 0; k < 8; ++k) set(fxId(t, k), float(p[size_t(8 + k)]));
        set(distId(t), float(p[16]));
        set(volId(t), float(p[17]));
        set(panId(t), float(p[18]));
        set(delId(t), float(p[19]));
        set(revId(t), float(p[20]));
        set(levelId(t), float(kit.levels[size_t(t)]));
        const auto& lfo = kit.lfos[size_t(t)];
        set(lfoId(t, 0), float(juce::jlimit(0, 15, int(lfo[0]))));
        set(lfoId(t, 1), float(juce::jlimit(0, 23, int(lfo[1]))));
        set(lfoId(t, 2), float(juce::jlimit(0, 5, int(lfo[2]))));
        set(lfoId(t, 3), float(juce::jlimit(0, 5, int(lfo[3]))));
        set(lfoId(t, 4), float(juce::jlimit(0, 2, int(lfo[4]))));
        set(lfoId(t, 5), float(p[21]));
        set(lfoId(t, 6), float(p[22]));
        set(lfoId(t, 7), float(p[23]));
        m_kitLfos[size_t(t)] = lfo;
        m_kitLfoPending[size_t(t)].store(true);
    }
    m_snap = true;
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) set(masterFxId(fx, k), float(kit.masterFx[size_t(fx)][size_t(k)]));
    for (auto& f : m_machineChanged) f.store(false);   // the kit's knobs, not the machines' defaults
    m_kitName = juce::String(kit.name);
    m_kitKey.clear();
    m_kitSnapshot = mnm::mddump::encodeKit(captureMdKit());
    return emptied;
}

namespace {
// The codec's kit (every byte of the message) and the engine's (what the plugin plays)
mnm::md::Kit engineKit(const mnm::mddump::Kit& k)
{
    mnm::md::Kit e;
    e.position = k.position;
    e.name = k.name;
    for (int t = 0; t < 16; ++t) {
        std::memcpy(e.params[size_t(t)].data(), k.params[t], 24);
        std::memcpy(e.lfos[size_t(t)].data(), k.lfos[t], 36);
        e.levels[size_t(t)] = k.levels[t];
        e.machines[size_t(t)] = k.models[t];
    }
    const uint8_t* fx[4] = {k.reverb, k.delay, k.eq, k.dynamics};
    for (int f = 0; f < 4; ++f) std::memcpy(e.masterFx[size_t(f)].data(), fx[f], 8);
    return e;
}

mnm::mddump::Kit freshKit()   // what an empty kit slot holds: no groups, every LFO on its own track
{
    mnm::mddump::Kit k;
    for (int t = 0; t < 16; ++t) { k.trigGroups[t] = 127; k.muteGroups[t] = 127; k.lfos[t][0] = uint8_t(t); }
    return k;
}
} // namespace

int MdProcessor::trigGroupOf(int t) const
{
    if (const auto* ok = m_kitOverride.load()) return ok->trigGroups[t] < kTracks ? ok->trigGroups[t] : -1;
    return int(std::lround(m_tracks[size_t(t)].trigGroup->load())) - 1;
}

int MdProcessor::muteGroupOf(int t) const
{
    if (const auto* ok = m_kitOverride.load()) return ok->muteGroups[t] < kTracks ? ok->muteGroups[t] : -1;
    return int(std::lround(m_tracks[size_t(t)].muteGroup->load())) - 1;
}

void MdProcessor::setGroupsFromKit(const mnm::mddump::Kit& kit)
{
    for (int t = 0; t < kTracks; ++t) {
        if (m_locked[size_t(t)].load()) continue;   // LOCK: the track keeps its own
        if (auto* p = apvts.getParameter(trigGroupId(t))) p->setValueNotifyingHost(p->convertTo0to1(float(groupParam(kit.trigGroups[t]))));
        if (auto* p = apvts.getParameter(muteGroupId(t))) p->setValueNotifyingHost(p->convertTo0to1(float(groupParam(kit.muteGroups[t]))));
    }
}

mnm::mddump::Kit MdProcessor::captureMdKit() const
{
    auto k = m_baseKit;
    auto val = [this](const juce::String& id) { const auto* v = apvts.getRawParameterValue(id); return v ? int(std::lround(v->load())) : 0; };
    auto u8 = [&](const juce::String& id) { return uint8_t(juce::jlimit(0, 127, val(id))); };
    const auto name = m_kitName.toUpperCase().substring(0, 16);
    if (juce::String(k.name) != name) {   // the name bytes as the hardware writes them
        std::memset(k.nameRaw, 0, sizeof(k.nameRaw));
        std::memcpy(k.nameRaw, name.toRawUTF8(), size_t(name.length()));
        k.name = name.toStdString();
    }
    for (int t = 0; t < kTracks; ++t) {
        const auto id = uint32_t(kMachines[juce::jlimit(0, kNumMachines - 1, val(machineId(t)))].id);
        if ((k.models[t] & 0xFF) != id) k.models[t] = id;   // the same machine keeps its flag bits
        auto* p = k.params[t];
        for (int i = 0; i < 8; ++i) { p[i] = u8(knobId(t, i)); p[8 + i] = u8(fxId(t, i)); }
        p[16] = u8(distId(t)); p[17] = u8(volId(t)); p[18] = u8(panId(t)); p[19] = u8(delId(t)); p[20] = u8(revId(t));
        p[21] = u8(lfoId(t, 5)); p[22] = u8(lfoId(t, 6)); p[23] = u8(lfoId(t, 7));
        k.levels[t] = u8(levelId(t));
        std::memcpy(k.lfos[t], m_kitLfos[size_t(t)].data(), 36);   // bytes 5.. are the OS's own LFO state, as the last kit had it
        for (int i = 0; i < 5; ++i) k.lfos[t][i] = uint8_t(val(lfoId(t, i)));
        k.trigGroups[t] = groupByte(val(trigGroupId(t)));
        k.muteGroups[t] = groupByte(val(muteGroupId(t)));
    }
    uint8_t* fx[4] = {k.reverb, k.delay, k.eq, k.dynamics};
    for (int f = 0; f < 4; ++f)
        for (int i = 0; i < 8; ++i) fx[f][i] = u8(masterFxId(f, i));
    return k;
}

int MdProcessor::loadMdKit(const juce::String& key, const mnm::mddump::Kit& kit, const juce::String& name)
{
    m_baseKit = kit;
    const int emptied = applyKit(engineKit(kit));
    setGroupsFromKit(kit);
    setLoadedKit(key, name);
    for (int t = 0; t < kTracks; ++t)   // each track's sound is now the kit's
        if (!m_locked[size_t(t)].load()) setLoadedSound(t, juce::String(mnm::mdcatalog::Catalog::soundHash(mnm::mdcatalog::Sound::fromKit(kit, t))), {});
    return emptied;
}

void MdProcessor::setLoadedKit(const juce::String& key, const juce::String& name)
{
    m_kitKey = key;
    m_kitName = name;
    m_kitSnapshot = mnm::mddump::encodeKit(captureMdKit());
}

bool MdProcessor::kitModified() const
{
    return !m_kitSnapshot.empty() && mnm::mddump::encodeKit(captureMdKit()) != m_kitSnapshot;
}

mnm::mdcatalog::Sound MdProcessor::captureSound(int t) const
{
    return mnm::mdcatalog::Sound::fromKit(captureMdKit(), t);
}

bool MdProcessor::loadSound(int t, const juce::String& key, const mnm::mdcatalog::Sound& s, const juce::String& name)
{
    const int id = s.machine();
    const int idx = machineIndexOf(id);
    if (kMachines[idx].id != id) return false;
    auto set = [&](const juce::String& pid, float v) { if (auto* p = apvts.getParameter(pid)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
    set(machineId(t), float(idx));
    for (int k = 0; k < 8; ++k) set(knobId(t, k), float(s.params[k]));
    for (int k = 0; k < 8; ++k) set(fxId(t, k), float(s.params[8 + k]));
    set(distId(t), float(s.params[16])); set(volId(t), float(s.params[17])); set(panId(t), float(s.params[18]));
    set(delId(t), float(s.params[19])); set(revId(t), float(s.params[20]));
    std::array<uint8_t, 36> lfo{};
    std::memcpy(lfo.data(), s.lfo, lfo.size());
    if (s.lfoOnSelf()) lfo[0] = uint8_t(t);   // an LFO on its own track follows the sound
    set(lfoId(t, 0), float(juce::jlimit(0, 15, int(lfo[0]))));
    set(lfoId(t, 1), float(juce::jlimit(0, 23, int(lfo[1]))));
    set(lfoId(t, 2), float(juce::jlimit(0, 5, int(lfo[2]))));
    set(lfoId(t, 3), float(juce::jlimit(0, 5, int(lfo[3]))));
    set(lfoId(t, 4), float(juce::jlimit(0, 2, int(lfo[4]))));
    set(lfoId(t, 5), float(s.params[21])); set(lfoId(t, 6), float(s.params[22])); set(lfoId(t, 7), float(s.params[23]));
    m_kitLfos[size_t(t)] = lfo;
    m_kitLfoPending[size_t(t)].store(true);
    m_machineChanged[size_t(t)].store(false);   // the sound's knobs, not the machine's defaults
    setLoadedSound(t, key, name);
    return true;
}

void MdProcessor::setLoadedSound(int t, const juce::String& key, const juce::String& name)
{
    m_sounds[size_t(t)] = {key, name, mnm::mdcatalog::Catalog::soundHash(captureSound(t))};
}

bool MdProcessor::soundModified(int t) const
{
    const auto& s = m_sounds[size_t(t)];
    return s.key.isNotEmpty() && mnm::mdcatalog::Catalog::soundHash(captureSound(t)) != s.hash;
}

void MdProcessor::parameterChanged(const juce::String& id, float)
{
    for (int t = 0; t < kTracks; ++t)
        if (id == machineId(t)) { m_machineChanged[size_t(t)].store(true); m_ctlRebase[size_t(t)].store(true); triggerAsyncUpdate(); }
}

// A machine change brings back that machine's knob values from the last time the track used it, else its defaults (as
// an assignment does on the hardware). Kits and sounds bring their own values (their loads clear the change flag).
void MdProcessor::handleAsyncUpdate()
{
    recordApply();
    if (const int pc = m_programChange.exchange(-1); pc >= 0) {   // a MIDI choice of pattern: the parameter follows, then rules again
        if (auto* p = apvts.getParameter(patternId())) p->setValueNotifyingHost(p->convertTo0to1(float(pc)));
        int expect = pc;
        m_ptnPending.compare_exchange_strong(expect, -1);
    }
    if (const int k = m_seqKitRequest.exchange(-1); k >= 0) {   // a pattern change brought this kit: the knobs take it
        const auto* kit = m_kitOverride.load();
        if (kit) {
            const auto copy = *kit;
            loadMdKit(juce::String(mnm::mdcatalog::Catalog::kitHash(copy)), copy, juce::String(copy.name));
            m_kitOverride.compare_exchange_strong(kit, nullptr);   // unless a later change has its own by now
        }
    }
    {   // the CTR machines' parameter writes
        const auto scope = m_ctlFifo.read(m_ctlFifo.getNumReady());
        auto apply = [&](int start, int count) {
            for (int i = start; i < start + count; ++i) {
                const auto& s = m_ctlBuf[size_t(i)];
                const auto id = s.target < kTracks ? trackParamId(s.target, s.p) : masterFxId(s.target - kTracks, s.p);
                if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(float(s.value)));
            }
        };
        apply(scope.startIndex1, scope.blockSize1);
        apply(scope.startIndex2, scope.blockSize2);
    }
    for (int t = 0; t < kTracks; ++t) {
        const int cur = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_tracks[size_t(t)].machine->load())));
        const bool user = m_machineChanged[size_t(t)].exchange(false);
        const int old = m_shadowIdx[size_t(t)];
        if (cur == old) continue;
        m_shadowIdx[size_t(t)] = cur;
        if (!user) continue;
        for (int k = 0; k < 8; ++k) m_shadow[size_t(t)][size_t(old)][size_t(k)] = uint8_t(juce::jlimit(0, 127, int(std::lround(m_tracks[size_t(t)].knobs[k]->load()))));
        {
            auto ctl = [](int idx) {   // CTR-AL's pages are the audio tracks' parameters (it turns them on every track)
                const int id = kMachines[idx].id;
                return isMidMachine(id) || (isCtrMachine(id) && id != kCtrAll) ? 1 : 0;
            };
            const int from = ctl(old), to = ctl(cur);
            if (from != to) {
                for (int p = 8; p <= 23; ++p) m_pageStore[size_t(t)][size_t(from)][size_t(p - 8)] = uint8_t(trackParam(t, p));
                m_pageStored[size_t(t)][size_t(from)] = true;
                const bool to8p = kMachines[cur].id == kCtr8p;
                for (int p = 8; p <= 23; ++p)
                    if (auto* prm = apvts.getParameter(trackParamId(t, p))) {
                        if (m_pageStored[size_t(t)][size_t(to)]) prm->setValueNotifyingHost(prm->convertTo0to1(float(m_pageStore[size_t(t)][size_t(to)][size_t(p - 8)])));
                        else if (p <= 20 || to8p) prm->setValueNotifyingHost(to ? 0.0f : prm->getDefaultValue());   // MID / CTR: CC numbers OFF, values 0
                    }
            }
        }
        m_visited[size_t(t)][size_t(old)] = true;
        const auto* m = machineInfo(kMachines[cur].id);
        for (int k = 0; k < 8; ++k) {
            const int v = m_visited[size_t(t)][size_t(cur)] ? int(m_shadow[size_t(t)][size_t(cur)][size_t(k)]) : m ? int(m->defaults[size_t(k)]) : -1;
            if (v >= 0) if (auto* p = apvts.getParameter(knobId(t, k))) p->setValueNotifyingHost(p->convertTo0to1(float(v)));
        }
    }
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));   // the knobs are named after the machines
}

juce::ValueTree MdProcessor::shadowsToTree() const
{
    juce::ValueTree shadows("SHADOWS");
    for (int t = 0; t < kTracks; ++t)
        for (int i = 0; i < kNumMachines; ++i) {
            if (!m_visited[size_t(t)][size_t(i)] || i == m_shadowIdx[size_t(t)]) continue;   // the current machine's values are the parameters
            juce::ValueTree s("SHADOW");
            s.setProperty("track", t + 1, nullptr);
            s.setProperty("machine", kMachines[i].id, nullptr);
            juce::StringArray vs;
            for (int k = 0; k < 8; ++k) vs.add(juce::String(int(m_shadow[size_t(t)][size_t(i)][size_t(k)])));
            s.setProperty("values", vs.joinIntoString(","), nullptr);
            shadows.appendChild(s, nullptr);
        }
    return shadows;
}

void MdProcessor::shadowsFromTree(const juce::ValueTree& shadows)
{
    for (auto& v : m_visited) v.fill(false);
    for (int t = 0; t < kTracks; ++t) m_shadowIdx[size_t(t)] = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_tracks[size_t(t)].machine->load())));
    if (!shadows.isValid()) return;
    for (auto s : shadows) {
        const int t = int(s.getProperty("track")) - 1;
        const int i = machineIndexOf(int(s.getProperty("machine")));
        juce::StringArray vs;
        vs.addTokens(s.getProperty("values").toString(), ",", "");
        if (t < 0 || t >= kTracks || kMachines[i].id != int(s.getProperty("machine")) || vs.size() != 8) continue;
        for (int k = 0; k < 8; ++k) m_shadow[size_t(t)][size_t(i)][size_t(k)] = uint8_t(juce::jlimit(0, 127, vs[k].getIntValue()));
        m_visited[size_t(t)][size_t(i)] = true;
    }
}

void MdProcessor::initKit()
{
    static const juce::StringArray globals{masterId(), velModeId(), accentId()};   // the plugin's own settings stay
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p); rp && !globals.contains(rp->getParameterID())) {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost(rp->getDefaultValue());
            rp->endChangeGesture();
        }
    for (int t = 0; t < kTracks; ++t) {   // the knob defaults follow the machines just set back
        for (int k = 0; k < 8; ++k)
            if (auto* p = apvts.getParameter(knobId(t, k))) p->setValueNotifyingHost(p->getDefaultValue());
        m_kitLfos[size_t(t)] = {};
        m_kitLfos[size_t(t)][0] = uint8_t(t);
        m_kitLfoPending[size_t(t)].store(true);
        m_sounds[size_t(t)] = {};
        m_baseKit.trigGroups[t] = 127; m_baseKit.muteGroups[t] = 127;
        for (const auto& id : {trigGroupId(t), muteGroupId(t)}) if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(0.0f);
    }
    for (auto& f : m_machineChanged) f.store(false);
    for (auto& v : m_visited) v.fill(false);
    for (int t = 0; t < kTracks; ++t) m_shadowIdx[size_t(t)] = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_tracks[size_t(t)].machine->load())));
    m_baseKit = {};
    for (int t = 0; t < kTracks; ++t) { m_baseKit.trigGroups[t] = 127; m_baseKit.muteGroups[t] = 127; m_baseKit.lfos[t][0] = uint8_t(t); }
    m_kitName.clear(); m_kitKey.clear(); m_kitSnapshot.clear();
    m_snap = true;
    updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
}

void MdProcessor::clearFirmware()
{
    setFirmwarePath({}, false);
    saveSharedSetting("mdOsPath", {});   // cleared for the other instances too
}

void MdProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("schema", 2, nullptr);
    state.setProperty("firmwarePath", m_firmwarePath, nullptr);
    state.setProperty("kitName", m_kitName, nullptr);
    state.setProperty("kitKey", m_kitKey, nullptr);
    juce::String locked;
    for (int t = 0; t < kTracks; ++t) locked << (m_locked[size_t(t)].load() ? "1" : "0");
    state.setProperty("locked", locked, nullptr);
    state.setProperty("solo", int(m_solo.load()), nullptr);
    state.setProperty("kitSnapshot", juce::String::toHexString(m_kitSnapshot.data(), int(m_kitSnapshot.size()), 0), nullptr);
    const auto base = mnm::mddump::encodeKit(m_baseKit);
    state.setProperty("baseKit", juce::String::toHexString(base.data(), int(base.size()), 0), nullptr);
    juce::StringArray soundKeys, soundNames, soundHashes;
    for (const auto& s : m_sounds) { soundKeys.add(s.key); soundNames.add(s.name); soundHashes.add(juce::String(s.hash)); }
    state.setProperty("soundKeys", soundKeys.joinIntoString("|"), nullptr);
    state.setProperty("soundNames", soundNames.joinIntoString("|"), nullptr);
    state.setProperty("soundHashes", soundHashes.joinIntoString("|"), nullptr);
    {   // the pattern bank: its patterns and kits as sysex
        std::shared_ptr<const SeqBank> bank;
        { const juce::SpinLock::ScopedLockType l(m_bankLock); bank = m_bank; }
        state.removeChild(state.getChildWithName("BANK"), nullptr);
        if (bank) {
            juce::MemoryBlock syx;
            for (int s = 0; s < 64; ++s) if (bank->hasKit[size_t(s)]) { const auto m = mnm::mddump::encodeKit(bank->kits[size_t(s)]); syx.append(m.data(), m.size()); }
            for (int s = 0; s < 128; ++s) if (bank->hasPattern[size_t(s)]) { const auto m = mnm::mddump::encodePattern(*bank->patterns[size_t(s)]); syx.append(m.data(), m.size()); }
            for (int s = 0; s < 32; ++s) if (bank->hasSong[size_t(s)]) { const auto m = mnm::mddump::encodeSong(bank->songs[size_t(s)]); syx.append(m.data(), m.size()); }
            juce::ValueTree b("BANK");
            b.setProperty("project", m_bankProjectId, nullptr);
            b.setProperty("name", m_bankName, nullptr);
            b.setProperty("kitSlot", m_seqKitSlot.load(), nullptr);
            b.setProperty("syx", syx.toBase64Encoding(), nullptr);
            state.appendChild(b, nullptr);
        }
    }
    {   // the MIDI settings
        const auto ms = midiSettings();
        juce::ValueTree g("MIDI");
        g.setProperty("base", ms.baseChannel, nullptr);
        g.setProperty("pc", ms.programChange, nullptr);
        g.setProperty("pcch", ms.pcChannel, nullptr);
        g.setProperty("pnm", ms.patternNoteMode, nullptr);
        g.setProperty("ctrlin", ms.ctrlIn, nullptr);
        juce::String acts;
        for (int i = 0; i < 128; ++i) acts << juce::String::toHexString(int(uint16_t(ms.noteAction[size_t(i)]))).paddedLeft('0', 4);
        g.setProperty("acts", acts, nullptr);
        g.setProperty("out", ms.midiOut, nullptr);
        juce::String map;
        for (int i = 0; i < 128; ++i) map << juce::String::toHexString(int(uint8_t(ms.noteTrack[size_t(i)]))).paddedLeft('0', 2);
        g.setProperty("map", map, nullptr);
        state.removeChild(state.getChildWithName("MIDI"), nullptr);
        state.appendChild(g, nullptr);
    }
    state.removeChild(state.getChildWithName("SAMPLES"), nullptr);
    state.appendChild(samplesToTree(), nullptr);
    state.removeChild(state.getChildWithName("SHADOWS"), nullptr);
    state.appendChild(shadowsToTree(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void MdProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    m_kitName = apvts.state.getProperty("kitName", "").toString();
    m_kitKey = apvts.state.getProperty("kitKey", "").toString();
    const auto locked = apvts.state.getProperty("locked", "").toString();
    for (int t = 0; t < kTracks; ++t) m_locked[size_t(t)].store(locked[t] == '1');
    m_solo.store(uint32_t(int(apvts.state.getProperty("solo", 0))) & 0xFFFFu);
    auto hexBytes = [this](const char* prop) {
        juce::MemoryBlock mb;
        mb.loadFromHexString(apvts.state.getProperty(prop, "").toString());
        return std::vector<uint8_t>(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
    };
    m_kitSnapshot = hexBytes("kitSnapshot");
    const auto base = hexBytes("baseKit");
    if (base.empty() || !mnm::mddump::decodeKit(base.data(), base.size(), m_baseKit)) m_baseKit = freshKit();
    if (!xml->toString().contains(trigGroupId(0).toRawUTF8()))   // a session from before the group parameters: the groups were the base kit's
        for (int t = 0; t < kTracks; ++t) {
            if (auto* p = apvts.getParameter(trigGroupId(t))) p->setValueNotifyingHost(p->convertTo0to1(float(groupParam(m_baseKit.trigGroups[t]))));
            if (auto* p = apvts.getParameter(muteGroupId(t))) p->setValueNotifyingHost(p->convertTo0to1(float(groupParam(m_baseKit.muteGroups[t]))));
        }
    juce::StringArray soundKeys, soundNames, soundHashes;
    soundKeys.addTokens(apvts.state.getProperty("soundKeys", "").toString(), "|", "");
    soundNames.addTokens(apvts.state.getProperty("soundNames", "").toString(), "|", "");
    soundHashes.addTokens(apvts.state.getProperty("soundHashes", "").toString(), "|", "");
    for (int t = 0; t < kTracks; ++t) m_sounds[size_t(t)] = {soundKeys[t], soundNames[t], soundHashes[t].toStdString()};
    samplesFromTree(apvts.state.getChildWithName("SAMPLES"));
    shadowsFromTree(apvts.state.getChildWithName("SHADOWS"));
    if (const auto g = apvts.state.getChildWithName("MIDI"); g.isValid()) {
        auto ms = defaultMidiSettings();
        ms.baseChannel = int(g.getProperty("base", 0));
        ms.programChange = int(g.getProperty("pc", 1));
        ms.pcChannel = int(g.getProperty("pcch", 0));
        ms.patternNoteMode = int(g.getProperty("pnm", 1));
        ms.ctrlIn = bool(g.getProperty("ctrlin", true));
        const auto acts = g.getProperty("acts").toString();
        if (acts.length() == 512)
            for (int i = 0; i < 128; ++i) ms.noteAction[size_t(i)] = int16_t(uint16_t(acts.substring(4 * i, 4 * i + 4).getHexValue32()));
        ms.midiOut = int(g.getProperty("out", 0));
        const auto map = g.getProperty("map").toString();
        if (map.length() == 256)
            for (int i = 0; i < 128; ++i) ms.noteTrack[size_t(i)] = int8_t(uint8_t(map.substring(2 * i, 2 * i + 2).getHexValue32()));
        setMidiSettings(ms);
    }
    if (const auto b = apvts.state.getChildWithName("BANK"); b.isValid()) {
        juce::MemoryBlock syx;
        if (syx.fromBase64Encoding(b.getProperty("syx").toString())) {
            const auto d = mnm::mddump::parseDump(static_cast<const uint8_t*>(syx.getData()), syx.getSize(), "bank");
            setPatternBank(b.getProperty("project").toString(), b.getProperty("name").toString(), d, int(b.getProperty("kitSlot", -1)));
        }
    }
    for (auto& f : m_machineChanged) f.store(false);   // restored knobs stay as saved
    m_snap = true;
    // the session's OS file, when it is there and differs (a session restore does not change the shared setting)
    const auto path = apvts.state.getProperty("firmwarePath", "").toString();
    if (path.isNotEmpty() && path != m_firmwarePath && juce::File(path).existsAsFile()) setFirmwarePath(path, false);
}

juce::AudioProcessorEditor* MdProcessor::createEditor() { return new MdEditor(*this); }

} // namespace mnm::plugin::md

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::md::MdProcessor(); }
