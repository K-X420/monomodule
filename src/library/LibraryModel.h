// The library as the plugins see it: the store, the current state of every project, the sounds saved from the
// plugins, and the catalog over both. Loaded lazily (parsing the projects takes a moment) and refreshed when the
// store's change stamp moves; one instance is shared by all plugin instances of a process
// (juce::SharedResourcePointer<LibraryModel>). Message thread only.
#pragma once
#include "Store.h"
#include "library/Catalog.h"
#include "MdCatalog.h"

namespace mnm::library {

class LibraryModel {
public:
    LibraryModel() = default;
    // Loads on first use, reloads when the library changed on disk. Returns true when the catalog was rebuilt.
    bool refresh(bool force = false);
    const mnm::catalog::Catalog& catalog() const { return m_catalog; }
    const std::vector<ProjectInfo>& projects() const { return m_projects; }
    const UserData& user() const { return m_user; }
    void setFavourite(const juce::String& id, bool on);   // catalog id; saved at once
    const mnm::dump::Dump* state(const juce::String& projectId) const { auto it = m_states.find(projectId); return it == m_states.end() ? nullptr : &it->second; }
    int revision() const { return m_revision; }   // bumps with every rebuild: views compare it
    // Machinedrum projects and the sounds saved from Monomodule MD, in a catalog of their own
    const mnm::mdcatalog::Catalog& mdCatalog() const { return m_mdCatalog; }
    const mnm::mddump::Dump* mdState(const juce::String& projectId) const { auto it = m_mdStates.find(projectId); return it == m_mdStates.end() ? nullptr : &it->second; }

    // Where a catalogued preset / kit sits in a project (its first project source), if anywhere.
    struct Slot { juce::String projectId, projectName; int kit = -1, track = -1; bool valid() const { return projectId.isNotEmpty(); } };
    Slot projectSlotOfPreset(const std::string& presetId) const;
    Slot projectSlotOfKit(const std::string& kitId) const;

    // Saving from a plugin never overwrites: the sound becomes an item of its own (with a link to what it was made
    // from). `into` valid = it is also put into that project slot, as a new version of the project, so the next
    // sysex export carries the edit back to the hardware.
    juce::Result savePreset(const mnm::dump::Kit& kit, int track, const juce::String& name, const juce::String& parentId,
                            const juce::String& savedFrom, const Slot& into, juce::String* presetIdOut = nullptr);
    juce::Result saveKit(const mnm::dump::Kit& kit, const juce::String& name, const juce::String& parentId,
                         const juce::String& savedFrom, const Slot& into, juce::String* kitIdOut = nullptr);

    // Monomodule MD: the same, for a Machinedrum sound (one track) or kit; `into` names a slot of an MD project.
    Slot projectSlotOfMdSound(const std::string& soundId) const;
    Slot projectSlotOfMdKit(const std::string& kitId) const;
    juce::Result saveMdSound(const mnm::mdcatalog::Sound& sound, const juce::String& name, const juce::String& parentId,
                             const juce::String& savedFrom, const Slot& into, juce::String* soundIdOut = nullptr);
    juce::Result saveMdKit(const mnm::mddump::Kit& kit, const juce::String& name, const juce::String& parentId,
                           const juce::String& savedFrom, const Slot& into, juce::String* kitIdOut = nullptr);
    // A plugin's patterns and songs into an MD project as a new version (each slot of `bank` that differs from the
    // project's replaces it). projectId empty: the bank (kits, patterns, songs) becomes a new project named `name`.
    // projectIdOut: the project it went into.
    juce::Result saveMdPatterns(const juce::String& projectId, const mnm::mddump::Dump& bank, const juce::String& name,
                                const juce::String& savedFrom, juce::String* projectIdOut = nullptr, juce::StringArray* changesOut = nullptr);

private:
    Store m_store;
    std::vector<ProjectInfo> m_projects;
    std::map<juce::String, mnm::dump::Dump> m_states;
    std::map<juce::String, int> m_stateVersion;
    std::vector<SavedItem> m_saved;
    UserData m_user;
    mnm::catalog::Catalog m_catalog;
    std::map<juce::String, mnm::mddump::Dump> m_mdStates;
    std::vector<SavedMdItem> m_mdSaved;
    mnm::mdcatalog::Catalog m_mdCatalog;
    juce::int64 m_stamp = -1;
    int m_revision = 0;
};

} // namespace mnm::library
