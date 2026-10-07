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

bool Pattern::hasExtras() const
{
    for (int t = 0; t < kTracks; ++t)
        for (int s = 0; s < 64; ++s) if (cond[t][s] || micro[t][s] || retrig[t][s]) return true;
    return false;
}

void Pattern::clearStepExtras(int track, int step)
{
    if (track < 0 || track >= kTracks || step < 0 || step >= 64) return;
    cond[track][step] = 0; micro[track][step] = 0; retrig[track][step] = 0;
}

namespace {
constexpr int kProbabilities[21] = {1, 2, 4, 6, 9, 13, 19, 25, 33, 41, 50, 59, 67, 75, 81, 87, 91, 94, 96, 98, 99};
}

int conditionPercent(int c) { return c >= 1 && c <= 21 ? kProbabilities[c - 1] : -1; }

bool conditionRatio(int c, int& a, int& b)
{
    if (c < kCondRatio || c >= kConditions) return false;
    int i = c - kCondRatio;
    for (b = 2; b <= 8; ++b) { if (i < b) { a = i + 1; return true; } i -= b; }
    return false;
}

std::string conditionName(int c)
{
    static const char* const named[8] = {"FILL", "!FILL", "PRE", "!PRE", "NEI", "!NEI", "1ST", "!1ST"};
    if (const int p = conditionPercent(c); p > 0) return std::to_string(p) + "%";
    if (c >= kCondFill && c < kCondRatio) return named[c - kCondFill];
    int a = 0, b = 0;
    if (conditionRatio(c, a, b)) return std::to_string(a) + ":" + std::to_string(b);
    return {};
}

int retrigHits(uint8_t r)
{
    static const int hits[8] = {0, 2, 3, 4, 6, 8, 12, 16};
    return hits[retrigRate(r)];
}

std::string retrigName(uint8_t r)
{
    const int h = retrigHits(r);
    if (h == 0) return {};
    std::string s = "1/" + std::to_string(16 * h);
    if (retrigSteps(r) > 1) s += "x" + std::to_string(retrigSteps(r));
    return s;
}

namespace {
void setBit(uint64_t& m, int s, bool on) { if (on) m |= 1ull << s; else m &= ~(1ull << s); }
bool getBit(uint64_t m, int s) { return (m >> s) & 1; }
}

void Pattern::copySteps(const Pattern& src, int from, int to, int count, int track, int dstTrack)
{
    const bool all = track < 0;
    for (int i = 0; i < count; ++i) {
        const int s = from + i, d = to + i;
        if (s < 0 || s >= 64 || d < 0 || d >= 64) continue;
        if (all) {
            setBit(accent, d, getBit(src.accent, s)); setBit(slide, d, getBit(src.slide, s)); setBit(swing, d, getBit(src.swing, s));
        }
        for (int t = all ? 0 : track; t < (all ? kTracks : track + 1); ++t) {
            const int dt = all ? t : dstTrack;
            if (dt < 0 || dt >= kTracks) continue;
            setBit(trigs[dt], d, getBit(src.trigs[t], s));
            setBit(accentPerTrack[dt], d, getBit(src.accentPerTrack[t], s));
            setBit(slidePerTrack[dt], d, getBit(src.slidePerTrack[t], s));
            setBit(swingPerTrack[dt], d, getBit(src.swingPerTrack[t], s));
            for (int q = 0; q < 24; ++q) {
                const int row = src.lockRow(t, q);
                const int v = row >= 0 ? src.locks[row][s] : 0xFF;
                if (v <= 127) setLock(dt, q, d, v); else clearLock(dt, q, d);
            }
            cond[dt][d] = src.cond[t][s]; micro[dt][d] = src.micro[t][s]; retrig[dt][d] = src.retrig[t][s];
        }
    }
}

void Pattern::clearSteps(int from, int count, int track)
{
    const bool all = track < 0;
    for (int i = 0; i < count; ++i) {
        const int s = from + i;
        if (s < 0 || s >= 64) continue;
        if (all) { setBit(accent, s, false); setBit(slide, s, false); setBit(swing, s, false); }
        for (int t = all ? 0 : track; t < (all ? kTracks : track + 1); ++t) {
            if (t < 0 || t >= kTracks) continue;
            setBit(trigs[t], s, false);
            setBit(accentPerTrack[t], s, false); setBit(slidePerTrack[t], s, false); setBit(swingPerTrack[t], s, false);
            clearStepLocks(t, s);
            clearStepExtras(t, s);
        }
    }
}

bool decodeGlobal(const uint8_t* msg, size_t n, Global& g)
{
    if (n < 10 + 16 + 147 + 19 + 5 || msg[6] != kGlobalId || !checksumOk(msg, n)) return false;
    g.position = msg[9];
    for (int t = 0; t < kTracks; ++t) g.routing[t] = msg[10 + t];
    size_t i = 26;
    int got = 0;
    while (got < 128 && i < n) {   // 7-bit packed: a byte of top bits, then up to 7
        const uint8_t hi = msg[i++];
        for (int j = 0; j < 7 && got < 128 && i < n; ++j) g.keyMap[got++] = uint8_t(msg[i++] | ((hi & (0x40 >> j)) ? 0x80 : 0));
    }
    if (got < 128 || i + 19 > n - 5) return false;
    const uint8_t* r = msg + i;
    g.baseChannel = r[0];
    g.tempo = (r[2] << 7) | r[3];
    g.flags = r[5];
    g.programChange = r[17];
    g.trigMode = r[18];
    return true;
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

bool isPatternExtras(const uint8_t* m, size_t n)
{
    return n >= 9 && m[0] == 0xF0 && m[1] == 0x7D && m[2] == 'M' && m[3] == 'N' && m[4] == 'X' && m[n - 1] == 0xF7;
}

std::vector<uint8_t> encodePatternExtras(const Pattern& p)
{
    std::vector<uint8_t> out;
    for (int t = 0; t < kTracks; ++t)
        for (int s = 0; s < 64; ++s)
            if (p.cond[t][s] || p.micro[t][s] || p.retrig[t][s]) {
                if (out.empty()) out = {0xF0, 0x7D, 'M', 'N', 'X', 1, uint8_t(p.position & 0x7F)};
                const int m = std::clamp(int(p.micro[t][s]), -23, 23) + 64;
                out.insert(out.end(), {uint8_t(t), uint8_t(s), uint8_t(p.cond[t][s] & 0x7F), uint8_t(m), uint8_t(p.retrig[t][s] & 0x7F)});
            }
    if (!out.empty()) out.push_back(0xF7);
    return out;
}

bool decodePatternExtras(const uint8_t* m, size_t n, int& position, Pattern& p)
{
    if (!isPatternExtras(m, n) || m[5] != 1 || (n - 8) % 5 != 0) return false;
    position = m[6];
    for (int t = 0; t < kTracks; ++t) for (int s = 0; s < 64; ++s) p.clearStepExtras(t, s);
    for (size_t i = 7; i + 5 <= n - 1; i += 5) {
        const int t = m[i], s = m[i + 1];
        if (t >= kTracks || s >= 64) continue;
        p.cond[t][s] = m[i + 2] < kConditions ? m[i + 2] : 0;
        p.micro[t][s] = int8_t(std::clamp(int(m[i + 3]) - 64, -23, 23));
        p.retrig[t][s] = uint8_t(m[i + 4] & 0x3F);
    }
    return true;
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
                Global g;
                if (decodeGlobal(r, n, g)) d.globals.push_back(g);
            } else {
                ++d.numUnknown;
            }
        } else if (isPatternExtras(r, n)) {
            m.extras = true;   // put on its pattern below (it comes after it)
        } else {
            ++d.numUnknown;
        }
        d.messages.push_back(std::move(m));
        i = j + 1;
    }
    for (const auto& m : d.messages) {
        if (!m.extras) continue;
        int pos = -1;
        Pattern probe;
        if (!decodePatternExtras(m.raw.data(), m.raw.size(), pos, probe)) continue;
        for (auto it = d.patterns.rbegin(); it != d.patterns.rend(); ++it)
            if (it->position == pos) {
                std::memcpy(it->cond, probe.cond, sizeof(probe.cond));
                std::memcpy(it->micro, probe.micro, sizeof(probe.micro));
                std::memcpy(it->retrig, probe.retrig, sizeof(probe.retrig));
                break;
            }
    }
    return d;
}

std::vector<uint8_t> encodeDump(const Dump& d, bool withExtras)
{
    std::vector<uint8_t> out;
    for (const auto& m : d.messages) {
        std::vector<uint8_t> bytes;
        if (m.extras) continue;   // written again after its pattern (from the pattern, so edits take effect)
        if (m.kitIndex >= 0) bytes = encodeKit(d.kits[size_t(m.kitIndex)]);
        else if (m.patternIndex >= 0) {
            bytes = encodePattern(d.patterns[size_t(m.patternIndex)]);
            if (withExtras) { const auto x = encodePatternExtras(d.patterns[size_t(m.patternIndex)]); bytes.insert(bytes.end(), x.begin(), x.end()); }
        }
        else if (m.songIndex >= 0) bytes = encodeSong(d.songs[size_t(m.songIndex)]);
        else bytes = m.raw;
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

} // namespace mnm::mddump
