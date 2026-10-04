#include "MdProcessor.h"
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MdEditor.h"
#include "SharedSettings.h"

namespace mnm::plugin::md {

namespace {
using mnm::md::ControlCpu;
using mnm::md::MixEngine;
constexpr double kEngineRate = 44100.0;
constexpr int kBlock = mnm::md::VoiceEngine::kBlockFrames;
// DAC frame offsets of the outputs: A=2 B=5 C=1 D=4 E=0 F=3; the main bus is A/B (where MAIN lands)
constexpr int kBusChannels[3][2] = {{2, 5}, {1, 4}, {0, 3}};
juce::String loadOsPath() { return loadSharedSetting("mdOsPath"); }
}

MdProcessor::MdProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input (INP machines)", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Main A/B", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Out C/D", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Out E/F", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
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
        apvts.addParameterListener(machineId(t), this);
    }
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) m_masterFx[size_t(fx)][size_t(k)] = apvts.getRawParameterValue(masterFxId(fx, k));
    m_master = apvts.getRawParameterValue(masterId());
    m_velMode = apvts.getRawParameterValue(velModeId());
    m_accent = apvts.getRawParameterValue(accentId());
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
    if (persist) saveSharedSetting("mdOsPath", path);
    loadEngine();
}

void MdProcessor::loadEngine()
{
    const juce::ScopedLock sl(m_engineLock);
    m_engineReady = false;
    m_mixer.reset(); m_voices.reset(); m_cpu.reset(); m_fw.reset();
    if (m_firmwarePath.isEmpty()) { m_status = "Select the Machinedrum OS file (Elektron_SPS1-1UW_OS1.63.syx)"; return; }
    try {
        auto fw = std::make_unique<mnm::md::Firmware>(mnm::md::loadFirmware(m_firmwarePath.toStdString()));
        auto cpu = std::make_unique<ControlCpu>(fw->mainOs);
        auto voices = std::make_unique<mnm::md::VoiceEngine>(*fw);
        auto mixer = std::make_unique<MixEngine>(*fw);
        m_fw = std::move(fw); m_cpu = std::move(cpu); m_voices = std::move(voices); m_mixer = std::move(mixer);
        for (auto& tr : m_tracks) { tr.sentMachine = -1; tr.sentRoute = -1; }
        m_snap = true;
        m_status = "OS loaded: " + juce::File(m_firmwarePath).getFileName();
        {   // the UW samples (inline: the engine lock is already held)
            std::array<std::vector<float>, mnm::md::VoiceEngine::kSlots> data;
            std::array<double, mnm::md::VoiceEngine::kSlots> rates{};
            std::array<int, mnm::md::VoiceEngine::kSlots> loops;
            loops.fill(-1);
            for (int i = 0; i < mnm::md::VoiceEngine::kSlots; ++i) { data[size_t(i)] = m_samples[size_t(i)].data; rates[size_t(i)] = m_samples[size_t(i)].rate; }
            m_voices->setSamples(data, rates, loops);
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
    m_pending.reserve(256);
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
    auto val = [](std::atomic<float>* p, float offset = 0.0f) { return uint8_t(juce::jlimit(0, 127, int(std::lround(p->load() + offset)))); };
    ControlCpu::TrackParams params{};
    std::array<uint8_t, 16> levels{};
    std::array<std::array<uint8_t, 8>, 4> master{};
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        auto& p = params[size_t(t)];
        for (int k = 0; k < 8; ++k) p[size_t(k)] = val(tr.knobs[k]);
        for (int k = 0; k < 8; ++k) p[size_t(8 + k)] = val(tr.mix[k]);
        p[16] = val(tr.mix[8]); p[17] = val(tr.mix[9]); p[18] = val(tr.mix[10], 64.0f); p[19] = val(tr.mix[11]); p[20] = val(tr.mix[12]);
        p[21] = val(tr.lfo[5]); p[22] = val(tr.lfo[6]); p[23] = val(tr.lfo[7]);
        levels[size_t(t)] = val(tr.mix[13]);
        const uint8_t cfg[5] = {uint8_t(val(tr.lfo[0])), uint8_t(val(tr.lfo[1])), uint8_t(val(tr.lfo[2])), uint8_t(val(tr.lfo[3])), uint8_t(val(tr.lfo[4]))};
        if (m_kitLfoPending[size_t(t)].exchange(false)) m_cpu->setLfo(t, m_kitLfos[size_t(t)].data());
        m_cpu->setLfoConfig(t, cfg);
    }
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) master[size_t(fx)][size_t(k)] = val(m_masterFx[size_t(fx)][size_t(k)]);
    m_cpu->setTargets(params, levels, master);
    const bool snap = m_snap.exchange(false);
    if (snap) m_cpu->snapToTargets();
    const double bpm = m_hostBpm.load();
    m_cpu->setTempo(bpm);
    m_cpu->tick((m_blockCount++ & 3) == 0);   // the LFOs advance every 4th block, as on the hardware

    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        // synthesis: the machine's control handler on the live raw words
        const int id = machineIdOf(t);
        std::array<int, 8> syn{};
        for (int k = 0; k < 8; ++k) syn[size_t(k)] = m_cpu->liveParam(t, k);
        if (snap || id != tr.sentMachine || syn != tr.synSent) {
            tr.sentMachine = id; tr.synSent = syn;
            if (const auto* m = m_fw->byId(id)) {
                std::array<uint16_t, 8> raw{};
                for (int k = 0; k < 8; ++k) raw[size_t(k)] = uint16_t(syn[size_t(k)]);
                std::array<uint32_t, ControlCpu::kMaxPacket> packet{};
                const int n = m_cpu->convert(m->handler, m->dspType(), raw, packet);
                if (n > 0) m_voices->setPacket(t, packet.data(), n);
            }
        }
        // DSP1: track effects (AMD..SRR, DIST) and routing (level, VOL, PAN, sends)
        std::array<int, kMixRaw> mix{};
        for (int k = 0; k < 9; ++k) mix[size_t(k)] = m_cpu->liveParam(t, 8 + k);
        for (int k = 9; k < 13; ++k) mix[size_t(k)] = m_cpu->liveParam(t, 8 + k);   // VOL PAN DEL REV = params 17..20
        mix[13] = m_cpu->liveLevel(t);
        const int route = juce::jlimit(0, kNumRoutes - 1, int(std::lround(tr.route->load())));
        if (snap || mix != tr.mixSent || route != tr.sentRoute || tr.accent != tr.sentAccent) {
            std::array<uint16_t, 9> fx{};
            for (int k = 0; k < 9; ++k) fx[size_t(k)] = uint16_t(mix[size_t(k)]);
            m_mixer->setTrackFx(t, fx);
            m_mixer->setRouting(t, MixEngine::routingWords(uint32_t(mix[13]), uint32_t(mix[9]), uint32_t(mix[10]), uint32_t(mix[12]), uint32_t(mix[11]), route, tr.accent));
            tr.mixSent = mix; tr.sentRoute = route; tr.sentAccent = tr.accent;
        }
    }
    // master effects (the delay also follows the tempo)
    for (int fx = 0; fx < ControlCpu::kNumMasterFx; ++fx) {
        const auto id = ControlCpu::MasterFx(fx);
        std::array<int, 8> raw{};
        for (int k = 0; k < 8; ++k) raw[size_t(k)] = m_cpu->liveLevel(16 + 8 * fx + k);
        const bool tempoChanged = id == ControlCpu::MasterFx::Delay && std::abs(bpm - m_tempoSent) > 0.01;
        if (!snap && !tempoChanged && raw == m_masterSent[size_t(fx)]) continue;
        m_masterSent[size_t(fx)] = raw;
        std::array<uint32_t, 16> words{};
        if (m_cpu->convertMasterFxFromTick(id, words)) {
            const auto& s = ControlCpu::masterFxSection(id);
            m_mixer->setY(s.dspAddr, words.data(), s.words);
        }
    }
    m_tempoSent = bpm;
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
    m_voices->setInput(m_inBlock.data());
    m_voices->renderPass(m_block);
    for (int t = 0; t < kTracks; ++t) {
        float pk = m_activity[size_t(t)].load() * 0.97f;
        for (auto v : m_block[size_t(t)]) pk = std::max(pk, std::abs(float(v)) * (1.0f / 8388608.0f));
        m_activity[size_t(t)].store(pk);
    }
    m_mixer->renderBlock(m_block, m_out);
    m_mixer->masterReturn(m_masterReturn.data());   // the main mix for the RAM recorders, next block
    m_voices->setMasterReturn(m_masterReturn.data());
    const float gain = float(m_master->load()) / 100.0f * (1.0f / 8388608.0f);
    for (int c = 0; c < kDac; ++c) {
        float* dst = m_fifo[size_t(c)].data() + m_fifoLen;
        for (int i = 0; i < kBlock; ++i) dst[i] = float(m_out[size_t(i)][size_t(c)]) * gain;
    }
    m_fifoLen += kBlock;
}

void MdProcessor::handleCc(int channel, int cc, int value)
{
    if (channel < 1 || channel > 4) return;
    auto set = [&](const juce::String& id, float v) {
        if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    if (cc >= 8 && cc <= 11) { set(levelId((channel - 1) * 4 + cc - 8), float(value)); return; }
    static const int bases[4] = {16, 40, 72, 96};
    for (int i = 0; i < 4; ++i) {
        const int k = cc - bases[i];
        if (k < 0 || k > 23) continue;
        const int t = (channel - 1) * 4 + i;
        if (k < 8) set(knobId(t, k), float(value));
        else if (k < 16) set(fxId(t, k - 8), float(value));
        else if (k < 21) {
            static juce::String (* const ids[5])(int) = {distId, volId, panId, delId, revId};
            set(ids[k - 16](t), k == 18 ? float(value - 64) : float(value));
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
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto b = pos->getBpm(); b && *b > 0.0) m_hostBpm.store(*b);
    const juce::ScopedTryLock sl(m_engineLock);
    if (!sl.isLocked() || !m_engineReady) { midi.clear(); return; }
    const double ratio = kEngineRate / m_hostRate;   // engine frames per host frame

    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) {
            for (int t = 0; t < kTracks; ++t)
                if (kTrackNotes[t] == m.getNoteNumber()) m_pending.push_back({t, meta.samplePosition * ratio, int(m.getVelocity())});
        } else if (m.isController()) {
            handleCc(m.getChannel(), m.getControllerNumber(), m.getControllerValue());
        }
    }
    midi.clear();
    for (int t = 0; t < kTracks; ++t)
        if (m_audition[size_t(t)].exchange(false)) m_pending.push_back({t, 0.0, 100});

    // render passes until the resampler has what it needs; each trig goes into the pass that covers its time
    const int needed = int(std::ceil(n * ratio)) + 4;
    if (int(m_fifo[0].size()) < needed + kBlock)
        for (auto& f : m_fifo) f.resize(size_t(needed + kBlock));
    while (m_fifoLen < needed) {
        const double passEnd = double(m_fifoLen + kBlock);
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->enginePos < passEnd) {
                m_voices->trig(it->track, machineIdOf(it->track) + 1);
                m_cpu->lfoTrig(it->track);
                {   // the trig's accent factor (MainOS 0x20CD76): VOLUME mode = velocity; ACCENT mode = 0x80, accented at >= 112
                    auto& tr = m_tracks[size_t(it->track)];
                    if (int(std::lround(m_velMode->load())) == 0) tr.accent = it->velocity;
                    else tr.accent = it->velocity >= 112 ? 0x80 + 2 * int(std::lround(m_accent->load())) : -128;
                }
                m_activity[size_t(it->track)].store(1.0f);
                it = m_pending.erase(it);
            } else ++it;
        }
        runPass();
    }

    int used = 0;
    for (int bus = 0; bus < 3; ++bus) {
        if (bus >= getBusCount(false) || !getBus(false, bus)->isEnabled()) continue;
        auto out = getBusBuffer(buffer, false, bus);
        for (int ch = 0; ch < std::min(2, out.getNumChannels()); ++ch) {
            const int dac = kBusChannels[bus][ch];
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
}

// ---- UW samples ----------------------------------------------------------------------------------------------

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
    if (!m_voices) return true;
    std::array<std::vector<float>, mnm::md::VoiceEngine::kSlots> data;
    std::array<double, mnm::md::VoiceEngine::kSlots> rates{};
    std::array<int, mnm::md::VoiceEngine::kSlots> loops;
    loops.fill(-1);
    for (int i = 0; i < mnm::md::VoiceEngine::kSlots; ++i) { data[size_t(i)] = m_samples[size_t(i)].data; rates[size_t(i)] = m_samples[size_t(i)].rate; }
    return m_voices->setSamples(data, rates, loops);
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
        const int id = int(kit.machines[size_t(t)]);
        int idx = machineIndexOf(id);
        if (kMachines[idx].id != id) { idx = 0; if (id != 0) ++emptied; }
        set(machineId(t), float(idx));
        const auto& p = kit.params[size_t(t)];
        for (int k = 0; k < 8; ++k) set(knobId(t, k), float(p[size_t(k)]));
        for (int k = 0; k < 8; ++k) set(fxId(t, k), float(p[size_t(8 + k)]));
        set(distId(t), float(p[16]));
        set(volId(t), float(p[17]));
        set(panId(t), float(p[18]) - 64.0f);
        set(delId(t), float(p[19]));
        set(revId(t), float(p[20]));
        set(levelId(t), float(kit.levels[size_t(t)]));
        set(routeId(t), float(kNumRoutes - 1));
        const auto& lfo = kit.lfos[size_t(t)];
        set(lfoId(t, 0), float(juce::jlimit(0, 15, int(lfo[0]))));
        set(lfoId(t, 1), float(juce::jlimit(0, 23, int(lfo[1]))));
        set(lfoId(t, 2), float(juce::jlimit(0, 7, int(lfo[2]))));
        set(lfoId(t, 3), float(juce::jlimit(0, 7, int(lfo[3]))));
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
    return emptied;
}

void MdProcessor::parameterChanged(const juce::String& id, float)
{
    for (int t = 0; t < kTracks; ++t)
        if (id == machineId(t)) { m_machineChanged[size_t(t)].store(true); triggerAsyncUpdate(); }
}

// A machine change loads that machine's knob defaults, as an assignment does on the hardware
void MdProcessor::handleAsyncUpdate()
{
    for (int t = 0; t < kTracks; ++t) {
        if (!m_machineChanged[size_t(t)].exchange(false)) continue;
        const auto* m = machineInfo(machineIdOf(t));
        if (!m) continue;
        for (int k = 0; k < 8; ++k)
            if (auto* p = apvts.getParameter(knobId(t, k))) p->setValueNotifyingHost(p->convertTo0to1(float(m->defaults[size_t(k)])));
    }
}

void MdProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("schema", 2, nullptr);
    state.setProperty("kitName", m_kitName, nullptr);
    state.removeChild(state.getChildWithName("SAMPLES"), nullptr);
    state.appendChild(samplesToTree(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void MdProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    m_kitName = apvts.state.getProperty("kitName", "").toString();
    samplesFromTree(apvts.state.getChildWithName("SAMPLES"));
    for (auto& f : m_machineChanged) f.store(false);   // restored knobs stay as saved
    m_snap = true;
}

juce::AudioProcessorEditor* MdProcessor::createEditor() { return new MdEditor(*this); }

} // namespace mnm::plugin::md

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::md::MdProcessor(); }
