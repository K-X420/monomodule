// Monomodule MD's processor: the engine, the audio block, the parameters, previews
#include "MdProcessorInternal.h"
#include <cmath>
#include "MdEditor.h"
#include "MdMachineText.h"

namespace mnm::plugin::md {

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
    m_extras = apvts.getRawParameterValue(extrasId());
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
        if (!(s && s->retrig)) trigLocks(t, s, pos);   // (a retrig's later hits: the locks are set already)
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

void MdProcessor::clearFirmware()
{
    setFirmwarePath({}, false);
    saveSharedSetting("mdOsPath", {});   // cleared for the other instances too
}

juce::AudioProcessorEditor* MdProcessor::createEditor() { return new MdEditor(*this); }

} // namespace mnm::plugin::md

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::md::MdProcessor(); }
