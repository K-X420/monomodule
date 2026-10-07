// The editor's pattern edits: the keys, the step / page / track / pattern operations, undo
#include "MdEditorInternal.h"
#include <cmath>

namespace mnm::plugin::md {

void MdEditor::holdStep(int step)
{
    if (step == m_heldStep) return;
    m_heldStep = step;
    const juce::String badge = step >= 0 ? "LOCK" + juce::String(step + 1) : juce::String();
    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->setBadge(step >= 0 ? badge.toRawUTF8() : nullptr);
    m_shownMachineId = -2;
    m_pagesMachineId = -2;
    rebuildSynPage();
    refreshGrid();
}

void MdEditor::stepMenu(int s)
{
    const int t = m_track, slot = editSlot();
    const auto p = m_proc.bankPattern(slot);
    auto maskBit = [&](uint32_t editAll, uint64_t global, uint64_t perTrack) { return (((editAll ? global : perTrack) >> s) & 1) != 0; };
    const bool trig = p && ((p->trigs[t] >> s) & 1);
    bool locks = false;
    if (p) for (int q = 0; q < 24; ++q) { const int row = p->lockRow(t, q); if (row >= 0 && p->locks[row][s] <= 127) locks = true; }
    juce::PopupMenu m;
    m.addSectionHeader("STEP " + juce::String(s + 1) + " - TRACK " + juce::String(t + 1));
    m.addItem(1, "Trig", true, trig);
    m.addItem(2, p && p->accentEditAll ? "Accent (all tracks)" : "Accent", true, p && maskBit(p->accentEditAll, p->accent, p->accentPerTrack[t]));
    m.addItem(3, p && p->slideEditAll ? "Slide (all tracks)" : "Slide", true, p && maskBit(p->slideEditAll, p->slide, p->slidePerTrack[t]));
    m.addItem(4, p && p->swingEditAll ? "Swing (all tracks)" : "Swing", true, p && maskBit(p->swingEditAll, p->swing, p->swingPerTrack[t]));
    m.addSeparator();
    m.addItem(5, m_heldStep == s ? "Release locks hold" : "Hold for locks (turn the knobs)");
    m.addItem(6, "Clear this step's locks", locks);
    m.addSeparator();
    m.addItem(7, "Accent / slide / swing per track", true, p && !p->accentEditAll);
    juce::PopupMenu tracks;
    for (int i = 0; i < kTracks; ++i) tracks.addItem(100 + i, "T" + juce::String(i + 1), true, i == t);
    m.addSubMenu("Select track", tracks);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_keys), [this, s, t, slot](int r) {
        if (r >= 100) { selectTrack(r - 100); return; }
        if (r == 5) { holdStep(m_heldStep == s ? -1 : s); return; }
        if (r <= 0) return;
        static const char* const names[8] = {"", "trig", "accent", "slide", "swing", "", "clear locks", "per-track marks"};
        doEdit(slot, names[juce::jlimit(0, 7, r)], [&](mnm::mddump::Pattern& p) {
            auto flip = [&](uint32_t editAll, uint64_t& global, uint64_t& perTrack) { (editAll ? global : perTrack) ^= 1ull << s; };
            switch (r) {
                case 1: if ((p.trigs[t] >> s) & 1) { p.trigs[t] &= ~(1ull << s); p.clearStepLocks(t, s); p.clearStepExtras(t, s); } else p.trigs[t] |= 1ull << s; break;
                case 2: flip(p.accentEditAll, p.accent, p.accentPerTrack[t]); break;
                case 3: flip(p.slideEditAll, p.slide, p.slidePerTrack[t]); break;
                case 4: flip(p.swingEditAll, p.swing, p.swingPerTrack[t]); break;
                case 6: p.clearStepLocks(t, s); break;
                case 7: {   // the masks of all tracks <-> each track's own (the current marks carry over)
                    const bool perTrack = p.accentEditAll != 0;
                    if (perTrack) for (int u = 0; u < 16; ++u) { p.accentPerTrack[u] = p.accent; p.slidePerTrack[u] = p.slide; p.swingPerTrack[u] = p.swing; }
                    else { p.accent = p.accentPerTrack[t]; p.slide = p.slidePerTrack[t]; p.swing = p.swingPerTrack[t]; }
                    p.accentEditAll = p.slideEditAll = p.swingEditAll = perTrack ? 0 : 1;
                    break;
                }
                default: break;
            }
        });
        if (r == 1 && s == m_heldStep) holdStep(-1);
        refreshGrid();
    });
}

void MdEditor::doEdit(int slot, const juce::String& label, const std::function<void(mnm::mddump::Pattern&)>& fn, int coalesce)
{
    seqOn();
    const auto now = juce::Time::currentTimeMillis();
    const bool merge = coalesce >= 0 && !m_undo.empty() && m_undo.back().slot == slot && m_lastCoalesce == coalesce && now - m_lastEditMs < 1000;
    if (!merge) {
        m_undo.push_back({slot, m_proc.bankPattern(slot), nullptr, label});
        if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_lastCoalesce = coalesce;
    m_lastEditMs = now;
    m_proc.editPattern(slot, fn);
    m_undo.back().after = m_proc.bankPattern(slot);
}

void MdEditor::doEditSong(int slot, const juce::String& label, const std::function<void(mnm::mddump::Song&)>& fn, int coalesce)
{
    const auto now = juce::Time::currentTimeMillis();
    const bool merge = coalesce >= 0 && !m_undo.empty() && m_undo.back().song == slot && m_lastCoalesce == coalesce && now - m_lastEditMs < 1000;
    if (!merge) {
        UndoStep u;
        u.song = slot; u.songBefore = m_proc.bankSong(slot); u.label = label;
        m_undo.push_back(u);
        if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_lastCoalesce = coalesce;
    m_lastEditMs = now;
    m_proc.editSong(slot, fn);
    m_undo.back().songAfter = m_proc.bankSong(slot);
    m_songEd.repaint();
}

void MdEditor::undo()
{
    if (m_undo.empty()) return;
    auto s = m_undo.back();
    m_undo.pop_back();
    if (s.song >= 0) { m_proc.setBankSong(s.song, s.songBefore); m_redo.push_back(s); m_lastCoalesce = -1; m_songEd.repaint(); return; }
    m_proc.setBankPattern(s.slot, s.before);
    m_redo.push_back(s);
    m_lastCoalesce = -1;
    refreshGrid();
    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_out}) { pg->pull(); pg->repaint(); }
}

void MdEditor::redo()
{
    if (m_redo.empty()) return;
    auto s = m_redo.back();
    m_redo.pop_back();
    if (s.song >= 0) { m_proc.setBankSong(s.song, s.songAfter); m_undo.push_back(s); m_lastCoalesce = -1; m_songEd.repaint(); return; }
    m_proc.setBankPattern(s.slot, s.after);
    m_undo.push_back(s);
    m_lastCoalesce = -1;
    refreshGrid();
    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo, &m_out}) { pg->pull(); pg->repaint(); }
}

bool MdEditor::keyPressed(const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if ((k.getKeyCode() == juce::KeyPress::upKey || k.getKeyCode() == juce::KeyPress::downKey) && !mods.isAnyModifierKeyDown()) {
        // the previous / next kit or sound, as the strip's arrows step (the selector used last)
        if (m_drop.isVisible()) m_drop.close();
        const int dir = k.getKeyCode() == juce::KeyPress::upKey ? -1 : 1;
        if (m_browseKits) stepKit(dir); else stepSound(dir);
        return true;
    }
    if (!mods.isAnyModifierKeyDown() && (k.getKeyCode() == 'G' || k.getKeyCode() == 'g')) { toggleGrid(); return true; }   // GRID on / off
    if (!mods.isAnyModifierKeyDown() && (k.getKeyCode() == 'M' || k.getKeyCode() == 'm')) { toggleMixer(); return true; }   // the mixer
    const int page = juce::jlimit(0, 3, m_gridPage);
    if (m_gridOn && !mods.isAnyModifierKeyDown()) {   // A / S / W: accent / slide / swing marks for all tracks <-> per track
        const int code = k.getKeyCode();
        const int f = code == 'A' || code == 'a' ? 0 : code == 'S' || code == 's' ? 1 : code == 'W' || code == 'w' ? 2 : -1;
        if (f >= 0) {   // the ACCENT / SLIDE / SWING edit window (again: back to the trigs)
            static const char* const names[3] = {"ACCENT EDIT", "SLIDE EDIT", "SWING EDIT"};
            m_markMode = m_markMode == f + 1 ? 0 : f + 1;
            flash(m_markMode ? juce::String(names[f]) : juce::String("TRIGS"));
            refreshGrid();
            return true;
        }
        if (code == juce::KeyPress::escapeKey && m_markMode) { m_markMode = 0; refreshGrid(); return true; }
        const int x = !m_proc.extrasOn() ? -1 : code == 'C' || code == 'c' ? 0 : code == 'T' || code == 't' ? 1 : code == 'R' || code == 'r' ? 2 : -1;
        if (x >= 0) {   // EXTRAS: the CONDITION / MICRO-TIMING / RETRIG edit window (again: back to the trigs)
            static const char* const names[3] = {"COND", "MICRO", "RETRIG"};
            m_markMode = m_markMode == x + 4 ? 0 : x + 4;
            flash(m_markMode ? juce::String(names[x]) : juce::String("TRIGS"));
            refreshGrid();
            return true;
        }
    }
    if (m_proc.extrasOn() && (k.getKeyCode() == 'F' || k.getKeyCode() == 'f') && !mods.isCtrlDown() && !mods.isCommandDown() && !mods.isAltDown()) {
        if (mods.isShiftDown()) { m_fillLatch = !m_fillLatch; flash(m_fillLatch ? "FILL ON" : "FILL OFF"); }   // latched
        m_proc.setFill(m_fillLatch || !mods.isShiftDown());   // held: the timer lets it go with the key
        refreshGrid();
        return true;
    }
    if (m_gridOn && mods.isShiftDown() && !mods.isCtrlDown() && !mods.isCommandDown() && !mods.isAltDown()) {   // all tracks <-> this track
        const int code = k.getKeyCode();
        const int f = code == 'A' || code == 'a' ? 0 : code == 'S' || code == 's' ? 1 : code == 'W' || code == 'w' ? 2 : -1;
        if (f >= 0) { togglePerTrack(f); return true; }
    }
    if (m_gridOn && (k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey)) {
        const int dir = k.getKeyCode() == juce::KeyPress::leftKey ? -1 : 1;
        if (mods.isShiftDown()) { shiftTrack(dir); return true; }   // the MD's FUNCTION + LEFT / RIGHT
        const auto p = m_proc.bankPattern(editSlot());
        const int pages = p ? (juce::jlimit(1, 64, int(p->length)) + 15) / 16 : 1;
        m_gridPage = juce::jlimit(0, pages - 1, m_gridPage + dir);
        m_pagePinned = m_proc.seqPlaying();
        refreshGrid();
        return true;
    }
    if (m_heldStep >= 0 && (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey)) {   // the held step's locks
        const int t = m_track, s = m_heldStep;
        doEdit(editSlot(), "clear note locks", [&](mnm::mddump::Pattern& x) { x.clearStepLocks(t, s); });
        flash("LOCKS CLEARED");
        for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->pull();
        refreshGrid();
        return true;
    }
    if ((mods.isCommandDown() || mods.isCtrlDown()) && mods.isShiftDown()) {   // the selected track's machine
        if (k.getKeyCode() == 'C' || k.getKeyCode() == 'c') { copyMachine(); return true; }
        if (k.getKeyCode() == 'V' || k.getKeyCode() == 'v') { pasteMachine(); return true; }
        if (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) { clearMachine(); return true; }
    }
    if ((mods.isCommandDown() || mods.isCtrlDown()) && mods.isAltDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z')) { undoKit(); return true; }
    if ((mods.isCommandDown() || mods.isCtrlDown()) && !mods.isShiftDown() && (k.getKeyCode() == 'R' || k.getKeyCode() == 'r')) { reloadKit(); return true; }
    if (m_heldStep >= 0 && (mods.isCommandDown() || mods.isCtrlDown()) && (k.getKeyCode() == 'C' || k.getKeyCode() == 'c')) { copyNote(m_heldStep); return true; }
    if (m_heldStep >= 0 && (mods.isCommandDown() || mods.isCtrlDown()) && (k.getKeyCode() == 'V' || k.getKeyCode() == 'v')) { pasteNote(m_heldStep); return true; }
    if ((k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey) && m_gridOn) { pageOp(2, page); return true; }
    if (!mods.isCommandDown() && !mods.isCtrlDown()) return false;
    const int code = k.getKeyCode();
    if (code == 'C' || code == 'c') { pageOp(0, page); return true; }
    if (code == 'V' || code == 'v') { pageOp(1, page); return true; }
    if (code == 'D' || code == 'd') { doublePattern(); return true; }
    if ((code == 'Z' || code == 'z') && mods.isShiftDown()) { redo(); return true; }
    if (code == 'Z' || code == 'z') { undo(); return true; }
    if (code == 'Y' || code == 'y') { redo(); return true; }
    return false;
}

// DEL PAGE: the page goes, the pages after it move up and the pattern is 16 steps shorter (one page: its steps cleared)
void MdEditor::deletePage(int page)
{
    const int slot = editSlot();
    const auto cur = m_proc.bankPattern(slot);
    if (!cur) { flash("EMPTY"); return; }
    const int pages = (juce::jlimit(1, 64, int(cur->length)) + 15) / 16;
    if (page >= pages) return;
    doEdit(slot, "delete page", [&](mnm::mddump::Pattern& x) {
        if (pages == 1) { x.clearSteps(0, 16, -1); x.swing = cur->swing; return; }
        const auto src = x;
        for (int k = page; k < pages - 1; ++k) x.copySteps(src, (k + 1) * 16, k * 16, 16, -1, -1);
        x.clearSteps((pages - 1) * 16, 16, -1);
        x.length = uint8_t(juce::jmax(16, int(x.length) - 16));
        x.scale = uint8_t((x.length - 1) / 16);
    });
    m_gridPage = juce::jmin(page, pages - 2 < 0 ? 0 : pages - 2);
    flash(pages == 1 ? "CLEARED P1" : "DEL P" + juce::String(page + 1));
    refreshGrid();
}

void MdEditor::shiftTrack(int dir)
{
    const int slot = editSlot(), t = m_track;
    const auto cur = m_proc.bankPattern(slot);
    if (!cur) { flash("EMPTY"); return; }
    const int len = juce::jlimit(1, 64, int(cur->length));
    doEdit(slot, dir > 0 ? "shift track right" : "shift track left", [&](mnm::mddump::Pattern& x) {
        const auto src = x;
        for (int s = 0; s < len; ++s) x.copySteps(src, s, (s + dir + len) % len, 1, t, t);
    }, 30000 + t);
    flash(dir > 0 ? "T" + juce::String(t + 1) + " >>" : "<< T" + juce::String(t + 1));
    refreshGrid();
}

void MdEditor::copyNote(int step)
{
    const auto p = m_proc.bankPattern(editSlot());
    if (!p || !((p->trigs[m_track] >> step) & 1)) { flash("NO NOTE"); return; }
    m_clip = {4, false, m_track, step, *p};
    flash("COPY NOTE " + juce::String(step + 1));
}

void MdEditor::pasteNote(int step)
{
    if (m_clip.kind != 4) { flash("NO NOTE"); return; }
    const auto clip = m_clip;
    const int t = m_track;
    doEdit(editSlot(), "paste note", [&](mnm::mddump::Pattern& x) { x.copySteps(clip.pat, clip.page, step, 1, clip.track, t); });
    flash("PASTE NOTE " + juce::String(step + 1));
    for (auto* pg : {&m_syn, &m_fx, &m_routing, &m_lfo}) pg->pull();
    refreshGrid();
}

void MdEditor::applyMuteQueue()
{
    for (int t = 0; t < kTracks; ++t) if ((m_muteQueue >> t) & 1) toggleMute(t);
    m_muteQueue = 0;
    m_keys.setMuteQueue(0);
}

void MdEditor::undoKit()
{
    if (!m_kitUndo.valid) { flash("NO KIT UNDO"); return; }
    const KitUndo back = m_kitUndo;
    m_kitUndo = {true, m_proc.captureMdKit(), m_proc.loadedKitKey(), m_proc.kitName()};   // undo again = redo
    m_proc.loadMdKit(back.key, back.kit, back.name);
    flash("KIT UNDONE");
    selectTrack(m_track);
    timerCallback();
}

void MdEditor::reloadKit()
{
    const auto key = m_proc.loadedKitKey();
    mnm::mddump::Kit kit;
    if (key.isEmpty() || !m_lib->loadKit(key, kit)) { flash("NO SAVED KIT"); return; }
    m_kitUndo = {true, m_proc.captureMdKit(), key, m_proc.kitName()};
    m_proc.loadMdKit(key, kit, m_proc.kitName());
    flash("KIT RELOADED");
    selectTrack(m_track);
    timerCallback();
}

void MdEditor::copyMachine()
{
    m_soundClip = m_proc.captureSound(m_track);
    m_soundClipName = m_proc.loadedSoundName(m_track).isNotEmpty() ? m_proc.loadedSoundName(m_track) : "T" + juce::String(m_track + 1) + " COPY";
    flash("COPY T" + juce::String(m_track + 1));
}

void MdEditor::pasteMachine()
{
    if (!m_soundClip) { flash("NOTHING COPIED"); return; }
    if (!m_proc.loadSound(m_track, {}, *m_soundClip, m_soundClipName)) { flash("NO SUCH MACHINE"); return; }
    flash("PASTE T" + juce::String(m_track + 1));
    bindTrackPages();
    timerCallback();
}

void MdEditor::clearMachine()
{
    if (auto* p = m_proc.apvts.getParameter(machineId(m_track))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(float(machineIndexOf(0))));   // GND---: the empty machine
        p->endChangeGesture();
    }
    flash("CLEAR T" + juce::String(m_track + 1));
    timerCallback();
}

void MdEditor::flipStepFlag(int step, int f)
{
    static const char* const names[3] = {"accent", "slide", "swing"};
    const int t = m_track;
    doEdit(editSlot(), names[f], [&](mnm::mddump::Pattern& p) {
        uint32_t all[3] = {p.accentEditAll, p.slideEditAll, p.swingEditAll};
        uint64_t* global[3] = {&p.accent, &p.slide, &p.swing};
        uint64_t* own[3] = {&p.accentPerTrack[t], &p.slidePerTrack[t], &p.swingPerTrack[t]};
        *(all[f] ? global[f] : own[f]) ^= 1ull << step;
    });
    refreshGrid();
}

void MdEditor::togglePerTrack(int f)
{
    static const char* const names[3] = {"ACCENT", "SLIDE", "SWING"};
    bool nowAll = true;
    const int t = m_track;
    doEdit(editSlot(), "per-track marks", [&](mnm::mddump::Pattern& p) {
        uint32_t* all[3] = {&p.accentEditAll, &p.slideEditAll, &p.swingEditAll};
        uint64_t* global[3] = {&p.accent, &p.slide, &p.swing};
        uint64_t* own[3] = {p.accentPerTrack, p.slidePerTrack, p.swingPerTrack};
        if (*all[f]) { for (int u = 0; u < 16; ++u) own[f][u] = *global[f]; *all[f] = 0; nowAll = false; }   // the marks carry over
        else { *global[f] = own[f][t]; *all[f] = 1; nowAll = true; }
    });
    flash(juce::String(names[f]) + (nowAll ? ": ALL" : ": T" + juce::String(t + 1)));
    refreshGrid();
}

void MdEditor::toggleMute(int t)
{
    if (auto* p = m_proc.apvts.getParameter(muteId(t))) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->getValue() >= 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
    refreshGrid();
}

void MdEditor::pageOp(int op, int page)
{
    const int slot = editSlot();
    const auto p = m_proc.bankPattern(slot);
    const juce::String pg = "P" + juce::String(page + 1);
    if (op == 0) {
        if (!p) { flash("EMPTY"); return; }
        m_clip = {1, true, m_track, page, *p};
        flash("COPY " + pg);
        return;
    }
    if (op == 1 && m_clip.kind != 1) { flash("NO PAGE"); return; }
    const auto clip = m_clip;
    doEdit(slot, op == 1 ? "paste page" : "clear page", [&](mnm::mddump::Pattern& x) {
        if (op == 1) {
            x.copySteps(clip.pat, clip.page * 16, page * 16, 16, clip.all ? -1 : clip.track, m_track);
            if (x.length < (page + 1) * 16) { x.length = uint8_t((page + 1) * 16); x.scale = uint8_t(page); }
        } else {
            x.clearSteps(page * 16, 16, -1);
        }
    });
    if (op == 1) m_gridPage = page;
    holdStep(-1);
    flash(op == 1 ? "PASTE " + pg : "CLEAR " + pg);
}

void MdEditor::trackOp(int op)
{
    const int slot = editSlot(), t = m_track;
    const auto p = m_proc.bankPattern(slot);
    const juce::String tr = "T" + juce::String(t + 1);
    if (op == 0) {
        if (!p) { flash("EMPTY"); return; }
        m_clip = {2, false, t, 0, *p};
        flash("COPY " + tr);
        return;
    }
    if (op == 1 && m_clip.kind != 2) { flash("NO TRACK"); return; }
    const auto clip = m_clip;
    doEdit(slot, op == 1 ? "paste track" : "clear track", [&](mnm::mddump::Pattern& x) {
        if (op == 1) x.copySteps(clip.pat, 0, 0, 64, clip.track, t);
        else x.clearSteps(0, 64, t);
    });
    holdStep(-1);
    flash(op == 1 ? "PASTE " + tr : "CLEAR " + tr);
}

void MdEditor::patternOp(int op)
{
    const int slot = editSlot();
    const auto p = m_proc.bankPattern(slot);
    const juce::String pn = kPatternNames[slot];
    if (op == 0) {
        if (!p) { flash("EMPTY"); return; }
        m_clip = {3, true, 0, 0, *p};
        flash("COPY " + pn);
        return;
    }
    if (op == 1 && m_clip.kind != 3) { flash("NO PTN"); return; }
    const auto clip = m_clip;
    doEdit(slot, op == 1 ? "paste pattern" : "clear pattern", [&](mnm::mddump::Pattern& x) {
        if (op == 1) { const int pos = x.position; x = clip.pat; x.position = pos; }
        else x.clearSteps(0, 64, -1);
    });
    holdStep(-1);
    flash(op == 1 ? "PASTE " + pn : "CLEAR " + pn);
}

void MdEditor::doublePattern()
{
    const int slot = editSlot();
    const auto p = m_proc.bankPattern(slot);
    const int len = p ? juce::jlimit(1, 64, int(p->length)) : 16;
    if (!p) { flash("EMPTY"); return; }
    if (len > 32) { flash("64 = FULL"); return; }
    doEdit(slot, "double", [&](mnm::mddump::Pattern& x) {
        const auto copy = x;
        x.copySteps(copy, 0, len, len, -1, 0);
        x.length = uint8_t(juce::jmin(64, 2 * len));
        x.scale = uint8_t((x.length - 1) / 16);
    });
    flash("X2 = " + juce::String(2 * len));
}

void MdEditor::editMenu()
{
    const int slot = editSlot(), t = m_track;
    const auto p = m_proc.bankPattern(slot);
    const int len = p ? juce::jlimit(1, 64, int(p->length)) : 16;
    const int page = juce::jlimit(0, 3, m_gridPage);
    const juce::String pg = "page " + juce::String(page + 1), tr = "T" + juce::String(t + 1), pn = kPatternNames[slot];
    juce::String what;
    if (m_clip.kind == 1) what = " (page " + juce::String(m_clip.page + 1) + (m_clip.all ? ", all tracks)" : ", T" + juce::String(m_clip.track + 1) + ")");
    juce::PopupMenu m;
    m.addItem(20, m_undo.empty() ? juce::String("Undo") : "Undo " + m_undo.back().label + "  (Ctrl+Z)", !m_undo.empty());
    m.addItem(21, m_redo.empty() ? juce::String("Redo") : "Redo " + m_redo.back().label + "  (Ctrl+Shift+Z)", !m_redo.empty());
    m.addSeparator();
    m.addSectionHeader(pn + "  -  " + pg.toUpperCase() + "  -  " + tr);
    m.addItem(1, "Copy " + pg + " (all tracks)", p != nullptr);
    m.addItem(2, "Copy " + pg + " of " + tr, p != nullptr);
    m.addItem(3, "Paste onto " + pg + what, m_clip.kind == 1);
    m.addItem(4, "Clear " + pg + " (all tracks)", p != nullptr);
    m.addItem(5, "Clear " + pg + " of " + tr, p != nullptr);
    m.addSeparator();
    m.addItem(6, "Copy track " + tr, p != nullptr);
    m.addItem(7, "Paste track onto " + tr + (m_clip.kind == 2 ? " (T" + juce::String(m_clip.track + 1) + ")" : juce::String()), m_clip.kind == 2);
    m.addItem(8, "Clear track " + tr, p != nullptr);
    m.addSeparator();
    m.addItem(9, "Copy pattern " + pn, p != nullptr);
    m.addItem(10, "Paste pattern onto " + pn, m_clip.kind == 3);
    m.addItem(11, "Clear pattern " + pn + " (its steps; the settings stay)", p != nullptr);
    m.addSeparator();
    {
        const int sl = juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load())));
        m.addItem(31, "Edit song " + juce::String(sl + 1).paddedLeft('0', 2) + " (the SONG knob's)...");
    }
    m.addItem(30, m_proc.bankProjectId().isNotEmpty() ? "Save patterns and songs to the library (" + m_proc.bankName() + ": a new version)"
                                                      : juce::String("Save patterns and songs to the library (a new project)"),
              m_proc.bankHasPattern(slot) || m_proc.bankName().isNotEmpty());
    m.addSeparator();
    m.addItem(12, "Double: steps 1-" + juce::String(len) + " again after themselves (length " + juce::String(juce::jmin(64, 2 * len)) + ")", p != nullptr && len <= 32);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_seqBar), [this, slot, t, page, p, len](int r) {
        if (r <= 0) return;
        if (r == 20) { undo(); return; }
        if (r == 30) { saveBankToLibrary(); return; }
        if (r == 31) {
            m_songEd.setBounds(m_syn.getX(), m_syn.getY(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY());
            m_songEd.open(juce::jlimit(0, 31, int(std::lround(m_proc.apvts.getRawParameterValue(songId())->load()))));
            m_songEd.grabKeyboardFocus();
            return;
        }
        if (r == 21) { redo(); return; }
        if (r == 1 || r == 2) { m_clip = {1, r == 1, t, page, *p}; return; }
        if (r == 6) { m_clip = {2, false, t, 0, *p}; return; }
        if (r == 9) { m_clip = {3, true, 0, 0, *p}; return; }
        const auto clip = m_clip;
        static const char* const names[13] = {"", "", "", "paste page", "clear page", "clear page", "", "paste track", "clear track", "", "paste pattern", "clear pattern", "double"};
        doEdit(slot, names[juce::jlimit(0, 12, r)], [&](mnm::mddump::Pattern& x) {
            switch (r) {
                case 3:   // the copied page onto this one (the pattern grows to reach it)
                    x.copySteps(clip.pat, clip.page * 16, page * 16, 16, clip.all ? -1 : clip.track, t);
                    if (x.length < (page + 1) * 16) { x.length = uint8_t((page + 1) * 16); x.scale = uint8_t(page); }
                    break;
                case 4: x.clearSteps(page * 16, 16, -1); break;
                case 5: x.clearSteps(page * 16, 16, t); break;
                case 7: x.copySteps(clip.pat, 0, 0, 64, clip.track, t); break;
                case 8: x.clearSteps(0, 64, t); break;
                case 10: { const int pos = x.position; x = clip.pat; x.position = pos; break; }
                case 11: x.clearSteps(0, 64, -1); break;
                case 12: {
                    const auto copy = x;
                    x.copySteps(copy, 0, len, len, -1, 0);
                    x.length = uint8_t(juce::jmin(64, 2 * len));
                    x.scale = uint8_t((x.length - 1) / 16);
                    break;
                }
                default: break;
            }
        });
        if (r == 3) m_gridPage = page;
        holdStep(-1);
        refreshGrid();
    });
}

} // namespace mnm::plugin::md
