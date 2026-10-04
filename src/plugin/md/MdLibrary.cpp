#include "MdLibrary.h"
#include <algorithm>
#include <cstring>
#include "MdNames.h"

namespace mnm::plugin::md {

using namespace mnm::library;

namespace {
juce::String U(const std::string& s) { return juce::String(s).toUpperCase(); }
constexpr const char* kSavedFrom = "Monomodule MD";

// The plugin-only library of earlier builds: dumps/<id>/original.syx, kits/<id>.json (its own kit JSON)
juce::File legacyRoot()
{
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Shnolk").getChildFile("Machinedrum").getChildFile("Library");
}

template <typename T> bool fill(const juce::var& v, T* out, size_t n)
{
    const auto* a = v.getArray();
    if (!a || size_t(a->size()) != n) return false;
    for (size_t i = 0; i < n; ++i) out[i] = T(int((*a)[int(i)]));
    return true;
}

bool legacyKit(const juce::var& v, mnm::mddump::Kit& k)
{
    k = {};
    const auto name = v["name"].toString().toUpperCase().substring(0, 16);
    std::memcpy(k.nameRaw, name.toRawUTF8(), size_t(name.length()));
    k.name = name.toStdString();
    if (!fill(v["machines"], k.models, 16) || !fill(v["levels"], k.levels, 16)) return false;
    const auto* params = v["params"].getArray();
    const auto* lfos = v["lfos"].getArray();
    const auto* fx = v["masterFx"].getArray();
    if (!params || params->size() != 16 || !lfos || lfos->size() != 16 || !fx || fx->size() != 4) return false;
    for (int t = 0; t < 16; ++t) {
        if (!fill((*params)[t], k.params[t], 24) || !fill((*lfos)[t], k.lfos[t], 36)) return false;
        k.trigGroups[t] = 127; k.muteGroups[t] = 127;
    }
    uint8_t* fxs[4] = {k.reverb, k.delay, k.eq, k.dynamics};
    for (int f = 0; f < 4; ++f) if (!fill((*fx)[f], fxs[f], 8)) return false;
    return true;
}
} // namespace

MdLibrary::MdLibrary() { migrateLegacy(); }

void MdLibrary::migrateLegacy()
{
    if (const char* env = std::getenv("MNM_LIBRARY_DIR"); env && *env) return;   // a test library: leave the user's own alone
    const auto root = legacyRoot();
    const auto marker = root.getChildFile("moved-to-shared-library.txt");
    if (!root.isDirectory() || marker.existsAsFile()) return;
    for (const auto& d : root.getChildFile("dumps").findChildFiles(juce::File::findDirectories, false))
        importSyx(d.getChildFile("original.syx"));
    for (const auto& f : root.getChildFile("kits").findChildFiles(juce::File::findFiles, false, "*.json")) {
        const auto v = juce::JSON::parse(f);
        mnm::mddump::Kit k;
        if (legacyKit(v["kit"], k)) saveKit(v["name"].toString(), k, {});
    }
    marker.replaceWithText("The kits of this folder were moved into the Monomodule Library (Machinedrum projects and saved kits) on "
                           + juce::Time::getCurrentTime().toString(true, true) + ".\n");
}

std::vector<KitEntry> MdLibrary::kits()
{
    m_model->refresh();
    const auto& c = m_model->mdCatalog();
    std::vector<KitEntry> saved, fromProjects;
    for (const auto& k : c.kits) {
        KitEntry e;
        e.key = juce::String(k.id); e.name = U(k.name); e.favourite = isFavourite(e.key); e.saved = k.saved;
        const mnm::mdcatalog::Source* src = nullptr;
        for (const auto& s : k.sources) if (!s.saved()) { src = &s; break; }
        if (k.saved || !src) { e.source = "SAVED"; e.sourceId = "saved"; saved.push_back(e); }
        else { e.source = U(src->importName); e.sourceId = juce::String(src->importId); e.position = src->slot; fromProjects.push_back(e); }
    }
    std::stable_sort(fromProjects.begin(), fromProjects.end(), [](const KitEntry& a, const KitEntry& b) {
        return a.source != b.source ? a.source < b.source : a.position < b.position; });
    saved.insert(saved.end(), fromProjects.begin(), fromProjects.end());
    return saved;
}

std::vector<SoundEntry> MdLibrary::sounds(int machine)
{
    m_model->refresh();
    std::vector<SoundEntry> v;
    for (const auto& s : m_model->mdCatalog().sounds) {
        if (machine >= 0 && s.sound.machine() != machine) continue;
        SoundEntry e;
        e.key = juce::String(s.id); e.name = U(s.name); e.machine = s.sound.machine(); e.saved = s.saved; e.favourite = isFavourite(e.key);
        e.source = s.saved || s.sources.empty() ? juce::String("SAVED") : U(s.sources.front().importName);
        v.push_back(e);
    }
    std::stable_sort(v.begin(), v.end(), [](const SoundEntry& a, const SoundEntry& b) {
        return a.machine != b.machine ? a.machine < b.machine : a.saved != b.saved ? a.saved : a.name < b.name; });
    return v;
}

bool MdLibrary::loadKit(const juce::String& key, mnm::mddump::Kit& out)
{
    m_model->refresh();
    if (const auto* k = m_model->mdCatalog().kit(key.toStdString())) { out = k->kit; return true; }
    return false;
}

bool MdLibrary::loadSound(const juce::String& key, mnm::mdcatalog::Sound& out)
{
    m_model->refresh();
    if (const auto* s = m_model->mdCatalog().sound(key.toStdString())) { out = s->sound; return true; }
    return false;
}

juce::Result MdLibrary::importSyx(const juce::File& syx, juce::String* projectIdOut)
{
    juce::MemoryBlock mb;
    if (!syx.loadFileAsData(mb)) return juce::Result::fail("Could not read " + syx.getFileName());
    const auto* bytes = static_cast<const uint8_t*>(mb.getData());
    if (!mnm::mddump::isMachinedrumSysex(bytes, mb.getSize())) return juce::Result::fail(syx.getFileName() + " is not Machinedrum sysex.");
    const auto d = mnm::mddump::parseDump(bytes, mb.getSize(), syx.getFileNameWithoutExtension().toStdString());
    if (d.kits.empty()) return juce::Result::fail("No Machinedrum kits in " + syx.getFileName());
    Store store;
    if (const auto similar = store.findSimilarMd(d); similar && similar->diff.identical()) {   // already in the library
        if (projectIdOut) *projectIdOut = similar->projectId;
        return juce::Result::ok();
    }
    ProjectInfo made;
    const auto r = store.importSysexFile(syx, ImportMode::NewProject, {}, &made);
    if (r.failed()) return r;
    m_model->refresh(true);
    if (projectIdOut) *projectIdOut = made.id;
    return juce::Result::ok();
}

juce::Result MdLibrary::saveKit(const juce::String& name, const mnm::mddump::Kit& kit, const juce::String& parentKey, juce::String* keyOut,
                                const LibraryModel::Slot& into)
{
    return m_model->saveMdKit(kit, name, parentKey, kSavedFrom, into, keyOut);
}

juce::Result MdLibrary::saveSound(const juce::String& name, const mnm::mdcatalog::Sound& sound, const juce::String& parentKey, juce::String* keyOut,
                                  const LibraryModel::Slot& into)
{
    return m_model->saveMdSound(sound, name, parentKey, kSavedFrom, into, keyOut);
}

LibraryModel::Slot MdLibrary::projectSlotOfKit(const juce::String& key)
{
    m_model->refresh();
    std::string id = key.toStdString();
    for (int hop = 0; hop < 8 && !id.empty(); ++hop) {
        if (auto s = m_model->projectSlotOfMdKit(id); s.valid()) return s;
        const auto* k = m_model->mdCatalog().kit(id);
        id = k ? k->parentId : std::string();
    }
    return {};
}

LibraryModel::Slot MdLibrary::projectSlotOfSound(const juce::String& key)
{
    m_model->refresh();
    std::string id = key.toStdString();
    for (int hop = 0; hop < 8 && !id.empty(); ++hop) {
        if (auto s = m_model->projectSlotOfMdSound(id); s.valid()) return s;
        const auto* s = m_model->mdCatalog().sound(id);
        id = s ? s->parentId : std::string();
    }
    return {};
}

} // namespace mnm::plugin::md
