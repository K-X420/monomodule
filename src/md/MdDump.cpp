#include "MdDump.h"
#include <algorithm>
#include <cstring>

namespace mnm::mddump {

namespace {

constexpr size_t kHeader = 10;   // F0 00 20 3C 02 00 id version revision position
constexpr uint8_t kDevice = 0x02;

bool isMdHeader(const uint8_t* m, size_t n)
{
    return n >= kHeader + 5 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x3C && m[4] == kDevice && m[n - 1] == 0xF7;
}

bool checksumOk(const uint8_t* m, size_t n)
{
    unsigned sum = 0;
    for (size_t i = 9; i < n - 5; ++i) sum += m[i];
    sum &= 0x3FFF;
    const unsigned len = unsigned(n - 10);
    return m[n - 5] == ((sum >> 7) & 0x7F) && m[n - 4] == (sum & 0x7F) && m[n - 3] == ((len >> 7) & 0x7F) && m[n - 2] == (len & 0x7F);
}

// Reads the body's sections in order
class Reader {
public:
    Reader(const uint8_t* m, size_t n) : m_m(m), m_pos(kHeader), m_end(n - 5) {}
    void raw(uint8_t* out, size_t len)
    {
        if (!m_ok || m_pos + len > m_end) { m_ok = false; return; }
        std::memcpy(out, m_m + m_pos, len);
        m_pos += len;
    }
    void packed(uint8_t* out, size_t len)
    {
        for (size_t i = 0; i < len && m_ok;) {
            const size_t g = std::min<size_t>(7, len - i);
            if (m_pos + 1 + g > m_end) { m_ok = false; return; }
            const uint8_t msb = m_m[m_pos++];
            for (size_t j = 0; j < g; ++j) out[i + j] = uint8_t(m_m[m_pos++] | ((msb << (j + 1)) & 0x80));
            i += g;
        }
    }
    size_t remaining() const { return m_end - m_pos; }
    bool ok() const { return m_ok; }
    bool done() const { return m_ok && m_pos == m_end; }
private:
    const uint8_t* m_m;
    size_t m_pos, m_end;
    bool m_ok = true;
};

class Writer {
public:
    Writer(uint8_t id, uint8_t version, uint8_t revision, int position)
        : m_v{0xF0, 0x00, 0x20, 0x3C, kDevice, 0x00, id, version, revision, uint8_t(position & 0x7F)} {}
    void raw(const uint8_t* in, size_t len) { for (size_t i = 0; i < len; ++i) m_v.push_back(in[i] & 0x7F); }
    void packed(const uint8_t* in, size_t len)
    {
        for (size_t i = 0; i < len; i += 7) {
            const size_t g = std::min<size_t>(7, len - i);
            uint8_t msb = 0;
            for (size_t j = 0; j < g; ++j) msb |= uint8_t((in[i + j] >> 7) << (6 - j));
            m_v.push_back(msb);
            for (size_t j = 0; j < g; ++j) m_v.push_back(in[i + j] & 0x7F);
        }
    }
    std::vector<uint8_t> finish()
    {
        unsigned sum = 0;
        for (size_t i = 9; i < m_v.size(); ++i) sum += m_v[i];
        sum &= 0x3FFF;
        const unsigned len = unsigned(m_v.size() + 5 - 10);
        m_v.push_back(uint8_t((sum >> 7) & 0x7F)); m_v.push_back(uint8_t(sum & 0x7F));
        m_v.push_back(uint8_t((len >> 7) & 0x7F)); m_v.push_back(uint8_t(len & 0x7F));
        m_v.push_back(0xF7);
        return std::move(m_v);
    }
private:
    std::vector<uint8_t> m_v;
};

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void putBe32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }

std::string printable(const uint8_t* raw, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n && raw[i]; ++i) if (raw[i] >= 32 && raw[i] < 127) s += char(raw[i]);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// Pattern sections
constexpr size_t kLockRowBytes = 32, kLockRows = 64;
constexpr size_t kPatExtended = 16 * 4 + 3 * 4 + kLockRows * kLockRowBytes + 48 * 4;   // 2316

} // namespace

bool Kit::isEmptySlot() const
{
    if (!name.empty()) return false;
    for (auto m : models) if ((m & 0xFF) != 0) return false;
    return true;
}

int Pattern::trigCount(int track) const
{
    int n = 0;
    for (int s = 0; s < length && s < 64; ++s) n += int((trigs[track] >> s) & 1);
    return n;
}

bool Pattern::empty() const
{
    for (int t = 0; t < kTracks; ++t) if (trigCount(t) > 0) return false;
    return true;
}

int Pattern::lockRow(int track, int param) const
{
    if (!((lockMasks[track] >> param) & 1)) return -1;
    int row = 0;
    for (int t = 0; t < kTracks; ++t)
        for (int p = 0; p < 24; ++p) {
            if (t == track && p == param) return row < int(kLockRows) ? row : -1;
            if ((lockMasks[t] >> p) & 1) ++row;
        }
    return -1;
}

bool Pattern::setLock(int track, int param, int step, int value)
{
    if (track < 0 || track >= kTracks || param < 0 || param >= 24 || step < 0 || step >= 64) return false;
    int row = 0;   // the row it has or would have: the locked (track, param) pairs before it
    for (int t = 0; t < kTracks; ++t)
        for (int q = 0; q < 24; ++q)
            if ((t < track || (t == track && q < param)) && ((lockMasks[t] >> q) & 1)) ++row;
    if (!((lockMasks[track] >> param) & 1)) {
        int used = 0;
        for (int t = 0; t < kTracks; ++t) for (int q = 0; q < 24; ++q) used += int((lockMasks[t] >> q) & 1);
        if (used >= int(kLockRows)) return false;
        for (int r = int(kLockRows) - 1; r > row; --r) std::memcpy(locks[r], locks[r - 1], 64);
        std::memset(locks[row], 0xFF, 64);
        lockMasks[track] |= 1u << param;
        numLockedRows = uint8_t(used + 1);
    }
    locks[row][step] = uint8_t(std::clamp(value, 0, 127));
    return true;
}

void Pattern::clearLock(int track, int param, int step)
{
    const int row = lockRow(track, param);
    if (row < 0 || step < 0 || step >= 64) return;
    locks[row][step] = 0xFF;
    for (int s = 0; s < 64; ++s) if (locks[row][s] <= 127) return;
    for (int r = row; r < int(kLockRows) - 1; ++r) std::memcpy(locks[r], locks[r + 1], 64);   // the row is empty: drop it
    std::memset(locks[kLockRows - 1], 0xFF, 64);
    lockMasks[track] &= ~(1u << param);
    if (numLockedRows > 0) --numLockedRows;
}

void Pattern::clearStepLocks(int track, int step)
{
    for (int q = 0; q < 24; ++q) clearLock(track, q, step);
}

std::string patternSlotName(int position)
{
    if (position < 0 || position > 127) return "?";
    const char bank = char('A' + position / 16);
    const int n = position % 16 + 1;
    return std::string(1, bank) + (n < 10 ? "0" : "") + std::to_string(n);
}

// ---------------------------------------------------------------------------------------------- kit

bool decodeKit(const uint8_t* msg, size_t n, Kit& kit)
{
    if (!isMdHeader(msg, n) || msg[6] != kKitId || !checksumOk(msg, n)) return false;
    if (msg[7] < 1 || msg[7] > 4) return false;   // stock kits only (MDX / SPS-X layouts differ)
    Kit k;
    k.version = msg[7]; k.revision = msg[8]; k.position = msg[9];
    Reader r(msg, n);
    uint8_t head[16 + kTracks * 24 + kTracks];
    r.raw(head, sizeof(head));
    std::memcpy(k.nameRaw, head, 16);
    std::memcpy(k.params, head + 16, sizeof(k.params));
    std::memcpy(k.levels, head + 16 + sizeof(k.params), kTracks);
    uint8_t models[kTracks * 4];
    r.packed(models, sizeof(models));
    for (int t = 0; t < kTracks; ++t) k.models[t] = be32(models + 4 * t);
    r.packed(&k.lfos[0][0], sizeof(k.lfos));
    uint8_t fx[32];
    r.raw(fx, sizeof(fx));
    std::memcpy(k.reverb, fx, 8); std::memcpy(k.delay, fx + 8, 8); std::memcpy(k.eq, fx + 16, 8); std::memcpy(k.dynamics, fx + 24, 8);
    uint8_t groups[32];
    r.packed(groups, sizeof(groups));
    std::memcpy(k.trigGroups, groups, 16); std::memcpy(k.muteGroups, groups + 16, 16);
    if (!r.done()) return false;
    k.name = printable(k.nameRaw, 16);
    kit = k;
    return true;
}

std::vector<uint8_t> encodeKit(const Kit& k)
{
    Writer w(kKitId, k.version, k.revision, k.position);
    uint8_t head[16 + kTracks * 24 + kTracks];
    std::memcpy(head, k.nameRaw, 16);
    std::memcpy(head + 16, k.params, sizeof(k.params));
    std::memcpy(head + 16 + sizeof(k.params), k.levels, kTracks);
    w.raw(head, sizeof(head));
    uint8_t models[kTracks * 4];
    for (int t = 0; t < kTracks; ++t) putBe32(models + 4 * t, k.models[t]);
    w.packed(models, sizeof(models));
    w.packed(&k.lfos[0][0], sizeof(k.lfos));
    uint8_t fx[32];
    std::memcpy(fx, k.reverb, 8); std::memcpy(fx + 8, k.delay, 8); std::memcpy(fx + 16, k.eq, 8); std::memcpy(fx + 24, k.dynamics, 8);
    w.raw(fx, sizeof(fx));
    uint8_t groups[32];
    std::memcpy(groups, k.trigGroups, 16); std::memcpy(groups + 16, k.muteGroups, 16);
    w.packed(groups, sizeof(groups));
    return w.finish();
}

// ---------------------------------------------------------------------------------------------- pattern

bool decodePattern(const uint8_t* msg, size_t n, Pattern& pat)
{
    if (!isMdHeader(msg, n) || msg[6] != kPatternId || !checksumOk(msg, n)) return false;
    Pattern p;
    p.version = msg[7]; p.revision = msg[8]; p.position = msg[9];
    Reader r(msg, n);
    uint8_t b[kLockRows * kLockRowBytes];
    r.packed(b, 64);
    for (int t = 0; t < kTracks; ++t) p.trigs[t] = be32(b + 4 * t);
    r.packed(b, 64);
    for (int t = 0; t < kTracks; ++t) p.lockMasks[t] = be32(b + 4 * t);
    r.packed(b, 16);
    p.accent = be32(b); p.slide = be32(b + 4); p.swing = be32(b + 8); p.swingAmount = be32(b + 12);
    r.raw(b, 6);
    p.accentAmount = b[0]; p.length = b[1]; p.doubleTempo = b[2]; p.scale = b[3]; p.kit = b[4]; p.numLockedRows = b[5];
    r.packed(b, kLockRows * kLockRowBytes);
    for (size_t i = 0; i < kLockRows; ++i) std::memcpy(p.locks[i], b + i * kLockRowBytes, kLockRowBytes);
    r.packed(b, 12 + 48 * 4);
    p.accentEditAll = be32(b); p.slideEditAll = be32(b + 4); p.swingEditAll = be32(b + 8);
    for (int t = 0; t < kTracks; ++t) {
        p.accentPerTrack[t] = be32(b + 12 + 4 * t);
        p.slidePerTrack[t] = be32(b + 12 + 64 + 4 * t);
        p.swingPerTrack[t] = be32(b + 12 + 128 + 4 * t);
    }
    if (!r.ok()) return false;
    if (r.remaining() > 0) {   // the extended (64-step) form: the upper halves
        p.extended = true;
        std::vector<uint8_t> x(kPatExtended);
        r.packed(x.data(), x.size());
        const uint8_t* q = x.data();
        for (int t = 0; t < kTracks; ++t) p.trigs[t] |= uint64_t(be32(q + 4 * t)) << 32;
        q += 64;
        p.accent |= uint64_t(be32(q)) << 32; p.slide |= uint64_t(be32(q + 4)) << 32; p.swing |= uint64_t(be32(q + 8)) << 32;
        q += 12;
        for (size_t i = 0; i < kLockRows; ++i) std::memcpy(p.locks[i] + 32, q + i * kLockRowBytes, kLockRowBytes);
        q += kLockRows * kLockRowBytes;
        for (int t = 0; t < kTracks; ++t) {
            p.accentPerTrack[t] |= uint64_t(be32(q + 4 * t)) << 32;
            p.slidePerTrack[t] |= uint64_t(be32(q + 64 + 4 * t)) << 32;
            p.swingPerTrack[t] |= uint64_t(be32(q + 128 + 4 * t)) << 32;
        }
    }
    if (!r.done()) return false;
    pat = p;
    return true;
}

std::vector<uint8_t> encodePattern(const Pattern& p)
{
    Writer w(kPatternId, p.version, p.revision, p.position);
    uint8_t b[kLockRows * kLockRowBytes];
    for (int t = 0; t < kTracks; ++t) putBe32(b + 4 * t, uint32_t(p.trigs[t]));
    w.packed(b, 64);
    for (int t = 0; t < kTracks; ++t) putBe32(b + 4 * t, p.lockMasks[t]);
    w.packed(b, 64);
    putBe32(b, uint32_t(p.accent)); putBe32(b + 4, uint32_t(p.slide)); putBe32(b + 8, uint32_t(p.swing)); putBe32(b + 12, p.swingAmount);
    w.packed(b, 16);
    const uint8_t mid[6] = {p.accentAmount, p.length, p.doubleTempo, p.scale, p.kit, p.numLockedRows};
    w.raw(mid, 6);
    for (size_t i = 0; i < kLockRows; ++i) std::memcpy(b + i * kLockRowBytes, p.locks[i], kLockRowBytes);
    w.packed(b, kLockRows * kLockRowBytes);
    putBe32(b, p.accentEditAll); putBe32(b + 4, p.slideEditAll); putBe32(b + 8, p.swingEditAll);
    for (int t = 0; t < kTracks; ++t) {
        putBe32(b + 12 + 4 * t, uint32_t(p.accentPerTrack[t]));
        putBe32(b + 12 + 64 + 4 * t, uint32_t(p.slidePerTrack[t]));
        putBe32(b + 12 + 128 + 4 * t, uint32_t(p.swingPerTrack[t]));
    }
    w.packed(b, 12 + 48 * 4);
    if (p.extended) {
        std::vector<uint8_t> x(kPatExtended);
        uint8_t* q = x.data();
        for (int t = 0; t < kTracks; ++t) putBe32(q + 4 * t, uint32_t(p.trigs[t] >> 32));
        q += 64;
        putBe32(q, uint32_t(p.accent >> 32)); putBe32(q + 4, uint32_t(p.slide >> 32)); putBe32(q + 8, uint32_t(p.swing >> 32));
        q += 12;
        for (size_t i = 0; i < kLockRows; ++i) std::memcpy(q + i * kLockRowBytes, p.locks[i] + 32, kLockRowBytes);
        q += kLockRows * kLockRowBytes;
        for (int t = 0; t < kTracks; ++t) {
            putBe32(q + 4 * t, uint32_t(p.accentPerTrack[t] >> 32));
            putBe32(q + 64 + 4 * t, uint32_t(p.slidePerTrack[t] >> 32));
            putBe32(q + 128 + 4 * t, uint32_t(p.swingPerTrack[t] >> 32));
        }
        w.packed(x.data(), x.size());
    }
    return w.finish();
}

// ---------------------------------------------------------------------------------------------- song

bool decodeSong(const uint8_t* msg, size_t n, Song& song)
{
    if (!isMdHeader(msg, n) || msg[6] != kSongId || !checksumOk(msg, n)) return false;
    Song s;
    s.version = msg[7]; s.revision = msg[8]; s.position = msg[9];
    Reader r(msg, n);
    r.raw(s.nameRaw, 16);
    if (!r.ok() || r.remaining() % 12 != 0) return false;   // each row: 10 bytes packed into 12
    while (r.ok() && r.remaining() > 0) {
        SongRow row;
        r.packed(row.bytes, 10);
        s.rows.push_back(row);
    }
    if (!r.done()) return false;
    s.name = printable(s.nameRaw, 16);
    song = s;
    return true;
}

std::vector<uint8_t> encodeSong(const Song& s)
{
    Writer w(kSongId, s.version, s.revision, s.position);
    w.raw(s.nameRaw, 16);
    for (const auto& row : s.rows) w.packed(row.bytes, 10);
    return w.finish();
}

// ---------------------------------------------------------------------------------------------- dump

bool isMachinedrumSysex(const uint8_t* data, size_t size)
{
    for (size_t i = 0; i + 5 < size; ++i)
        if (data[i] == 0xF0) return data[i + 1] == 0x00 && data[i + 2] == 0x20 && data[i + 3] == 0x3C && data[i + 4] == kDevice;
    return false;
}

Dump parseDump(const uint8_t* data, size_t size, const std::string& filename)
{
    Dump d;
    d.file = filename;
    size_t i = 0;
    while (i < size) {
        if (data[i] != 0xF0) { ++i; continue; }
        size_t j = i + 1;
        while (j < size && data[j] != 0xF7 && !(data[j] & 0x80)) ++j;
        if (j >= size || data[j] != 0xF7) { i = j; continue; }   // a truncated message: dropped like any non-sysex byte
        Message m;
        m.raw.assign(data + i, data + j + 1);
        const uint8_t* r = m.raw.data();
        const size_t n = m.raw.size();
        if (isMdHeader(r, n)) {
            m.id = r[6]; m.version = r[7]; m.revision = r[8]; m.position = r[9];
            if (m.id == kKitId) {
                Kit k;
                if (decodeKit(r, n, k)) { m.kitIndex = int(d.kits.size()); d.kits.push_back(k); }
                else if (checksumOk(r, n) && (m.version < 1 || m.version > 4)) ++d.numUnknown;   // another kit layout
                else { m.damaged = true; ++d.numDamaged; }
            } else if (m.id == kPatternId) {
                Pattern p;
                if (decodePattern(r, n, p)) { m.patternIndex = int(d.patterns.size()); d.patterns.push_back(p); }
                else { m.damaged = true; ++d.numDamaged; }
            } else if (m.id == kSongId) {
                Song s;
                if (decodeSong(r, n, s)) { m.songIndex = int(d.songs.size()); d.songs.push_back(s); }
                else { m.damaged = true; ++d.numDamaged; }
            } else if (m.id == kGlobalId) {
                ++d.numGlobals;
            } else {
                ++d.numUnknown;
            }
        } else {
            ++d.numUnknown;
        }
        d.messages.push_back(std::move(m));
        i = j + 1;
    }
    return d;
}

std::vector<uint8_t> encodeDump(const Dump& d)
{
    std::vector<uint8_t> out;
    for (const auto& m : d.messages) {
        std::vector<uint8_t> bytes;
        if (m.kitIndex >= 0) bytes = encodeKit(d.kits[size_t(m.kitIndex)]);
        else if (m.patternIndex >= 0) bytes = encodePattern(d.patterns[size_t(m.patternIndex)]);
        else if (m.songIndex >= 0) bytes = encodeSong(d.songs[size_t(m.songIndex)]);
        else bytes = m.raw;
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

} // namespace mnm::mddump
