// What the library does with Machinedrum states (decoded dumps), as core/library/Project.h does for the Monomachine:
// comparing two states slot by slot, the slot operations of an edit, and partial exports. Slots are addressed by
// position (kits 0-63, patterns 0-127 = A01..H16). JUCE-free.
#pragma once
#include <vector>
#include "MdDump.h"

namespace mnm::mdproject {

struct DumpDiff {
    std::vector<int> kits, patterns;              // positions whose content differs (one side empty counts)
    int kitsCompared = 0, patternsCompared = 0;   // slots in use on at least one side
    int kitsSame = 0, patternsSame = 0;
    bool identical() const { return kits.empty() && patterns.empty(); }
    double similarity() const { const int n = kitsCompared + patternsCompared; return n ? double(kitsSame + patternsSame) / n : 0.0; }
};
DumpDiff diffDumps(const mddump::Dump& a, const mddump::Dump& b);

bool kitInUse(const mddump::Dump& d, int pos);
bool patternInUse(const mddump::Dump& d, int pos);
int firstFreeKitSlot(const mddump::Dump& d);      // -1 when all 64 are in use

// Slot operations. A slot the dump does not carry gets a message of its own, appended.
void putKit(mddump::Dump& d, int pos, const mddump::Kit& kit);
void putPattern(mddump::Dump& d, int pos, const mddump::Pattern& pattern, int kitSlot = -1);
void clearKit(mddump::Dump& d, int pos);
void clearPattern(mddump::Dump& d, int pos);
void swapKits(mddump::Dump& d, int a, int b);     // patterns follow their kit
void swapPatterns(mddump::Dump& d, int a, int b);
void copyKit(mddump::Dump& d, int from, int to);
void copyPattern(mddump::Dump& d, int from, int to);

// The sysex of chosen slots only (kits, then patterns, in slot order)
std::vector<uint8_t> encodeSlots(const mddump::Dump& d, const std::vector<int>& kits, const std::vector<int>& patterns);

} // namespace mnm::mdproject
