// The Machinedrum kit library, shared by every Monomodule MD instance:
//   <root>/dumps/<id>/dump.json + original.syx   Machinedrum sysex files as imported (a kit, or a whole dump of 64)
//   <root>/kits/<id>.json                        kits saved from the plugin (never overwritten: a save is a new kit)
//   <root>/user.json                             favourites (kit keys)
// <root> = <user application data>/Shnolk/Machinedrum/Library, or $MD_LIBRARY_DIR.
// A kit is addressed by a key: "saved:<id>" or "dump:<id>:<position>". Ids are time-ordered random strings; names are
// metadata, never file names.
#pragma once
#include <array>
#include <map>
#include <vector>
#include <juce_core/juce_core.h>
#include "MdKit.h"

namespace mnm::plugin::md {

// A kit as the plugin holds it: the hardware kit plus the per-track outputs (global settings on the hardware)
struct LibraryKit {
    mnm::md::Kit kit;
    std::array<int, 16> routes{6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6};   // 6 = MAIN
};

struct KitEntry {
    juce::String key, name, source;   // source: "SAVED" or the dump's name
    juce::String sourceId;            // "saved" or the dump id
    int position = -1;                // in its dump
    juce::Time time;
    bool favourite = false;
};

class MdLibrary {
public:
    static juce::File defaultRoot();
    explicit MdLibrary(juce::File root = defaultRoot());
    const juce::File& root() const { return m_root; }

    // Every kit: saved ones (newest first), then each dump (newest first) in kit order. Empty dump slots are left out.
    std::vector<KitEntry> kits();
    bool load(const juce::String& key, LibraryKit& out);
    // Imports a .syx holding Machinedrum kits; the same file twice is one dump. Returns the dump id in idOut.
    juce::Result importSyx(const juce::File& syx, juce::String* idOut = nullptr);
    juce::Result saveKit(const juce::String& name, const LibraryKit& kit, const juce::String& parentKey, juce::String* keyOut = nullptr);
    bool deleteSaved(const juce::String& key);
    bool isFavourite(const juce::String& key);
    void setFavourite(const juce::String& key, bool on);
    juce::int64 changeStamp() const;   // cheap: changes when anything is imported, saved, deleted or favourited

    static juce::var kitToJson(const LibraryKit& k);
    static bool kitFromJson(const juce::var& v, LibraryKit& out);

private:
    const std::vector<mnm::md::Kit>& dumpKits(const juce::String& id);
    juce::StringArray favourites();
    juce::File m_root;
    std::map<juce::String, std::vector<mnm::md::Kit>> m_dumpCache;
    juce::CriticalSection m_lock;
};

} // namespace mnm::plugin::md
