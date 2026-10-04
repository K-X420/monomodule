// Drag-and-drop transfer between the Library app and the plugins: a track (.mnmtrack) or a whole kit
// (.mnmkit) as a JSON file the plugin editors accept as a file drop, and pattern MIDI (.mid) for the DAW.
// The JSON carries the complete kit (kitToJson) plus the track index, so nothing is lost on the way and a
// track drop can also apply the kit-level flags that belong to it (routing, key tracking).
#pragma once
#include <juce_core/juce_core.h>
#include "library/MnmDump.h"
#include "MdCatalog.h"
#include "MdDump.h"

namespace mnm::library {

constexpr const char* kTrackFileExtension = ".mnmtrack";
constexpr const char* kKitFileExtension = ".mnmkit";

enum class TransferKind { None, Track, Kit };

struct TransferPayload {
    TransferKind kind = TransferKind::None;
    juce::String name;        // display name ("SUPERWAVES T1", "SUPERWAVES")
    mnm::dump::Kit kit;
    int track = -1;           // for Track: which of kit.tracks
};

juce::var trackToTransferJson(const mnm::dump::Kit& kit, int track, const juce::String& name);
juce::var kitToTransferJson(const mnm::dump::Kit& kit, const juce::String& name);
bool transferFromJson(const juce::var& json, TransferPayload& out);
bool readTransferFile(const juce::File& file, TransferPayload& out);
bool isTransferFile(const juce::String& path, TransferKind kind);   // by extension

// Files handed to the OS for an external drag live in a per-process temp folder, cleared on first use.
juce::File dragDirectory();
juce::File writeTrackDragFile(const mnm::dump::Kit& kit, int track, const juce::String& baseName);
juce::File writeKitDragFile(const mnm::dump::Kit& kit, const juce::String& baseName);
// track = -1: the whole pattern as a multi-track SMF (one MIDI track per Monomachine track with trigs).
juce::File writePatternMidiDragFile(const mnm::dump::Dump& dump, const mnm::dump::Pattern& pat, int track);

// Machinedrum: a sound (.mdsound: one track's machine, parameters and LFO) or a kit (.mdkit: the kit message itself,
// lossless), dragged from the Library app onto Monomodule MD.
constexpr const char* kMdSoundFileExtension = ".mdsound";
constexpr const char* kMdKitFileExtension = ".mdkit";
struct MdTransferPayload {
    bool isKit = false;
    juce::String name;
    mnm::mdcatalog::Sound sound;
    mnm::mddump::Kit kit;
};
juce::File writeMdSoundDragFile(const mnm::mdcatalog::Sound& sound, const juce::String& baseName);
juce::File writeMdKitDragFile(const mnm::mddump::Kit& kit, const juce::String& baseName);
bool readMdTransferFile(const juce::File& file, MdTransferPayload& out);
// A Machinedrum pattern's MIDI (MdMidiExport.h): track -1 = every track with trigs
juce::File writeMdPatternMidiDragFile(const mnm::mddump::Kit* kit, const mnm::mddump::Pattern& pat, const juce::String& baseName, int track);
inline bool isMdTransferFile(const juce::String& path) { return path.endsWithIgnoreCase(kMdSoundFileExtension) || path.endsWithIgnoreCase(kMdKitFileExtension); }

} // namespace mnm::library
