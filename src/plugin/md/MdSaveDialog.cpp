// The save dialog: a name, a project option, versions
#include "MdLibraryUi.h"
#include "MdNames.h"
#include "MdParams.h"

namespace mnm::plugin::md {

using namespace one;   // LcdCanvas and the LcdDraw helpers
namespace spec = mnm::uispec;

void MdSaveDialog::open(const juce::String& title, const juce::String& name, const juce::String& projectOption, const juce::String& versionOf)
{
    m_project = projectOption.toUpperCase();
    m_versionOf = versionOf.toUpperCase();
    m_asVersion = m_versionOf.isNotEmpty();
    m_intoProject = false;
    m_title = title.toUpperCase();
    m_name = name.toUpperCase().substring(0, 16);
    m_error.clear();
    setVisible(true);
    toFront(true);
    grabKeyboardFocus();
    repaint();
}

void MdSaveDialog::paint(juce::Graphics& g)
{
    g.fillAll(lcd::paper.withAlpha(0.6f));
    const auto b = box();
    LcdCanvas cv(kLcdW, kLcdH);
    cv.clear(false);
    frame(cv, {0, 0, kLcdW, kLcdH});
    cv.fillRect(0, 0, kLcdW, 11, true);
    cv.text(spec::kFontBold8, m_title.toRawUTF8(), 4, 2, false);
    cv.text(spec::kFontTiny3x5, "NAME", 6, 20, true);
    dottedFrame(cv, {28, 15, kLcdW - 34, 15});
    textMarked(cv, spec::kFontBold8, m_name, 32, 19, true, false, true);
    m_version = {};
    if (m_versionOf.isNotEmpty()) {   // a new version of the loaded item (its history keeps the old ones), or a new item
        m_version = {6, 33, kLcdW - 12, 12};
        frame(cv, {6, 34, 9, 9});
        if (m_asVersion) cv.fillRect(8, 36, 5, 5, true);
        cv.text(spec::kFontSmall4x5, fit(spec::kFontSmall4x5, "NEW VERSION OF " + m_versionOf, kLcdW - 30).toRawUTF8(), 19, 36, true);
    } else {
        cv.text(spec::kFontTiny3x5, "A NEW ITEM IN THE LIBRARY; NOTHING IS OVERWRITTEN", 6, 37, true);
    }
    if (m_error.isNotEmpty()) cv.text(spec::kFontTiny3x5, fit(spec::kFontTiny3x5, m_error.toUpperCase(), kLcdW - 128).toRawUTF8(), 6, kLcdH - 14, true);
    m_option = {};
    if (m_project.isNotEmpty()) {   // also into the project slot it came from, as a new version of the project
        m_option = {6, 46, kLcdW - 12, 13};
        frame(cv, {6, 47, 9, 9});
        if (m_intoProject) cv.fillRect(8, 49, 5, 5, true);
        cv.text(spec::kFontSmall4x5, fit(spec::kFontSmall4x5, "ALSO PUT IT INTO " + m_project, kLcdW - 30).toRawUTF8(), 19, 49, true);
        cv.text(spec::kFontTiny3x5, "(A NEW VERSION OF THE PROJECT; EXPORT IT TO THE UNIT FROM THE LIBRARY APP)", 19, 57, true);
    }
    m_cancel = {kLcdW - 116, kLcdH - 19, 52, 13};
    m_save = {kLcdW - 58, kLcdH - 19, 52, 13};
    frame(cv, m_cancel);
    cv.textCentred(spec::kFontSmall4x5, "CANCEL", m_cancel.getX(), m_cancel.getWidth(), m_cancel.getY() + 4, true);
    cv.fillRect(m_save.getX(), m_save.getY(), m_save.getWidth(), m_save.getHeight(), true);
    cv.textCentred(spec::kFontSmall4x5, "SAVE", m_save.getX(), m_save.getWidth(), m_save.getY() + 4, false);
    cv.draw(g, b.getX(), b.getY(), kS);
}

void MdSaveDialog::save()
{
    if (m_name.trim().isEmpty()) { m_error = "TYPE A NAME"; repaint(); return; }
    const auto err = onSave ? onSave(m_name.trim(), m_intoProject, m_asVersion && m_versionOf.isNotEmpty()) : juce::String();
    if (err.isNotEmpty()) { m_error = err; repaint(); return; }
    setVisible(false);
}

void MdSaveDialog::mouseDown(const juce::MouseEvent& e)
{
    const auto b = box();
    if (!b.contains(e.getPosition())) { setVisible(false); return; }
    const auto lcd = (e.getPosition() - b.getPosition()) / kS;
    if (m_option.contains(lcd)) { m_intoProject = !m_intoProject; repaint(); return; }
    if (m_version.contains(lcd)) { m_asVersion = !m_asVersion; repaint(); return; }
    if (m_cancel.contains(lcd)) setVisible(false);
    else if (m_save.contains(lcd)) save();
}

bool MdSaveDialog::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { setVisible(false); return true; }
    if (k == juce::KeyPress::returnKey) { save(); return true; }
    if (typeInto(m_name, k, 16)) { m_error.clear(); repaint(); return true; }
    return false;
}

} // namespace mnm::plugin::md
