// Audio previews of Machinedrum items, as the Monomachine ones (core/preview/Preview.h), rendered with the emulated
// Machinedrum (MdEngine) from the user's MD OS file:
//   pattern  the pattern on its kit, looped to a minimum length, then a tail for decays and the master effects;
//            the mix and the 16 voices come out of the same render
//   kit      its best pattern (the caller picks it), else a demo pattern: each track in turn, then all together
//   sound    one trig of the sound alone (track 1 of an otherwise empty kit)
// The sequencer plays what the pattern holds: trigs, accents (the pattern's accent amount), swing, parameter locks and
// double tempo, at the preview tempo (patterns carry none; it is global on the unit). MID and CTR tracks make no sound;
// a CTR track's locks act as on the unit, into the kit (they last until the next one): CTR-RE / GB / EQ / DX set
// their master effect's parameter, CTR-AL moves that parameter on every audio track by the change, CTR-8P sets the
// parameter its P knob is assigned to; an LFO on a CTR-RE..DX knob moves the master effect (MdEngine).
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "MdCatalog.h"
#include "MdDump.h"
#include "MdEngine.h"
#include "MdMachines.h"

namespace mnm::mdpreview {

constexpr int kSampleRate = 44100;

struct Options {
    double bpm = 120.0;
    double minSeconds = 4.0;     // loop a pattern until at least this long ...
    int maxLoops = 4;            // ... but no more than this often
    double tailSeconds = 1.5;
    double soundSeconds = 1.5;   // a sound's trig and its decay
};

struct Event {
    uint32_t frame = 0;
    int track = 0;
    int accent = -128;                     // the trig's accent factor (-128 = none)
    std::vector<std::pair<int, int>> locks;   // (param 0-23, value) for this trig
};

struct Spec {
    mddump::Kit kit;
    std::vector<Event> events;            // in time order
    uint32_t frames = 0, loopFrames = 0;
    int loops = 1;
    bool stems = true;                    // the 16 voices are of interest (patterns and kits)
};

Spec patternPreview(const mddump::Kit& kit, const mddump::Pattern& pat, const Options& opt);
Spec soundPreview(const mdcatalog::Sound& sound, const Options& opt);
mddump::Pattern demoPattern(const mddump::Kit& kit);   // 16 steps: the tracks with a machine in turn, then all on step 13
// The kit's pattern to preview: the most tracks with trigs, then the most trigs; "" for none
std::string choosePreviewPattern(const mdcatalog::Catalog& cat, const mdcatalog::KitItem& kit);

struct Block {
    static constexpr int kFrames = md::Engine::kFrames;
    std::array<float, kFrames> mixL{}, mixR{};
    std::array<std::array<float, kFrames>, 16> stem{};   // the voices (mono, before the track effects)
};

class Renderer {
public:
    explicit Renderer(const md::Firmware& fw);   // throws when a DSP cannot boot
    // Renders the spec's frames in passes of 32; false (with error()) on a failure or when cancelled
    bool render(const Spec& spec, double bpm, const std::function<void(uint32_t frame0, const Block&)>& out, const std::atomic<bool>* cancel = nullptr);
    const std::string& error() const { return m_error; }
private:
    void loadKit(const mddump::Kit& kit, double bpm);
    md::Engine::Track trackFor(const mddump::Kit& kit, int t) const;   // the engine's view of a kit track (MID/CTR silent)
    // A CTR track's parameter set to v (a lock), as the OS's parameter-change routine does; the kit and master
    // effects it changes are updated in place. Returns true when an audio track's parameters changed.
    bool control(mddump::Kit& kit, int t, int q, int v, std::array<bool, 16>& changed);
    std::unique_ptr<md::Engine> m_engine;
    const md::Firmware& m_fw;
    std::string m_error;
};

} // namespace mnm::mdpreview
