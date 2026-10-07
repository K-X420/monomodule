// The processor's kits and sounds, captured and loaded, and the plugin state
#include "MdProcessorInternal.h"
#include <cstring>
#include <cmath>

namespace mnm::plugin::md {

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

} // namespace mnm::plugin::md
