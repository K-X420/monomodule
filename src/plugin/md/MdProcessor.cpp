#include "MdProcessor.h"
#include <cmath>
#include "MdEditor.h"
#include "SharedSettings.h"

namespace mnm::plugin::md {

namespace {
using mnm::md::ControlCpu;
using mnm::md::MixEngine;
constexpr double kEngineRate = 44100.0;
constexpr int kBlock = mnm::md::VoiceEngine::kBlockFrames;
constexpr int kSlewStep = 384;   // raw units per 32-frame block (3 knob steps): a full sweep in ~31 ms
// DAC frame offsets of the outputs: A=2 B=5 C=1 D=4 E=0 F=3; the main bus is A/B (where MAIN lands)
constexpr int kBusChannels[3][2] = {{2, 5}, {1, 4}, {0, 3}};
juce::String loadOsPath() { return loadSharedSetting("mdOsPath"); }
int toRaw(float v) { return int(std::lround(juce::jlimit(0.0f, 127.0f, v))) << 7; }
int slew(int cur, int target) { return cur < target ? std::min(target, cur + kSlewStep) : std::max(target, cur - kSlewStep); }
}

MdProcessor::MdProcessor()
    : AudioProcessor(BusesProperties()
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
        apvts.addParameterListener(machineId(t), this);
    }
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) m_masterFx[size_t(fx)][size_t(k)] = apvts.getRawParameterValue(masterFxId(fx, k));
    m_master = apvts.getRawParameterValue(masterId());
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
        for (auto& tr : m_tracks) { tr.sentMachine = -1; tr.sentRoute = -1; tr.snap = true; }
        m_masterSnap = true;
        m_status = "OS loaded: " + juce::File(m_firmwarePath).getFileName();
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
    m_pending.clear();
    m_pending.reserve(256);
}

bool MdProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int b = 1; b < layouts.outputBuses.size(); ++b) {
        const auto& s = layouts.outputBuses.getReference(b);
        if (!s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void MdProcessor::refreshParameters()
{
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        // synthesis: the machine's control handler on the slewed raw words
        const int id = machineIdOf(t);
        const bool machineChanged = id != tr.sentMachine;
        for (int k = 0; k < 8; ++k) {
            const int target = toRaw(tr.knobs[k]->load());
            tr.synRaw[size_t(k)] = (tr.snap || machineChanged) ? target : slew(tr.synRaw[size_t(k)], target);
        }
        if (machineChanged || tr.synRaw != tr.synSent) {
            tr.sentMachine = id; tr.synSent = tr.synRaw;
            if (const auto* m = m_fw->byId(id)) {
                std::array<uint16_t, 8> raw{};
                for (int k = 0; k < 8; ++k) raw[size_t(k)] = uint16_t(tr.synRaw[size_t(k)]);
                std::array<uint32_t, ControlCpu::kMaxPacket> packet{};
                const int n = m_cpu->convert(m->handler, m->dspType(), raw, packet);
                if (n > 0) m_voices->setPacket(t, packet.data(), n);
            }
        }
        // effects and routing on DSP1
        for (int k = 0; k < kMixRaw; ++k) {
            const float v = tr.mix[k]->load() + (k == 10 ? 64.0f : 0.0f);   // PAN -64..63 -> 0..127
            const int target = toRaw(v);
            tr.mixRaw[size_t(k)] = tr.snap ? target : slew(tr.mixRaw[size_t(k)], target);
        }
        const int route = juce::jlimit(0, kNumRoutes - 1, int(std::lround(tr.route->load())));
        if (tr.snap || tr.mixRaw != tr.mixSent || route != tr.sentRoute) {
            std::array<uint16_t, 9> fx{};
            for (int k = 0; k < 9; ++k) fx[size_t(k)] = uint16_t(tr.mixRaw[size_t(k)]);
            m_mixer->setTrackFx(t, fx);
            const auto& r = tr.mixRaw;
            m_mixer->setRouting(t, MixEngine::routingWords(uint32_t(r[13]), uint32_t(r[9]), uint32_t(r[10]), uint32_t(r[12]), uint32_t(r[11]), route));
            tr.mixSent = tr.mixRaw; tr.sentRoute = route;
        }
        tr.snap = false;
    }
    // master effects (the delay also follows the tempo)
    const double bpm = m_hostBpm.load();
    m_cpu->setTempo(bpm);
    for (int fx = 0; fx < ControlCpu::kNumMasterFx; ++fx) {
        std::array<int, 8> v{};
        for (int k = 0; k < 8; ++k) v[size_t(k)] = int(std::lround(m_masterFx[size_t(fx)][size_t(k)]->load()));
        const bool tempoChanged = fx == int(ControlCpu::MasterFx::Delay) && std::abs(bpm - m_tempoSent) > 0.01;
        if (!m_masterSnap && !tempoChanged && v == m_masterSent[size_t(fx)]) continue;
        m_masterSent[size_t(fx)] = v;
        std::array<uint16_t, 8> raw{};
        for (int k = 0; k < 8; ++k) raw[size_t(k)] = ControlCpu::rawFromValue(v[size_t(k)]);
        std::array<uint32_t, 16> words{};
        const auto id = ControlCpu::MasterFx(fx);
        if (m_cpu->convertMasterFx(id, raw, words)) {
            const auto& s = ControlCpu::masterFxSection(id);
            m_mixer->setY(s.dspAddr, words.data(), s.words);
        }
    }
    m_tempoSent = bpm;
    m_masterSnap = false;
}

void MdProcessor::runPass()
{
    refreshParameters();
    m_voices->renderPass(m_block);
    for (int t = 0; t < kTracks; ++t) {
        float pk = m_activity[size_t(t)].load() * 0.97f;
        for (auto v : m_block[size_t(t)]) pk = std::max(pk, std::abs(float(v)) * (1.0f / 8388608.0f));
        m_activity[size_t(t)].store(pk);
    }
    m_mixer->renderBlock(m_block, m_out);
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
        if (k < 0 || k > 20) continue;
        const int t = (channel - 1) * 4 + i;
        if (k < 8) set(knobId(t, k), float(value));
        else if (k < 16) set(fxId(t, k - 8), float(value));
        else {
            static juce::String (* const ids[5])(int) = {distId, volId, panId, delId, revId};
            set(ids[k - 16](t), k == 18 ? float(value - 64) : float(value));
        }
        return;
    }
}

void MdProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
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
                if (kTrackNotes[t] == m.getNoteNumber()) m_pending.push_back({t, meta.samplePosition * ratio});
        } else if (m.isController()) {
            handleCc(m.getChannel(), m.getControllerNumber(), m.getControllerValue());
        }
    }
    midi.clear();
    for (int t = 0; t < kTracks; ++t)
        if (m_audition[size_t(t)].exchange(false)) m_pending.push_back({t, 0.0});

    // render passes until the resampler has what it needs; each trig goes into the pass that covers its time
    const int needed = int(std::ceil(n * ratio)) + 4;
    if (int(m_fifo[0].size()) < needed + kBlock)
        for (auto& f : m_fifo) f.resize(size_t(needed + kBlock));
    while (m_fifoLen < needed) {
        const double passEnd = double(m_fifoLen + kBlock);
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->enginePos < passEnd) {
                m_voices->trig(it->track, machineIdOf(it->track) + 1);
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
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void MdProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    for (auto& f : m_machineChanged) f.store(false);   // restored knobs stay as saved
    for (auto& tr : m_tracks) tr.snap = true;
    m_masterSnap = true;
}

juce::AudioProcessorEditor* MdProcessor::createEditor() { return new MdEditor(*this); }

} // namespace mnm::plugin::md

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::md::MdProcessor(); }
