#include "MdProcessor.h"
#include <cmath>
#include "MdEditor.h"
#include "SharedSettings.h"

namespace mnm::plugin::md {

namespace {
constexpr double kEngineRate = 44100.0;
constexpr int kBlock = mnm::md::VoiceEngine::kBlockFrames;
constexpr float kHeadroom = 0.5f;   // raw DSP2 voices reach full scale; the hardware's DSP1 scales them
juce::String loadOsPath() { return loadSharedSetting("mdOsPath"); }
}

MdProcessor::MdProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        tr.machine = apvts.getRawParameterValue(machineId(t));
        for (int k = 0; k < 8; ++k) tr.knobs[k] = apvts.getRawParameterValue(knobId(t, k));
        tr.level = apvts.getRawParameterValue(levelId(t));
        tr.pan = apvts.getRawParameterValue(panId(t));
        apvts.addParameterListener(machineId(t), this);
    }
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
    m_engine.reset(); m_cpu.reset(); m_fw.reset();
    if (m_firmwarePath.isEmpty()) { m_status = "Select the Machinedrum OS file (Elektron_SPS1-1UW_OS1.63.syx)"; return; }
    try {
        auto fw = std::make_unique<mnm::md::Firmware>(mnm::md::loadFirmware(m_firmwarePath.toStdString()));
        auto cpu = std::make_unique<mnm::md::ControlCpu>(fw->mainOs);
        auto engine = std::make_unique<mnm::md::VoiceEngine>(*fw);
        m_fw = std::move(fw); m_cpu = std::move(cpu); m_engine = std::move(engine);
        for (auto& tr : m_tracks) tr.sentMachine = -1;   // resend every packet
        m_status = "OS loaded: " + juce::File(m_firmwarePath).getFileName() + " (" + juce::String(int(m_fw->machines.size())) + " machines)";
        m_engineReady = true;
    } catch (const std::exception& e) {
        m_status = juce::String("OS file error: ") + e.what();
    }
}

void MdProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    m_hostRate = sampleRate;
    const int cap = int(std::ceil(samplesPerBlock * kEngineRate / sampleRate)) + 2 * kBlock + 16;
    m_fifoL.assign(size_t(cap), 0.0f); m_fifoR.assign(size_t(cap), 0.0f);
    m_fifoLen = 0;
    m_interpL.reset(); m_interpR.reset();
    m_pending.clear();
    m_pending.reserve(256);
}

bool MdProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MdProcessor::refreshPackets()
{
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        const int id = machineIdOf(t);
        std::array<int, 8> knobs{};
        for (int k = 0; k < 8; ++k) knobs[size_t(k)] = int(std::lround(tr.knobs[k]->load()));
        if (id == tr.sentMachine && knobs == tr.sentKnobs) continue;
        tr.sentMachine = id; tr.sentKnobs = knobs;
        const auto* m = m_fw->byId(id);
        if (!m) continue;
        std::array<uint16_t, 8> raw{};
        for (int k = 0; k < 8; ++k) raw[size_t(k)] = mnm::md::ControlCpu::rawFromValue(knobs[size_t(k)]);
        std::array<uint32_t, mnm::md::ControlCpu::kMaxPacket> packet{};
        const int n = m_cpu->convert(m->handler, m->dspType(), raw, packet);
        if (n > 0) m_engine->setPacket(t, packet.data(), n);
    }
}

void MdProcessor::runPass()
{
    refreshPackets();
    m_engine->renderPass(m_block);
    const float master = float(m_master->load()) / 127.0f * kHeadroom;
    float* L = m_fifoL.data() + m_fifoLen;
    float* R = m_fifoR.data() + m_fifoLen;
    std::fill_n(L, kBlock, 0.0f); std::fill_n(R, kBlock, 0.0f);
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        const float lev = float(tr.level->load()) / 127.0f * master;
        const float pan = float(tr.pan->load());
        const float gl = lev * (pan <= 0.0f ? 1.0f : 1.0f - pan / 63.0f);
        const float gr = lev * (pan >= 0.0f ? 1.0f : 1.0f + pan / 64.0f);
        const auto& v = m_block[size_t(t)];
        float pk = m_activity[size_t(t)].load() * 0.97f;
        for (int i = 0; i < kBlock; ++i) {
            const float s = float(v[size_t(i)]) * (1.0f / 8388608.0f);
            L[i] += s * gl; R[i] += s * gr;
            pk = std::max(pk, std::abs(s));
        }
        m_activity[size_t(t)].store(pk);
    }
    m_fifoLen += kBlock;
}

void MdProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    const juce::ScopedTryLock sl(m_engineLock);
    if (!sl.isLocked() || !m_engineReady) { midi.clear(); return; }
    const double ratio = kEngineRate / m_hostRate;   // engine frames per host frame

    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (!m.isNoteOn()) continue;
        for (int t = 0; t < kTracks; ++t)
            if (kTrackNotes[t] == m.getNoteNumber()) m_pending.push_back({t, meta.samplePosition * ratio});
    }
    midi.clear();
    for (int t = 0; t < kTracks; ++t)
        if (m_audition[size_t(t)].exchange(false)) m_pending.push_back({t, 0.0});

    // render passes until the resampler has what it needs; each trig goes into the pass that covers its time
    const int needed = int(std::ceil(n * ratio)) + 4;
    if (int(m_fifoL.size()) < needed + kBlock) { m_fifoL.resize(size_t(needed + kBlock)); m_fifoR.resize(size_t(needed + kBlock)); }
    while (m_fifoLen < needed) {
        const double passEnd = double(m_fifoLen + kBlock);
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->enginePos < passEnd) {
                m_engine->trig(it->track, machineIdOf(it->track) + 1);
                m_activity[size_t(it->track)].store(1.0f);
                it = m_pending.erase(it);
            } else ++it;
        }
        runPass();
    }

    auto out = getBusBuffer(buffer, false, 0);
    float* outL = out.getWritePointer(0);
    float* outR = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;
    int used = 0;
    if (std::abs(ratio - 1.0) < 1e-9) {
        std::copy_n(m_fifoL.data(), n, outL);
        if (outR) std::copy_n(m_fifoR.data(), n, outR);
        used = n;
    } else {
        used = m_interpL.process(ratio, m_fifoL.data(), outL, n);
        if (outR) m_interpR.process(ratio, m_fifoR.data(), outR, n);
    }
    std::copy(m_fifoL.begin() + used, m_fifoL.begin() + m_fifoLen, m_fifoL.begin());
    std::copy(m_fifoR.begin() + used, m_fifoR.begin() + m_fifoLen, m_fifoR.begin());
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
    state.setProperty("schema", 1, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void MdProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    for (auto& f : m_machineChanged) f.store(false);   // restored knobs stay as saved
}

juce::AudioProcessorEditor* MdProcessor::createEditor() { return new MdEditor(*this); }

} // namespace mnm::plugin::md

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::md::MdProcessor(); }
