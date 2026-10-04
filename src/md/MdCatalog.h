// The Machinedrum catalog, as the Monomachine one (core/library/Catalog.h): a read-only view over the projects' current
// states and the sounds saved from the plugin that gives every sound (a kit track), kit and used pattern a
// content-based identity and links them to each other. Identical items found in several projects merge into one entry
// that lists its sources. Rebuilt whenever the library changes; nothing here is stored.
//
// Identity: a sound is its machine, its 24 parameters and its LFO setup (destination, shapes, type; an LFO aimed at its
// own track counts as "self", so the sound is the same on any track; the LFO's running state is not part of it). Level
// and output are the kit's mix and stay with the kit. A kit is its whole content; a pattern is its content plus the kit
// it resolves to. GND--- tracks, empty kit slots and empty patterns are not catalogued.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "MdDump.h"

namespace mnm::mdcatalog {

// One track's sound, as it sits in a kit
struct Sound {
    uint32_t model = 0;
    uint8_t params[24] = {};
    uint8_t lfo[36] = {};       // the kit's LFO struct of that track (dest track, dest param, shape 1, shape 2, type, state)
    int track = 0;              // the track it was taken from (resolves an LFO aimed at itself)
    int machine() const { return int(model & 0xFF); }
    bool lfoOnSelf() const { return lfo[0] == track; }
    // Puts the sound on `t` of `kit`: machine, parameters, LFO (an LFO aimed at itself follows the sound)
    void applyTo(mddump::Kit& kit, int t) const;
    static Sound fromKit(const mddump::Kit& kit, int t);
};

struct Source {
    std::string importId, importName;   // a project's id and name; empty id = saved from the plugin
    int slot = -1;                      // kit or pattern slot
    int track = -1;                     // sounds: the track of that kit
    bool saved() const { return importId.empty(); }
};

struct SavedInput {
    std::string itemId, name, parent, savedFrom, time;
    bool isKit = false;
    Sound sound;                        // !isKit
    mddump::Kit kit;                    // isKit
};

struct SoundItem {
    std::string id, name;               // name: "<KIT NAME> T<n>" of the first source, or the user's
    Sound sound;
    bool saved = false;
    std::string itemId, parentId, savedFrom, savedAt;
    std::vector<Source> sources;
    std::vector<std::string> kitIds, patternIds;
};

struct KitItem {
    std::string id, name;
    mddump::Kit kit;
    std::string soundIds[16];           // empty for a GND--- track
    bool saved = false;
    std::string itemId, parentId, savedFrom, savedAt;
    std::vector<Source> sources;
    std::vector<std::string> patternIds;
};

struct PatternItem {
    std::string id, name;               // "<slot> <project name>" of the first source, e.g. "A01 MY MD"
    mddump::Pattern pattern;
    std::string kitId;
    std::vector<Source> sources;
};

struct Input { std::string importId, importName; const mddump::Dump* dump = nullptr; };

struct Catalog {
    std::vector<SoundItem> sounds;
    std::vector<KitItem> kits;
    std::vector<PatternItem> patterns;

    void build(const std::vector<Input>& inputs, const std::vector<SavedInput>& saved = {});
    void clear();
    const SoundItem* sound(const std::string& id) const;
    const KitItem* kit(const std::string& id) const;
    const PatternItem* pattern(const std::string& id) const;
    std::string kitIdOf(const std::string& importId, int slot) const;
    std::string patternIdOf(const std::string& importId, int slot) const;
    std::string soundIdOf(const std::string& importId, int kitSlot, int track) const;

    static std::string soundHash(const Sound& s);
    static std::string kitHash(const mddump::Kit& k);
    static std::string patternHash(const mddump::Pattern& p, const std::string& kitId);

private:
    std::map<std::string, size_t> m_soundIndex, m_kitIndex, m_patternIndex;
    std::map<std::string, std::string> m_kitBySlot, m_patternBySlot, m_soundBySlot;
};

} // namespace mnm::mdcatalog
