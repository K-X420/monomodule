// Monomodule MD's view of the shared library (the Monomodule Library's store and catalog, library/LibraryModel.h):
// the Machinedrum kits and sounds of every MD project and those saved from the plugin, shared by every instance and
// by the Library app. Items are addressed by their catalog id (a content hash), as in Monomodule One / Six.
// Imported .syx files become MD projects of the library. The plugin-only kit library of earlier builds
// (<user data>/Shnolk/Machinedrum/Library) is moved into the shared one on first use.
#pragma once
#include <vector>
#include <juce_core/juce_core.h>
#include "LibraryModel.h"

namespace mnm::plugin::md {

struct KitEntry {
    juce::String key, name, source;   // source: "SAVED" or the project's name
    juce::String sourceId;            // "saved" or the project id
    int position = -1;                // its slot in that project
    bool favourite = false, saved = false;
};

struct SoundEntry {
    juce::String key, name, source;
    int machine = 0;
    bool favourite = false, saved = false;
};

class MdLibrary {
public:
    MdLibrary();
    mnm::library::LibraryModel& model() { return *m_model; }
    int revision() { m_model->refresh(); return m_model->revision(); }

    std::vector<KitEntry> kits();                 // saved first, then each project's kits in slot order
    std::vector<SoundEntry> sounds(int machine);  // machine < 0 = every machine; saved first, then by name
    bool loadKit(const juce::String& key, mnm::mddump::Kit& out);
    bool loadSound(const juce::String& key, mnm::mdcatalog::Sound& out);
    // A .syx with Machinedrum kits becomes a project (the same file twice is one project); projectIdOut names it
    juce::Result importSyx(const juce::File& syx, juce::String* projectIdOut = nullptr);
    // `into` valid = the item also goes into that project slot, as a new version of the project (LibraryModel)
    juce::Result saveKit(const juce::String& name, const mnm::mddump::Kit& kit, const juce::String& parentKey, juce::String* keyOut = nullptr,
                         const mnm::library::LibraryModel::Slot& into = {});
    juce::Result saveSound(const juce::String& name, const mnm::mdcatalog::Sound& sound, const juce::String& parentKey, juce::String* keyOut = nullptr,
                           const mnm::library::LibraryModel::Slot& into = {});
    // The project slot a loaded item came from: its own, or the one of what it was made from (a saved item's parent)
    mnm::library::LibraryModel::Slot projectSlotOfKit(const juce::String& key);
    mnm::library::LibraryModel::Slot projectSlotOfSound(const juce::String& key);
    bool isFavourite(const juce::String& key) { return m_model->user().isFavourite(key); }
    void setFavourite(const juce::String& key, bool on) { m_model->setFavourite(key, on); }

private:
    void migrateLegacy();
    juce::SharedResourcePointer<mnm::library::LibraryModel> m_model;
};

} // namespace mnm::plugin::md
