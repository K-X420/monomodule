// The editor's library side: kits and sounds, files dropped in and dragged out, the OS file
#include "MdEditorInternal.h"
#include "MdMidiExport.h"
#include <cmath>
#include "Transfer.h"

namespace mnm::plugin::md {

void MdEditor::saveBankToLibrary()
{
    const auto bank = m_proc.bankDump();
    const bool fresh = m_proc.bankProjectId().isEmpty();
    const juce::String name = fresh ? "MD PATTERNS " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M") : m_proc.bankName();
    juce::String id;
    juce::StringArray changes;
    const auto r = m_lib->model().saveMdPatterns(m_proc.bankProjectId(), bank, name, "Monomodule MD", &id, &changes);
    if (r.failed()) { juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save Patterns", r.getErrorMessage()); return; }
    if (fresh) {
        m_proc.setBankProject(id, name);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Save Patterns", "Saved as the new project " + name + " in the library.");
    } else if (changes.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Save Patterns", "Nothing differs from " + name + " in the library: no new version.");
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Save Patterns",
                                               "Saved as a new version of " + name + ":\n" + changes.joinIntoString("\n"));
    }
}

// PTN / TRK dragged out of the bar: the pattern (or the track) as a MIDI file, dropped where it lands (an Ableton track:
// a clip). Notes on the tracks' trig notes (so the clip plays the plugin back), accents as velocity, locks as CCs.
void MdEditor::dragOutClip(bool trackOnly)
{
    const int slot = editSlot();
    const auto pat = m_proc.bankPattern(slot);
    if (!pat) { flash("EMPTY"); return; }
    const auto kit = m_proc.captureMdKit();
    const auto mf = trackOnly ? mnm::library::buildMdTrackMidiFile(&kit, *pat, m_track) : mnm::library::buildMdPatternMidiFile(&kit, *pat);
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Monomodule MD clips");
    dir.createDirectory();
    juce::String name = kPatternNames[juce::jlimit(0, 127, slot)];
    if (trackOnly) {
        const int mi = juce::jlimit(0, kNumMachines - 1, m_machineIndex);
        name << " T" << (m_track + 1) << " " << (shortOf(mi) == "---" ? familyOf(mi) : shortOf(mi));
    }
    const auto file = dir.getChildFile(juce::File::createLegalFileName(name) + ".mid");
    file.deleteFile();
    if (auto out = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())) { mf.writeTo(*out); out->flush(); }
    else { flash("NO CLIP"); return; }
    juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this);
}

bool MdEditor::isMidiFile(const juce::String& path) { return path.endsWithIgnoreCase(".mid") || path.endsWithIgnoreCase(".midi"); }

// A MIDI clip dropped in: the notes onto the tracks by the note map (or all onto the key's track), the export's way back
juce::String MdEditor::importMidiClip(const juce::File& f, int onlyTrack)
{
    juce::MidiFile mf;
    {
        juce::FileInputStream in(f);
        if (!in.openedOk() || !mf.readFrom(in)) return "not a MIDI file";
    }
    const auto ms = m_proc.midiSettings();
    std::array<int, 128> map{};
    for (int n = 0; n < 128; ++n) map[size_t(n)] = ms.noteTrack[size_t(n)];
    const auto kit = m_proc.captureMdKit();
    const int slot = editSlot();
    mnm::mddump::Pattern probe;   // first on a copy: nothing usable, no undo step
    if (const auto cur = m_proc.bankPattern(slot)) probe = *cur; else { probe.length = 16; probe.accentEditAll = probe.slideEditAll = probe.swingEditAll = 1; }
    juce::String why;
    const int placed = mnm::library::mdPatternFromMidi(mf, map, onlyTrack, &kit, probe, &why);
    if (placed == 0) return why;
    doEdit(slot, "MIDI clip", [&](mnm::mddump::Pattern& p) { mnm::library::mdPatternFromMidi(mf, map, onlyTrack, &kit, p, nullptr); });
    flash("CLIP: " + juce::String(placed) + " TRIGS" + (onlyTrack >= 0 ? " ON T" + juce::String(onlyTrack + 1) : juce::String()));
    if (onlyTrack >= 0 && onlyTrack != m_track) selectTrack(onlyTrack);
    refreshGrid();
    return {};
}

void MdEditor::chooseOsFile()
{
    const juce::File current(m_proc.firmwarePath());
    m_chooser = std::make_unique<juce::FileChooser>("Select the Machinedrum OS .syx (Elektron_SPS1-1UW_OS1.63.syx)",
        current.existsAsFile() ? current.getParentDirectory() : juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile()) { m_proc.setFirmwarePath(f.getFullPathName()); m_shownMachineId = -2; timerCallback(); }
    });
}

// Machinedrum sysex files (kits or whole dumps) into the kit library, then the list to pick from
void MdEditor::importSyx()
{
    m_chooser = std::make_unique<juce::FileChooser>("Import Machinedrum kits (.syx kit or dump)",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.syx");
    m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& fc) { importFiles(fc.getResults()); });
}

void MdEditor::importFiles(const juce::Array<juce::File>& files)
{
    juce::StringArray problems;
    juce::String lastId;
    int imported = 0;
    for (const auto& f : files) {
        if (!f.existsAsFile()) continue;
        juce::String id;
        const auto r = m_lib->importSyx(f, &id);
        if (r.failed()) problems.add(r.getErrorMessage());
        else { lastId = id; ++imported; }
    }
    if (!problems.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import", problems.joinIntoString("\n"));
    if (imported == 0) return;
    // a file of a single kit is what the user wants to hear: load it; a dump opens the list
    std::vector<KitEntry> fromLast;
    for (const auto& k : m_lib->kits()) if (k.sourceId == lastId) fromLast.push_back(k);
    if (imported == 1 && fromLast.size() == 1) loadKit(fromLast.front());
    else openKitList();
}

bool MdEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files) if (f.endsWithIgnoreCase(".syx") || mnm::library::isMdTransferFile(f) || MdProcessor::isAudioFile(f) || isMidiFile(f)) return true;
    return false;
}

juce::Rectangle<int> MdEditor::dropFrame(const juce::StringArray& files, int x, int y) const
{
    const auto pages = juce::Rectangle<int>(m_syn.getX(), m_syn.getY() + m_syn.overhangPx(), m_routing.getRight() - m_syn.getX(), m_out.getBottom() - m_syn.getY() - m_syn.overhangPx());
    bool sound = false, midi = false;
    for (const auto& f : files) { sound = sound || f.endsWithIgnoreCase(".mdsound") || MdProcessor::isAudioFile(f); midi = midi || isMidiFile(f); }
    if (midi) {   // a MIDI clip: onto a track key = that track; anywhere else = the pattern (the keys framed)
        const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
        if (over >= 0) return (m_keys.keyBounds(over) * kScale + m_keys.getPosition()).expanded(kScale);
        return m_keys.getBounds().expanded(kScale);
    }
    if (sound) {   // a sound or a sample: the track key it lands on (the one under the pointer, else the selected track's)
        const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
        const auto key = m_keys.keyBounds(over >= 0 ? over : m_track);
        return (key * kScale + m_keys.getPosition()).expanded(kScale);
    }
    return pages;
}

void MdEditor::fileDragEnter(const juce::StringArray& files, int x, int y) { m_dragOver = true; m_dropRect = dropFrame(files, x, y); repaint(); }

void MdEditor::fileDragMove(const juce::StringArray& files, int x, int y) { const auto r = dropFrame(files, x, y); if (r != m_dropRect) { m_dropRect = r; repaint(); } }

void MdEditor::fileDragExit(const juce::StringArray&) { m_dragOver = false; repaint(); }

void MdEditor::paintOverChildren(juce::Graphics& g)
{
    if (!m_dragOver || m_dropRect.isEmpty()) return;
    g.setColour(lcd::ink);
    const auto r = m_dropRect;
    for (int x = r.getX(); x < r.getRight(); x += 2 * kScale) {
        g.fillRect(x, r.getY(), kScale, 2 * kScale);
        g.fillRect(x, r.getBottom() - 2 * kScale, kScale, 2 * kScale);
    }
    for (int y = r.getY(); y < r.getBottom(); y += 2 * kScale) {
        g.fillRect(r.getX(), y, 2 * kScale, kScale);
        g.fillRect(r.getRight() - 2 * kScale, y, 2 * kScale, kScale);
    }
}

void MdEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    m_dragOver = false;
    repaint();
    juce::Array<juce::File> syx, audio;
    juce::StringArray errors;
    for (const auto& path : files) {
        const juce::File f(path);
        if (isMidiFile(path)) {   // a MIDI clip: into the pattern GRID edits (onto a track key: all its notes on that track)
            const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
            const auto err = importMidiClip(f, over);
            if (err.isNotEmpty()) errors.add(f.getFileName() + ": " + err);
            continue;
        }
        if (path.endsWithIgnoreCase(".syx")) { syx.add(f); continue; }
        if (MdProcessor::isAudioFile(path)) { audio.add(f); continue; }
        mnm::library::MdTransferPayload p;
        if (!mnm::library::readMdTransferFile(f, p)) { errors.add(f.getFileName() + " is not a Machinedrum sound or kit file."); continue; }
        if (p.isKit) {
            const int emptied = m_proc.loadMdKit(juce::String(mnm::mdcatalog::Catalog::kitHash(p.kit)), p.kit, p.name.isNotEmpty() ? p.name.toUpperCase() : juce::String(p.kit.name));
            if (emptied > 0) errors.add(juce::String(emptied) + " track(s) of " + f.getFileName() + " use machines Monomodule MD does not have; they were left empty.");
            selectTrack(m_track);
        } else {
            const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
            const int t = over >= 0 ? over : m_track;
            if (!m_proc.loadSound(t, juce::String(mnm::mdcatalog::Catalog::soundHash(p.sound)), p.sound, p.name.toUpperCase()))
                errors.add(f.getFileName() + " uses a machine Monomodule MD does not have.");
            if (t != m_track) selectTrack(t); else bindTrackPages();
        }
    }
    if (!syx.isEmpty()) importFiles(syx);
    // samples: the first onto the track it was dropped on (its ROM slot, or the first empty ROM slot with the track
    // switched to that ROM machine); any more into the next empty ROM slots
    for (int n = 0; n < audio.size(); ++n) {
        const auto& f = audio.getReference(n);
        int slot = -1;
        if (n == 0) {
            const int over = m_keys.trackAt(m_keys.getLocalPoint(this, juce::Point<int>(x, y)));
            const int t = over >= 0 ? over : m_track;
            const int id = m_proc.machineIdOf(t);
            slot = isRomMachine(id) ? romSlotOf(id) : m_proc.firstEmptyRomSlot();
            if (slot < 0) { errors.add("Every ROM slot holds a sample: clear one (or drop onto a ROM track to replace its sample)."); break; }
            const auto err = m_proc.loadSample(slot, f);
            if (err.isNotEmpty()) { errors.add(f.getFileName() + ": " + err); continue; }
            if (!isRomMachine(id))
                if (auto* p = m_proc.apvts.getParameter(machineId(t))) p->setValueNotifyingHost(p->convertTo0to1(float(machineIndexOf(slot + 128))));
            if (t != m_track) selectTrack(t);
        } else {
            slot = m_proc.firstEmptyRomSlot();
            if (slot < 0) { errors.add("No empty ROM slot left for " + f.getFileName() + " and the rest."); break; }
            const auto err = m_proc.loadSample(slot, f);
            if (err.isNotEmpty()) errors.add(f.getFileName() + ": " + err);
        }
    }
    if (!errors.isEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Monomodule MD", errors.joinIntoString("\n"));
    timerCallback();
}

void MdEditor::openKitList()
{
    const int maxH = m_out.getBottom() - m_strip.getBottom() - 2;
    const auto part = m_strip.partBounds(MdKitStrip::Kit) + m_strip.getPosition();
    m_drop.openKits(m_proc.loadedKitKey(), {juce::jmax(0, part.getX()), m_strip.getBottom() + 2}, maxH);
    m_strip.setOpen(MdKitStrip::Kit);
}

void MdEditor::openSoundList()
{
    const int maxH = m_out.getBottom() - m_strip.getBottom() - 2;
    const auto part = m_strip.partBounds(MdKitStrip::Sound) + m_strip.getPosition();
    const int x = juce::jmin(part.getX(), getWidth() - MdLibraryDrop::kLcdW * MdLibraryDrop::kS - 10);
    m_drop.openSounds(m_proc.machineIdOf(m_track), m_track, m_proc.loadedSoundKey(m_track), {x, m_strip.getBottom() + 2}, maxH);
    m_strip.setOpen(MdKitStrip::Sound);
}

void MdEditor::loadKit(const KitEntry& e)
{
    mnm::mddump::Kit kit;
    if (!m_lib->loadKit(e.key, kit)) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Kit", "The kit " + e.name + " is no longer in the library.");
        return;
    }
    m_kitUndo = {true, m_proc.captureMdKit(), m_proc.loadedKitKey(), m_proc.kitName()};
    const int emptied = m_proc.loadMdKit(e.key, kit, e.name);
    // a project's kit: its patterns become the pattern bank (SEQ / PTN)
    if (e.sourceId.isNotEmpty() && e.sourceId != "saved")
        if (const auto* dump = m_lib->model().mdState(e.sourceId)) m_proc.setPatternBank(e.sourceId, e.source, *dump, e.position);
    if (emptied > 0)
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Kit",
            juce::String(emptied) + " track(s) use machines Monomodule MD does not have; they were left empty.");
    selectTrack(m_track);
    timerCallback();
}

// previous / next kit in the list order, wrapping
void MdEditor::stepKit(int dir)
{
    const auto kits = m_lib->kits();
    if (kits.empty()) { openKitList(); return; }
    int i = -1;
    for (int k = 0; k < int(kits.size()); ++k) if (kits[size_t(k)].key == m_proc.loadedKitKey()) i = k;
    i = i < 0 ? (dir > 0 ? 0 : int(kits.size()) - 1) : (i + dir + int(kits.size())) % int(kits.size());
    loadKit(kits[size_t(i)]);
}

juce::String MdEditor::slotText(const mnm::library::LibraryModel::Slot& s)
{
    if (!s.valid() || s.kit < 0) return {};
    return "PROJECT " + s.projectName + ", KIT " + juce::String(s.kit + 1).paddedLeft('0', 2) + (s.track >= 0 ? " T" + juce::String(s.track + 1) : juce::String());
}

juce::String MdEditor::saveKit(const juce::String& name, bool intoProject, bool asVersion)
{
    juce::String key;
    const auto into = intoProject ? m_lib->projectSlotOfKit(m_proc.loadedKitKey()) : mnm::library::LibraryModel::Slot{};
    const auto r = m_lib->saveKit(name, m_proc.captureMdKit(), asVersion ? m_proc.loadedKitKey() : juce::String(), &key, into);
    if (r.failed()) return r.getErrorMessage();
    m_proc.setLoadedKit(key, name.toUpperCase());
    timerCallback();
    return {};
}

void MdEditor::loadSound(const SoundEntry& e)
{
    mnm::mdcatalog::Sound s;
    if (!m_lib->loadSound(e.key, s)) return;
    if (!m_proc.loadSound(m_track, e.key, s, e.name))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Sound", "This sound uses a machine Monomodule MD does not have yet.");
    bindTrackPages();
    timerCallback();
}

// previous / next sound of the selected track's machine, wrapping
void MdEditor::stepSound(int dir)
{
    const auto sounds = m_lib->sounds(m_proc.machineIdOf(m_track));
    if (sounds.empty()) { openSoundList(); return; }
    int i = -1;
    for (int k = 0; k < int(sounds.size()); ++k) if (sounds[size_t(k)].key == m_proc.loadedSoundKey(m_track)) i = k;
    i = i < 0 ? (dir > 0 ? 0 : int(sounds.size()) - 1) : (i + dir + int(sounds.size())) % int(sounds.size());
    loadSound(sounds[size_t(i)]);
}

juce::String MdEditor::saveSound(const juce::String& name, bool intoProject, bool asVersion)
{
    juce::String key;
    const auto into = intoProject ? m_lib->projectSlotOfSound(m_proc.loadedSoundKey(m_track)) : mnm::library::LibraryModel::Slot{};
    const auto r = m_lib->saveSound(name, m_proc.captureSound(m_track), asVersion ? m_proc.loadedSoundKey(m_track) : juce::String(), &key, into);
    if (r.failed()) return r.getErrorMessage();
    m_proc.setLoadedSound(m_track, key, name.toUpperCase());
    timerCallback();
    return {};
}

// A kit (its best pattern, else a demo), a sound (one trig) or a pattern (on its kit), heard before loading it; a second
// click stops
void MdEditor::audition(const juce::String& key, int kind)
{
    if (m_proc.previewKey() == key) { m_proc.previewStop(); m_drop.repaint(); m_panel.repaint(); return; }
    const auto& cat = m_lib->model().mdCatalog();
    mnm::mdpreview::Options opt;
    if (kind == MdLibraryPanel::Kits) {
        const auto* k = cat.kit(key.toStdString());
        if (!k) return;
        const auto kitData = k->kit;
        const auto best = mnm::mdpreview::choosePreviewPattern(cat, *k);
        if (const auto* p = best.empty() ? nullptr : cat.pattern(best)) { const auto pat = p->pattern; m_proc.previewPlay(key, [kitData, pat, opt] { return mnm::mdpreview::patternPreview(kitData, pat, opt); }); }
        else m_proc.previewPlay(key, [kitData, opt] { return mnm::mdpreview::patternPreview(kitData, mnm::mdpreview::demoPattern(kitData), opt); });
    } else if (kind == MdLibraryPanel::Patterns) {
        const auto* p = cat.pattern(key.toStdString());
        const auto* k = p ? cat.kit(p->kitId) : nullptr;
        if (!p || !k) { m_panel.message = "THIS PATTERN'S KIT SLOT IS EMPTY"; m_panel.repaint(); return; }
        const auto pat = p->pattern; const auto kitData = k->kit;
        m_proc.previewPlay(key, [kitData, pat, opt] { return mnm::mdpreview::patternPreview(kitData, pat, opt); });
    } else {
        const auto* s = cat.sound(key.toStdString());
        if (!s) return;
        const auto snd = s->sound;
        m_proc.previewPlay(key, [snd, opt] { return mnm::mdpreview::soundPreview(snd, opt); });
    }
    if (const auto st = m_proc.previewStatus(); st.isNotEmpty()) { m_panel.message = st.toUpperCase(); juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Audition", st); }
    m_drop.repaint();
    m_panel.repaint();
}

void MdEditor::loadKitKey(const juce::String& key)
{
    for (const auto& k : m_lib->kits()) if (k.key == key) { loadKit(k); return; }
}

void MdEditor::loadSoundKey(int track, const juce::String& key)
{
    mnm::mdcatalog::Sound s;
    if (!m_lib->loadSound(key, s)) return;
    const auto* item = m_lib->model().mdCatalog().sound(key.toStdString());
    const auto name = item ? juce::String(item->name).toUpperCase() : juce::String();
    if (!m_proc.loadSound(track, key, s, name))
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Load Sound", "This sound uses a machine Monomodule MD does not have yet.");
    bindTrackPages();
    timerCallback();
}

// The selected track's sound: the name it was loaded or saved under, else its catalog name, else "-"
juce::String MdEditor::soundDisplayName(int t)
{
    if (m_proc.machineIdOf(t) == 0) return "-";
    if (const auto n = m_proc.loadedSoundName(t); n.isNotEmpty()) return n;
    if (const auto key = m_proc.loadedSoundKey(t); key.isNotEmpty())
        if (const auto* s = m_lib->model().mdCatalog().sound(key.toStdString())) return juce::String(s->name).toUpperCase();
    return "-";
}

void MdEditor::sampleMenu()
{
    const int id = m_proc.machineIdOf(m_track);
    if (!isRomMachine(id)) return;
    const int slot = romSlotOf(id);
    juce::PopupMenu m;
    m.addItem(1, "LOAD SAMPLE...");
    m.addItem(2, "CLEAR SAMPLE", m_proc.sampleName(slot).isNotEmpty());
    m.addSeparator();
    if (m_proc.sampleName(slot).isNotEmpty())
        m.addItem(3, m_proc.sampleName(slot).toUpperCase() + "  " + juce::String(m_proc.sampleSeconds(slot), 2) + " S", false);
    m.addItem(4, juce::String("SAMPLE MEMORY USED ") + juce::String(int(std::lround(m_proc.sampleMemoryUsed() * 100.0))) + "%", false);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_sample), [this, slot](int r) {
        if (r == 2) { m_proc.clearSample(slot); return; }
        if (r != 1) return;
        m_chooser = std::make_unique<juce::FileChooser>("Load a sample into " + juce::String(kMachines[machineIndexOf(slot + 128)].name),
            juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac");
        m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            const auto err = m_proc.loadSample(slot, f);
            if (err.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Load Sample", err);
        });
    });
}

} // namespace mnm::plugin::md
