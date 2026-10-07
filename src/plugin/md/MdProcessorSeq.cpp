// The processor's sequencer: the pattern bank, patterns and songs played, chains, live recording
#include "MdProcessorInternal.h"
#include <cmath>

namespace mnm::plugin::md {

bool MdProcessor::bankHasPattern(int slot) const
{
    const juce::SpinLock::ScopedLockType l(m_bankLock);
    return m_bank && slot >= 0 && slot < 128 && m_bank->hasPattern[size_t(slot)];
}

std::shared_ptr<const mnm::mddump::Song> MdProcessor::bankSong(int slot) const
{
    const juce::SpinLock::ScopedLockType l(m_bankLock);
    if (!m_bank || slot < 0 || slot >= 32 || !m_bank->hasSong[size_t(slot)]) return nullptr;
    return std::make_shared<const mnm::mddump::Song>(m_bank->songs[size_t(slot)]);
}

void MdProcessor::setBankSong(int slot, std::shared_ptr<const mnm::mddump::Song> s)
{
    if (slot < 0 || slot >= 32) return;
    std::shared_ptr<const SeqBank> cur;
    { const juce::SpinLock::ScopedLockType l(m_bankLock); cur = m_bank; }
    if (!cur && !s) return;
    auto bank = cur ? std::make_shared<SeqBank>(*cur) : std::make_shared<SeqBank>();
    if (!cur) { bank->patterns.resize(128); bank->kits.resize(64); bank->songs.resize(32); m_bankName = "PLUGIN"; }
    if (s) { bank->songs[size_t(slot)] = *s; bank->songs[size_t(slot)].position = slot; }
    else bank->songs[size_t(slot)] = {};
    bank->hasSong[size_t(slot)] = s && !s->rows.empty();
    m_songEdited.store(true);
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        m_bankOld = std::move(m_bank);
        m_bank = std::move(bank);
    }
    if (!cur) m_bankFresh.store(true);
}

void MdProcessor::editSong(int slot, const std::function<void(mnm::mddump::Song&)>& fn)
{
    if (slot < 0 || slot >= 32) return;
    mnm::mddump::Song s;
    if (const auto cur = bankSong(slot)) s = *cur;
    s.position = slot;
    fn(s);
    setBankSong(slot, std::make_shared<const mnm::mddump::Song>(s));
}

mnm::mddump::Dump MdProcessor::bankDump() const
{
    std::shared_ptr<const SeqBank> bank;
    { const juce::SpinLock::ScopedLockType l(m_bankLock); bank = m_bank; }
    std::vector<uint8_t> bytes;
    if (bank) {
        auto add = [&](const std::vector<uint8_t>& m) { bytes.insert(bytes.end(), m.begin(), m.end()); };
        for (int s = 0; s < 64; ++s) if (bank->hasKit[size_t(s)]) add(mnm::mddump::encodeKit(bank->kits[size_t(s)]));
        for (int s = 0; s < 128; ++s) if (bank->hasPattern[size_t(s)]) add(mnm::mddump::encodePattern(*bank->patterns[size_t(s)]));
        for (int s = 0; s < 32; ++s) if (bank->hasSong[size_t(s)]) add(mnm::mddump::encodeSong(bank->songs[size_t(s)]));
    }
    return mnm::mddump::parseDump(bytes.data(), bytes.size(), "bank");
}

std::shared_ptr<const mnm::mddump::Pattern> MdProcessor::bankPattern(int slot) const
{
    const juce::SpinLock::ScopedLockType l(m_bankLock);
    if (!m_bank || slot < 0 || slot >= 128 || !m_bank->hasPattern[size_t(slot)]) return nullptr;
    return m_bank->patterns[size_t(slot)];
}

void MdProcessor::editPattern(int slot, const std::function<void(mnm::mddump::Pattern&)>& fn)
{
    if (slot < 0 || slot >= 128) return;
    std::shared_ptr<const SeqBank> cur;
    { const juce::SpinLock::ScopedLockType l(m_bankLock); cur = m_bank; }
    auto bank = cur ? std::make_shared<SeqBank>(*cur) : std::make_shared<SeqBank>();
    if (!cur) {
        bank->patterns.resize(128);
        bank->kits.resize(64);
        bank->songs.resize(32);
        m_bankProjectId = {};
        m_bankName = "PLUGIN";
    }
    mnm::mddump::Pattern p;
    if (bank->hasPattern[size_t(slot)]) {
        p = *bank->patterns[size_t(slot)];
    } else {   // a fresh pattern: 16 steps at 1x, the accents / slides / swing of all tracks edited together
        p.position = slot;
        p.length = 16;
        p.kit = uint8_t(juce::jmax(0, m_seqKitSlot.load()));
        p.accentEditAll = p.slideEditAll = p.swingEditAll = 1;
        p.swing = 0xAAAAAAAAAAAAAAAAull;   // steps 2, 4, 6...: the swing trigs every factory pattern has
        p.accentAmount = 64;
        for (auto& row : p.locks) std::fill(std::begin(row), std::end(row), uint8_t(0xFF));
    }
    fn(p);
    p.position = slot;
    p.extended = p.extended || p.length > 32;
    bank->patterns[size_t(slot)] = std::make_shared<const mnm::mddump::Pattern>(p);
    bank->hasPattern[size_t(slot)] = true;
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        m_bankOld = std::move(m_bank);
        m_bank = std::move(bank);
    }
    const int was = m_patternEdited.exchange(slot);
    if (was >= 0 && was != slot) m_patternEdited.store(-2);
    if (!cur) m_bankFresh.store(true);
}

void MdProcessor::setBankPattern(int slot, std::shared_ptr<const mnm::mddump::Pattern> p)
{
    if (slot < 0 || slot >= 128) return;
    std::shared_ptr<const SeqBank> cur;
    { const juce::SpinLock::ScopedLockType l(m_bankLock); cur = m_bank; }
    if (!cur && !p) return;
    auto bank = cur ? std::make_shared<SeqBank>(*cur) : std::make_shared<SeqBank>();
    if (!cur) { bank->patterns.resize(128); bank->kits.resize(64); bank->songs.resize(32); m_bankName = "PLUGIN"; }
    bank->patterns[size_t(slot)] = p;
    bank->hasPattern[size_t(slot)] = p != nullptr;
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        m_bankOld = std::move(m_bank);
        m_bank = std::move(bank);
    }
    const int was = m_patternEdited.exchange(slot);
    if (was >= 0 && was != slot) m_patternEdited.store(-2);
    if (!cur) m_bankFresh.store(true);
}

void MdProcessor::setPatternBank(const juce::String& projectId, const juce::String& name, const mnm::mddump::Dump& dump, int kitSlot)
{
    m_seqKitSlot.store(kitSlot);
    auto bank = std::make_shared<SeqBank>();
    bank->patterns.resize(128);
    bank->kits.resize(64);
    for (const auto& p : dump.patterns)
        if (p.position >= 0 && p.position < 128) { bank->patterns[size_t(p.position)] = std::make_shared<const mnm::mddump::Pattern>(p); bank->hasPattern[size_t(p.position)] = true; }
    for (const auto& k : dump.kits)
        if (k.position >= 0 && k.position < 64) { bank->kits[size_t(k.position)] = k; bank->hasKit[size_t(k.position)] = true; }
    bank->songs.resize(32);
    for (const auto& s : dump.songs)
        if (s.position >= 0 && s.position < 32 && !s.rows.empty()) { bank->songs[size_t(s.position)] = s; bank->hasSong[size_t(s.position)] = true; }
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        m_bankOld = std::move(m_bank);   // the audio thread may still hold it this block
        m_bank = std::move(bank);
    }
    m_bankProjectId = projectId;
    m_bankName = name;
    m_bankGlobals = dump.globals;
    m_bankFresh.store(true);
}

// A trig's locks (MainOS 0x23B342..0x23B7D8, applied at 0x20B0D8): each parameter locked on the step jumps to the lock;
// one its last trig locked and this one does not jumps back to the kit value; every glide of the track ends, and the
// step's slides start. A MIDI / UI trig (s = null) releases the locks as a trig without any. MID and CTR tracks have
// no voice to jump: their values move (midStream / controlMachines read seqParam).
void MdProcessor::trigLocks(int t, const mnm::md::SeqTrig* s, int pos)
{
    const bool ccOut = m_midiOutMode.load() >= 2;
    const auto* ok = m_kitOverride.load();
    const int id = ok ? overrideMachine(*ok, t) : machineIdOf(t);
    const bool voice = !isMidMachine(id) && !isCtrMachine(id);
    auto& locks = m_lockVal[size_t(t)];
    const bool ext = extendedMode();   // CLASSIC: the pattern's locks (and so its slides) are not played
    for (int q = 0; q < 24; ++q) {
        m_glide[size_t(t)][size_t(q)].active = false;
        const int v = s && ext ? s->locks[size_t(q)] : -1;
        if (v >= 0) {
            if (ccOut && locks[size_t(q)] != v) sendCc(t, q, v, pos);
            locks[size_t(q)] = int16_t(v);
            if (voice) m_engine->jumpParam(t, q, uint8_t(v));
        } else if (locks[size_t(q)] >= 0) {
            locks[size_t(q)] = -1;
            if (voice) m_engine->jumpParam(t, q, uint8_t(kitParam(t, q)));
            if (ccOut) sendCc(t, q, kitParam(t, q), pos);
        }
    }
    if (s && ext && s->slideMask)
        for (int q = 0; q < 24; ++q)
            if ((s->slideMask >> q) & 1) {
                const int to = s->slideTo[size_t(q)] >= 0 ? s->slideTo[size_t(q)] : kitParam(t, q);
                m_glide[size_t(t)][size_t(q)].start(s->locks[size_t(q)], to, s->slideClocks[size_t(q)], s->clock);
            }
}

void MdProcessor::seqStop()
{
    m_seqPlayingUi.store(false);
    for (auto& tr : m_lockVal) tr.fill(-1);   // the knobs slew back to the kit
    for (auto& tr : m_glide) for (auto& g : tr) g.active = false;
    m_seqRunning = false;
    m_seqStepUi.store(-1);
}

void MdProcessor::seqStart(Segment& seg, int slot, int start, int end, int64_t steps, uint16_t mutes, double origin,
                           const SeqBank* bank, const std::shared_ptr<const SeqBank>& hold, double atEnginePos)
{
    seg.slot = slot;
    seg.origin = origin;
    seg.steps = steps;
    seg.mutes = mutes;
    // an empty slot (or no bank) plays as an empty pattern, as on the unit: 16 steps at 1x, nothing on them
    static const mnm::mddump::Pattern empty = [] {
        mnm::mddump::Pattern p;
        p.length = 16;
        p.accentEditAll = p.slideEditAll = p.swingEditAll = 1;
        p.swing = 0xAAAAAAAAAAAAAAAAull;
        for (auto& row : p.locks) std::fill(std::begin(row), std::end(row), uint8_t(0xFF));
        return p;
    }();
    seg.valid = slot >= 0 && slot < 128;
    m_seqPatternUi.store(slot);
    if (!seg.valid) { for (auto& u : m_seqTrigsUi) u.store(0); return; }
    const bool real = bank && bank->hasPattern[size_t(slot)];
    const auto& pat = real ? *bank->patterns[size_t(slot)] : empty;
    seg.player.set(pat);
    seg.player.setRange(start, end);
    seg.accentOn = 0x80 + 2 * int(pat.accentAmount);
    m_seqLenUi.store(seg.player.length());
    for (int t = 0; t < kTracks; ++t) m_seqTrigsUi[size_t(t)].store(pat.trigs[t]);
    if (!real || !extendedMode()) return;   // no kit to bring in (CLASSIC: patterns have no kit)
    const int k = pat.kit;
    if (k < 0 || k >= 64 || !bank->hasKit[size_t(k)] || k == m_seqKitSlot.load()) return;
    m_seqKitSlot.store(k);
    m_overrideBank = hold;
    m_kitSwitch = &bank->kits[size_t(k)];
    if (atEnginePos >= 0) m_pending.push_back({-2, atEnginePos, 0});
    else applyKitSwitch();
}

// SONG rows (MdDump SongRow): pattern 0..127 (0xFE LOOP, 0xFF END, 0xFD skipped), -, repeats - 1, the LOOP's target
// row, the muted tracks (16 bits, track 1 = bit 0), the tempo (the host's tempo rules here), start step, end step
// (exclusive). As the OS's song walker (MainOS 0x23D998): a LOOP row jumps back to its target as often as it says
// (0 = forever), a LOOP onto itself is HALT (the song ends), END or the rows running out end it; an empty pattern slot
// plays as an empty pattern.
void MdProcessor::seqNext(double origin, const SeqBank* bank, const std::shared_ptr<const SeqBank>& hold, double atEnginePos, bool song)
{
    if (!song) {   // PATTERN: the queued one, else the chain's next, else the same one on
        int slot = m_seqQueued >= 0 ? m_seqQueued : m_seg.slot;
        if (const int n = juce::jlimit(0, 16, m_chainLen.load()); n > 1 && m_seqQueued < 0 && m_seg.slot >= 0) {
            if (m_chain[size_t(m_chainPos % n)].load() != m_seg.slot)   // find where the playing pattern is in it
                for (int i = 0; i < n; ++i) if (m_chain[size_t(i)].load() == m_seg.slot) { m_chainPos = i; break; }
            m_chainPos = (m_chainPos + 1) % n;
            slot = m_chain[size_t(m_chainPos)].load();
            m_patternSeen = slot; m_ptnPending.store(slot); m_programChange.store(slot); triggerAsyncUpdate();   // the PTN follows
        }
        m_seqQueued = -1;
        seqStart(m_seg, slot, 0, 64, -1, 0, origin, bank, hold, atEnginePos);
        m_seqRowUi.store(-1);
        return;
    }
    const int sl = juce::jlimit(0, 31, int(std::lround(m_songParam->load())));
    const mnm::mddump::Song* s = bank && bank->hasSong[size_t(sl)] ? &bank->songs[size_t(sl)] : nullptr;
    if (const int cue = m_songCue.exchange(-1); cue >= 0) m_cursor.row = cue;   // a cued row plays next
    for (int guard = 0; s && guard < 4096; ++guard) {
        auto& cur = m_cursor;
        if (cur.row < 0 || cur.row >= int(s->rows.size()) || cur.row >= 256) break;
        const auto* r = s->rows[size_t(cur.row)].bytes;
        if (r[0] == 0xFF) break;   // END
        if (r[0] == 0xFE) {        // LOOP
            if (r[3] == cur.row) break;   // HALT
            auto& n = cur.loops[size_t(cur.row)];
            if (r[2] == 0 || n < r[2]) { ++n; cur.row = r[3]; }
            else { n = 0; ++cur.row; }
            continue;
        }
        const int row = cur.row++;
        if (r[0] >= 128) continue;
        seqStart(m_seg, r[0], r[8], r[9], 0, uint16_t((r[4] << 8) | r[5]), origin, bank, hold, atEnginePos);
        m_seg.steps = int64_t(m_seg.player.span()) * (int(r[2]) + 1);
        m_seg.row = row;
        m_seqRowUi.store(row);
        return;
    }
    m_seg.valid = false;   // the song has ended
    m_seg.steps = -1;
    m_seg.row = -1;
    m_seqRowUi.store(-1);
}

int MdProcessor::songPeekPattern(const SeqBank* bank) const
{
    const int sl = juce::jlimit(0, 31, int(std::lround(m_songParam->load())));
    if (!bank || !bank->hasSong[size_t(sl)]) return -1;
    const auto& s = bank->songs[size_t(sl)];
    auto cur = m_cursor;
    for (int guard = 0; guard < 4096; ++guard) {
        if (cur.row < 0 || cur.row >= int(s.rows.size()) || cur.row >= 256) return -1;
        const auto* r = s.rows[size_t(cur.row)].bytes;
        if (r[0] == 0xFF) return -1;
        if (r[0] == 0xFE) {
            if (r[3] == cur.row) return -1;
            auto& n = cur.loops[size_t(cur.row)];
            if (r[2] == 0 || n < r[2]) { ++n; cur.row = r[3]; } else { n = 0; ++cur.row; }
            continue;
        }
        if (r[0] >= 128) { ++cur.row; continue; }
        return r[0];
    }
    return -1;
}

void MdProcessor::applyKitSwitch()
{
    const auto* k = m_kitSwitch;
    if (!k) return;
    m_kitSwitch = nullptr;
    m_kitOverride.store(k);
    for (int t = 0; t < kTracks; ++t) m_engine->setLfoState(t, k->lfos[t]);
    for (auto& tr : m_lockVal) tr.fill(-1);
    for (auto& tr : m_glide) for (auto& g : tr) g.active = false;
    m_snap = true;   // a kit load: every knob at its value
    m_seqKitRequest.store(m_seqKitSlot.load());
    triggerAsyncUpdate();
}

void MdProcessor::seqGenerate(const Segment& seg, double from, double to, double ratio)
{
    if (!seg.valid || to <= from) return;
    const size_t first = m_seqTrigs.size();
    seg.player.trigs(from - seg.origin, to - seg.origin, m_seqTrigs, seg.steps);
    for (size_t i = first; i < m_seqTrigs.size(); ++i) {
        auto& s = m_seqTrigs[i];
        s.clock += seg.origin;
        if (silenced(s.track) || ((seg.mutes >> s.track) & 1)) continue;   // muted: no trig
        if (&seg == &m_seg && m_recSkip[size_t(s.track)] == s.stepIndex) { m_recSkip[size_t(s.track)] = -1; continue; }   // played live already
        const double host = (s.clock - m_seqClock0) / m_seqCps;
        m_pending.push_back({s.track, juce::jmax(0.0, host * ratio), s.accent ? seg.accentOn : -128, int(i)});
    }
}

// Once per host block, before the MIDI: the trigs in this block, timed from the host's position (clock = quarter
// notes x 24). Playback follows the transport; at a jump in the position (a loop, a locate), a start, or a new MODE /
// SONG / bank, it is placed again as if it had played from the start of the timeline: PATTERN = the pattern looping
// from there, SONG = the song's rows walked from there (LOOP counts and all).
void MdProcessor::scheduleSequencer(int n, double ratio)
{
    std::shared_ptr<const SeqBank> bank;
    {
        const juce::SpinLock::ScopedLockType l(m_bankLock);
        bank = m_bank;
    }
    m_seqTrigs.clear();
    const bool song = m_seqMode->load() >= 0.5f;
    const int songSlot = juce::jlimit(0, 31, int(std::lround(m_songParam->load())));
    bool relocate = false;
    if (const int ed = m_patternEdited.exchange(-1); ed != -1 && bank) {   // an edit: the new pattern in place
        for (auto* s : {&m_seg, &m_prevSeg}) {
            if (s->slot < 0 || (ed >= 0 && s->slot != ed) || !bank->hasPattern[size_t(s->slot)]) continue;
            const int start = s->player.rangeStart(), end = start + s->player.span();
            const bool whole = start == 0 && end == s->player.length();
            const auto& pat = *bank->patterns[size_t(s->slot)];
            s->player.set(pat);
            if (!whole) s->player.setRange(start, end);
            s->accentOn = 0x80 + 2 * int(pat.accentAmount);
            s->valid = true;
            if (s == &m_seg) {
                m_seqLenUi.store(s->player.length());
                for (int t = 0; t < kTracks; ++t) m_seqTrigsUi[size_t(t)].store(pat.trigs[t]);
            }
        }
    }
    if (m_bankFresh.exchange(false)) { m_patternSeen = -1; m_seg.slot = -1; relocate = true; }
    if (int(song) != m_modeSeen || songSlot != m_songSeen) { m_modeSeen = int(song); m_songSeen = songSlot; relocate = true; }
    if (m_songEdited.exchange(false) && song && !m_seg.valid) relocate = true;   // a song that had ended: rows added
    if (m_songRelocate.exchange(false) && song) relocate = true;                 // a new start row
    const int want = juce::jlimit(0, 127, int(std::lround(m_patternParam->load())));
    if (m_ptnPending.load() < 0 && want != m_patternSeen) {
        m_patternSeen = want; m_seqQueued = want == m_seg.slot ? -1 : want;
        bool inChain = false;   // a pattern chosen another way ends a chain
        for (int i = 0; i < juce::jlimit(0, 16, m_chainLen.load()); ++i) inChain = inChain || m_chain[size_t(i)].load() == want;
        if (!inChain) m_chainLen.store(0);
    }
    if (!m_hostPlaying) m_seqHalted = false;   // a STOP lasts until the transport stops (or a START / pattern note)
    const bool on = m_seqOn->load() >= 0.5f && !m_seqHalted;   // with no bank, the pattern is empty: the steps still run
    if (!on || !m_hostPlaying) {   // stopped: PATTERN shows the next pattern at once; SONG starts over
        if (m_seqRunning) seqStop();
        m_prevSeg.valid = false;
        if (!song && (m_seqQueued >= 0 || m_seg.slot < 0)) {
            if ((m_pcMode.load() & 2) && m_seqQueued >= 0 && m_seqQueued != m_pcLastSent && pcOutChannel() >= 0) {   // PRG CHANGE OUT: a select while stopped
                midSend(0, uint8_t(0xC0 | pcOutChannel()), uint8_t(m_seqQueued));
                m_pcLastSent = m_seqQueued;
            }
            seqNext(0.0, bank.get(), bank, -1.0, false);
        }
        return;
    }
    const double c0 = m_hostPpq * 24.0;
    const double cps = juce::jmax(1.0, m_seqBpm) * 24.0 / 60.0 / m_hostRate;
    m_seqClock0 = c0;
    m_seqCps = cps;
    const double c1 = c0 + n * cps;
    auto engineAt = [&](double clock) { return juce::jmax(0.0, (clock - c0) / cps * ratio); };
    if (!m_seqRunning || relocate || std::abs(c0 - m_seqExpect) > 0.25) {   // start, a jump, a new mode / song / bank
        if (m_seqRunning) seqStop();
        m_seqRunning = true;
        m_prevSeg.valid = false;
        if (!song) {
            if (m_seqQueued < 0) { const int pend = m_ptnPending.load(); m_seqQueued = pend >= 0 ? pend : want; }
            seqNext(0.0, bank.get(), bank, -1.0, false);
        } else {
            m_cursor = {};
            m_cursor.row = m_songStart.load();   // from the start row (ENTER on a row while stopped)
            seqNext(0.0, bank.get(), bank, -1.0, true);
            for (int guard = 0; guard < 100000 && m_seg.valid && m_seg.steps >= 0 && m_seg.endClock() <= c0; ++guard)
                seqNext(m_seg.endClock(), bank.get(), bank, -1.0, true);
        }
    }
    if (m_ptnJump >= 0 && !song) {   // a GATE / MOMENTARY pattern note: that pattern from its first step, now
        m_prevSeg.valid = false;   // the old pattern stops where it is
        m_seqQueued = m_ptnJump;
        seqNext(c0, bank.get(), bank, 0.0, false);
    }
    m_ptnJump = -1;
    m_seqExpect = c1;
    m_seqPlayingUi.store(true);
    // the segment that ended last: its swung steps' trigs after its end
    if (m_prevSeg.valid) {
        seqGenerate(m_prevSeg, c0, c1, ratio);
        if (c0 > m_prevSeg.endClock() + m_prevSeg.player.swingClocks() + 1.0) m_prevSeg.valid = false;
    }
    for (int guard = 0; guard < 256; ++guard) {
        if (!song) {
            if (m_seqQueued >= 0) {
                if (!m_seg.valid) {   // nothing playing: the new pattern at once, in step with the timeline
                    const double origin = std::floor(c0 / 96.0) * 96.0;
                    seqNext(origin, bank.get(), bank, engineAt(c0), false);
                } else if (m_seg.steps < 0) {   // ends at the end of its pass
                    const double len = m_seg.player.lengthClocks();
                    m_seg.steps = int64_t(std::floor((c0 - m_seg.origin) / len) + 1.0) * m_seg.player.span();
                }
            } else if (m_chainLen.load() > 1) {   // a chain: each pass ends (at least the pass that is starting)
                if (m_seg.valid && m_seg.steps < 0) {
                    const double len = m_seg.player.lengthClocks();
                    m_seg.steps = juce::jmax<int64_t>(1, int64_t(std::floor((c0 - m_seg.origin) / len) + 1.0)) * m_seg.player.span();
                }
            } else if (m_seg.steps >= 0 && m_seg.endClock() > c0) {
                m_seg.steps = -1;   // the queue was taken back: play on
            }
        }
        if (song && !m_seg.valid && m_songCue.load() >= 0) {   // a HALTed / ended song: the cued row starts at the next step
            const double origin = std::ceil(c0 / 6.0) * 6.0;
            if (origin < c1) seqNext(origin, bank.get(), bank, engineAt(origin), true);
        }
        seqGenerate(m_seg, c0, c1, ratio);
        if (m_seg.valid && m_seg.steps >= 0 && m_seg.endClock() < c1) {
            const double end = m_seg.endClock();
            m_prevSeg = m_seg;
            seqNext(end, bank.get(), bank, engineAt(end), song);
            continue;
        }
        break;
    }
    // PRG CHANGE OUT (MainOS 0x23BE6A): at the start of the playing pattern's last step, the next pattern, when it
    // differs from the program last sent
    if ((m_pcMode.load() & 2) && pcOutChannel() >= 0 && m_seg.valid && m_seg.steps >= 0) {
        const double last = m_seg.endClock() - m_seg.player.clocksPerStep();
        if (last >= c0 && last < c1) {
            const int next = song ? songPeekPattern(bank.get()) : (m_seqQueued >= 0 ? m_seqQueued : m_seg.slot);
            if (next >= 0 && next < 128 && next != m_pcLastSent) {
                midSend(juce::jlimit(0, juce::jmax(0, n - 1), int((last - c0) / cps)), uint8_t(0xC0 | pcOutChannel()), uint8_t(next));
                m_pcLastSent = next;
            }
        }
    }
    for (auto& sk : m_recSkip)   // a live-played step's mark ends once its step has gone by
        if (sk >= 0 && (!m_seg.valid || m_seg.origin + double(sk + 1) * m_seg.player.clocksPerStep() < c0)) sk = -1;
    if (m_seg.valid && c1 >= m_seg.origin) {
        int64_t k = int64_t(std::floor((c1 - m_seg.origin) / m_seg.player.clocksPerStep()));
        if (m_seg.steps >= 0 && k >= m_seg.steps) k = m_seg.steps - 1;
        m_seqStepUi.store(m_seg.player.stepOf(k));
    } else {
        m_seqStepUi.store(-1);
    }
}

// ---------------------------------------------------------------------------------------------- live recording

void MdProcessor::recordPush(const RecEvent& e)
{
    const auto scope = m_recFifo.write(1);
    if (scope.blockSize1 > 0) m_recBuf[size_t(scope.startIndex1)] = e;
    else if (scope.blockSize2 > 0) m_recBuf[size_t(scope.startIndex2)] = e;
    else return;
    triggerAsyncUpdate();
}

void MdProcessor::setChain(const std::vector<int>& slots)
{
    const int n = juce::jmin(16, int(slots.size()));
    m_chainLen.store(0);
    for (int i = 0; i < n; ++i) m_chain[size_t(i)].store(int8_t(juce::jlimit(0, 127, slots[size_t(i)])));
    m_chainLen.store(n > 1 ? n : 0);
}

std::vector<int> MdProcessor::chain() const
{
    std::vector<int> v;
    for (int i = 0; i < juce::jlimit(0, 16, m_chainLen.load()); ++i) v.push_back(m_chain[size_t(i)].load());
    return v;
}

void MdProcessor::recordTrig(int t, double clock, int velocity)
{
    juce::ignoreUnused(velocity);
    if (!m_recordingUi.load() || !m_seg.valid || m_seg.slot < 0) return;
    const double T = m_seg.player.clocksPerStep();
    const double at = (clock - m_seg.origin) / T;
    int64_t k = int64_t(std::floor(at));
    const double late = T == 3.0 ? 2.0 / 3.0 : T == 8.0 ? 5.0 / 8.0 : T == 4.0 ? 0.5 : 2.0;   // clocks per step 6 3 8 4 = 1x 2x 3/4x 3/2x
    if (at - double(k) >= late - 1e-9) ++k;
    if (k < 0) k = 0;
    if (m_seg.steps >= 0 && k >= m_seg.steps) k = m_seg.steps - 1;
    const int step = m_seg.player.stepOf(k);
    recordPush({0, int8_t(t), 0, int8_t(step), 0, int16_t(m_seg.slot)});
    if (m_seg.origin + double(k) * T > clock) m_recSkip[size_t(t)] = k;   // its step is still to come in this pass: played already
}

void MdProcessor::recordScanKnobs()
{
    if (m_clock < m_ctlQuietUntil) {   // a kit load: not a turn
        for (int t = 0; t < kTracks; ++t) for (int q = 0; q < 24; ++q) m_recSeen[size_t(t)][size_t(q)] = int16_t(trackParam(t, q));
        return;
    }
    for (int t = 0; t < kTracks; ++t)
        for (int q = 0; q < 24; ++q) {
            const int v = trackParam(t, q);
            if (v == m_recSeen[size_t(t)][size_t(q)]) continue;
            m_recSeen[size_t(t)][size_t(q)] = int16_t(v);
            m_recTouched[size_t(t)][size_t(q)] = m_clock;
        }
}

void MdProcessor::recordApply()
{
    std::vector<RecEvent> evs;
    const auto scope = m_recFifo.read(m_recFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i) evs.push_back(m_recBuf[size_t(scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i) evs.push_back(m_recBuf[size_t(scope.startIndex2 + i)]);
    auto finish = [&] {
        if (m_recSession) { m_recRun.after = bankPattern(m_recRun.slot); m_recDone.push_back(m_recRun); }
        m_recSession = false;
    };
    for (size_t i = 0; i < evs.size();) {
        const int slot = evs[i].slot;
        size_t j = i;
        while (j < evs.size() && evs[j].slot == slot) ++j;
        if (m_recSession && m_recRun.slot != slot) finish();
        if (!m_recSession) { m_recSession = true; m_recRun = {slot, bankPattern(slot), nullptr}; }
        editPattern(slot, [&](mnm::mddump::Pattern& p) {
            for (size_t e = i; e < j; ++e) {
                const auto& ev = evs[e];
                if (ev.kind == 0) {
                    p.trigs[ev.track] |= 1ull << ev.step;
                    if (ev.value) (p.accentEditAll ? p.accent : p.accentPerTrack[ev.track]) |= 1ull << ev.step;
                } else if ((p.trigs[ev.track] >> ev.step) & 1) {
                    p.setLock(ev.track, ev.param, ev.step, ev.value);
                }
            }
        });
        i = j;
    }
    if (m_recEnded.exchange(false)) finish();
}

bool MdProcessor::takeRecordedEdit(RecordedEdit& out)
{
    if (m_recDone.empty()) return false;
    out = m_recDone.front();
    m_recDone.erase(m_recDone.begin());
    return true;
}

} // namespace mnm::plugin::md
