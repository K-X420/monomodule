// Monomodule MD: the Machinedrum's sound engine from the user's MD OS file.
//   DSP2 renders the 16 voices (md/MdVoiceEngine), DSP1 runs the track effects, routing, mixer and master effects
//   (md/MdMixEngine); the OS's own ColdFire code converts the synthesis knobs and the master effects (md/MdControl),
//   and its routing law converts level, volume, pan and the sends. Knobs are slewed as raw words, as the OS does.
// MIDI (any channel for trigs; base channel 1 for CCs, as the hardware's default map):
//   trigs  notes 36 38 40 41 43 45 47 48 50 52 53 55 57 59 60 62 = tracks 1-16
//   CCs    channel 1 + t/4: [16, 40, 72, 96][t%4] + 0..7 synthesis, +8..15 effects, +16..20 DIST VOL PAN DEL REV;
//          CC 8 + t%4 = level
// Outputs: main = the hardware's A/B (MAIN and A, B routes), optional C/D and E/F buses.
// As on the hardware, a new machine starts playing at its track's next trig.
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "MdParams.h"
#include "MdLibrary.h"
#include "PreviewPlayer.h"
#include "MdPreview.h"
#include "MdCatalog.h"
#include "MdDump.h"
#include "MdControl.h"
#include "MdEngine.h"
#include "MdFirmware.h"
#include "MdKit.h"
#include "MdMixEngine.h"
#include "MdSequencer.h"
#include "MdVoiceEngine.h"

namespace mnm::plugin::md {

class MdProcessor : public juce::AudioProcessor,
                    private juce::AudioProcessorValueTreeState::Listener,
                    private juce::AsyncUpdater {
public:
    MdProcessor();
    ~MdProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Monomodule MD"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }   // the MID machines
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // OS file (message thread). The path is shared by every Monomodule MD instance.
    juce::String firmwarePath() const { return m_firmwarePath; }
    void setFirmwarePath(const juce::String& path, bool persist = true);
    bool engineReady() const { return m_engineReady.load(); }
    // The status line: the OS load, then a DSP fault, the DSPs' load per pass and a resampling warning (as Monomodule's)
    juce::String statusText() const;
    double hostBpm() const { return m_hostBpm.load(); }
    double tempo() const;   // what the engine runs at: the host's tempo when synced, else the BPM parameter
    // Another instance (or plugin) picked an OS file: take it when it differs and exists (the editor polls this)
    void refreshSharedOsPath();
    const mnm::md::Machine* machineInfo(int id) const { return m_fw ? m_fw->byId(id) : nullptr; }

    int machineIdOf(int t) const;
    // Kit loading (message thread): every track's machine and knobs, levels and the master effects. Machines the
    // plugin does not run (ROM/RAM, MIDI, controller, input) become GND---. Returns how many tracks were emptied.
    int applyKit(const mnm::md::Kit& kit);
    juce::String kitName() const { return m_kitName; }
    // The library (MdLibrary, the shared Monomodule Library): the sound as a Machinedrum kit (what the hardware would
    // hold: unknown bytes such as trig groups and the LFOs' running state come from the kit last loaded), loading a
    // kit or one track's sound by catalog id, and which ones are loaded.
    mnm::mddump::Kit captureMdKit() const;
    int loadMdKit(const juce::String& key, const mnm::mddump::Kit& kit, const juce::String& name);   // returns emptied tracks
    void setLoadedKit(const juce::String& key, const juce::String& name);   // after a save: the new kit is the loaded one
    juce::String loadedKitKey() const { return m_kitKey; }
    bool kitModified() const;   // the sound differs from the loaded kit
    mnm::mdcatalog::Sound captureSound(int t) const;
    bool loadSound(int t, const juce::String& key, const mnm::mdcatalog::Sound& sound, const juce::String& name);   // false: a machine the plugin has not
    void setLoadedSound(int t, const juce::String& key, const juce::String& name);
    juce::String loadedSoundKey(int t) const { return m_sounds[size_t(t)].key; }
    juce::String loadedSoundName(int t) const { return m_sounds[size_t(t)].name; }
    bool soundModified(int t) const;
    // INIT KIT: every track, routing, LFO and master effect back to a fresh instance's; the library refs are cleared
    void initKit();
    void syncMachineSideEffects() { handleUpdateNowIfNeeded(); }   // dev tools without a message loop: apply machine changes now
    void clearFirmware();   // forgets the OS file (here and for every Monomodule MD)
    // Library previews (auditioning a kit or a sound before loading it), mixed into the main output. The renderer and
    // its own emulated Machinedrum exist from the first preview on.
    void previewPlay(const juce::String& key, const std::function<mnm::mdpreview::Spec()>& build);
    void previewStop();
    juce::String previewKey() const { return m_previewKey; }
    void previewSetLoop(bool on) { m_previewVoice.setLoop(on && m_previewKey.isNotEmpty()); }   // off again on stop or another preview
    bool previewLoop() const { return m_previewKey.isNotEmpty() && m_previewVoice.loop(); }
    juce::String previewStatus() const { return m_previewRenderer ? m_previewRenderer->status() : juce::String(); }
    bool previewPoll();   // editor timer: true when the playing state changed
    // LOCK: a locked track keeps its sound when a kit is loaded (kept in the plugin state)
    bool trackLocked(int t) const { return m_locked[size_t(t)].load(); }
    void setTrackLocked(int t, bool on) { m_locked[size_t(t)].store(on); }

    // UW samples (message thread): any audio file into a ROM slot (mixed to mono, kept at its own rate; the DSP
    // resamples). Kept in the plugin state. Returns an error text, empty on success.
    juce::String loadSample(int slot, const juce::File& file);
    static bool isAudioFile(const juce::String& path);   // a format loadSample reads (wav, aiff, flac, ogg, ...)
    int firstEmptyRomSlot() const;                        // in ROM-01..48 order; -1 when every ROM slot holds a sample
    void clearSample(int slot);
    juce::String sampleName(int slot) const { return m_samples[size_t(slot)].name; }
    double sampleSeconds(int slot) const { const auto& s = m_samples[size_t(slot)]; return s.rate > 0 ? double(s.data.size()) / s.rate : 0.0; }
    double sampleMemoryUsed() const;   // 0..1 of the UW sample memory
    void auditionTrack(int t) { m_audition[size_t(t)].store(true); }   // UI: trig as from MIDI
    float trackActivity(int t) const { return m_activity[size_t(t)].load(); }   // decays between UI polls
    // The track's output level for the LEV fader's meter, as Monomodule's: linear peak at the plugin's output scale,
    // falling off x0.8 per host block so it stays readable between UI polls
    float trackPeak(int t) const { return m_peak[size_t(t)].load(); }
    mnm::md::Engine* engineForTests() { return m_engine.get(); }   // dev: md-plugintest probes

    // Pattern playback (message thread): the pattern bank is a project's patterns and kits (kept in the plugin state).
    // The editor sets it from the project of a kit loaded from the library.
    // kitSlot: the bank kit that is loaded now (-1: none of them), so a pattern on it does not load it again
    void setPatternBank(const juce::String& projectId, const juce::String& name, const mnm::mddump::Dump& dump, int kitSlot);
    juce::String bankName() const { return m_bankName; }
    juce::String bankProjectId() const { return m_bankProjectId; }
    bool bankHasPattern(int slot) const;
    // Pattern editing (message thread): the pattern of a slot (null: none), and an edit of it: fn changes a copy (an
    // empty slot starts as a fresh 16-step pattern on the loaded bank kit), which replaces it in the bank; the playing
    // pattern takes it from its next block. With no bank, a new one ("PLUGIN") is made. Kept in the plugin state.
    std::shared_ptr<const mnm::mddump::Pattern> bankPattern(int slot) const;
    void editPattern(int slot, const std::function<void(mnm::mddump::Pattern&)>& fn);
    int seqStep() const { return m_seqStepUi.load(); }        // the playing step, -1 when not playing
    int seqLength() const { return m_seqLenUi.load(); }       // the pattern's length (steps)
    int seqPattern() const { return m_seqPatternUi.load(); }  // the playing (or next, when stopped) pattern slot, -1 = none
    uint64_t seqTrigs(int t) const { return m_seqTrigsUi[size_t(t)].load(); }   // that pattern's trig steps of track t
    int seqSongRow() const { return m_seqRowUi.load(); }      // SONG: the playing row, -1 = none
    // dev: the trigs the last blocks fired (track, host sample from the start of logging), when logging is on
    struct TrigLog { int track; int64_t sample; int step; };
    void setTrigLogging(bool on) { m_trigLogOn = on; m_trigLog.clear(); m_trigLog.reserve(on ? 8192 : 0); m_logClock = 0; }
    const std::vector<TrigLog>& trigLog() const { return m_trigLog; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kMixRaw = 14;   // AMD AMF EQF EQG FLTF FLTW FLTQ SRR, DIST, VOL, PAN, DEL, REV, LEVEL
    struct Track {
        std::atomic<float>* machine = nullptr;
        std::atomic<float>* knobs[8] = {};
        std::atomic<float>* mix[kMixRaw] = {};
        std::atomic<float>* lfo[8] = {};             // TRK PARAM SHP1 SHP2 TYPE SPD DEP MIX
        std::atomic<float>* route = nullptr;
        std::atomic<float>* mute = nullptr;
    };

    void loadEngine();
    bool pushSamples();   // every slot into DSP2 (under the engine lock)
    struct Sample { juce::String name; std::vector<float> data; double rate = 44100.0; };
    std::array<Sample, mnm::md::VoiceEngine::kSlots> m_samples;
    juce::ValueTree samplesToTree() const;
    void samplesFromTree(const juce::ValueTree& t);
    void refreshParameters();   // audio thread, before each pass: slew, convert, send
    void runPass();             // one 32-frame block through DSP2 and DSP1 into the FIFO
    void handleCc(int channel, int cc, int value);

    // CTR and MID machines (audio thread, once per host block): what the OS's parameter-change routine (MainOS
    // 0x20C930) does when a knob of such a track moves, and the MID trig (0x209914). Writes to other parameters go
    // through m_ctlFifo to the message thread (setValueNotifyingHost); MIDI goes into the block's MIDI output.
    int trackParam(int t, int p) const;   // 0..127 by the MD's numbering (see trackParamId)
    int midValue(int t, int p) const { return juce::jlimit(0, 127, seqParam(t, p) + m_lfoOffset[size_t(t)][size_t(p)]); }
    void midStream(int pos);              // after each pass: MID values that moved (knob or LFO) -> MIDI
    std::array<std::array<int16_t, 24>, kTracks> m_lfoOffset{};   // per MID parameter: the LFOs' part (0..127 scale)
    std::array<std::array<int16_t, 24>, kTracks> m_midSent{};     // the value last sent (-1 = not yet: no send)
    std::array<int, kTracks> m_midMachine{};
    void controlMachines(int n);
    void midTrig(int t, int pos);
    void midSend(int pos, uint8_t a, uint8_t b, int c = -1);
    void queueSet(int target, int p, int value, bool mirror = false);   // target 0-15 = track, 16-19 = master effect;
                                                                      // mirror: a CTR knob <-> master effect echo, not a turn
    struct CtlSet { int8_t target, p, value; };
    juce::AbstractFifo m_ctlFifo{512};
    std::array<CtlSet, 512> m_ctlBuf{};
    std::array<std::array<int16_t, 24>, kTracks + 4> m_ctlSeen{};      // the values last seen (tracks, then the master effects)
    std::array<std::array<int16_t, 24>, kTracks + 4> m_ctlPending{};   // a queued write not yet arrived (-1 none)
    std::array<int64_t, kTracks + 4> m_ctlPendingUntil{};
    std::array<int, kTracks> m_ctlMachine{};
    std::array<std::atomic<bool>, kTracks> m_ctlRebase{};              // a machine change: the track's values are not turns
    int64_t m_ctlQuietUntil = 0;                                        // a load: nothing is a turn until then
    bool m_ctlStarted = false;
    struct MidNote { uint8_t status, note; int64_t offAt; };
    std::array<std::vector<MidNote>, kTracks> m_midNotes;
    std::array<int, 16> m_midLastPb{}, m_midLastMw{}, m_midLastPc{};   // per channel, as the OS remembers what it sent
    juce::MidiBuffer m_midiOut;
    int64_t m_clock = 0;   // host samples since prepare
    int m_blockLen = 1;    // the current host block (MIDI positions stay inside it)
    void parameterChanged(const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;

    juce::String m_firmwarePath, m_status, m_kitName, m_kitKey;
    mnm::mddump::Kit m_baseKit;                   // the kit last loaded: the bytes the plugin has no control for
    std::vector<uint8_t> m_kitSnapshot;           // the loaded kit as captured right after loading (kitModified compares)
    struct LoadedSound { juce::String key, name; std::string hash; };
    std::array<LoadedSound, kTracks> m_sounds;
    std::unique_ptr<mnm::md::Firmware> m_fw;
    std::unique_ptr<mnm::md::Engine> m_engine;   // the OS's control CPU and the two DSPs (src/md/MdEngine)
    mutable juce::CriticalSection m_engineLock;
    std::atomic<bool> m_engineReady{false};

    std::array<Track, kTracks> m_tracks;
    std::array<std::array<std::atomic<float>*, 8>, 4> m_masterFx{};
    std::atomic<bool> m_snap{true};   // the next tick jumps every knob to its target (load, state restore, kit)
    std::array<std::array<uint8_t, 36>, kTracks> m_kitLfos{};   // a loaded kit's LFO structs, for the audio thread
    std::array<std::atomic<bool>, kTracks> m_kitLfoPending{};
    std::atomic<float>* m_master = nullptr;
    std::atomic<float>* m_velMode = nullptr;
    std::atomic<float>* m_outputMode = nullptr;
    std::atomic<uint32_t> m_directMask{0};   // PER TRACK: the tracks on their own buses (set each block)
    std::atomic<float>* m_accent = nullptr;
    std::atomic<double> m_hostBpm{120.0};
    std::atomic<float>* m_bpmSync = nullptr;
    std::atomic<float>* m_bpm = nullptr;
    bool m_wasSynced = true;
    std::array<std::atomic<bool>, kTracks> m_audition{};
    std::array<std::atomic<bool>, kTracks> m_locked{};
    std::unique_ptr<mnm::library::PreviewRenderer> m_previewRenderer;
    mnm::library::PreviewVoice m_previewVoice;
    std::shared_ptr<mnm::library::PreviewAudio> m_previewAudio;
    juce::String m_previewKey;
    void mixPreview(juce::AudioBuffer<float>& buffer);
    std::array<std::atomic<float>, kTracks> m_activity{};
    std::array<std::atomic<float>, kTracks> m_peak{};
    std::array<std::atomic<bool>, kTracks> m_machineChanged{};   // message thread: load that machine's defaults
    // per-machine knob memory (state, not parameters): the eight values of every machine a track has visited, as
    // Monomodule keeps its SYN values; a machine change brings back that machine's values (else its defaults)
    std::array<std::array<std::array<uint8_t, 8>, kNumMachines>, kTracks> m_shadow{};
    std::array<std::array<bool, kNumMachines>, kTracks> m_visited{};
    std::array<int, kTracks> m_shadowIdx{};
    // EFFECTS + ROUTING (+ the LFO page's last three: parameters 8..23) mean other things on MID / CTR machines (CC
    // pairs, 8P assignments): a switch between an audio machine and a MID / CTR one keeps each side's values (session
    // only), else that side's defaults (8..20 at 0; 21..23 at 0 for a CTR-8P, kept otherwise as they are LFO knobs)
    std::array<std::array<std::array<uint8_t, 16>, 2>, kTracks> m_pageStore{};
    std::array<std::array<bool, 2>, kTracks> m_pageStored{};
    juce::ValueTree shadowsToTree() const;
    void shadowsFromTree(const juce::ValueTree& t);
    MachineKnobInfo m_knobInfo;
    std::array<int, kNumMachines> m_idOfIndex{};

    // engine-rate (44.1 kHz) output FIFO: the six DAC channels, then the per-track outputs (L/R per track), and
    // their host-rate resamplers
    static constexpr int kDac = mnm::md::MixEngine::kChannels;
    static constexpr int kFifoChannels = kDac + 2 * kTracks;
    std::array<std::vector<float>, kFifoChannels> m_fifo;
    int m_fifoLen = 0;
    double m_hostRate = 44100.0;
    std::array<juce::LagrangeInterpolator, kFifoChannels> m_interp;
    // side-chain input for the INP machines, resampled to the engine rate
    std::array<std::vector<float>, 2> m_inFifo;
    int m_inLen = 0;
    std::array<juce::LagrangeInterpolator, 2> m_inInterp;
    std::array<int32_t, 64> m_inBlock{};
    // seq: index into m_seqTrigs (a sequencer trig with its locks / slides / accent), -1 = a MIDI note or the UI
    struct PendingTrig { int track; double enginePos; int velocity; int seq = -1; };   // engine frames from the current FIFO read point
    std::vector<PendingTrig> m_pending;

    // ---- pattern playback (audio thread unless noted)
    struct SeqBank {
        std::vector<std::shared_ptr<const mnm::mddump::Pattern>> patterns;   // by slot (128); an edit replaces one
        std::vector<mnm::mddump::Kit> kits;           // by slot (64)
        std::vector<mnm::mddump::Song> songs;         // by slot (32)
        std::array<bool, 128> hasPattern{};
        std::array<bool, 64> hasKit{};
        std::array<bool, 32> hasSong{};
    };
    std::shared_ptr<const SeqBank> m_bank, m_bankOld;   // message thread swaps (under m_bankLock); the old one stays alive
    juce::SpinLock m_bankLock;
    juce::String m_bankName, m_bankProjectId;
    std::atomic<float>* m_seqOn = nullptr;
    std::atomic<float>* m_patternParam = nullptr;
    // What plays: a pattern until another is queued (PATTERN), or a song row's pattern steps x repeats (SONG). The one
    // that ended last keeps playing its swung steps' trigs that come after its end.
    struct Segment {
        mnm::md::PatternPlayer player;
        bool valid = false;
        int slot = -1, row = -1, accentOn = 0x80;
        double origin = 0;          // the clock its first step starts at
        int64_t steps = -1;         // how many steps it plays (-1: until a change)
        uint16_t mutes = 0;         // a song row's muted tracks (bit t)
        double endClock() const { return origin + double(steps) * player.clocksPerStep(); }
    };
    Segment m_seg, m_prevSeg;
    struct SongCursor { int row = 0; std::array<int16_t, 256> loops{}; };   // the next row; each LOOP row's jumps so far
    SongCursor m_cursor;
    int m_patternSeen = -1, m_seqQueued = -1, m_modeSeen = -1, m_songSeen = -1;
    std::atomic<float>* m_seqMode = nullptr;
    std::atomic<float>* m_songParam = nullptr;
    std::array<std::atomic<uint64_t>, kTracks> m_seqTrigsUi{};
    std::atomic<int> m_seqRowUi{-1};
    std::atomic<int> m_programChange{-1};                // a MIDI program change for the message thread (PATTERN follows)
    bool m_seqRunning = false;
    double m_seqExpect = 0, m_seqClock0 = 0, m_seqCps = 0;   // clocks: the block's expected start, its start, per host sample
    std::vector<mnm::md::SeqTrig> m_seqTrigs;
    std::array<std::array<int16_t, 24>, kTracks> m_lockVal{};      // a parameter held by a lock or a slide (-1 = the kit's)
    std::array<std::array<mnm::md::Glide, 24>, kTracks> m_glide{};
    std::atomic<const mnm::mddump::Kit*> m_kitOverride{nullptr};   // a pattern's kit until the message thread has loaded it
    std::shared_ptr<const SeqBank> m_overrideBank;
    std::atomic<int> m_seqKitSlot{-1};                             // the bank's kit that is loaded (-1 = another)
    std::atomic<int> m_seqKitRequest{-1};
    std::atomic<int> m_seqStepUi{-1}, m_seqLenUi{16}, m_seqPatternUi{-1};
    bool m_hostPlaying = false;
    double m_hostPpq = 0;
    const mnm::mddump::Kit* m_kitSwitch = nullptr;   // the kit a pending switch event (PendingTrig track -2) brings in
    std::atomic<bool> m_bankFresh{false};
    std::atomic<int> m_patternEdited{-1};   // a slot whose pattern an edit replaced (-1 none; -2 several)
    bool m_trigLogOn = false;
    std::vector<TrigLog> m_trigLog;
    int64_t m_logClock = 0;
    // CTR writes (queueSet) take effect at once: the value until the parameter has it (-1 = none)
    std::array<std::array<int16_t, 24>, kTracks + 4> m_ctlWrite{};
    std::array<int64_t, kTracks + 4> m_ctlWriteUntil{};
    void scheduleSequencer(int n, double ratio);
    void seqGenerate(const Segment& seg, double from, double to, double ratio);
    // seg plays `slot` from `origin`: steps [start, end) of it, `steps` of them (-1: on); its kit comes in at its start
    // (atEnginePos >= 0: a switch event in the pending list, else at once)
    void seqStart(Segment& seg, int slot, int start, int end, int64_t steps, uint16_t mutes, double origin,
                  const SeqBank* bank, const std::shared_ptr<const SeqBank>& hold, double atEnginePos);
    // the segment after the current one, from `origin` (PATTERN: the queued pattern; SONG: the next row)
    void seqNext(double origin, const SeqBank* bank, const std::shared_ptr<const SeqBank>& hold, double atEnginePos, bool song);
    void seqStop();
    void applyKitSwitch();   // at a pattern change's time: its kit plays (m_kitSwitch) until the message thread has loaded it
    static int overrideMachine(const mnm::mddump::Kit& kit, int t);   // the kit's machine, GND--- for one the plugin has not
    void trigLocks(int t, const mnm::md::SeqTrig* s);   // a trig's locks and slides (none: a MIDI / UI trig releases them)
    int kitParam(int t, int p) const;   // the knob's own value (a pattern's kit until loaded, a CTR write, else the parameter)
    int seqParam(int t, int p) const { const int v = m_lockVal[size_t(t)][size_t(p)]; return v >= 0 ? v : kitParam(t, p); }
};

} // namespace mnm::plugin::md
