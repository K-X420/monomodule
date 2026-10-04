#include "MdLibrary.h"
#include <algorithm>

namespace mnm::plugin::md {

namespace {
juce::String newId()
{
    static juce::Random rng;
    const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    juce::String s;
    for (auto v = juce::Time::currentTimeMillis(); v > 0; v /= 36) s = juce::String::charToString(juce::juce_wchar(digits[v % 36])) + s;
    s = s.paddedLeft('0', 9);   // sortable
    for (int i = 0; i < 6; ++i) s += juce::String::charToString(juce::juce_wchar(digits[rng.nextInt(36)]));
    return s;
}
template <typename T> juce::var array(const T* data, size_t n) { juce::Array<juce::var> a; for (size_t i = 0; i < n; ++i) a.add(int(data[i])); return a; }
template <typename T> bool fill(const juce::var& v, T* out, size_t n)
{
    const auto* a = v.getArray();
    if (!a || size_t(a->size()) != n) return false;
    for (size_t i = 0; i < n; ++i) out[i] = T(int((*a)[int(i)]));
    return true;
}

// FNV-1a over the file: the same file imported twice is one dump
juce::String fileHash(const juce::File& f)
{
    juce::MemoryBlock mb;
    f.loadFileAsData(mb);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < mb.getSize(); ++i) { h ^= uint8_t(mb[i]); h *= 1099511628211ull; }
    return juce::String::toHexString(juce::int64(h)) + "-" + juce::String(juce::int64(mb.getSize()));
}

bool emptyKit(const mnm::md::Kit& k)
{
    if (juce::String(k.name).trim().isNotEmpty()) return false;
    for (auto m : k.machines) if (m != 0) return false;
    return true;
}
juce::String kitName(const mnm::md::Kit& k)
{
    const auto n = juce::String(k.name).trim();
    return n.isNotEmpty() ? n.toUpperCase() : "KIT " + juce::String(k.position + 1).paddedLeft('0', 2);
}
} // namespace

juce::File MdLibrary::defaultRoot()
{
    if (const char* env = std::getenv("MD_LIBRARY_DIR"); env && *env) return juce::File(juce::String(env));
    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("Shnolk").getChildFile("Machinedrum").getChildFile("Library");
}

MdLibrary::MdLibrary(juce::File root) : m_root(std::move(root)) {}

juce::var MdLibrary::kitToJson(const LibraryKit& lk)
{
    const auto& k = lk.kit;
    auto* o = new juce::DynamicObject();
    o->setProperty("name", juce::String(k.name));
    o->setProperty("machines", array(k.machines.data(), 16));
    juce::Array<juce::var> params, lfos, fx;
    for (const auto& p : k.params) params.add(array(p.data(), p.size()));
    for (const auto& l : k.lfos) lfos.add(array(l.data(), l.size()));
    for (const auto& m : k.masterFx) fx.add(array(m.data(), m.size()));
    o->setProperty("params", params);
    o->setProperty("levels", array(k.levels.data(), 16));
    o->setProperty("lfos", lfos);
    o->setProperty("masterFx", fx);
    o->setProperty("routes", array(lk.routes.data(), 16));
    return juce::var(o);
}

bool MdLibrary::kitFromJson(const juce::var& v, LibraryKit& out)
{
    LibraryKit lk;
    auto& k = lk.kit;
    k.name = v["name"].toString().toStdString();
    if (!fill(v["machines"], k.machines.data(), 16) || !fill(v["levels"], k.levels.data(), 16)) return false;
    const auto* params = v["params"].getArray();
    const auto* lfos = v["lfos"].getArray();
    const auto* fx = v["masterFx"].getArray();
    if (!params || params->size() != 16 || !lfos || lfos->size() != 16 || !fx || fx->size() != 4) return false;
    for (int t = 0; t < 16; ++t)
        if (!fill((*params)[t], k.params[size_t(t)].data(), 24) || !fill((*lfos)[t], k.lfos[size_t(t)].data(), 36)) return false;
    for (int f = 0; f < 4; ++f) if (!fill((*fx)[f], k.masterFx[size_t(f)].data(), 8)) return false;
    fill(v["routes"], lk.routes.data(), 16);   // optional
    for (auto& r : lk.routes) r = juce::jlimit(0, 6, r);
    out = lk;
    return true;
}

const std::vector<mnm::md::Kit>& MdLibrary::dumpKits(const juce::String& id)
{
    auto it = m_dumpCache.find(id);
    if (it != m_dumpCache.end()) return it->second;
    std::vector<mnm::md::Kit> kits;
    try { kits = mnm::md::loadKits(m_root.getChildFile("dumps").getChildFile(id).getChildFile("original.syx").getFullPathName().toStdString()); }
    catch (const std::exception&) {}
    return m_dumpCache[id] = std::move(kits);
}

juce::StringArray MdLibrary::favourites()
{
    const auto v = juce::JSON::parse(m_root.getChildFile("user.json"));
    juce::StringArray out;
    if (const auto* a = v["favourites"].getArray()) for (const auto& f : *a) out.add(f.toString());
    return out;
}

bool MdLibrary::isFavourite(const juce::String& key) { const juce::ScopedLock sl(m_lock); return favourites().contains(key); }

void MdLibrary::setFavourite(const juce::String& key, bool on)
{
    const juce::ScopedLock sl(m_lock);
    auto favs = favourites();
    if (on) favs.addIfNotAlreadyThere(key); else favs.removeString(key);
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> a;
    for (const auto& f : favs) a.add(f);
    o->setProperty("favourites", a);
    m_root.createDirectory();
    m_root.getChildFile("user.json").replaceWithText(juce::JSON::toString(juce::var(o)));
}

std::vector<KitEntry> MdLibrary::kits()
{
    const juce::ScopedLock sl(m_lock);
    std::vector<KitEntry> out;
    const auto favs = favourites();
    auto saved = m_root.getChildFile("kits").findChildFiles(juce::File::findFiles, false, "*.json");
    std::sort(saved.begin(), saved.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    for (const auto& f : saved) {
        const auto v = juce::JSON::parse(f);
        KitEntry e;
        e.key = "saved:" + f.getFileNameWithoutExtension();
        e.name = v["name"].toString().toUpperCase();
        e.source = "SAVED"; e.sourceId = "saved";
        e.time = juce::Time(juce::int64(v["time"]));
        e.favourite = favs.contains(e.key);
        out.push_back(e);
    }
    auto dumps = m_root.getChildFile("dumps").findChildFiles(juce::File::findDirectories, false);
    std::sort(dumps.begin(), dumps.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    for (const auto& d : dumps) {
        const auto info = juce::JSON::parse(d.getChildFile("dump.json"));
        const auto id = d.getFileName();
        for (const auto& k : dumpKits(id)) {
            if (emptyKit(k)) continue;
            KitEntry e;
            e.key = "dump:" + id + ":" + juce::String(k.position);
            e.name = kitName(k);
            e.source = info["name"].toString().toUpperCase();
            e.sourceId = id;
            e.position = k.position;
            e.time = juce::Time(juce::int64(info["time"]));
            e.favourite = favs.contains(e.key);
            out.push_back(e);
        }
    }
    return out;
}

bool MdLibrary::load(const juce::String& key, LibraryKit& out)
{
    const juce::ScopedLock sl(m_lock);
    if (key.startsWith("saved:")) {
        const auto v = juce::JSON::parse(m_root.getChildFile("kits").getChildFile(key.fromFirstOccurrenceOf(":", false, false) + ".json"));
        return kitFromJson(v["kit"], out);
    }
    if (key.startsWith("dump:")) {
        const auto rest = key.fromFirstOccurrenceOf(":", false, false);
        const auto id = rest.upToFirstOccurrenceOf(":", false, false);
        const int pos = rest.fromFirstOccurrenceOf(":", false, false).getIntValue();
        for (const auto& k : dumpKits(id))
            if (k.position == pos) { out = LibraryKit{}; out.kit = k; return true; }
    }
    return false;
}

juce::Result MdLibrary::importSyx(const juce::File& syx, juce::String* idOut)
{
    const juce::ScopedLock sl(m_lock);
    std::vector<mnm::md::Kit> kits;
    try { kits = mnm::md::loadKits(syx.getFullPathName().toStdString()); } catch (const std::exception&) {}
    if (kits.empty()) return juce::Result::fail("No Machinedrum kits in " + syx.getFileName());
    const auto hash = fileHash(syx);
    for (const auto& d : m_root.getChildFile("dumps").findChildFiles(juce::File::findDirectories, false))
        if (juce::JSON::parse(d.getChildFile("dump.json"))["hash"].toString() == hash) {   // already in the library
            if (idOut) *idOut = d.getFileName();
            return juce::Result::ok();
        }
    const auto id = newId();
    const auto dir = m_root.getChildFile("dumps").getChildFile(id);
    if (!dir.createDirectory() || !syx.copyFileTo(dir.getChildFile("original.syx")))
        return juce::Result::fail("Could not write to the library at " + m_root.getFullPathName());
    auto* o = new juce::DynamicObject();
    o->setProperty("name", syx.getFileNameWithoutExtension());
    o->setProperty("file", syx.getFullPathName());
    o->setProperty("time", juce::Time::currentTimeMillis());
    o->setProperty("hash", hash);
    o->setProperty("kits", int(kits.size()));
    dir.getChildFile("dump.json").replaceWithText(juce::JSON::toString(juce::var(o)));
    m_dumpCache[id] = std::move(kits);
    if (idOut) *idOut = id;
    return juce::Result::ok();
}

juce::Result MdLibrary::saveKit(const juce::String& name, const LibraryKit& kit, const juce::String& parentKey, juce::String* keyOut)
{
    const juce::ScopedLock sl(m_lock);
    const auto dir = m_root.getChildFile("kits");
    if (!dir.createDirectory()) return juce::Result::fail("Could not write to the library at " + m_root.getFullPathName());
    const auto id = newId();
    auto lk = kit;
    lk.kit.name = name.toUpperCase().substring(0, 16).toStdString();
    auto* o = new juce::DynamicObject();
    o->setProperty("format", 1);
    o->setProperty("name", juce::String(lk.kit.name));
    o->setProperty("time", juce::Time::currentTimeMillis());
    o->setProperty("parent", parentKey);
    o->setProperty("kit", kitToJson(lk));
    if (!dir.getChildFile(id + ".json").replaceWithText(juce::JSON::toString(juce::var(o))))
        return juce::Result::fail("Could not write to the library at " + m_root.getFullPathName());
    if (keyOut) *keyOut = "saved:" + id;
    return juce::Result::ok();
}

bool MdLibrary::deleteSaved(const juce::String& key)
{
    const juce::ScopedLock sl(m_lock);
    if (!key.startsWith("saved:")) return false;
    return m_root.getChildFile("kits").getChildFile(key.fromFirstOccurrenceOf(":", false, false) + ".json").deleteFile();
}

juce::int64 MdLibrary::changeStamp() const
{
    juce::int64 s = 0;
    for (const char* sub : {"kits", "dumps"}) {
        const auto d = m_root.getChildFile(sub);
        s += d.getLastModificationTime().toMilliseconds() + d.getNumberOfChildFiles(juce::File::findFilesAndDirectories);
    }
    s += m_root.getChildFile("user.json").getLastModificationTime().toMilliseconds();
    return s;
}

} // namespace mnm::plugin::md
