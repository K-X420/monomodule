#include "MdProcessor.h"
#include <cstring>
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
        apvts.addParameterListener(machineId(t), this);
    }
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) m_masterFx[size_t(fx)][size_t(k)] = apvts.getRawParameterValue(masterFxId(fx, k));
    m_master = apvts.getRawParameterValue(masterId());
    m_velMode = apvts.getRawParameterValue(velModeId());
    m_outputMode = apvts.getRawParameterValue(outputModeId());
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
    m_engine.reset(); m_fw.reset();
    if (m_firmwarePath.isEmpty()) { m_status = "Select the Machinedrum OS file (Elektron_SPS1-1UW_OS1.63.syx)"; return; }
    try {
        auto fw = std::make_unique<mnm::md::Firmware>(mnm::md::loadFirmware(m_firmwarePath.toStdString()));
        auto engine = std::make_unique<mnm::md::Engine>(*fw);
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
    m_pending.reserve(256);
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
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m_tracks[size_t(t)];
        mnm::md::Engine::Track e;
        e.machine = machineIdOf(t);
        auto& p = e.params;
        for (int k = 0; k < 8; ++k) p[size_t(k)] = val(tr.knobs[k]);
        for (int k = 0; k < 8; ++k) p[size_t(8 + k)] = val(tr.mix[k]);
        p[16] = val(tr.mix[8]); p[17] = val(tr.mix[9]); p[18] = val(tr.mix[10]); p[19] = val(tr.mix[11]); p[20] = val(tr.mix[12]);
        p[21] = val(tr.lfo[5]); p[22] = val(tr.lfo[6]); p[23] = val(tr.lfo[7]);
        e.level = val(tr.mix[13]);
        for (int k = 0; k < 5; ++k) e.lfoConfig[size_t(k)] = val(tr.lfo[k]);
        e.route = juce::jlimit(0, kNumRoutes - 1, int(std::lround(tr.route->load())));
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
    for (int fx = 0; fx < 4; ++fx)
        for (int k = 0; k < 8; ++k) master[size_t(fx)][size_t(k)] = val(m_masterFx[size_t(fx)][size_t(k)]);
    m_engine->setMasterFx(master);
    if (m_snap.exchange(false)) { m_engine->snap(); m_ctlQuietUntil = m_clock + int64_t(0.2 * m_hostRate); }
    m_engine->setTempo(m_hostBpm.load());
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
// they differ from what the channel last got, AT when not 0. The notes last 3 x (LEN + 1) sequencer ticks (96 per
// quarter note; LEN 0 = 4 ticks), so LEN 7 is one 16th step and LEN 127 a bar.
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
    double bpm = m_hostBpm.load();
    if (bpm < 20.0 || bpm > 400.0) bpm = 120.0;
    const int ticks = len > 0 ? 3 + 3 * len : 4;
    const int64_t offAt = m_clock + pos + int64_t(std::lround(ticks * 60.0 / (bpm * 96.0) * m_hostRate));
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
            m_ctlSeen[size_t(target)][size_t(p)] = int16_t(target < kTracks ? trackParam(target, p)
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
            const int now = trackParam(t, p);
            if (!moved(t, p, now)) continue;
            const int before = m_ctlSeen[size_t(t)][size_t(p)];
            m_ctlSeen[size_t(t)][size_t(p)] = int16_t(now);
            if (isMidMachine(id)) {
                const int ch = id - 96;
                if (p == 20 && now > 0) { midSend(0, uint8_t(0xC0 | ch), uint8_t(now - 1)); m_midLastPc[size_t(ch)] = now - 1; }
                // PB MW AT and the CC values go out from midStream (knob + LFO)
            } else if (const int fx = ctrMasterFx(id); fx >= 0) {
                if (p < 8) queueSet(kTracks + fx, p, now, true);
            } else if (id == kCtrAll) {
                const int d = juce::jlimit(1, 126, now) - juce::jlimit(1, 126, before);
                if (d == 0) continue;
                for (int u = 0; u < kTracks; ++u) {
                    const int uid = ids[size_t(u)];
                    if (u == t || isMidMachine(uid) || isCtrMachine(uid)) continue;
                    if (p < 8 && (uid == 160 || uid == 161 || uid == 165 || uid == 166)) continue;   // RAM-R1..R4
                    const int cur = m_ctlPending[size_t(u)][size_t(p)] >= 0 ? m_ctlPending[size_t(u)][size_t(p)] : trackParam(u, p);
                    queueSet(u, p, juce::jlimit(0, 127, cur + d));
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
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto b = pos->getBpm(); b && *b > 0.0) m_hostBpm.store(*b);
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

    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) {
            for (int t = 0; t < kTracks; ++t)
                if (kTrackNotes[t] == m.getNoteNumber() && m_tracks[size_t(t)].mute->load() < 0.5f) {
                    const int id = machineIdOf(t);
                    if (isMidMachine(id)) { midTrig(t, meta.samplePosition); m_activity[size_t(t)].store(1.0f); }
                    else if (isCtrMachine(id)) m_activity[size_t(t)].store(1.0f);
                    else m_pending.push_back({t, meta.samplePosition * ratio, int(m.getVelocity())});
                }
        } else if (m.isController()) {
            handleCc(m.getChannel(), m.getControllerNumber(), m.getControllerValue());
        }
    }
    midi.clear();
    for (int t = 0; t < kTracks; ++t)
        if (m_audition[size_t(t)].exchange(false)) {
            const int id = machineIdOf(t);
            if (isMidMachine(id)) { midTrig(t, 0); m_activity[size_t(t)].store(1.0f); }
            else if (!isCtrMachine(id)) m_pending.push_back({t, 0.0, 100});
        }
    controlMachines(n);
    for (auto& p : m_peak) p.store(p.load() * 0.8f);   // the meters' fall-off

    // render passes until the resampler has what it needs; each trig goes into the pass that covers its time
    const int needed = int(std::ceil(n * ratio)) + 4;
    if (int(m_fifo[0].size()) < needed + kBlock)
        for (auto& f : m_fifo) f.resize(size_t(needed + kBlock));
    while (m_fifoLen < needed) {
        const double passEnd = double(m_fifoLen + kBlock);
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->enginePos < passEnd) {
                // the trig's accent factor (MainOS 0x20CD76): VOLUME mode = velocity; ACCENT mode = 0x80, accented at >= 112
                const int accent = int(std::lround(m_velMode->load())) == 0 ? it->velocity
                                 : it->velocity >= 112 ? 0x80 + 2 * int(std::lround(m_accent->load())) : -128;
                m_engine->trig(it->track, machineIdOf(it->track), accent);
                m_activity[size_t(it->track)].store(1.0f);
                it = m_pending.erase(it);
            } else ++it;
        }
        runPass();
    }

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
    double bpm = m_hostBpm.load();
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
    set(lfoId(t, 2), float(juce::jlimit(0, 7, int(lfo[2]))));
    set(lfoId(t, 3), float(juce::jlimit(0, 7, int(lfo[3]))));
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
    setFirmwarePath({}, true);
}

void MdProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("schema", 2, nullptr);
    state.setProperty("kitName", m_kitName, nullptr);
    state.setProperty("kitKey", m_kitKey, nullptr);
    juce::String locked;
    for (int t = 0; t < kTracks; ++t) locked << (m_locked[size_t(t)].load() ? "1" : "0");
    state.setProperty("locked", locked, nullptr);
    state.setProperty("kitSnapshot", juce::String::toHexString(m_kitSnapshot.data(), int(m_kitSnapshot.size()), 0), nullptr);
    const auto base = mnm::mddump::encodeKit(m_baseKit);
    state.setProperty("baseKit", juce::String::toHexString(base.data(), int(base.size()), 0), nullptr);
    juce::StringArray soundKeys, soundNames, soundHashes;
    for (const auto& s : m_sounds) { soundKeys.add(s.key); soundNames.add(s.name); soundHashes.add(juce::String(s.hash)); }
    state.setProperty("soundKeys", soundKeys.joinIntoString("|"), nullptr);
    state.setProperty("soundNames", soundNames.joinIntoString("|"), nullptr);
    state.setProperty("soundHashes", soundHashes.joinIntoString("|"), nullptr);
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
    auto hexBytes = [this](const char* prop) {
        juce::MemoryBlock mb;
        mb.loadFromHexString(apvts.state.getProperty(prop, "").toString());
        return std::vector<uint8_t>(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
    };
    m_kitSnapshot = hexBytes("kitSnapshot");
    const auto base = hexBytes("baseKit");
    if (base.empty() || !mnm::mddump::decodeKit(base.data(), base.size(), m_baseKit)) m_baseKit = freshKit();
    juce::StringArray soundKeys, soundNames, soundHashes;
    soundKeys.addTokens(apvts.state.getProperty("soundKeys", "").toString(), "|", "");
    soundNames.addTokens(apvts.state.getProperty("soundNames", "").toString(), "|", "");
    soundHashes.addTokens(apvts.state.getProperty("soundHashes", "").toString(), "|", "");
    for (int t = 0; t < kTracks; ++t) m_sounds[size_t(t)] = {soundKeys[t], soundNames[t], soundHashes[t].toStdString()};
    samplesFromTree(apvts.state.getChildWithName("SAMPLES"));
    shadowsFromTree(apvts.state.getChildWithName("SHADOWS"));
    for (auto& f : m_machineChanged) f.store(false);   // restored knobs stay as saved
    m_snap = true;
}

juce::AudioProcessorEditor* MdProcessor::createEditor() { return new MdEditor(*this); }

} // namespace mnm::plugin::md

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mnm::plugin::md::MdProcessor(); }
