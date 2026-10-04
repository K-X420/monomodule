#include "MdCatalog.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace mnm::mdcatalog {

using namespace mnm::mddump;

namespace {

// Two independent 64-bit FNV-1a streams over the same bytes, printed as 32 hex characters (as the Monomachine catalog)
struct Hasher {
    uint64_t a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
    void put(const void* data, size_t n)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) { a = (a ^ p[i]) * 0x100000001b3ull; b = (b ^ (p[i] + 0x9e)) * 0x100000001b3ull; }
    }
    void put8(uint8_t v) { put(&v, 1); }
    void put32(uint32_t v) { for (int i = 0; i < 4; ++i) put8(uint8_t(v >> (8 * i))); }
    void put64(uint64_t v) { put32(uint32_t(v)); put32(uint32_t(v >> 32)); }
    void putStr(const std::string& s) { put32(uint32_t(s.size())); put(s.data(), s.size()); }
    std::string hex() const
    {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
        return buf;
    }
};

void hashSound(Hasher& h, const Sound& s)
{
    h.put32(s.model);
    h.put(s.params, sizeof(s.params));
    h.put8(s.lfoOnSelf() ? 0xFF : s.lfo[0]);   // an LFO on its own track is "self" wherever the sound sits
    h.put(s.lfo + 1, 4);                         // dest param, shapes, type (not the running state)
}

std::string slotKey(const std::string& importId, int slot, int track = -1)
{
    return importId + "/" + std::to_string(slot) + (track >= 0 ? "/" + std::to_string(track) : "");
}

template <typename T> void addUnique(std::vector<T>& v, const T& x) { if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x); }

} // namespace

Sound Sound::fromKit(const Kit& kit, int t)
{
    Sound s;
    s.model = kit.models[t];
    std::memcpy(s.params, kit.params[t], sizeof(s.params));
    std::memcpy(s.lfo, kit.lfos[t], sizeof(s.lfo));
    s.track = t;
    return s;
}

void Sound::applyTo(Kit& kit, int t) const
{
    kit.models[t] = model;
    std::memcpy(kit.params[t], params, sizeof(params));
    std::memcpy(kit.lfos[t], lfo, sizeof(lfo));
    if (lfoOnSelf()) kit.lfos[t][0] = uint8_t(t);
}

std::string Catalog::soundHash(const Sound& s)
{
    Hasher h;
    h.putStr("mdsound1");
    hashSound(h, s);
    return h.hex();
}

std::string Catalog::kitHash(const Kit& k)
{
    Hasher h;
    h.putStr("mdkit1");
    h.put(k.nameRaw, sizeof(k.nameRaw));
    h.put8(k.version); h.put8(k.revision);
    h.put(k.params, sizeof(k.params));
    h.put(k.levels, sizeof(k.levels));
    for (auto m : k.models) h.put32(m);
    for (const auto& l : k.lfos) h.put(l, 5);   // the LFO setups, not their running state
    h.put(k.reverb, 8); h.put(k.delay, 8); h.put(k.eq, 8); h.put(k.dynamics, 8);
    h.put(k.trigGroups, sizeof(k.trigGroups)); h.put(k.muteGroups, sizeof(k.muteGroups));
    return h.hex();
}

std::string Catalog::patternHash(const Pattern& p, const std::string& kitId)
{
    Hasher h;
    h.putStr("mdpattern1");
    h.putStr(kitId);
    h.put8(p.version); h.put8(p.revision); h.put8(p.extended ? 1 : 0);
    for (int t = 0; t < kTracks; ++t) { h.put64(p.trigs[t]); h.put32(p.lockMasks[t]); h.put64(p.accentPerTrack[t]); h.put64(p.slidePerTrack[t]); h.put64(p.swingPerTrack[t]); }
    h.put64(p.accent); h.put64(p.slide); h.put64(p.swing); h.put32(p.swingAmount);
    h.put8(p.accentAmount); h.put8(p.length); h.put8(p.doubleTempo); h.put8(p.scale); h.put8(p.kit); h.put8(p.numLockedRows);
    // the lock rows in use (stale bytes of unassigned rows do not make a different pattern)
    int rows = 0;
    for (int t = 0; t < kTracks; ++t) for (int q = 0; q < 24; ++q) rows += int((p.lockMasks[t] >> q) & 1);
    for (int r = 0; r < rows && r < 64; ++r) h.put(p.locks[r], p.extended ? 64 : 32);
    h.put32(p.accentEditAll); h.put32(p.slideEditAll); h.put32(p.swingEditAll);
    return h.hex();
}

void Catalog::clear()
{
    sounds.clear(); kits.clear(); patterns.clear();
    m_soundIndex.clear(); m_kitIndex.clear(); m_patternIndex.clear();
    m_kitBySlot.clear(); m_patternBySlot.clear(); m_soundBySlot.clear();
}

void Catalog::build(const std::vector<Input>& inputs, const std::vector<SavedInput>& saved)
{
    clear();
    auto addSound = [&](const Sound& s, const std::string& name) -> SoundItem& {
        const std::string id = soundHash(s);
        auto it = m_soundIndex.find(id);
        if (it == m_soundIndex.end()) {
            SoundItem item;
            item.id = id; item.name = name; item.sound = s;
            it = m_soundIndex.emplace(id, sounds.size()).first;
            sounds.push_back(std::move(item));
        }
        return sounds[it->second];
    };
    auto addKit = [&](const Kit& k, const std::string& importId, const std::string& importName) -> size_t {
        const std::string kid = kitHash(k);
        auto it = m_kitIndex.find(kid);
        if (it == m_kitIndex.end()) {
            KitItem item;
            item.id = kid; item.name = k.name; item.kit = k;
            it = m_kitIndex.emplace(kid, kits.size()).first;
            kits.push_back(std::move(item));
        }
        const size_t index = it->second;
        kits[index].sources.push_back({importId, importName, importId.empty() ? -1 : k.position, -1});
        if (!importId.empty()) m_kitBySlot[slotKey(importId, k.position)] = kid;
        for (int t = 0; t < kTracks; ++t) {
            if (k.model(t) == 0) continue;   // GND---: nothing to keep
            auto& s = addSound(Sound::fromKit(k, t), k.name + " T" + std::to_string(t + 1));
            s.sources.push_back({importId, importName, importId.empty() ? -1 : k.position, t});
            addUnique(s.kitIds, kid);
            kits[index].soundIds[t] = s.id;
            if (!importId.empty()) m_soundBySlot[slotKey(importId, k.position, t)] = s.id;
        }
        return index;
    };
    for (const auto& sv : saved) {   // saved sounds first: their names are the user's
        const std::string from = "Saved from " + (sv.savedFrom.empty() ? std::string("a plugin") : sv.savedFrom);
        if (sv.isKit) {
            Kit k = sv.kit;
            if (!sv.name.empty()) k.name = sv.name;
            auto& item = kits[addKit(k, "", from)];
            item.saved = true; item.itemId = sv.itemId; item.parentId = sv.parent; item.savedFrom = sv.savedFrom; item.savedAt = sv.time;
        } else {
            if (sv.sound.machine() == 0) continue;
            auto& s = addSound(sv.sound, sv.name);
            s.saved = true; s.itemId = sv.itemId; s.parentId = sv.parent; s.savedFrom = sv.savedFrom; s.savedAt = sv.time;
            s.sources.push_back({"", from, -1, sv.sound.track});
        }
    }
    for (const auto& in : inputs)
        if (in.dump) for (const auto& k : in.dump->kits) if (!k.isEmptySlot()) addKit(k, in.importId, in.importName);
    for (const auto& in : inputs) {
        if (!in.dump) continue;
        for (const auto& p : in.dump->patterns) {
            if (p.empty()) continue;
            const std::string kid = kitIdOf(in.importId, p.kit);
            const std::string pid = patternHash(p, kid);
            auto it = m_patternIndex.find(pid);
            if (it == m_patternIndex.end()) {
                PatternItem item;
                item.id = pid; item.name = patternSlotName(p.position) + " " + in.importName; item.pattern = p; item.kitId = kid;
                it = m_patternIndex.emplace(pid, patterns.size()).first;
                patterns.push_back(std::move(item));
            }
            patterns[it->second].sources.push_back({in.importId, in.importName, p.position, -1});
            m_patternBySlot[slotKey(in.importId, p.position)] = pid;
        }
    }
    for (const auto& p : patterns) {   // back links: kit -> patterns, sound -> patterns
        auto kit = m_kitIndex.find(p.kitId);
        if (kit == m_kitIndex.end()) continue;
        addUnique(kits[kit->second].patternIds, p.id);
        for (const auto& sid : kits[kit->second].soundIds)
            if (!sid.empty()) if (auto s = m_soundIndex.find(sid); s != m_soundIndex.end()) addUnique(sounds[s->second].patternIds, p.id);
    }
}

const SoundItem* Catalog::sound(const std::string& id) const { auto it = m_soundIndex.find(id); return it == m_soundIndex.end() ? nullptr : &sounds[it->second]; }
const KitItem* Catalog::kit(const std::string& id) const { auto it = m_kitIndex.find(id); return it == m_kitIndex.end() ? nullptr : &kits[it->second]; }
const PatternItem* Catalog::pattern(const std::string& id) const { auto it = m_patternIndex.find(id); return it == m_patternIndex.end() ? nullptr : &patterns[it->second]; }
std::string Catalog::kitIdOf(const std::string& importId, int slot) const { auto it = m_kitBySlot.find(slotKey(importId, slot)); return it == m_kitBySlot.end() ? std::string() : it->second; }
std::string Catalog::patternIdOf(const std::string& importId, int slot) const { auto it = m_patternBySlot.find(slotKey(importId, slot)); return it == m_patternBySlot.end() ? std::string() : it->second; }
std::string Catalog::soundIdOf(const std::string& importId, int kitSlot, int track) const { auto it = m_soundBySlot.find(slotKey(importId, kitSlot, track)); return it == m_soundBySlot.end() ? std::string() : it->second; }

} // namespace mnm::mdcatalog
