#include "MdKit.h"
#include <algorithm>
#include "firmware/Firmware.h"

namespace mnm::md {

namespace {
struct Reader {
    const uint8_t* p; const uint8_t* end;
    bool ok = true;
    uint8_t byte() { if (p >= end) { ok = false; return 0; } return *p++; }
    void raw(uint8_t* dst, size_t n) { for (size_t i = 0; i < n; ++i) dst[i] = byte(); }
    // n decoded bytes of a 7-bit section (the section starts a new group)
    void unpack7(uint8_t* dst, size_t n)
    {
        size_t i = 0;
        while (i < n) {
            const uint8_t msbs = byte();
            for (int k = 0; k < 7 && i < n; ++k, ++i) dst[i] = uint8_t(byte() | (((msbs >> (6 - k)) & 1) << 7));
        }
    }
};
}

bool parseKit(const uint8_t* msg, size_t len, Kit& out)
{
    static const uint8_t hdr[] = {0xF0, 0x00, 0x20, 0x3C, 0x02, 0x00, 0x52};
    if (len < 10 + 16 + 16 * 24 + 16 || std::equal(hdr, hdr + 7, msg) == false) return false;
    const uint8_t version = msg[7];
    if (version < 1 || version > 4) return false;
    Reader r{msg + 10, msg + len - 1};
    out.position = msg[9];
    char name[17] = {};
    r.raw(reinterpret_cast<uint8_t*>(name), 16);
    out.name = name;
    while (!out.name.empty() && (out.name.back() == ' ' || out.name.back() == '\0')) out.name.pop_back();
    for (auto& t : out.params) r.raw(t.data(), 24);
    r.raw(out.levels.data(), 16);
    uint8_t models[64];
    r.unpack7(models, 64);
    for (int t = 0; t < 16; ++t)
        out.machines[size_t(t)] = (uint32_t(models[4 * t]) << 24) | (uint32_t(models[4 * t + 1]) << 16) | (uint32_t(models[4 * t + 2]) << 8) | models[4 * t + 3];
    uint8_t lfos[16 * 36];
    r.unpack7(lfos, sizeof(lfos));
    for (auto& fx : out.masterFx) r.raw(fx.data(), 8);
    return r.ok;
}

std::vector<Kit> loadKits(const std::filesystem::path& syxPath)
{
    const auto data = fw::readFile(syxPath);
    std::vector<Kit> kits;
    for (size_t i = 0; i < data.size();) {
        if (data[i] != 0xF0) { ++i; continue; }
        size_t j = i;
        while (j < data.size() && data[j] != 0xF7) ++j;
        if (j >= data.size()) break;
        Kit k;
        if (parseKit(&data[i], j - i + 1, k)) kits.push_back(std::move(k));
        i = j + 1;
    }
    return kits;
}

} // namespace mnm::md
