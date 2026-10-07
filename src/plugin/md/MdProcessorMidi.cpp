// The processor's MIDI: settings, notes, CCs in and out, the MID and CTR machines
#include "MdProcessorInternal.h"
#include <cmath>

namespace mnm::plugin::md {

// ---------------------------------------------------------------------------------------------- MIDI settings

MdProcessor::MidiSettings MdProcessor::defaultMidiSettings()
{
    // (no pattern notes: the factory map puts A01..A16 on the white keys from E3, FROM PROJECT brings it in)
    MidiSettings s;
    s.noteTrack.fill(-1);
    s.noteAction.fill(-1);
    for (int t = 0; t < kTracks; ++t) s.noteTrack[size_t(kTrackNotes[t])] = int8_t(t);
    return s;
}

MdProcessor::MidiSettings MdProcessor::midiSettings() const
{
    MidiSettings s;
    s.baseChannel = m_baseCh.load();
    for (int i = 0; i < 128; ++i) s.noteTrack[size_t(i)] = m_noteTrack[size_t(i)].load();
    s.programChange = m_pcMode.load();
    s.pcChannel = m_pcChannel.load();
    for (int i = 0; i < 128; ++i) s.noteAction[size_t(i)] = m_noteAction[size_t(i)].load();
    s.patternNoteMode = m_ptnNoteMode.load();
    s.ctrlIn = m_ctrlIn.load();
    s.midiOut = m_midiOutMode.load();
    return s;
}

void MdProcessor::setMidiSettings(const MidiSettings& s)
{
    m_baseCh.store(juce::jlimit(-1, 15, s.baseChannel));
    for (int i = 0; i < 128; ++i) m_noteTrack[size_t(i)].store(s.noteTrack[size_t(i)] >= 0 && s.noteTrack[size_t(i)] < kTracks ? s.noteTrack[size_t(i)] : int8_t(-1));
    for (int t = 0; t < kTracks; ++t) {   // the note a track sends: its first in the map
        int note = -1;
        for (int i = 0; i < 128 && note < 0; ++i) if (s.noteTrack[size_t(i)] == t) note = i;
        m_trackNote[size_t(t)].store(int8_t(note));
    }
    m_pcMode.store(juce::jlimit(0, 3, s.programChange));
    m_pcChannel.store(juce::jlimit(0, 16, s.pcChannel));
    m_ptnNoteMode.store(juce::jlimit(0, 2, s.patternNoteMode));
    m_ctrlIn.store(s.ctrlIn);
    for (int i = 0; i < 128; ++i) {
        const int a = s.noteAction[size_t(i)];
        const bool ok = s.noteTrack[size_t(i)] < 0 && ((a >= 0 && a < 128) || a == kStartNote || a == kStopNote);
        m_noteAction[size_t(i)].store(int16_t(ok ? a : -1));
    }
    m_midiOutMode.store(juce::jlimit(0, 2, s.midiOut));
}

MdProcessor::MidiSettings MdProcessor::fromGlobal(const mnm::mddump::Global& g, const MidiSettings& keep)
{
    MidiSettings s = keep;
    s.baseChannel = g.baseChannel < 16 ? g.baseChannel : -1;
    for (int i = 0; i < 128; ++i) s.noteTrack[size_t(i)] = int8_t(g.trackOfNote(i));
    s.programChange = g.programChange & 3;   // bits 0-1 the mode (IN, OUT), bits 2-6 the channel (0 AUTO)
    s.pcChannel = juce::jlimit(0, 16, (g.programChange >> 2) & 31);
    for (int i = 0; i < 128; ++i) {
        const int k = g.keyMap[i];
        s.noteAction[size_t(i)] = int16_t(k >= 16 && k < 16 + 128 ? k - 16 : k == 0x90 ? kStartNote : k == 0x91 ? kStopNote : -1);
    }
    s.patternNoteMode = juce::jlimit(0, 2, int(g.trigMode));
    return s;
}

// The CC map (relative to the base channel): channel + t / 4, CC [16, 40, 72, 96][t % 4] + p; level CC 8 + t % 4
void MdProcessor::sendCc(int t, int p, int value, int pos)
{
    const int base = m_baseCh.load();
    if (base < 0) return;
    const int ch = base + t / 4;
    if (ch > 15) return;
    static const int bases[4] = {16, 40, 72, 96};
    const int cc = p == 24 ? 8 + t % 4 : bases[t % 4] + p;
    midSend(pos, uint8_t(0xB0 | ch), uint8_t(cc), juce::jlimit(0, 127, value));
    m_outSeen[size_t(t)][size_t(p)] = int16_t(value);
}

// Knob turns (and automation) out as CCs; a value that came in as a CC is not sent back
void MdProcessor::scanKnobsOut(int pos)
{
    const bool quiet = m_clock < m_ctlQuietUntil;   // a kit load: not turns
    for (int t = 0; t < kTracks; ++t) {
        if (isMidMachine(machineIdOf(t))) continue;   // a MID track's values are its own MIDI
        for (int p = 0; p <= 24; ++p) {
            const int v = p == 24 ? juce::jlimit(0, 127, int(std::lround(m_tracks[size_t(t)].mix[13]->load()))) : trackParam(t, p);
            auto& seen = m_outSeen[size_t(t)][size_t(p)];
            if (v == seen) continue;
            if (seen < 0 || quiet) { seen = int16_t(v); continue; }
            sendCc(t, p, v, pos);
        }
    }
}

// A trig played while recording (MainOS 0x2379AC): onto the step playing; at 2x, 3/4x and 3/2x a hit late in its step
// goes onto the next one (past 2/3, 5/8 and 1/2 of the step). Recorded trigs are never accented, whatever the velocity.
// A note the key map gives a pattern, START or STOP (MainOS 0x20D104 / 0x20D1B8), by the PATTERN NOTES mode:
// GATE plays the pattern at once and its note-off stops; MOMENTARY plays it at once and its note-off queues the pattern
// that was playing (or stops, if none was); QUEUE makes it the next pattern (at once when stopped).
void MdProcessor::patternNote(int note, bool on)
{
    const int act = m_noteAction[size_t(note)].load();
    const int mode = m_ptnNoteMode.load();
    const bool running = m_seqRunning && !m_seqHalted;
    auto choose = [&](int p) { m_patternSeen = p; m_ptnPending.store(p); m_programChange.store(p); triggerAsyncUpdate(); };
    auto stop = [&] { m_seqHalted = true; m_intPlay.store(false); };
    auto start = [&] { m_seqHalted = false; if (!m_hostPlaying) m_intPlay.store(true); };
    if (!on) {
        if (note != m_ptnHeldNote) return;
        m_ptnHeldNote = -1;
        if (mode == 0) stop();
        else if (mode == 1) {
            if (m_ptnMomentBack < 0) stop();
            else { m_seqQueued = m_ptnMomentBack == m_seg.slot ? -1 : m_ptnMomentBack; choose(m_ptnMomentBack); }
        }
        return;
    }
    if (act == kStartNote) { start(); return; }
    if (act == kStopNote) { stop(); return; }
    if (act < 0 || act > 127) return;
    if (mode == 2) {   // QUEUE
        if (running) { m_seqQueued = act == m_seg.slot ? -1 : act; choose(act); }
        else { m_seqQueued = act; choose(act); start(); }
        return;
    }
    if (m_ptnHeldNote < 0) m_ptnMomentBack = running ? m_seg.slot : -1;   // MOMENTARY: what comes back
    m_ptnHeldNote = note;
    choose(act);
    if (running) m_ptnJump = act;
    else { m_seqQueued = act; start(); }
}

void MdProcessor::midSend(int pos, uint8_t a, uint8_t b, int c)
{
    const uint8_t bytes[3] = {a, b, uint8_t(juce::jlimit(0, 127, c))};
    m_midiOut.addEvent(bytes, c < 0 ? 2 : 3, juce::jlimit(0, juce::jmax(0, m_blockLen - 1), pos));
}

// The MID trig (MainOS 0x209914) on channel n of MID-n: the track's sounding notes end; PCHG (if set and not the program
// last sent on the channel); NOTE, and N2 / N3 when off centre (NOTE + N - 64), at VEL (0 plays as 1); PB and MW when
// they differ from what the channel last got, AT when not 0. The notes last 3 x (LEN + 1) sequencer ticks (24 per
// quarter note, MainOS 0x209DB2 / 0x23A68C; LEN 0 = 4 ticks), so LEN 1 is one 16th step, LEN 7 a quarter note and
// LEN 127 four bars.
void MdProcessor::midTrig(int t, int pos)
{
    const int ch = machineIdOf(t) - 96;
    auto& notes = m_midNotes[size_t(t)];
    for (const auto& nt : notes) midSend(pos, uint8_t(0x80 | ch), nt.note, 0);
    notes.clear();
    const int pc = midValue(t, 20);
    if (pc > 0 && pc - 1 != m_midLastPc[size_t(ch)]) { midSend(pos, uint8_t(0xC0 | ch), uint8_t(pc - 1)); m_midLastPc[size_t(ch)] = pc - 1; }
    const int note = midValue(t, 0), len = midValue(t, 3);   // the LFOs count (added after the knob, as the OS does)
    const int vel = juce::jmax(1, midValue(t, 4));
    const double bpm = tempo();
    const int ticks = len > 0 ? 3 + 3 * len : 4;
    const int64_t offAt = m_clock + pos + int64_t(std::lround(ticks * 60.0 / (bpm * 24.0) * m_hostRate));
    auto play = [&](int nn) {
        midSend(pos, uint8_t(0x90 | ch), uint8_t(nn), vel);
        notes.push_back({uint8_t(0x90 | ch), uint8_t(nn), offAt});
    };
    play(note);
    for (int k : {1, 2}) {
        const int n = midValue(t, k);
        if (n != 64) play(juce::jlimit(0, 127, note + n - 64));
    }
    const int pb = midValue(t, 5), mw = midValue(t, 6), at = midValue(t, 7);
    if (pb != m_midLastPb[size_t(ch)]) { midSend(pos, uint8_t(0xE0 | ch), 0, pb); m_midLastPb[size_t(ch)] = pb; }
    if (mw != m_midLastMw[size_t(ch)]) { midSend(pos, uint8_t(0xB0 | ch), 1, mw); m_midLastMw[size_t(ch)] = mw; }
    if (at != 0) midSend(pos, uint8_t(0xD0 | ch), uint8_t(at));
}

// After each pass, every MID track's continuous values, knob plus LFO ("LFOs applied to MIDI machines are always added
// after all locks and slides", OS 1.33): PB, MW, AT and the six CC values go out when they change, at the pass's place
// in the host block. A machine change or a load settles them without sending. (PCHG and the CC numbers are not
// streamed: they send on a turn only, in controlMachines.)
void MdProcessor::midStream(int pos)
{
    const bool quiet = !m_ctlStarted || m_clock < m_ctlQuietUntil;
    for (int t = 0; t < kTracks; ++t) {
        const int id = machineIdOf(t);
        if (!isMidMachine(id)) { m_midMachine[size_t(t)] = id; continue; }
        const bool settle = quiet || id != m_midMachine[size_t(t)];
        m_midMachine[size_t(t)] = id;
        const int ch = id - 96;
        for (int p : {5, 6, 7, 9, 11, 13, 15, 17, 19}) {
            const int v = midValue(t, p);
            auto& sent = m_midSent[size_t(t)][size_t(p)];
            if (settle || sent < 0) { sent = int16_t(v); continue; }
            if (v == sent) continue;
            sent = int16_t(v);
            if (p == 5) { midSend(pos, uint8_t(0xE0 | ch), 0, v); m_midLastPb[size_t(ch)] = v; }
            else if (p == 6) { midSend(pos, uint8_t(0xB0 | ch), 1, v); m_midLastMw[size_t(ch)] = v; }
            else if (p == 7) midSend(pos, uint8_t(0xD0 | ch), uint8_t(v));
            else if (const int cc = trackParam(t, p - 1); cc > 0) midSend(pos, uint8_t(0xB0 | ch), uint8_t(cc == 1 ? 0 : cc), v);
        }
    }
}

// Once per host block: every parameter of a CTR / MID track (and the master effects) against the value last seen. A
// change while the machine stays and no load is settling is a turn, as the OS's parameter-change routine sees it:
//   MID     PB / MW / AT / a CC value (on its CC number; 0 = off, 1 = CC 0) / PCHG -> MIDI out now
//   CTR-RE GB EQ DX   SYNTHESIS knob k -> that master effect's parameter k (and the knobs follow the master effect)
//   CTR-AL  parameter p moved by d (its own value counted within 1..126) -> p + d on every other track, except MID
//           and CTR tracks and the RAM recorders' SYNTHESIS knobs (MainOS 0x207E0E)
//   CTR-8P  P1..P8 -> the parameter assigned by its TRK / PAR pair (EFFECTS and ROUTING pages, then the LFO page's
//           last three), unless that track is a CTR-AL or CTR-8P (MainOS 0x207D36)
void MdProcessor::controlMachines(int n)
{
    juce::ignoreUnused(n);
    const bool quiet = !m_ctlStarted || m_clock < m_ctlQuietUntil;
    std::array<int, kTracks> ids{};
    for (int t = 0; t < kTracks; ++t) ids[size_t(t)] = machineIdOf(t);
    auto rebase = [&](int target) {
        for (int p = 0; p < 24; ++p) {
            m_ctlSeen[size_t(target)][size_t(p)] = int16_t(target < kTracks ? seqParam(target, p)
                                                           : p < 8 ? juce::jlimit(0, 127, int(std::lround(m_masterFx[size_t(target - kTracks)][size_t(p)]->load()))) : 0);
            m_ctlPending[size_t(target)][size_t(p)] = -1;
        }
    };
    // what moved since last block (a queued write counts once it has arrived, not before)
    auto moved = [&](int target, int p, int now) {
        auto& pend = m_ctlPending[size_t(target)][size_t(p)];
        auto& seen = m_ctlSeen[size_t(target)][size_t(p)];
        if (pend >= 0) {
            if (now == pend || m_clock > m_ctlPendingUntil[size_t(target)]) { pend = -1; seen = int16_t(now); }
            return false;
        }
        if (now == seen) return false;
        return true;
    };
    for (int t = 0; t < kTracks; ++t) {
        const int id = ids[size_t(t)];
        if (quiet || m_ctlRebase[size_t(t)].exchange(false) || id != m_ctlMachine[size_t(t)]) {
            m_ctlMachine[size_t(t)] = id;
            rebase(t);
            if (!isMidMachine(id))
                for (const auto& nt : m_midNotes[size_t(t)]) midSend(0, uint8_t(nt.status & 0xEF), nt.note, 0);
            if (!isMidMachine(id)) m_midNotes[size_t(t)].clear();
            continue;
        }
        if (!isMidMachine(id) && !isCtrMachine(id)) continue;
        for (int p = 0; p < 24; ++p) {
            const int now = seqParam(t, p);   // a pattern lock acts as a turn of the knob (CTR-AL: see below)
            const bool held = m_lockVal[size_t(t)][size_t(p)] >= 0;
            const bool lockMove = held || m_ctlLockHeld[size_t(t)][size_t(p)];   // a lock / slide, or its release
            m_ctlLockHeld[size_t(t)][size_t(p)] = held;
            if (!moved(t, p, now)) continue;
            // a muted CTR track (its mute or the song row's) applies no locks (MainOS 0x23B19C); knob turns still act
            const bool ctrMuted = isCtrMachine(id) && lockMove && (silenced(t) || (m_seg.valid && ((m_seg.mutes >> t) & 1)));
            const int before = m_ctlSeen[size_t(t)][size_t(p)];
            m_ctlSeen[size_t(t)][size_t(p)] = int16_t(now);
            if (isMidMachine(id)) {
                const int ch = id - 96;
                if (p == 20 && now > 0) { midSend(0, uint8_t(0xC0 | ch), uint8_t(now - 1)); m_midLastPc[size_t(ch)] = now - 1; }
                // PB MW AT and the CC values go out from midStream (knob + LFO)
            } else if (ctrMuted) {
                continue;
            } else if (const int fx = ctrMasterFx(id); fx >= 0) {
                if (p < 8) queueSet(kTracks + fx, p, now, true);
            } else if (id == kCtrAll) {
                // a knob turn moves every track's parameter by as much (MainOS 0x207E3A); a lock, and its release, set
                // them all to the value itself (0x237AE4); a muted CTR-AL's locks do nothing (0x23B17E)
                const int d = juce::jlimit(1, 126, now) - juce::jlimit(1, 126, before);
                if (!lockMove && d == 0) continue;
                for (int u = 0; u < kTracks; ++u) {
                    const int uid = ids[size_t(u)];
                    if (u == t || isMidMachine(uid) || isCtrMachine(uid)) continue;
                    if (p < 8 && (uid == 160 || uid == 161 || uid == 165 || uid == 166)) continue;   // RAM-R1..R4
                    const int cur = m_ctlPending[size_t(u)][size_t(p)] >= 0 ? m_ctlPending[size_t(u)][size_t(p)] : kitParam(u, p);
                    queueSet(u, p, juce::jlimit(0, 127, lockMove ? now : cur + d));
                }
            } else if (id == kCtr8p && p < 8) {
                const int tt = juce::jmin(15, trackParam(t, 8 + 2 * p)), tp = juce::jmin(23, trackParam(t, 9 + 2 * p));
                if (ids[size_t(tt)] == kCtrAll || ids[size_t(tt)] == kCtr8p) continue;
                queueSet(tt, tp, now);
            }
        }
    }
    // the master effects: a CTR-RE / GB / EQ / DX track's knobs show them
    for (int fx = 0; fx < 4; ++fx) {
        const int target = kTracks + fx;
        if (quiet) { rebase(target); continue; }
        for (int k = 0; k < 8; ++k) {
            const int now = juce::jlimit(0, 127, int(std::lround(m_masterFx[size_t(fx)][size_t(k)]->load())));
            if (!moved(target, k, now)) continue;
            m_ctlSeen[size_t(target)][size_t(k)] = int16_t(now);
            for (int t = 0; t < kTracks; ++t)
                if (ctrMasterFx(ids[size_t(t)]) == fx && trackParam(t, k) != now) queueSet(t, k, now, true);
        }
    }
    m_ctlStarted = true;
}

void MdProcessor::handleCc(int channel, int cc, int value)
{
    if (channel < 1 || channel > 4) return;
    auto set = [&](const juce::String& id, float v) {
        if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    if (cc >= 8 && cc <= 11) { const int t = (channel - 1) * 4 + cc - 8; set(levelId(t), float(value)); m_outSeen[size_t(t)][24] = int16_t(value); return; }
    static const int bases[4] = {16, 40, 72, 96};
    for (int i = 0; i < 4; ++i) {
        const int k = cc - bases[i];
        if (k < 0 || k > 23) continue;
        const int t = (channel - 1) * 4 + i;
        m_outSeen[size_t(t)][size_t(k)] = int16_t(value);
        if (k < 8) set(knobId(t, k), float(value));
        else if (k < 16) set(fxId(t, k - 8), float(value));
        else if (k < 21) {
            static juce::String (* const ids[5])(int) = {distId, volId, panId, delId, revId};
            set(ids[k - 16](t), float(value));
        } else {
            set(lfoId(t, 5 + (k - 21)), float(value));   // LFOS LFOD LFOM
        }
        return;
    }
}

} // namespace mnm::plugin::md
