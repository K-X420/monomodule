#include "MdProject.h"
#include <algorithm>
#include <cstring>

namespace mnm::mdproject {

using namespace mnm::mddump;

namespace {
bool sameBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) { return a == b; }

Kit* kitSlot(Dump& d, int pos) { for (auto& k : d.kits) if (k.position == pos) return &k; return nullptr; }
Pattern* patternSlot(Dump& d, int pos) { for (auto& p : d.patterns) if (p.position == pos) return &p; return nullptr; }

Kit& ensureKit(Dump& d, int pos)
{
    if (auto* k = kitSlot(d, pos)) return *k;
    Kit k;
    k.position = pos;
    for (int t = 0; t < kTracks; ++t) { k.trigGroups[t] = 127; k.muteGroups[t] = 127; k.lfos[t][0] = uint8_t(t); }
    Message m;
    m.id = kKitId; m.version = k.version; m.revision = k.revision; m.position = pos;
    m.kitIndex = int(d.kits.size());
    d.kits.push_back(k);
    d.messages.push_back(m);
    return d.kits.back();
}

Pattern& ensurePattern(Dump& d, int pos)
{
    if (auto* p = patternSlot(d, pos)) return *p;
    Pattern p;
    p.position = pos;
    Message m;
    m.id = kPatternId; m.version = p.version; m.revision = p.revision; m.position = pos;
    m.patternIndex = int(d.patterns.size());
    d.patterns.push_back(p);
    d.messages.push_back(m);
    return d.patterns.back();
}
} // namespace

bool kitInUse(const Dump& d, int pos) { const auto* k = d.kitAt(pos); return k && !k->isEmptySlot(); }
bool patternInUse(const Dump& d, int pos) { const auto* p = d.patternAt(pos); return p && !p->empty(); }

int firstFreeKitSlot(const Dump& d)
{
    for (int i = 0; i < kKitSlots; ++i) if (!kitInUse(d, i)) return i;
    return -1;
}

DumpDiff diffDumps(const Dump& a, const Dump& b)
{
    DumpDiff diff;
    for (int i = 0; i < kKitSlots; ++i) {
        const bool ua = kitInUse(a, i), ub = kitInUse(b, i);
        if (!ua && !ub) continue;
        ++diff.kitsCompared;
        if (ua && ub && sameBytes(encodeKit(*a.kitAt(i)), encodeKit(*b.kitAt(i)))) ++diff.kitsSame;
        else diff.kits.push_back(i);
    }
    for (int i = 0; i < kPatternSlots; ++i) {
        const bool ua = patternInUse(a, i), ub = patternInUse(b, i);
        if (!ua && !ub) continue;
        ++diff.patternsCompared;
        if (ua && ub && sameBytes(encodePattern(*a.patternAt(i)), encodePattern(*b.patternAt(i)))) ++diff.patternsSame;
        else diff.patterns.push_back(i);
    }
    return diff;
}

void putKit(Dump& d, int pos, const Kit& kit)
{
    auto& k = ensureKit(d, pos);
    k = kit;
    k.position = pos;
}

void putPattern(Dump& d, int pos, const Pattern& pattern, int kitSlotPos)
{
    auto& p = ensurePattern(d, pos);
    p = pattern;
    p.position = pos;
    if (kitSlotPos >= 0) p.kit = uint8_t(kitSlotPos);
}

void putSong(Dump& d, int pos, const Song& song)
{
    for (auto& s : d.songs)
        if (s.position == pos) { const auto v = s.version, r = s.revision; s = song; s.position = pos; s.version = v; s.revision = r; return; }
    Song s = song;
    s.position = pos;
    Message m;
    m.id = kSongId; m.version = s.version; m.revision = s.revision; m.position = pos;
    m.songIndex = int(d.songs.size());
    d.songs.push_back(s);
    d.messages.push_back(m);
}

void clearKit(Dump& d, int pos)
{
    if (auto* k = kitSlot(d, pos)) {
        Kit empty;
        empty.position = pos; empty.version = k->version; empty.revision = k->revision;
        for (int t = 0; t < kTracks; ++t) { empty.trigGroups[t] = 127; empty.muteGroups[t] = 127; empty.lfos[t][0] = uint8_t(t); }
        *k = empty;
    }
}

void clearPattern(Dump& d, int pos)
{
    if (auto* p = patternSlot(d, pos)) {
        Pattern empty;
        empty.position = pos; empty.version = p->version; empty.revision = p->revision; empty.kit = p->kit;
        *p = empty;
    }
}

void swapKits(Dump& d, int a, int b)
{
    if (a == b) return;
    ensureKit(d, a); ensureKit(d, b);   // both exist before taking references (ensuring may grow the vector)
    auto& ka = *kitSlot(d, a);
    auto& kb = *kitSlot(d, b);
    std::swap(ka, kb);
    ka.position = a; kb.position = b;
    for (auto& p : d.patterns) {   // every pattern keeps the kit it was written for
        if (p.kit == a) p.kit = uint8_t(b);
        else if (p.kit == b) p.kit = uint8_t(a);
    }
}

void swapPatterns(Dump& d, int a, int b)
{
    if (a == b) return;
    ensurePattern(d, a); ensurePattern(d, b);
    auto& pa = *patternSlot(d, a);
    auto& pb = *patternSlot(d, b);
    std::swap(pa, pb);
    pa.position = a; pb.position = b;
}

void copyKit(Dump& d, int from, int to)
{
    if (const auto* k = d.kitAt(from)) { const Kit copy = *k; putKit(d, to, copy); }
}

void copyPattern(Dump& d, int from, int to)
{
    if (const auto* p = d.patternAt(from)) { const Pattern copy = *p; putPattern(d, to, copy); }
}

std::vector<uint8_t> encodeSlots(const Dump& d, const std::vector<int>& kits, const std::vector<int>& patterns)
{
    std::vector<uint8_t> out;
    auto ks = kits; std::sort(ks.begin(), ks.end());
    auto ps = patterns; std::sort(ps.begin(), ps.end());
    for (int k : ks) if (const auto* kit = d.kitAt(k)) { const auto b = encodeKit(*kit); out.insert(out.end(), b.begin(), b.end()); }
    for (int p : ps) if (const auto* pat = d.patternAt(p)) { const auto b = encodePattern(*pat); out.insert(out.end(), b.begin(), b.end()); }
    return out;
}

} // namespace mnm::mdproject
