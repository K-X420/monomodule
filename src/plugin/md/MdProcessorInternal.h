// Monomodule MD's processor, the parts its files share
#pragma once
#include "MdProcessor.h"
#include "SharedSettings.h"

namespace mnm::plugin::md {

namespace processor_detail {
using mnm::md::ControlCpu;
using mnm::md::MixEngine;
inline constexpr double kEngineRate = 44100.0;
inline constexpr int kBlock = mnm::md::VoiceEngine::kBlockFrames;
// DAC frame offsets of the outputs: A=2 B=5 C=1 D=4 E=0 F=3; the main bus is A/B (where MAIN lands)
inline constexpr int kBusChannels[3][2] = {{2, 5}, {1, 4}, {0, 3}};
inline constexpr int kHardwareBuses = 3;   // then "Track 1".."Track 16" (PER TRACK outputs)

inline juce::String loadOsPath() { return loadSharedSetting("mdOsPath"); }
} // namespace processor_detail

using namespace processor_detail;

} // namespace mnm::plugin::md
