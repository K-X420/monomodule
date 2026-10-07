// The kit strip: the kit name, the track's machine and the LCD-style header over the keys
#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

void MdKitStrip::setKit(const juce::String& name, bool modified)
{
    const auto n = name.isEmpty() ? juce::String("DEFAULT") : name.toUpperCase();
    if (n == m_kit && modified == m_kitMod) return;
    m_kit = n; m_kitMod = modified;
    repaint();
}

void MdKitStrip::setSound(int track, const juce::String& name, bool modified)
{
    const auto n = name.toUpperCase();
    if (n == m_sound && modified == m_soundMod && track == m_track) return;
    m_sound = n; m_soundMod = modified; m_track = track;
    repaint();
}

void MdKitStrip::resized()
{
    // joined parts share their edges; the kit and the sound are two strips with a gap between
    const int w = getWidth() / kS, arrowW = 12, iconW = 15, gap = 6;
    const int fixed = 2 * (2 * (arrowW - 1) + (iconW - 1)) + gap + (iconW - 1);
    const int kitW = (w - fixed) / 2, soundW = w - fixed - kitW;
    int x = 0;
    auto take = [&](Part p, int pw) { m_rects[size_t(p)] = {x, 0, pw, kLcdH}; x += pw - 1; };
    take(KitPrev, arrowW); take(Kit, kitW); take(KitNext, arrowW); take(KitSave, iconW);
    x += 1 + gap;
    take(SoundPrev, arrowW); take(Sound, soundW); take(SoundNext, arrowW); take(SoundSave, iconW); take(Library, iconW);
}

void MdKitStrip::paint(juce::Graphics& g)
{
    LcdCanvas cv(getWidth() / kS, kLcdH);
    auto part = [&](Part p, bool solid) {
        const auto r = m_rects[size_t(p)];
        if (solid) cv.fillRect(r.getX(), r.getY(), r.getWidth(), r.getHeight(), true);
        frame(cv, r);
        if (!solid && m_hover == p) frame(cv, r.reduced(1));
        return r;
    };
    auto selector = [&](Part p, const juce::String& label, const juce::String& name, bool modified) {
        const bool open = m_open == p;
        const auto r = part(p, open);
        const int lw = LcdCanvas::textWidth(spec::kFontTiny3x5, label.toRawUTF8());
        cv.text(spec::kFontTiny3x5, label.toRawUTF8(), r.getX() + 4, r.getY() + 5, !open);
        const int nx = r.getX() + 4 + lw + 4, maxW = r.getRight() - 10 - nx;
        textMarked(cv, spec::kFontBold8, fit(spec::kFontBold8, name, maxW - (modified ? 7 : 0)), nx, r.getY() + 4, !open, modified);
        caret(cv, r.getRight() - 9, r.getY() + 6, open, !open);
    };
    auto arrows = [&](Part prev, Part next) {
        auto r = part(prev, false); arrowH(cv, r.getCentreX(), r.getCentreY(), true, true);
        r = part(next, false); arrowH(cv, r.getCentreX(), r.getCentreY(), false, true);
    };
    arrows(KitPrev, KitNext);
    selector(Kit, "KIT", m_kit, m_kitMod);
    auto r = part(KitSave, m_kitMod); pixelIcon(cv, kIconSave, 9, r.getX() + 3, r.getY() + 3, !m_kitMod);
    arrows(SoundPrev, SoundNext);
    selector(Sound, "T" + juce::String(m_track + 1), m_sound.isEmpty() ? juce::String("-") : m_sound, m_soundMod);
    r = part(SoundSave, m_soundMod); pixelIcon(cv, kIconSave, 9, r.getX() + 3, r.getY() + 3, !m_soundMod);
    r = part(Library, m_libOpen); pixelIcon(cv, kIconLibrary, 9, r.getX() + 3, r.getY() + 3, !m_libOpen);
    cv.draw(g, 0, 0, kS);
}

} // namespace mnm::plugin::md
