// The processor's UW samples: loading, renaming, RAM to ROM, resampling, the sample memory
#include "MdProcessorInternal.h"
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MdPreview.h"

namespace mnm::plugin::md {

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

juce::String MdProcessor::resampleTrack(int t, int slot, double seconds)
{
    if (t < 0 || t >= kTracks || slot < 0 || slot >= mnm::md::VoiceEngine::kSlots || (slot >= 32 && slot < 48)) return "Not a ROM slot";
    {
        const juce::ScopedLock sl(m_engineLock);
        if (!m_fw) return "No OS loaded";
    }
    mnm::mdpreview::Options opt;
    opt.soundSeconds = seconds;
    opt.bpm = tempo();
    const auto spec = mnm::mdpreview::soundPreview(captureSound(t), opt);
    Sample s;
    s.rate = mnm::mdpreview::kSampleRate;
    try {
        mnm::mdpreview::Renderer r(*m_fw);
        s.data.reserve(spec.frames);
        const bool ok = r.render(spec, opt.bpm, [&](uint32_t, const mnm::mdpreview::Block& b) {
            for (int i = 0; i < mnm::mdpreview::Block::kFrames && s.data.size() < spec.frames; ++i) s.data.push_back(0.5f * (b.mixL[size_t(i)] + b.mixR[size_t(i)]));
        });
        if (!ok) return juce::String(r.error());
    } catch (const std::exception& e) { return juce::String(e.what()); }
    size_t end = s.data.size();   // the silent tail goes
    while (end > 441 && std::abs(s.data[end - 1]) < 1e-4f) --end;
    s.data.resize(end);
    float peak = 0; for (float v : s.data) peak = std::max(peak, std::abs(v));
    if (peak < 1e-4f) return "T" + juce::String(t + 1) + " makes no sound on its own";
    const int mi = juce::jlimit(0, kNumMachines - 1, int(std::lround(m_tracks[size_t(t)].machine->load())));
    s.name = "T" + juce::String(t + 1) + " " + juce::String(kMachines[mi].name);
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

} // namespace mnm::plugin::md
