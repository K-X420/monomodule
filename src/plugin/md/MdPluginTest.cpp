// md-plugintest <os.syx> <out.wav> [ui.png]: headless check of Monomodule MD. Plays a two-bar beat through the
// plugin processor at 48 kHz (resampled from the engine's 44.1 kHz) and writes the stereo result; optionally renders
// the editor to a PNG.
#include <cstdio>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MdProcessor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3) { std::printf("usage: md-plugintest <os.syx> <out.wav> [ui.png]\n"); return 2; }
    using namespace mnm::plugin::md;
    MdProcessor proc;
    proc.setFirmwarePath(juce::String(argv[1]), false);
    std::printf("%s\n", proc.statusText().toRawUTF8());
    if (!proc.engineReady()) return 1;
    const double rate = 48000.0;
    const int block = 480;
    proc.setPlayConfigDetails(0, 2, rate, block);
    proc.prepareToPlay(rate, block);
    bool kitLoaded = false;
    if (std::getenv("MD_ROM_TEST")) {   // a 220 Hz WAV into ROM-01 on track 1, then the state round trip into a 2nd instance
        juce::File wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("md-rom-test.wav");
        wav.deleteFile();
        {
            juce::AudioBuffer<float> b(1, 44100);
            for (int i = 0; i < 44100; ++i) b.setSample(0, i, 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * float(i) / 44100.0f) * (1.0f - float(i) / 44100.0f));
            juce::WavAudioFormat fmt;
            auto stream = std::unique_ptr<juce::FileOutputStream>(wav.createOutputStream());
            if (auto w = std::unique_ptr<juce::AudioFormatWriter>(fmt.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0))) { stream.release(); w->writeFromAudioSampleBuffer(b, 0, 44100); }
        }
        if (auto* p = proc.apvts.getParameter(machineId(0))) p->setValueNotifyingHost(p->convertTo0to1(float(machineIndexOf(128))));
        static const int romDefaults[8] = {64, 64, 127, 0, 0, 127, 0, 64};   // no message loop here: set the ROM knob defaults
        for (int k = 0; k < 8; ++k) if (auto* p = proc.apvts.getParameter(knobId(0, k))) p->setValueNotifyingHost(p->convertTo0to1(float(romDefaults[k])));
        const auto err = proc.loadSample(0, wav);
        std::printf("ROM test: load '%s' -> %s (%.2f s, memory %.1f%%)\n", wav.getFileName().toRawUTF8(), err.isEmpty() ? "ok" : err.toRawUTF8(),
                    proc.sampleSeconds(0), proc.sampleMemoryUsed() * 100.0);
        juce::MemoryBlock state;
        proc.getStateInformation(state);
        MdProcessor other;
        other.setFirmwarePath(juce::String(argv[1]), false);
        other.setStateInformation(state.getData(), int(state.getSize()));
        std::printf("state %zu bytes; restored instance has '%s' (%.2f s) in slot 0\n", state.getSize(), other.sampleName(0).toRawUTF8(), other.sampleSeconds(0));
    }
    const bool lfoTest = std::getenv("MD_LFO_TEST") != nullptr;
    const bool velTest = std::getenv("MD_VEL_TEST") != nullptr;
    if (lfoTest) {   // track 1: GND-SN, long decay, LFO on its own PTCH (SYN1), depth 127, SPD 32
        auto set = [&](const juce::String& id, float v) { if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
        set(machineId(0), float(machineIndexOf(1)));
        set(knobId(0, 0), 64.0f); set(knobId(0, 1), 127.0f);
        set(lfoId(0, 0), 0.0f); set(lfoId(0, 1), 0.0f); set(lfoId(0, 2), 0.0f); set(lfoId(0, 3), 0.0f);
        set(lfoId(0, 5), 32.0f); set(lfoId(0, 6), 127.0f); set(lfoId(0, 7), 0.0f);
        std::printf("LFO test: GND-SN with an LFO on PTCH\n");
    }
    if (argc > 5) {   // md-plugintest <os> <out.wav> <ui.png> <dump.syx> <kit position>
        const auto kits = mnm::md::loadKits(argv[4]);
        const int pos = std::atoi(argv[5]);
        for (const auto& k : kits)
            if (k.position == pos) {
                const int emptied = proc.applyKit(k);
                std::printf("kit %d '%s' loaded (%d tracks emptied)\n", pos, k.name.c_str(), emptied);
                kitLoaded = true;
            }
    }

    // 120 BPM, 16th notes: kick 1, snare 2, closed hat 7, clap 4, open hat 8 on offbeats
    const double bpm = 120.0, stepSec = 60.0 / bpm / 4.0;
    const int steps = 32;
    const int total = int(steps * stepSec * rate) + int(rate);
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < total; pos += block) {
        const int n = std::min(block, total - pos);
        juce::AudioBuffer<float> buf(2, n);
        juce::MidiBuffer midi;
        for (int s = 0; s < steps; ++s) {
            const int at = int(s * stepSec * rate);
            if (at < pos || at >= pos + n) continue;
            auto note = [&](int nn, int vel = 100) { midi.addEvent(juce::MidiMessage::noteOn(1, nn, uint8_t(vel)), at - pos); };
            if (velTest) {   // kick at 127, 64, 20 (VOLUME mode), then 100 and 120 (ACCENT mode)
                static const int vels[5] = {127, 64, 20, 100, 120};
                if (s % 6 == 0 && s / 6 < 5) {
                    if (s / 6 == 3) if (auto* p = proc.apvts.getParameter(velModeId())) p->setValueNotifyingHost(1.0f);
                    note(36, vels[s / 6]);
                }
                continue;
            }
            if (lfoTest || std::getenv("MD_ROM_TEST")) { if (s == 0) note(36); continue; }
            if (kitLoaded) {   // a busier pattern over all 16 tracks
                for (int t = 0; t < kTracks; ++t)
                    if ((s * (t + 3) + t) % (t < 2 ? 4 : 7) == 0) note(kTrackNotes[t]);
                continue;
            }
            if (s % 4 == 0) note(36);                 // kick
            if (s % 8 == 4) note(38);                 // snare
            if (s % 2 == 0) note(48);                 // closed hat (track 8 = TRX-CH... track 7)
            if (s % 4 == 2) note(50);                 // open hat
            if (s == 14 || s == 30) note(41);         // clap
            if (s % 8 == 7) note(53);                 // claves
        }
        proc.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    float peak = 0; double sum = 0;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) { const float v = out.getSample(c, i); peak = std::max(peak, std::abs(v)); sum += double(v) * v; }
    std::printf("rendered %.2f s in %.0f ms (%.1fx realtime); peak %.3f RMS %.4f\n", total / rate, ms, total / rate * 1000.0 / ms, peak, std::sqrt(sum / (2.0 * total)));

    juce::File wav{juce::String(argv[2])};
    wav.deleteFile();
    juce::WavAudioFormat fmt;
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(wav.createOutputStream())) {
        if (auto w = std::unique_ptr<juce::AudioFormatWriter>(fmt.createWriterFor(stream.get(), rate, 2, 16, {}, 0))) {
            stream.release();
            w->writeFromAudioSampleBuffer(out, 0, total);
            std::printf("wrote %s\n", argv[2]);
        }
    }
    if (argc > 3) {
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::File png{juce::String(argv[3])};
        png.deleteFile();
        juce::PNGImageFormat pf;
        if (auto s = std::unique_ptr<juce::FileOutputStream>(png.createOutputStream())) pf.writeImageToStream(img, *s);
        std::printf("wrote %s\n", argv[3]);
    }
    return 0;
}
