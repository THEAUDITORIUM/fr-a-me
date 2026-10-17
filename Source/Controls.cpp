/*
  ==============================================================================
    fr(a)me — Controls.cpp
  ==============================================================================
*/

#include "Controls.h"
#include "FrameLookAndFeel.h"
#include "Scale.h"

//==============================================================================
//  ModeToggle
//==============================================================================
ModeToggle::ModeToggle (juce::RangedAudioParameter& param)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("mode");
    attachment = std::make_unique<juce::ParameterAttachment> (param, [this] (float v)
    {
        mode = juce::jlimit (0, 1, juce::roundToInt (v));
        repaint();
    });
    attachment->sendInitialUpdate();
}

juce::Rectangle<float> ModeToggle::optionBounds (int index) const
{
    auto b = getLocalBounds().toFloat();
    const float slashW = 26.0f;
    const float optW = (b.getWidth() - slashW) * 0.5f;
    return index == 0 ? b.withWidth (optW) : b.withTrimmedLeft (optW + slashW);
}

void ModeToggle::paint (juce::Graphics& g)
{
    const char* labels[2] = { "major", "minor" };

    for (int i = 0; i < 2; ++i)
    {
        const auto r = optionBounds (i);
        const bool active = (i == mode);

        if (active)   // surlignage façon marqueur : bande jaune derrière le mot
        {
            g.setColour (Palette::accent);
            g.fillRect (r.reduced (2.0f, r.getHeight() * 0.18f));
        }

        drawMonoText (g, labels[i], r, 15.0f, juce::Justification::centred,
                      active ? Palette::ink : Palette::pencil, active);
    }

    drawMonoText (g, "/", getLocalBounds().toFloat(), 15.0f, juce::Justification::centred, Palette::pencil);
}

void ModeToggle::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;

    // Clic sur un mot = ce mode ; clic sur la barre = bascule.
    int target = 1 - mode;
    if (optionBounds (0).contains (e.position)) target = 0;
    else if (optionBounds (1).contains (e.position)) target = 1;

    attachment->setValueAsCompleteGesture ((float) target);
}

//==============================================================================
//  NoteRow
//==============================================================================
NoteRow::NoteRow (juce::RangedAudioParameter& param)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("note");
    attachment = std::make_unique<juce::ParameterAttachment> (param, [this] (float v)
    {
        note = juce::jlimit (0, 11, juce::roundToInt (v));
        repaint();
    });
    attachment->sendInitialUpdate();
}

void NoteRow::setScaleMask (uint16_t newMask)
{
    if (newMask != mask) { mask = newMask; repaint(); }
}

juce::Rectangle<float> NoteRow::cellBounds (int index) const
{
    const auto b = getLocalBounds().toFloat();
    const float cellW = b.getWidth() / 12.0f;
    return { b.getX() + (float) index * cellW, b.getY(), cellW, b.getHeight() - 12.0f };
}

int NoteRow::cellAt (juce::Point<float> p) const
{
    for (int i = 0; i < 12; ++i)
        if (cellBounds (i).contains (p))
            return i;
    return -1;
}

void NoteRow::paint (juce::Graphics& g)
{
    for (int i = 0; i < 12; ++i)
    {
        const auto r = cellBounds (i);
        const bool active = (i == note);
        const bool sharp = juce::String (frame::noteNames[i]).containsChar ('#');

        if (active)
        {
            g.setColour (Palette::accent);
            g.fillRect (r);
        }
        else if (i == hover)
        {
            g.setColour (Palette::faint.withAlpha (0.6f));
            g.fillRect (r);
        }

        // Cases au trait fin ; les altérations légèrement plus basses (clin d'œil au clavier).
        g.setColour (Palette::ink);
        g.drawRect (r, 0.7f);

        drawMonoText (g, frame::noteNames[i], r.withTrimmedTop (sharp ? 6.0f : 0.0f), 12.0f,
                      juce::Justification::centred, active ? Palette::ink : Palette::graphite, active, 0.04f);

        // Point sous les notes de la gamme courante (jaune pour la tonique).
        if (frame::maskContains (mask, i))
        {
            const float cx = r.getCentreX(), cy = r.getBottom() + 6.0f;
            g.setColour (active ? Palette::accentInk : Palette::ink);
            g.fillEllipse (cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
        }
    }
}

void NoteRow::mouseDown (const juce::MouseEvent& e)
{
    const int cell = cellAt (e.position);
    if (cell < 0) return;

    dragging = true;
    attachment->beginGesture();
    attachment->setValueAsPartOfGesture ((float) cell);
}

void NoteRow::mouseDrag (const juce::MouseEvent& e)
{
    // Glisser le long de la rangée parcourt les toniques.
    const int cell = cellAt ({ juce::jlimit (0.0f, (float) getWidth() - 1.0f, e.position.x), cellBounds (0).getCentreY() });
    if (dragging && cell >= 0 && cell != note)
        attachment->setValueAsPartOfGesture ((float) cell);
}

void NoteRow::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        attachment->endGesture();
    dragging = false;
}

void NoteRow::mouseMove (const juce::MouseEvent& e)
{
    const int cell = cellAt (e.position);
    if (cell != hover) { hover = cell; repaint(); }
}

void NoteRow::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}
