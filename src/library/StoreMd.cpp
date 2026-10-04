// The Store's Machinedrum projects and saved Monomodule MD items (Store.h). A version's state is state.syx: the dump as
// MdDump encodes it, which for an import is the received file byte for byte.
#include "Store.h"
#include <cstring>

namespace mnm::library {

using namespace mnm::mddump;

namespace {
bool writeJsonFile(const juce::File& f, const juce::var& v)
{
    return f.getParentDirectory().createDirectory() && f.replaceWithText(juce::JSON::toString(v));
}
juce::var stringsVar(const juce::StringArray& a) { juce::Array<juce::var> v; for (const auto& s : a) v.add(s); return v; }
juce::StringArray stringsOf(const juce::var& v) { juce::StringArray a; if (const auto* arr = v.getArray()) for (const auto& s : *arr) a.add(s.toString()); return a; }
juce::String hexOf(const void* data, size_t n) { return juce::String::toHexString(data, int(n), 0); }
bool fromHex(const juce::var& v, std::vector<uint8_t>& out)
{
    juce::MemoryBlock mb;
    mb.loadFromHexString(v.toString());
    out.assign(static_cast<const uint8_t*>(mb.getData()), static_cast<const uint8_t*>(mb.getData()) + mb.getSize());
    return !out.empty();
}

void fillMdCounts(VersionInfo& v, const Dump& d)
{
    int named = 0, used = 0;
    for (const auto& k : d.kits) named += k.isEmptySlot() ? 0 : 1;
    for (const auto& p : d.patterns) used += p.empty() ? 0 : 1;
    v.kits = int(d.kits.size()); v.namedKits = named; v.patterns = int(d.patterns.size()); v.usedPatterns = used;
    v.songs = int(d.songs.size()); v.globals = d.numGlobals; v.damaged = d.numDamaged; v.unknown = d.numUnknown;
}

juce::String kitLabel(int k) { return juce::String(k + 1).paddedLeft('0', 2); }
} // namespace

juce::Result Store::writeMdVersion(const juce::String& projectId, VersionInfo& v, const Dump* state)
{
    v.n = nextVersionNumber(projectId);
    v.dir = versionDir(projectId, v.n);
    if (!v.dir.createDirectory()) return juce::Result::fail("Could not create " + v.dir.getFullPathName());
    if (state) {
        v.stateOf = v.n;
        fillMdCounts(v, *state);
        const auto bytes = encodeDump(*state);
        if (!v.dir.getChildFile("state.syx").replaceWithData(bytes.data(), bytes.size())) return juce::Result::fail("Could not write state.syx");
    }
    return writeVersionRecord(projectId, v);
}

juce::Result Store::importMdSysexData(const void* data, size_t size, const juce::String& name, const juce::String& sourceFile,
                                      ImportMode mode, const juce::String& projectId, ProjectInfo* out)
{
    auto d = parseDump(static_cast<const uint8_t*>(data), size, name.toStdString());
    if (d.messages.empty() || (d.kits.empty() && d.patterns.empty() && d.songs.empty() && d.numGlobals == 0 && d.numDamaged == 0))
        return juce::Result::fail(name + " does not look like a Machinedrum sysex dump.");
    juce::String pid = projectId;
    VersionInfo v;
    v.kind = "imported"; v.sourceFile = sourceFile;
    v.title = "Imported " + (sourceFile.isNotEmpty() ? juce::File(sourceFile).getFileName() : name);
    if (mode == ImportMode::NewVersion) {
        ProjectInfo p;
        if (!loadProject(pid, p) || p.versions.empty()) return juce::Result::fail("No such project");
        if (!p.isMd()) return juce::Result::fail(p.name + " is a Monomachine project; this is a Machinedrum dump.");
        v.parent = p.versions.front().n;
        Dump prev;
        if (loadMdVersion(pid, v.parent, prev)) {   // what the unit changed since
            const auto diff = mnm::mdproject::diffDumps(prev, d);
            juce::StringArray ks, ps;
            for (int k : diff.kits) ks.add(kitLabel(k));
            for (int pp : diff.patterns) ps.add(juce::String(patternSlotName(pp)));
            if (diff.identical()) v.changes.add("Identical to " + p.versions.front().label());
            if (!ks.isEmpty()) v.changes.add(juce::String(ks.size()) + (ks.size() == 1 ? " kit differs: " : " kits differ: ") + ks.joinIntoString(", "));
            if (!ps.isEmpty()) v.changes.add(juce::String(ps.size()) + (ps.size() == 1 ? " pattern differs: " : " patterns differ: ") + ps.joinIntoString(", "));
        }
    } else {
        pid = newId();
        auto* o = new juce::DynamicObject();
        o->setProperty("name", name); o->setProperty("device", "MD"); o->setProperty("pack", mode == ImportMode::Pack);
        o->setProperty("createdAt", juce::Time::getCurrentTime().toISO8601(true));
        if (!writeJsonFile(projectDir(pid).getChildFile("project.json"), juce::var(o))) return juce::Result::fail("Could not create the project");
    }
    if (v.changes.isEmpty()) {
        int named = 0, used = 0;
        for (const auto& k : d.kits) named += k.isEmptySlot() ? 0 : 1;
        for (const auto& p : d.patterns) used += p.empty() ? 0 : 1;
        v.changes.add(juce::String(named) + " named kits, " + juce::String(used) + " used patterns, " + juce::String(int(d.songs.size())) + " songs, " + juce::String(d.numGlobals) + " globals");
    }
    auto r = writeMdVersion(pid, v, &d);
    if (r.failed()) return r;
    if (!v.dir.getChildFile("original.syx").replaceWithData(data, size)) return juce::Result::fail("Could not archive the original dump");
    if (out) loadProject(pid, *out);
    return juce::Result::ok();
}

bool Store::loadMdVersion(const juce::String& projectId, int n, Dump& out) const
{
    VersionInfo v;
    if (!readVersion(versionDir(projectId, n), v)) return false;
    const auto dir = versionDir(projectId, v.stateOf > 0 ? v.stateOf : n);
    for (const char* f : {"state.syx", "original.syx"}) {
        juce::MemoryBlock mb;
        if (!dir.getChildFile(f).loadFileAsData(mb)) continue;
        out = parseDump(static_cast<const uint8_t*>(mb.getData()), mb.getSize(), projectId.toStdString());
        return true;
    }
    return false;
}

juce::Result Store::addMdVersion(const juce::String& projectId, const Dump& state, const juce::String& kind, const juce::String& title,
                                 const juce::StringArray& changes, const juce::String& note, int parent, VersionInfo* out)
{
    if (!projectDir(projectId).isDirectory()) return juce::Result::fail("No such project");
    VersionInfo v;
    v.kind = kind; v.title = title; v.changes = changes; v.note = note; v.parent = parent;
    auto r = writeMdVersion(projectId, v, &state);
    if (r.wasOk() && out) *out = v;
    return r;
}

std::optional<Store::SimilarMd> Store::findSimilarMd(const Dump& dump)
{
    std::optional<SimilarMd> best;
    for (const auto& p : listProjects()) {
        if (p.pack || !p.isMd()) continue;
        Dump cur;
        if (!loadMdVersion(p.id, p.versions.front().n, cur)) continue;
        auto diff = mnm::mdproject::diffDumps(cur, dump);
        if (diff.similarity() >= 0.5 && (!best || diff.similarity() > best->diff.similarity())) best = SimilarMd{p.id, p.name, p.versions.front().n, std::move(diff)};
    }
    return best;
}

juce::Result Store::exportMdVersion(const juce::String& projectId, int n, const juce::File& dest, const std::vector<int>* kits,
                                    const std::vector<int>* patterns, VersionInfo* out)
{
    VersionInfo src;
    Dump d;
    if (!readVersion(versionDir(projectId, n), src) || !loadMdVersion(projectId, n, d)) return juce::Result::fail("No such version");
    const bool whole = kits == nullptr && patterns == nullptr;
    const auto bytes = whole ? encodeDump(d) : mnm::mdproject::encodeSlots(d, kits ? *kits : std::vector<int>{}, patterns ? *patterns : std::vector<int>{});
    if (bytes.empty()) return juce::Result::fail("Nothing to export");
    if (!dest.replaceWithData(bytes.data(), bytes.size())) return juce::Result::fail("Could not write " + dest.getFullPathName());
    ProjectInfo p;
    loadProject(projectId, p);
    VersionInfo v = src;
    v.kind = "exported"; v.title = "Exported to " + dest.getFileName(); v.note.clear(); v.sourceFile.clear(); v.time = {};
    v.exportFile = dest.getFullPathName();
    v.parent = p.versions.empty() ? n : p.versions.front().n;
    v.stateOf = src.stateOf > 0 ? src.stateOf : n;
    v.changes.clear();
    if (whole) v.changes.add("The whole project: every kit, pattern, song and global");
    else {
        juce::StringArray ks, ps;
        if (kits) for (int k : *kits) ks.add(kitLabel(k));
        if (patterns) for (int pp : *patterns) ps.add(juce::String(patternSlotName(pp)));
        if (!ks.isEmpty()) v.changes.add("Kits " + ks.joinIntoString(", "));
        if (!ps.isEmpty()) v.changes.add("Patterns " + ps.joinIntoString(", "));
    }
    v.n = nextVersionNumber(projectId);
    v.dir = versionDir(projectId, v.n);
    if (!v.dir.createDirectory()) return juce::Result::fail("Could not create " + v.dir.getFullPathName());
    auto r = writeVersionRecord(projectId, v);
    if (r.failed()) return r;
    v.dir.getChildFile("export.syx").replaceWithData(bytes.data(), bytes.size());
    if (out) *out = v;
    return juce::Result::ok();
}

juce::Result Store::saveMdItem(const SavedMdItem& item, juce::String* idOut)
{
    const juce::String id = item.id.isNotEmpty() ? item.id : newId();
    auto* o = new juce::DynamicObject();
    o->setProperty("format", 1); o->setProperty("kind", item.isKit ? "md-kit" : "md-sound");
    o->setProperty("name", item.name); o->setProperty("parent", item.parent); o->setProperty("savedFrom", item.savedFrom);
    o->setProperty("tags", stringsVar(item.tags)); o->setProperty("time", (item.time == juce::Time() ? juce::Time::getCurrentTime() : item.time).toISO8601(true));
    if (item.isKit) {
        const auto syx = encodeKit(item.kit);   // the kit message itself: lossless, and what a hardware export would send
        o->setProperty("kitSyx", hexOf(syx.data(), syx.size()));
    } else {
        const auto& s = item.sound;
        o->setProperty("model", juce::int64(s.model));
        o->setProperty("params", hexOf(s.params, sizeof(s.params)));
        o->setProperty("lfo", hexOf(s.lfo, sizeof(s.lfo)));
        o->setProperty("track", s.track);
    }
    const auto f = m_root.getChildFile("items").getChildFile(item.isKit ? "mdkits" : "mdsounds").getChildFile(id + ".json");
    if (!writeJsonFile(f, juce::var(o))) return juce::Result::fail("Could not write " + f.getFullPathName());
    if (idOut) *idOut = id;
    return juce::Result::ok();
}

std::vector<SavedMdItem> Store::listSavedMdItems() const
{
    std::vector<SavedMdItem> v;
    for (const char* sub : {"mdsounds", "mdkits"}) {
        auto files = m_root.getChildFile("items").getChildFile(sub).findChildFiles(juce::File::findFiles, false, "*.json");
        files.sort();
        for (const auto& f : files) {
            const auto json = juce::JSON::parse(f.loadFileAsString());
            auto* o = json.getDynamicObject();
            if (!o) continue;
            SavedMdItem it;
            it.id = f.getFileNameWithoutExtension(); it.isKit = o->getProperty("kind").toString() == "md-kit";
            it.name = o->getProperty("name").toString(); it.parent = o->getProperty("parent").toString(); it.savedFrom = o->getProperty("savedFrom").toString();
            it.tags = stringsOf(o->getProperty("tags")); it.time = juce::Time::fromISO8601(o->getProperty("time").toString());
            if (it.isKit) {
                std::vector<uint8_t> syx;
                if (!fromHex(o->getProperty("kitSyx"), syx) || !decodeKit(syx.data(), syx.size(), it.kit)) continue;
            } else {
                std::vector<uint8_t> params, lfo;
                if (!fromHex(o->getProperty("params"), params) || params.size() != 24 || !fromHex(o->getProperty("lfo"), lfo) || lfo.size() != 36) continue;
                it.sound.model = uint32_t(juce::int64(o->getProperty("model")));
                std::memcpy(it.sound.params, params.data(), 24);
                std::memcpy(it.sound.lfo, lfo.data(), 36);
                it.sound.track = juce::jlimit(0, 15, int(o->getProperty("track")));
            }
            v.push_back(std::move(it));
        }
    }
    std::reverse(v.begin(), v.end());
    return v;
}

} // namespace mnm::library
