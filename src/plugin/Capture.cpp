// Dev tool (not installed): renders fixed audio scenarios through the plugin processor, along the exact path a host
// drives (MnmOneProcessor::prepareToPlay + processBlock with MIDI, a transport and the main/side-chain input), to
// 32-bit float WAV files. Made for before/after comparisons of the audio path: the 44.1 kHz references the resampling
// rework is checked against (issue #3), and the FX key-tracking check (issue #5).
//
//   MnmCapture --list
//   MnmCapture --out <dir> [--fw os.syx] [--rates 44100,48000] [--blocks 512,32,441,2048,var] [--scenario a,b]
//               [--inputs] [--keep-blocks] [--note "text"]
//       Renders every scenario at every rate. The first block size is the one written (<scenario>.<rate>.wav); it is
//       rendered twice (determinism), the other sizes once each and compared with it sample for sample. manifest.json
//       records the results, sample hashes and provenance. --inputs also writes each scenario's input signal,
//       --keep-blocks every block size's render.
//   MnmCapture --fx-response [--fw os.syx] [--machines THRU,REVERB] [--csv file]
//       Monomodule FX as a user loads it: stepped-sine level, input to output, per frequency (44.1 kHz).
//   MnmCapture --check-fx-flat [--fw os.syx]
//       Pass/fail (ctest): THRU in Monomodule FX must stay within 1 dB of its 1 kHz level from 30 Hz to 4 kHz, with the
//       key-tracking parameters on as a 1.0.1 session saves them. Exit code 77 (skipped) without an OS file.
//   MnmCapture --check-preview
//       Pass/fail (ctest, no OS file needed): library previews through the sample-rate converter at 44.1/48/96 kHz.
//   MnmCapture --check-latency [--fw os.syx]
//       Pass/fail (ctest): the latency the plugins report matches what bypass and, with an OS file, THRU really do.
//
// Renders at other rates are compared with the 44.1 kHz references by tests/compare_captures.py.
//
// The OS file: --fw, else $MNM_OS, else the MNM_OS_SYX CMake option.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <vector>
#include "one/OneProcessor.h"

using namespace mnm::plugin::one;
using mnm::host::Machine;
using mnm::host::Page;

namespace {

// Every event sits on a grid of 2352 engine frames (1/18.75 s = 147 DSP blocks = 49 host-model frames), a whole number
// of samples at 22.05/44.1/88.2/176.4 kHz and 48/96/192 kHz, so a render at any of those rates can deliver each event
// to the same 16-frame DSP block and renders at different rates stay comparable. One tick = a 1/32 note at 140.625 BPM.
constexpr int kTickFrames = 2352;
constexpr double kTickSeconds = kTickFrames / 44100.0;
constexpr double kBpm = 140.625;
// Rendered first and dropped: the machine-assign trig each track plays at load is released and the slewed page words
// settle, so every scenario starts from silence with its parameters in place.
constexpr int kPreRollTicks = 16;
constexpr int kTailTicks = 16;        // FX scenarios: the input stops this long before the end
constexpr int kMaxVarBlock = 1024;    // "var" block sizes: 1..1024
constexpr int kSkipped = 77;          // ctest SKIP_RETURN_CODE
constexpr double kTwoPi = juce::MathConstants<double>::twoPi;

juce::int64 samplesAt(int ticks, double rate) { return juce::int64(std::llround(double(ticks) * kTickSeconds * rate)); }
double midiHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// ---- input signals: functions of time (seconds from the scenario start), so every rate samples the same signal ----

enum class Input { None, Drums, Sweep, Multitone };
const char* inputName(Input in) { return in == Input::Drums ? "drums" : in == Input::Sweep ? "sweep" : in == Input::Multitone ? "multitone" : "none"; }

struct Stereo { double l = 0.0, r = 0.0; };

double fadeOut(double tau, double end, double len) { return tau < end - len ? 1.0 : tau >= end ? 0.0 : 0.5 * (1.0 + std::cos(juce::MathConstants<double>::pi * (tau - (end - len)) / len)); }

// Kick on every quarter, a band-limited saw bass on 16ths (E1 riff, 41 Hz and up), metallic hats on the off-beat 8ths.
Stereo drums(double t)
{
    Stereo s;
    const double quarter = 8 * kTickSeconds, sixteenth = 2 * kTickSeconds;
    {
        const double tau = t - std::floor(t / quarter) * quarter;   // 0.43 s apart; each kick is faded out by 0.42 s
        const double ph = kTwoPi * (48.0 * tau + 110.0 * 0.025 * (1.0 - std::exp(-tau / 0.025)));
        const double k = 0.7 * std::min(1.0, tau / 0.001) * std::exp(-tau / 0.18) * fadeOut(tau, 0.42, 0.06) * std::sin(ph);
        s.l += k; s.r += k;
    }
    {
        static const int riff[16] = {28, 28, 40, 28, 31, 28, 43, 28, 26, 26, 38, 26, 33, 31, 28, 26};
        const int step = int(std::floor(t / sixteenth));
        const double tau = t - step * sixteenth, f = midiHz(riff[step % 16]);
        const int harmonics = std::min(40, int(15000.0 / f));
        double saw = 0.0;
        for (int h = 1; h <= harmonics; ++h) saw += std::sin(kTwoPi * h * f * tau) / h;
        const double b = 0.25 * std::min(1.0, tau / 0.002) * std::exp(-tau / 0.09) * fadeOut(tau, sixteenth, 0.008) * saw;
        s.l += b; s.r += b;
    }
    if (t >= quarter / 2) {
        const double tau = t - quarter / 2 - std::floor((t - quarter / 2) / quarter) * quarter;
        static const double partials[6] = {5800.0, 7300.0, 8650.0, 9900.0, 11700.0, 13300.0};
        double hat = 0.0;
        for (double f : partials) hat += std::sin(kTwoPi * f * tau);
        hat *= 0.04 * std::min(1.0, tau / 0.0005) * std::exp(-tau / 0.015) * fadeOut(tau, 0.06, 0.01);
        s.l += 0.7 * hat; s.r += hat;
    }
    return s;
}

// Exponential sine sweep 20 Hz to 20 kHz over `length` seconds, -6 dBFS.
Stereo sweep(double t, double length)
{
    const double f1 = 20.0, f2 = 20000.0, lr = std::log(f2 / f1);
    const double v = 0.5 * std::min(1.0, t / 0.01) * fadeOut(t, length, 0.01) * std::sin(kTwoPi * f1 * length / lr * (std::exp(t * lr / length) - 1.0));
    return {v, v};
}

// 40 sines, log-spaced 30 Hz to 16 kHz, fixed phases (different on the right channel).
Stereo multitone(double t, double length)
{
    Stereo s;
    for (int i = 0; i < 40; ++i) {
        const double f = 30.0 * std::pow(16000.0 / 30.0, i / 39.0);
        s.l += std::sin(kTwoPi * (f * t + std::fmod(i * 0.6180339887, 1.0)));
        s.r += std::sin(kTwoPi * (f * t + std::fmod(i * 0.4142135624, 1.0)));
    }
    const double g = 0.03 * std::min(1.0, t / 0.02) * fadeOut(t, length, 0.02);
    return {g * s.l, g * s.r};
}

Stereo inputAt(Input in, double t, double length)
{
    if (t < 0.0 || t >= length) return {};
    switch (in) {
    case Input::Drums: return drums(t);
    case Input::Sweep: return sweep(t, length);
    case Input::Multitone: return multitone(t, length);
    case Input::None: break;
    }
    return {};
}

// ---- scenarios ----

// Parameter setup through the processor's parameters, as a host or the editor sets them (raw 0..127 kit bytes).
class Setup {
public:
    explicit Setup(MnmOneProcessor& p) : m_p(p) {}
    void set(const juce::String& id, float value) { if (auto* q = m_p.apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(value)); }
    // the machine first: its change loads that machine's SYN values (and the FX AMP defaults), as the editor's does
    void machine(int t, Machine m) { set(machineId(t), float(machineSlot(m))); m_p.syncMachineSideEffects(); }
    void syn(int t, int k, int v) { set(synId(t, k), float(v)); }
    void amp(int t, int k, int v) { set(pageId(t, Page::AMP, k), float(v)); }      // ATK HOLD DEC REL DIST VOL PAN PORT
    void filt(int t, int k, int v) { set(pageId(t, Page::FILT, k), float(v)); }    // BASE WDTH HPQ LPQ ATK DEC BOFS WOFS
    void efx(int t, int k, int v) { set(pageId(t, Page::EFX, k), float(v)); }      // EQF EQG SRR DTIM DSND DFB DBAS DWID
    // list entries by index (PAGE 0-8, DEST 0-7, TRIG 0-4, WAVE 0-10, MULT 0-6), the rest raw
    void lfo(int t, int l, int page, int dest, int trig, int wave, int mult, int spd, int intl, int dpth)
    {
        const int raw[8] = {mnm::uispec::listRawMid(page, 9), mnm::uispec::listRawMid(dest, 8), mnm::uispec::listRawMid(trig, 5),
                            mnm::uispec::listRawMid(wave, 11), mnm::uispec::listRawMid(mult, 7), spd, intl, dpth};
        for (int k = 0; k < 8; ++k) set(lfoId(t, l, k), float(raw[k]));
    }
    void route(int t, FxInput in) { set(inputId(t), float(int(in))); }                  // Six
    void outBuses(int t, bool ab, bool cd, bool ef) { set(outBusId(t, 0), ab ? 1.f : 0.f); set(outBusId(t, 1), cd ? 1.f : 0.f); set(outBusId(t, 2), ef ? 1.f : 0.f); }
    void outputs(OutputMode m) { set(outputModeId(), float(int(m))); }                   // Six

private:
    MnmOneProcessor& m_p;
};

struct Event { int tick; juce::MidiMessage msg; };

class Events {
public:
    void note(int ch, int tick, int len, int pitch, float velocity = 0.8f)
    {
        m_ev.push_back({tick, juce::MidiMessage::noteOn(ch, pitch, velocity)});
        m_ev.push_back({tick + len, juce::MidiMessage::noteOff(ch, pitch)});
    }
    void cc(int ch, int tick, int number, int value) { m_ev.push_back({tick, juce::MidiMessage::controllerEvent(ch, number, juce::jlimit(0, 127, value))}); }
    void ramp(int ch, int number, int fromTick, int toTick, int from, int to)   // one CC per tick
    {
        for (int t = fromTick; t <= toTick; ++t) cc(ch, t, number, from + (to - from) * (t - fromTick) / std::max(1, toTick - fromTick));
    }
    // by tick; within a tick note-offs first, as hosts send touching notes
    std::vector<Event> sorted() const
    {
        auto ev = m_ev;
        std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.tick != b.tick ? a.tick < b.tick : (a.msg.isNoteOff() && !b.msg.isNoteOff()); });
        return ev;
    }

private:
    std::vector<Event> m_ev;
};

struct Scenario {
    juce::String name, about;
    Variant variant = Variant::One;
    int ticks = 64;                        // length after the pre-roll
    Input input = Input::None;             // FX: the main input; One/Six: the side-chain (sideChain)
    bool sideChain = false;
    std::function<void(Setup&)> setup;
    std::vector<Event> events;
    double inputSeconds() const { return double(variant == Variant::Fx ? ticks - kTailTicks : ticks) * kTickSeconds; }
};

// CC numbers of the pages (the hardware's CC map, OneProcessor::handleMidi)
constexpr int kCcAmp = 56, kCcFilt = 72, kCcLevel = 7;

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> list;
    auto one = [&](const char* name, const char* about, int ticks, std::function<void(Setup&)> setup, const Events& ev) {
        Scenario s; s.name = name; s.about = about; s.ticks = ticks; s.setup = std::move(setup); s.events = ev.sorted();
        list.push_back(std::move(s));
    };
    auto fx = [&](const char* name, const char* about, Machine m, Input in, int ticks, std::function<void(Setup&)> more = {}) {
        Scenario s; s.name = name; s.about = about; s.variant = Variant::Fx; s.ticks = ticks; s.input = in;
        s.setup = [m, more](Setup& p) { p.machine(0, m); if (more) more(p); };
        list.push_back(std::move(s));
    };
    auto sixteenths = [](Events& ev, int ch, int fromTick, std::initializer_list<int> pitches, int len = 2) {
        int t = fromTick;
        for (int p : pitches) { if (p > 0) ev.note(ch, t, len, p); t += 2; }
    };

    // ---- One: one per machine family, plus filter, LFO, portamento, EFX, trig density and CC automation ----
    { Events ev; for (int i = 0; i < 4; ++i) ev.note(1, i * 20, 16, std::array<int, 4>{36, 57, 69, 93}[size_t(i)]);
      one("one-sin-pitch", "GND SIN: four sustained notes (65 Hz to 1.76 kHz); pitch and clicks on pure tones", 92,
          [](Setup& p) { p.machine(0, Machine::SIN); p.amp(0, 2, 127); p.amp(0, 3, 24); }, ev); }
    { Events ev; sixteenths(ev, 1, 0, {36, 36, 48, 36, 39, 36, 46, 36, 34, 34, 46, 34, 41, 39, 36, 34});
      sixteenths(ev, 1, 32, {36, 36, 48, 36, 39, 36, 46, 36, 34, 34, 46, 34, 41, 39, 36, 34}, 3);   // second bar legato (overlaps)
      one("one-saw-bass", "SWAVE SAW 16th bass line, second bar legato; resonant low-pass", 80,
          [](Setup& p) { p.machine(0, Machine::SAW); p.amp(0, 2, 70); p.amp(0, 3, 16); p.filt(0, 1, 90); p.filt(0, 3, 40); }, ev); }
    { Events ev; ev.note(1, 0, 40, 45); ev.note(1, 44, 40, 52);
      one("one-puls-lfo", "SWAVE PULS held notes; LFO1 on pulse width (free), LFO2 vibrato (trig)", 92,
          [](Setup& p) { p.machine(0, Machine::PULS); p.amp(0, 2, 127); p.amp(0, 3, 30);
                         p.lfo(0, 0, 1, 4, 0, 0, 0, 40, 0, 90); p.lfo(0, 1, 0, 0, 1, 0, 0, 70, 0, 40); }, ev); }
    { Events ev; ev.note(1, 0, 88, 60); ev.ramp(1, kCcFilt + 0, 0, 44, 0, 120); ev.ramp(1, kCcFilt + 0, 45, 88, 120, 0);
      one("one-nois-sweep", "GND NOIS through a narrow resonant band; FILT BASE swept up and down by CC", 96,
          [](Setup& p) { p.machine(0, Machine::NOIS); p.amp(0, 2, 127); p.amp(0, 3, 20); p.filt(0, 1, 30); p.filt(0, 3, 100); }, ev); }
    { Events ev; ev.note(1, 0, 88, 33); ev.ramp(1, kCcFilt + 1, 0, 44, 10, 127); ev.ramp(1, kCcFilt + 1, 45, 88, 127, 10);
      one("one-saw-reso", "SWAVE SAW held low A; FILT WDTH swept by CC at high LPQ", 96,
          [](Setup& p) { p.machine(0, Machine::SAW); p.amp(0, 2, 127); p.amp(0, 3, 20); p.filt(0, 3, 110); }, ev); }
    { Events ev; int t = 0; for (int n : {60, 63, 67, 70, 72, 70, 67, 63, 58, 62, 65, 69, 70, 69, 65, 62}) { ev.note(1, t, 3, n); t += 4; }
      one("one-fm-par", "FM+ PAR 8th-note plucks", 88, [](Setup& p) { p.machine(0, Machine::FM_PAR); }, ev); }
    { Events ev; sixteenths(ev, 1, 0, {48, 55, 60, 55, 51, 58, 63, 58, 46, 53, 58, 53, 50, 57, 62, 57});
      sixteenths(ev, 1, 32, {48, 55, 60, 55, 51, 58, 63, 58, 46, 53, 58, 53, 50, 57, 62, 57});
      one("one-fm-dyn", "FM+ DYN 16th arpeggio", 80, [](Setup& p) { p.machine(0, Machine::FM_DYN); }, ev); }
    { Events ev; ev.note(1, 0, 24, 41); ev.note(1, 28, 24, 48); ev.note(1, 56, 24, 53);
      one("one-fm-stat", "FM+ STAT held notes", 96, [](Setup& p) { p.machine(0, Machine::FM_STAT); p.amp(0, 2, 110); p.amp(0, 3, 30); }, ev); }
    { Events ev; ev.note(1, 0, 28, 57); ev.note(1, 32, 28, 60); ev.note(1, 64, 20, 64);
      one("one-ens", "SWAVE ENS held notes", 100, [](Setup& p) { p.machine(0, Machine::ENS); p.amp(0, 2, 127); p.amp(0, 3, 30); }, ev); }
    { Events ev; sixteenths(ev, 1, 0, {45, 57, 52, 57, 45, 57, 55, 57, 43, 55, 50, 55, 43, 55, 53, 55});
      sixteenths(ev, 1, 32, {45, 57, 52, 57, 45, 57, 55, 57, 43, 55, 50, 55, 43, 55, 53, 55});
      one("one-sid", "SID 6581 16th riff", 80, [](Setup& p) { p.machine(0, Machine::SID); }, ev); }
    { Events ev; for (int i = 0; i < 4; ++i) ev.note(1, i * 14, 12, std::array<int, 4>{48, 52, 55, 50}[size_t(i)]);
      one("one-vo6", "VO-6 notes", 72, [](Setup& p) { p.machine(0, Machine::VO6); p.amp(0, 2, 100); }, ev); }
    struct Dpro { const char* name; const char* about; Machine m; };
    for (const Dpro& d : {Dpro{"one-dpro-wave", "DPRO WAVE notes", Machine::WAVE}, Dpro{"one-dpro-bbox", "DPRO BBOX hits", Machine::BBOX},
                          Dpro{"one-dpro-ddrw", "DPRO DDRW notes", Machine::DDRW}, Dpro{"one-dpro-dens", "DPRO DENS notes", Machine::DENS}}) {
        Events ev; int t = 0; for (int n : {48, 55, 60, 52, 48, 43}) { ev.note(1, t, 5, n); t += 6; }
        const Machine m = d.m;
        one(d.name, d.about, 56, [m](Setup& p) { p.machine(0, m); }, ev);
    }
    { Events ev; ev.note(1, 0, 12, 40); ev.note(1, 10, 12, 47); ev.note(1, 20, 12, 52); ev.note(1, 30, 14, 43);   // each overlaps the next
      one("one-portamento", "SWAVE SAW legato slides (AMP PORT)", 64,
          [](Setup& p) { p.machine(0, Machine::SAW); p.amp(0, 2, 127); p.amp(0, 3, 20); p.amp(0, 7, 70); }, ev); }
    { Events ev; ev.note(1, 0, 2, 60); ev.note(1, 6, 2, 67); ev.note(1, 12, 2, 72); ev.note(1, 32, 2, 63);
      one("one-efx-delay", "SWAVE PULS plucks through the EFX page: delay with feedback, SRR, EQ", 112,
          [](Setup& p) { p.machine(0, Machine::PULS); p.amp(0, 2, 40); p.amp(0, 3, 10);
                         p.efx(0, 0, 90); p.efx(0, 1, 80); p.efx(0, 2, 20); p.efx(0, 3, 48); p.efx(0, 4, 110); p.efx(0, 5, 80); }, ev); }
    { Events ev; for (int i = 0; i < 64; ++i) ev.note(1, i, 1, 40 + (i * 7) % 24);   // back to back: off and on in the same tick
      one("one-fast-trigs", "SWAVE SAW 1/32 notes back to back, a new pitch on every trig", 80,
          [](Setup& p) { p.machine(0, Machine::SAW); p.amp(0, 2, 30); p.amp(0, 3, 4); }, ev); }
    { Events ev; ev.note(1, 0, 88, 45); ev.ramp(1, kCcAmp + 4, 0, 30, 64, 127); ev.ramp(1, kCcAmp + 6, 30, 60, 0, 127); ev.ramp(1, kCcLevel, 60, 88, 100, 20);
      one("one-dist-pan-level", "SWAVE SAW held note; AMP DIST, AMP PAN and track level automated by CC", 96,
          [](Setup& p) { p.machine(0, Machine::SAW); p.amp(0, 2, 127); p.amp(0, 3, 20); }, ev); }
    { Scenario s; s.name = "one-sidechain-chorus"; s.about = "One with the CHORUS machine on a multitone side-chain input";
      s.ticks = 96; s.input = Input::Multitone; s.sideChain = true;
      s.setup = [](Setup& p) { p.machine(0, Machine::CHORUS); };
      list.push_back(std::move(s)); }

    // ---- Six: six machines on their own MIDI channels and outputs; FX routing (NEIBOR, side-chain, mix-bus insert) ----
    { Events ev;
      sixteenths(ev, 1, 0, {36, 0, 36, 0, 39, 0, 36, 43, 34, 0, 34, 0, 41, 0, 39, 0}); sixteenths(ev, 1, 32, {36, 0, 36, 0, 39, 0, 36, 43, 34, 0, 34, 0, 41, 0, 39, 0});
      for (int i = 0; i < 8; ++i) ev.note(2, i * 8, 4, std::array<int, 8>{72, 75, 79, 82, 84, 82, 79, 75}[size_t(i)]);
      for (int i = 0; i < 16; ++i) ev.note(3, i * 4 + 2, 1, 80);
      ev.note(4, 0, 30, 60); ev.note(4, 32, 30, 58);
      for (int i = 0; i < 4; ++i) ev.note(5, i * 16 + 6, 2, 63);
      ev.note(6, 0, 60, 67);
      Scenario s; s.name = "six-tracks"; s.about = "Six: SAW bass, FM+ PAR lead, NOIS hats, SIN, PULS stabs, ENS pad; one output per track";
      s.variant = Variant::Six; s.ticks = 80; s.events = ev.sorted();
      s.setup = [](Setup& p) {
          p.machine(0, Machine::SAW); p.amp(0, 2, 90); p.amp(0, 3, 16);
          p.machine(1, Machine::FM_PAR);
          p.machine(2, Machine::NOIS); p.amp(2, 2, 12); p.amp(2, 3, 4);
          p.machine(3, Machine::SIN); p.amp(3, 2, 127); p.amp(3, 3, 30);
          p.machine(4, Machine::PULS); p.amp(4, 2, 70);
          p.machine(5, Machine::ENS); p.amp(5, 0, 60); p.amp(5, 2, 127); p.amp(5, 3, 40);
          p.outputs(OutputMode::Tracks);
      };
      list.push_back(std::move(s)); }
    for (const OutputMode mode : {OutputMode::Buses, OutputMode::Tracks}) {
        Events ev;
        sixteenths(ev, 1, 0, {48, 0, 0, 55, 0, 0, 60, 0, 51, 0, 0, 58, 0, 0, 63, 0}); sixteenths(ev, 1, 32, {48, 0, 0, 55, 0, 0, 60, 0, 51, 0, 0, 58, 0, 0, 63, 0});
        for (int i = 0; i < 8; ++i) ev.note(3, i * 8 + 4, 3, std::array<int, 8>{67, 70, 74, 70, 65, 69, 72, 69}[size_t(i)]);
        for (int i = 0; i < 8; ++i) ev.note(5, i * 8, 6, 45);
        Scenario s; s.variant = Variant::Six; s.ticks = 96; s.input = Input::Drums; s.sideChain = true; s.events = ev.sorted();
        s.name = mode == OutputMode::Buses ? "six-fx-buses" : "six-fx-tracks";
        s.about = juce::String("Six routing: T1 SAW only feeds T2 REVERB (NEIBOR); T3 FM+ PAR and T4 DYNAMIX on the drums side-chain to bus CD; "
                               "T5 PULS to bus AB, T6 FLANGER inserted on bus AB; ") + (mode == OutputMode::Buses ? "outputs = the mix buses" : "outputs = per track");
        s.setup = [mode](Setup& p) {
            p.machine(0, Machine::SAW); p.amp(0, 2, 90); p.outBuses(0, false, false, false);
            p.machine(1, Machine::REVERB); p.route(1, FxInput::Neighbor); p.outBuses(1, true, false, false);
            p.machine(2, Machine::FM_PAR); p.outBuses(2, false, true, false);
            p.machine(3, Machine::DYNAMIX); p.route(3, FxInput::InpAB); p.outBuses(3, false, true, false);
            p.machine(4, Machine::PULS); p.amp(4, 2, 80); p.outBuses(4, true, false, false);
            p.machine(5, Machine::FLANGER); p.route(5, FxInput::BusAB); p.outBuses(5, true, false, false);
            p.outputs(mode);
        };
        list.push_back(std::move(s));
    }

    // ---- FX: every machine on a fitting input, default settings unless noted (as a user loads the plugin) ----
    fx("fx-thru-drums", "THRU on kick, sub bass and hats (issue #5: the low end must pass)", Machine::THRU, Input::Drums, 96);
    fx("fx-thru-sweep", "THRU on a 20 Hz-20 kHz sine sweep", Machine::THRU, Input::Sweep, 128);
    fx("fx-thru-multitone", "THRU on 40 steady sines, 30 Hz-16 kHz", Machine::THRU, Input::Multitone, 96);
    fx("fx-thru-efx", "THRU on drums through the EFX page: SRR, delay with feedback, EQ", Machine::THRU, Input::Drums, 112,
       [](Setup& p) { p.efx(0, 0, 40); p.efx(0, 1, 90); p.efx(0, 2, 40); p.efx(0, 3, 30); p.efx(0, 4, 90); p.efx(0, 5, 60); });
    fx("fx-reverb-drums", "REVERB on drums (the plugin's default machine)", Machine::REVERB, Input::Drums, 112);
    fx("fx-chorus-multitone", "CHORUS on 40 steady sines", Machine::CHORUS, Input::Multitone, 96);
    fx("fx-dynamix-drums", "DYNAMIX compressing drums", Machine::DYNAMIX, Input::Drums, 96,
       [](Setup& p) { p.syn(0, 2, 40); p.syn(0, 4, 100); p.syn(0, 5, 40); });
    fx("fx-ringmod-sweep", "RINGMOD on a sine sweep, fully wet", Machine::RINGMOD, Input::Sweep, 128, [](Setup& p) { p.syn(0, 0, 40); p.syn(0, 3, 127); });
    fx("fx-phaser-multitone", "PHASER on 40 steady sines", Machine::PHASER, Input::Multitone, 96);
    fx("fx-flanger-drums", "FLANGER on drums", Machine::FLANGER, Input::Drums, 96);
    return list;
}

// ---- rendering ----

class Transport final : public juce::AudioPlayHead {
public:
    explicit Transport(double rate) : m_rate(rate) {}
    void moveTo(juce::int64 sample) { m_sample = sample; }
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm(kBpm);
        p.setTimeSignature(TimeSignature{});
        p.setIsPlaying(true);
        p.setTimeInSamples(m_sample);
        p.setTimeInSeconds(double(m_sample) / m_rate);
        p.setPpqPosition(double(m_sample) / m_rate * kBpm / 60.0);
        return p;
    }

private:
    double m_rate;
    juce::int64 m_sample = 0;
};

struct Blocks {
    int size = 512;
    bool variable = false;   // sizes 1..kMaxVarBlock from a fixed generator, as hosts that split blocks at automation points send
    juce::String name() const { return variable ? juce::String("var") : juce::String(size); }
    int maxSize() const { return variable ? kMaxVarBlock : size; }
};

struct Audio {
    double rate = 0.0;
    int latency = 0, conversion = 0;   // what the plugin reports (getLatencySamples), and the sample-rate conversion's part
    std::vector<std::vector<float>> ch;
    int channels() const { return int(ch.size()); }
    juce::int64 frames() const { return ch.empty() ? 0 : juce::int64(ch[0].size()); }
};

bool render(const Scenario& s, const juce::String& fw, double rate, const Blocks& blocks, Audio& out, Audio* input, juce::String& error)
{
    MnmOneProcessor p(s.variant);
    p.setFirmwarePath(fw, false);
    if (!p.engineReady()) { error = p.statusText(); return false; }
    if (s.sideChain) p.enableAllBuses();   // the side-chain on, as a host does when the user picks a source
    Setup setup(p);
    if (s.setup) s.setup(setup);
    Transport transport(rate);
    p.setPlayHead(&transport);
    p.setRateAndBufferSizeDetails(rate, blocks.maxSize());
    p.prepareToPlay(rate, blocks.maxSize());

    const int inCh = p.getTotalNumInputChannels(), outCh = p.getTotalNumOutputChannels();
    juce::AudioBuffer<float> buf(std::max(inCh, outCh), blocks.maxSize());
    const juce::int64 pre = samplesAt(kPreRollTicks, rate), total = pre + samplesAt(s.ticks, rate);
    std::vector<std::pair<juce::int64, juce::MidiMessage>> ev;
    if (s.variant != Variant::Fx)   // release the machine-assign trigs early in the pre-roll
        for (int c = 1; c <= numTracksOf(s.variant); ++c) ev.push_back({samplesAt(2, rate), juce::MidiMessage::allNotesOff(c)});
    for (const auto& e : s.events) ev.push_back({samplesAt(kPreRollTicks + e.tick, rate), e.msg});

    out.rate = rate;
    out.latency = p.getLatencySamples();
    out.conversion = p.conversionLatency();
    out.ch.assign(size_t(outCh), std::vector<float>(size_t(total - pre)));
    if (input) { input->rate = rate; input->ch.assign(size_t(inCh), std::vector<float>(size_t(total - pre))); }
    uint32_t seed = 0x2545F491u;
    size_t next = 0;
    for (juce::int64 pos = 0; pos < total;) {
        int n = blocks.size;
        if (blocks.variable) { seed = seed * 1664525u + 1013904223u; n = 1 + int((seed >> 8) % uint32_t(kMaxVarBlock)); }
        n = int(std::min<juce::int64>(n, total - pos));
        buf.setSize(buf.getNumChannels(), n, false, false, true);
        buf.clear();
        if (inCh > 0)
            for (int i = 0; i < n; ++i) {
                const auto x = inputAt(s.input, double(pos + i - pre) / rate, s.inputSeconds());
                buf.setSample(0, i, float(x.l));
                if (inCh > 1) buf.setSample(1, i, float(x.r));
                if (input && pos + i >= pre) { input->ch[0][size_t(pos + i - pre)] = float(x.l); if (inCh > 1) input->ch[1][size_t(pos + i - pre)] = float(x.r); }
            }
        juce::MidiBuffer midi;
        for (; next < ev.size() && ev[next].first < pos + n; ++next) midi.addEvent(ev[next].second, int(ev[next].first - pos));
        transport.moveTo(pos);
        p.processBlock(buf, midi);
        for (int c = 0; c < outCh; ++c)
            for (int i = 0; i < n; ++i)
                if (pos + i >= pre) out.ch[size_t(c)][size_t(pos + i - pre)] = buf.getSample(c, i);
        pos += n;
    }
    p.setPlayHead(nullptr);
    return true;
}

// ---- files and measurements ----

bool writeWav(const juce::File& f, const Audio& a)
{
    f.deleteFile();
    f.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream>(f);
    if (!static_cast<juce::FileOutputStream*>(os.get())->openedOk()) return false;
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor(os, juce::AudioFormatWriterOptions{}.withSampleRate(a.rate).withNumChannels(a.channels()).withBitsPerSample(32)
                                         .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (!w) return false;
    std::vector<const float*> ptrs;
    for (const auto& c : a.ch) ptrs.push_back(c.data());
    return w->writeFromFloatArrays(ptrs.data(), a.channels(), int(a.frames()));
}

// SHA-256 of the interleaved float32 samples, i.e. of the WAV file's data chunk
juce::String hashOf(const Audio& a)
{
    juce::MemoryBlock mb(size_t(a.frames()) * size_t(a.channels()) * sizeof(float));
    auto* d = static_cast<float*>(mb.getData());
    for (juce::int64 i = 0; i < a.frames(); ++i)
        for (int c = 0; c < a.channels(); ++c) *d++ = a.ch[size_t(c)][size_t(i)];
    return juce::SHA256(mb).toHexString();
}

double peakDb(const Audio& a)
{
    float pk = 0.f;
    for (const auto& c : a.ch) for (float v : c) pk = std::max(pk, std::abs(v));
    return pk > 0.f ? 20.0 * std::log10(double(pk)) : -999.0;
}

double rmsDb(const Audio& a)
{
    double e = 0.0;
    for (const auto& c : a.ch) for (float v : c) e += double(v) * v;
    const double n = double(a.frames()) * a.channels();
    return e > 0.0 && n > 0.0 ? 10.0 * std::log10(e / n) : -999.0;
}

// largest sample difference; 0 = identical (also compares the lengths)
float maxDiff(const Audio& a, const Audio& b)
{
    if (a.channels() != b.channels() || a.frames() != b.frames()) return std::numeric_limits<float>::infinity();
    float d = 0.f;
    for (size_t c = 0; c < a.ch.size(); ++c)
        for (size_t i = 0; i < a.ch[c].size(); ++i) d = std::max(d, std::abs(a.ch[c][i] - b.ch[c][i]));
    return d;
}

juce::String git(const juce::StringArray& args)
{
#ifdef MNM_SOURCE_DIR
    juce::StringArray cmd{"git", "-C", MNM_SOURCE_DIR};
    cmd.addArray(args);
    juce::ChildProcess cp;
    if (cp.start(cmd)) { const auto out = cp.readAllProcessOutput().trim(); if (cp.getExitCode() == 0) return out; }
#endif
    juce::ignoreUnused(args);
    return {};
}

// Stepped-sine level of Monomodule FX at its defaults with machine m: a fresh plugin per frequency, 0.6 s to settle,
// then 0.5 s measured (dB, output over input, both channels).
double fxLevelDb(const juce::String& fw, Machine m, double hz)
{
    MnmOneProcessor p(Variant::Fx);
    p.setFirmwarePath(fw, false);
    if (!p.engineReady()) return -999.0;
    Setup setup(p);
    setup.machine(0, m);
    setup.set(lpKeyTrackId(0), 1.0f);   // on, as every 1.0.1 session stores them (the parameter defaults)
    setup.set(hpKeyTrackId(0), 1.0f);
    p.prepareToPlay(44100.0, 512);
    juce::AudioBuffer<float> buf(2, 512);
    const int settle = 26460, measure = 22050;
    double phase = 0.0, eIn = 0.0, eOut = 0.0;
    for (int pos = 0; pos < settle + measure; pos += 512) {
        for (int i = 0; i < 512; ++i) {
            const float x = float(0.25 * std::sin(phase));
            phase = std::fmod(phase + kTwoPi * hz / 44100.0, kTwoPi);
            buf.setSample(0, i, x); buf.setSample(1, i, x);
            if (pos >= settle) eIn += 2.0 * double(x) * x;
        }
        juce::MidiBuffer midi;
        p.processBlock(buf, midi);
        if (pos >= settle)
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) eOut += double(buf.getSample(c, i)) * buf.getSample(c, i);
    }
    return eOut > 0.0 ? 10.0 * std::log10(eOut / eIn) : -999.0;
}

Machine fxMachineByName(const juce::String& name, bool& ok)
{
    ok = true;
    for (const auto& d : mnm::host::kMachineDefs)
        if (mnm::host::isFxMachine(d.machine) && name.equalsIgnoreCase(d.name)) return d.machine;
    ok = false;
    return Machine::THRU;
}

void setEnv(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

// ---- commands ----

int listScenarios()
{
    for (const auto& s : scenarios())
        std::printf("%-22s %-5s %5.2f s  %s\n", s.name.toRawUTF8(), s.variant == Variant::Fx ? "FX" : s.variant == Variant::Six ? "Six" : "One",
                    s.ticks * kTickSeconds, s.about.toRawUTF8());
    return 0;
}

int fxResponse(const juce::String& fw, const juce::StringArray& machines, const juce::String& csvPath)
{
    static const double freqs[] = {20, 30, 40, 50, 63, 80, 100, 125, 160, 200, 315, 500, 1000, 2000, 4000, 8000, 12000, 16000};
    juce::String csv = "machine,hz,level_db,relative_to_1khz_db\n";
    std::printf("Monomodule FX at its defaults, stepped sine, level in dB relative to the 1 kHz level\n%-8s", "Hz");
    for (double f : freqs) std::printf("%7.0f", f);
    std::printf("\n");
    for (const auto& name : machines) {
        bool ok = false;
        const Machine m = fxMachineByName(name, ok);
        if (!ok) { std::fprintf(stderr, "not an FX machine: %s\n", name.toRawUTF8()); return 2; }
        std::vector<double> lv;
        for (double f : freqs) lv.push_back(fxLevelDb(fw, m, f));
        const double ref = lv[size_t(std::find(std::begin(freqs), std::end(freqs), 1000.0) - std::begin(freqs))];
        std::printf("%-8s", name.toRawUTF8());
        for (size_t i = 0; i < lv.size(); ++i) {
            std::printf("%7.1f", lv[i] - ref);
            csv << name << "," << freqs[i] << "," << juce::String(lv[i], 2) << "," << juce::String(lv[i] - ref, 2) << "\n";
        }
        std::printf("   (1 kHz: %.1f dB input to output)\n", ref);
    }
    if (csvPath.isNotEmpty() && !juce::File::getCurrentWorkingDirectory().getChildFile(csvPath).replaceWithText(csv)) { std::fprintf(stderr, "could not write %s\n", csvPath.toRawUTF8()); return 1; }
    return 0;
}

int checkFxFlat(const juce::String& fw)
{
    // Issue #5: an effect plays no notes, its one trig is a fixed note 60, so the filters must not track it. With the HPF
    // tracking that note THRU lost 29 dB at 30 Hz, 16 dB at 50 Hz, 7 dB at 80 Hz. Without it THRU is flat to well within
    // a dB up to 4 kHz (above that the machine's own gentle roll-off begins).
    static const double freqs[] = {30, 50, 80, 120, 200, 400, 2000, 4000};
    const double ref = fxLevelDb(fw, Machine::THRU, 1000.0);
    bool ok = ref > -20.0;
    std::printf("THRU, Monomodule FX defaults (key tracking parameters on), level relative to 1 kHz (%.2f dB):\n", ref);
    for (double f : freqs) {
        const double d = fxLevelDb(fw, Machine::THRU, f) - ref;
        const bool good = std::abs(d) <= 1.0;
        std::printf("  %6.0f Hz  %+6.2f dB  %s\n", f, d, good ? "ok" : "FAIL (more than 1 dB from 1 kHz)");
        ok = ok && good;
    }
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}

int capture(const juce::String& fw, const juce::File& outDir, const std::vector<double>& rates, const std::vector<Blocks>& blocks,
            const juce::StringArray& only, bool writeInputs, bool keepBlocks, const juce::String& note)
{
    auto all = scenarios();
    std::vector<const Scenario*> todo;
    for (const auto& s : all) if (only.isEmpty() || only.contains(s.name)) todo.push_back(&s);
    for (const auto& n : only) if (std::none_of(all.begin(), all.end(), [&](const Scenario& s) { return s.name == n; })) { std::fprintf(stderr, "unknown scenario: %s\n", n.toRawUTF8()); return 2; }
    if (!outDir.createDirectory()) { std::fprintf(stderr, "cannot create %s\n", outDir.getFullPathName().toRawUTF8()); return 1; }

    juce::Array<juce::var> renders;
    bool failed = false, allIdentical = true;
    for (const Scenario* s : todo)
        for (double rate : rates) {
            const juce::String base = s->name + "." + juce::String(int(std::lround(rate)));
            Audio a, in, again;
            juce::String err;
            if (!render(*s, fw, rate, blocks[0], a, writeInputs && s->input != Input::None ? &in : nullptr, err)) {
                std::fprintf(stderr, "%s: %s\n", base.toRawUTF8(), err.toRawUTF8());
                failed = true;
                continue;
            }
            const auto file = outDir.getChildFile(base + ".wav");
            if (!writeWav(file, a)) { std::fprintf(stderr, "cannot write %s\n", file.getFullPathName().toRawUTF8()); failed = true; continue; }
            if (!in.ch.empty()) writeWav(outDir.getChildFile(base + ".input.wav"), in);
            render(*s, fw, rate, blocks[0], again, nullptr, err);
            const bool deterministic = maxDiff(a, again) == 0.f;

            auto* r = new juce::DynamicObject();
            r->setProperty("scenario", s->name);
            r->setProperty("about", s->about);
            r->setProperty("plugin", s->variant == Variant::Fx ? "FX" : s->variant == Variant::Six ? "Six" : "One");
            r->setProperty("input", inputName(s->input));
            r->setProperty("rate", rate);
            r->setProperty("block", blocks[0].name());
            r->setProperty("file", file.getFileName());
            r->setProperty("channels", a.channels());
            r->setProperty("frames", a.frames());
            r->setProperty("sha256", hashOf(a));
            r->setProperty("peakDbfs", juce::String(peakDb(a), 2));
            r->setProperty("rmsDbfs", juce::String(rmsDb(a), 2));
            r->setProperty("deterministic", deterministic);
            r->setProperty("latencySamples", a.latency);
            r->setProperty("alignSamples", a.conversion);   // every output is this much later than at 44.1 kHz
            juce::Array<juce::var> others;
            juce::String line;
            for (size_t b = 1; b < blocks.size(); ++b) {
                Audio o;
                if (!render(*s, fw, rate, blocks[b], o, nullptr, err)) { failed = true; continue; }
                const float d = maxDiff(a, o);
                auto* ob = new juce::DynamicObject();
                ob->setProperty("block", blocks[b].name());
                ob->setProperty("identical", d == 0.f);
                ob->setProperty("maxAbsDiff", double(d));
                others.add(juce::var(ob));
                line << " " << blocks[b].name() << (d == 0.f ? "=" : "!=");
                allIdentical = allIdentical && d == 0.f;
                if (keepBlocks) writeWav(outDir.getChildFile(base + ".b" + blocks[b].name() + ".wav"), o);
            }
            r->setProperty("otherBlocks", others);
            renders.add(juce::var(r));
            allIdentical = allIdentical && deterministic;
            std::printf("%-22s %6.0f Hz  %2d ch  %5.2f s  peak %6.1f dBFS  %s  block %s vs%s\n", s->name.toRawUTF8(), rate, a.channels(),
                        double(a.frames()) / rate, peakDb(a), deterministic ? "deterministic" : "NOT DETERMINISTIC", blocks[0].name().toRawUTF8(),
                        line.isEmpty() ? " -" : line.toRawUTF8());
            std::fflush(stdout);
        }

    auto* m = new juce::DynamicObject();
    m->setProperty("tool", "mnm-capture");
    m->setProperty("created", juce::Time::getCurrentTime().toISO8601(true));
    m->setProperty("note", note);
    m->setProperty("gitHead", git({"rev-parse", "HEAD"}));
    m->setProperty("gitDiffShortstat", git({"diff", "--shortstat"}));
    const juce::File fwFile(fw);
    m->setProperty("firmware", fwFile.getFileName());
    m->setProperty("firmwareSha256", juce::SHA256(fwFile).toHexString());
    m->setProperty("format", "WAV, 32-bit float; sha256 = hash of the interleaved float32 samples (the data chunk)");
    m->setProperty("grid", "events on ticks of 2352 engine frames (a 1/32 note at 140.625 BPM, host transport at that tempo); "
                           "16 ticks of pre-roll rendered and dropped before sample 0");
    m->setProperty("renders", renders);
    outDir.getChildFile("manifest.json").replaceWithText(juce::JSON::toString(juce::var(m)));
    std::printf("%d render(s) -> %s  (%s)\n", renders.size(), outDir.getFullPathName().toRawUTF8(),
                allIdentical ? "every render deterministic and identical at every block size" : "DIFFERENCES: see manifest.json");
    return failed ? 1 : 0;
}

// Library previews at the output's rate (no OS file needed): a synthetic 44.1 kHz render (1 kHz + 15 kHz, whole cycles
// per second, so its loop point is seamless) played through PreviewVoice to its end and looping across its loop point,
// at 44.1, 48 and 96 kHz. At 44.1 kHz the output must be the render itself, sample for sample; at the other rates the
// band-limited render, within -90 dB; played to its end it must stop after its 2 s.
int checkPreview()
{
    using mnm::library::PreviewAudio;
    using mnm::library::PreviewVoice;
    auto ideal = [](double t) { return 0.3 * std::sin(kTwoPi * 1000.0 * t) + 0.2 * std::sin(kTwoPi * 15000.0 * t); };
    auto audio = std::make_shared<PreviewAudio>();
    const uint32_t frames = 88200;
    audio->frames = frames;
    audio->loopStart = 44100; audio->loopEnd = frames;   // the second second
    audio->mixL.resize(frames); audio->mixR.resize(frames);
    for (uint32_t i = 0; i < frames; ++i) { audio->mixL[i] = float(ideal(i / 44100.0)); audio->mixR[i] = -audio->mixL[i]; }
    audio->ready.store(frames);
    audio->done.store(true);
    bool ok = true;
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (bool loop : {false, true}) {
            PreviewVoice v;
            v.prepare(rate);
            v.start(audio, -1);
            v.setLoop(loop);
            std::vector<float> L, R, bl(512), br(512);
            const size_t want = size_t(rate * 3.0);
            while (L.size() < want) {
                if (!v.process(bl.data(), br.data(), 512, rate, true)) break;
                L.insert(L.end(), bl.begin(), bl.end()); R.insert(R.end(), br.begin(), br.end());
            }
            const double played = double(L.size()) / rate;
            const size_t margin = size_t(0.1 * rate), end = loop ? L.size() : std::min(L.size(), size_t(2.0 * rate));
            double e = 0.0, s = 0.0;
            for (size_t m = margin; m + margin < end; ++m) {
                // 44.1 kHz: the render's own frames (wrapped in the loop region); otherwise the signal it samples
                const double x = rate == 44100.0 ? double(audio->mixL[m < frames ? m : 44100 + (m - 44100) % 44100]) : ideal(double(m) / rate);
                e += (L[m] - x) * (L[m] - x) + (R[m] + x) * (R[m] + x);
                s += 2.0 * x * x;
            }
            const double db = e > 0.0 ? 10.0 * std::log10(e / s) : -999.0;
            const bool lengthOk = loop ? played >= 3.0 : played >= 2.0 && played < 2.0 + 1024.0 / rate;
            const bool good = (rate == 44100.0 ? e == 0.0 : db < -90.0) && lengthOk;
            std::printf("preview at %6.0f Hz, %-7s played %.3f s, error %s  %s\n", rate, loop ? "looping" : "once", played,
                        e == 0.0 ? "none (bit-exact)" : (juce::String(db, 1) + " dB").toRawUTF8(), good ? "ok" : "FAIL");
            ok = ok && good;
        }
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}

// The latency the plugins report is the real one. Bypassed, Monomodule FX must delay its input by exactly that (no OS
// file needed); with the OS file, an impulse through THRU must come out that late, to within a sample (THRU's own
// response is a few samples wide).
int checkLatency(const juce::String& fw)
{
    bool ok = true;
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        auto impulseDelay = [&](MnmOneProcessor& p, bool bypassed) {
            juce::AudioBuffer<float> buf(2, 512);
            juce::MidiBuffer midi;
            constexpr int kAt = 40 * 512 + 100;   // after ~0.2 s (the machine change has settled)
            int peakPos = -1;
            float peak = 0.f;
            for (int b = 0; b < 80; ++b) {
                buf.clear();
                if (b * 512 <= kAt && kAt < (b + 1) * 512) { buf.setSample(0, kAt - b * 512, 0.5f); buf.setSample(1, kAt - b * 512, 0.5f); }
                if (bypassed) p.processBlockBypassed(buf, midi); else p.processBlock(buf, midi);
                for (int i = 0; i < 512; ++i) if (std::abs(buf.getSample(0, i)) > peak) { peak = std::abs(buf.getSample(0, i)); peakPos = b * 512 + i; }
            }
            return peakPos - kAt;
        };
        MnmOneProcessor bypass(Variant::Fx);   // no engine needed
        bypass.prepareToPlay(rate, 512);
        const int reported = bypass.getLatencySamples(), bypassed = impulseDelay(bypass, true);
        bool good = bypassed == reported;
        juce::String thru = "THRU: skipped (no OS file)";
        if (fw.isNotEmpty()) {
            MnmOneProcessor p(Variant::Fx);
            p.setFirmwarePath(fw, false);
            Setup setup(p);
            setup.machine(0, Machine::THRU);
            p.prepareToPlay(rate, 512);
            const int measured = impulseDelay(p, false);
            good = good && std::abs(measured - reported) <= 1;
            thru = "THRU impulse peak at " + juce::String(measured);
        }
        std::printf("Monomodule FX at %6.0f Hz: reports %3d samples; bypassed impulse delayed %3d; %s  %s\n", rate, reported, bypassed, thru.toRawUTF8(), good ? "ok" : "FAIL");
        ok = ok && good;
    }
    std::printf(ok ? "OK\n" : "FAILED\n");
    return ok ? 0 : 1;
}

void usage()
{
    std::fprintf(stderr,
        "MnmCapture --list\n"
        "MnmCapture --out <dir> [--fw os.syx] [--rates 44100,...] [--blocks 512,32,441,2048,var] [--scenario a,b] [--inputs] [--keep-blocks] [--note text]\n"
        "MnmCapture --fx-response [--fw os.syx] [--machines THRU,REVERB] [--csv file]\n"
        "MnmCapture --check-fx-flat [--fw os.syx]\n"
        "MnmCapture --check-preview\n"
        "MnmCapture --check-latency [--fw os.syx]\n");
}

} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    // Every render loads --fw itself; the plugins' shared settings on this machine (OS path, skin) stay out of it.
    setEnv("MNM_SETTINGS_DIR", juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mnm-capture-settings").getFullPathName().toRawUTF8());

    juce::String mode, fw, outDir, csv, note;
    juce::StringArray rates{"44100"}, blockNames{"512", "32", "441", "2048", "var"}, only, machines{"THRU", "REVERB"};
    bool writeInputs = false, keepBlocks = false;
    for (int i = 1; i < argc; ++i) {
        const juce::String a(argv[i]);
        auto value = [&]() -> juce::String { return i + 1 < argc ? juce::String(argv[++i]) : juce::String(); };
        auto list = [&]() { juce::StringArray l; l.addTokens(value(), ",", ""); l.trim(); l.removeEmptyStrings(); return l; };
        if (a == "--list" || a == "--fx-response" || a == "--check-fx-flat" || a == "--check-preview" || a == "--check-latency") mode = a;
        else if (a == "--out") { mode = a; outDir = value(); }
        else if (a == "--fw") fw = value();
        else if (a == "--rates") rates = list();
        else if (a == "--blocks") blockNames = list();
        else if (a == "--scenario") only = list();
        else if (a == "--machines") machines = list();
        else if (a == "--csv") csv = value();
        else if (a == "--note") note = value();
        else if (a == "--inputs") writeInputs = true;
        else if (a == "--keep-blocks") keepBlocks = true;
        else { usage(); return 2; }
    }
    if (mode.isEmpty()) { usage(); return 2; }
    if (mode == "--list") return listScenarios();
    if (mode == "--check-preview") return checkPreview();

    if (fw.isEmpty()) { const char* env = std::getenv("MNM_OS"); fw = env && *env ? juce::String(env) : juce::String(MNM_OS_SYX); }
    if (mode == "--check-latency")   // the OS file is optional here
        return checkLatency(fw.isNotEmpty() && juce::File::getCurrentWorkingDirectory().getChildFile(fw).existsAsFile()
                                ? juce::File::getCurrentWorkingDirectory().getChildFile(fw).getFullPathName() : juce::String());
    if (fw.isEmpty() || !juce::File::getCurrentWorkingDirectory().getChildFile(fw).existsAsFile()) {
        std::printf("no Monomachine OS file (pass --fw, set MNM_OS or -DMNM_OS_SYX=...): skipped\n");
        return mode == "--check-fx-flat" ? kSkipped : 2;
    }
    fw = juce::File::getCurrentWorkingDirectory().getChildFile(fw).getFullPathName();
    if (mode == "--fx-response") return fxResponse(fw, machines, csv);
    if (mode == "--check-fx-flat") return checkFxFlat(fw);

    std::vector<double> rateList;
    for (const auto& r : rates) if (r.getDoubleValue() >= 8000.0) rateList.push_back(r.getDoubleValue()); else { usage(); return 2; }
    std::vector<Blocks> blockList;
    for (const auto& b : blockNames)
        if (b == "var") blockList.push_back({kMaxVarBlock, true});
        else if (b.getIntValue() >= 1) blockList.push_back({b.getIntValue(), false});
        else { usage(); return 2; }
    if (rateList.empty() || blockList.empty()) { usage(); return 2; }
    return capture(fw, juce::File::getCurrentWorkingDirectory().getChildFile(outDir), rateList, blockList, only, writeInputs, keepBlocks, note);
}
