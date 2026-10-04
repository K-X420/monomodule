#include "MdFirmware.h"
#include <algorithm>

namespace mnm::md {

namespace {

// F0 00 20 3C 02 00 7E ... : the Monomachine transport with device id 2
fw::FlashImage parseMdSysex(const std::vector<uint8_t>& syx)
{
    fw::FlashImage img;
    bool haveBase = false;
    uint32_t expect = 0;
    for (size_t i = 0; i < syx.size();) {
        if (syx[i] != 0xF0) { ++i; continue; }
        size_t j = i;
        while (j < syx.size() && syx[j] != 0xF7) ++j;
        if (j >= syx.size()) break;
        const size_t len = j - i + 1;
        const uint8_t* m = &syx[i];
        if (len > 15 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x3C && m[4] == 0x02 && m[5] == 0x00 && m[6] == 0x7E) {
            uint32_t addr = 0;
            for (int k = 0; k < 6; ++k) addr = (addr << 4) | (m[9 + k] & 0xF);
            if ((len - 16) % 3 != 0) throw fw::FirmwareError("sysex data block with bad length");
            if (!haveBase) { img.base = expect = addr; haveBase = true; }
            if (addr != expect) throw fw::FirmwareError("non-contiguous sysex data block");
            for (size_t k = 15; k + 2 < len - 1; k += 3) {
                const uint32_t w = (uint32_t(m[k]) << 14) | (uint32_t(m[k + 1]) << 7) | m[k + 2];
                img.bytes.push_back(uint8_t(w >> 8));
                img.bytes.push_back(uint8_t(w & 0xFF));
            }
            expect += uint32_t((len - 16) / 3 * 2);
        } else {
            img.trailers.emplace_back(m, m + len);
        }
        i = j + 1;
    }
    if (!haveBase) throw fw::FirmwareError("no Machinedrum OS data messages found");
    return img;
}

fw::DspImage parseRecords(const std::vector<uint8_t>& sec)
{
    fw::DspImage img;
    const size_t nw = sec.size() / 3;
    auto w = [&](size_t k) { return uint32_t(sec[3 * k]) | (uint32_t(sec[3 * k + 1]) << 8) | (uint32_t(sec[3 * k + 2]) << 16); };
    for (size_t p = 0; p < nw;) {
        const uint32_t t = w(p);
        if (t == 3 || t == 4) {   // 3 = start address, 4 = marker (value unused)
            if (p + 1 >= nw) break;
            if (t == 3 && !img.startAddr) img.startAddr = w(p + 1);
            p += 2;
            continue;
        }
        if (t > 4 || p + 3 > nw) throw fw::FirmwareError("dsp records: bad record header");
        fw::DspRecord r;
        r.space = fw::Space(t);
        r.addr = w(p + 1);
        const uint32_t cnt = w(p + 2);
        if (p + 3 + cnt > nw) throw fw::FirmwareError("dsp records: truncated record");
        r.words.resize(cnt);
        for (uint32_t k = 0; k < cnt; ++k) r.words[k] = w(p + 3 + k);
        img.records.push_back(std::move(r));
        p += 3 + cnt;
    }
    return img;
}

constexpr uint32_t kDescriptorEmpty = 0x24EF54;
constexpr uint32_t kIdTable = 0x252092;

} // namespace

const Machine* Firmware::byId(int id) const
{
    for (const auto& m : machines) if (m.id == id) return &m;
    return nullptr;
}

Firmware loadFirmware(const std::filesystem::path& syxPath)
{
    const auto c = fw::parseContainer(parseMdSysex(fw::readFile(syxPath)));
    if (c.sections.size() != 3) throw fw::FirmwareError("OS file has " + std::to_string(c.sections.size()) + " sections, expected 3 (not a Machinedrum OS?)");
    for (const auto& s : c.sections) if (!s.sumOk) throw fw::FirmwareError("section " + std::to_string(s.index) + " checksum mismatch");
    Firmware f;
    f.source = syxPath;
    f.mainOs = c.sections[0].data;
    f.voiceDsp = parseRecords(c.sections[1].data);
    f.mixDsp = parseRecords(c.sections[2].data);

    const auto& os = f.mainOs;
    auto inOs = [&](uint32_t a, uint32_t n) { return a >= kMainOsBase && a - kMainOsBase + n <= os.size(); };
    auto be32 = [&](uint32_t a) { const uint8_t* p = &os[a - kMainOsBase]; return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; };
    if (!inOs(kIdTable, 192 * 4)) throw fw::FirmwareError("MainOS too small for the machine table (not OS 1.63?)");
    for (int id = 1; id < 192; ++id) {
        const uint32_t d = be32(kIdTable + 4 * uint32_t(id));
        if (d == kDescriptorEmpty || !inOs(d, 86)) continue;
        const uint8_t* r = &os[d - kMainOsBase];
        if (r[4] != id) continue;   // descriptor must carry its own ID
        Machine m;
        m.id = id;
        m.handler = be32(d);
        m.family.assign(reinterpret_cast<const char*>(r + 5), 3);
        m.suffix.assign(reinterpret_cast<const char*>(r + 8), 2);
        for (int k = 0; k < 8; ++k) {
            std::string l(reinterpret_cast<const char*>(r + 10 + 4 * k), 4);
            l.erase(std::find(l.begin(), l.end(), '\0'), l.end());
            while (!l.empty() && l.back() == ' ') l.pop_back();
            m.labels[size_t(k)] = l;
            m.defaults[size_t(k)] = r[42 + k];
        }
        for (int k = 0; k < 4; ++k) m.format[size_t(k)] = r[50 + k];
        f.machines.push_back(std::move(m));
    }
    if (f.machines.empty()) throw fw::FirmwareError("no machines found in the MainOS");
    return f;
}

} // namespace mnm::md
