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
                      active ? Palette::onAccent : Palette::pencil, active);
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
//  RetuneToggle
//==============================================================================
RetuneToggle::RetuneToggle (juce::RangedAudioParameter& param)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("retune");
    attachment = std::make_unique<juce::ParameterAttachment> (param, [this] (float v)
    {
        on = v > 0.5f;
        repaint();
    });
    attachment->sendInitialUpdate();
}

void RetuneToggle::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float box = juce::jmin (b.getHeight() - 4.0f, 16.0f);
    const juce::Rectangle<float> sq (b.getX() + 2.0f, b.getCentreY() - box * 0.5f, box, box);

    if (on)
    {
        g.setColour (Palette::accent);
        g.fillRect (sq);
        // coche fine
        juce::Path tick;
        tick.startNewSubPath (sq.getX() + box * 0.22f, sq.getCentreY());
        tick.lineTo (sq.getX() + box * 0.43f, sq.getBottom() - box * 0.25f);
        tick.lineTo (sq.getRight() - box * 0.2f, sq.getY() + box * 0.25f);
        g.setColour (Palette::onAccent);
        g.strokePath (tick, juce::PathStrokeType (1.3f));
    }
    g.setColour (Palette::ink);
    g.drawRect (sq, 0.8f);

    drawMonoText (g, "retune", b.withTrimmedLeft (box + 12.0f), 14.0f, juce::Justification::centredLeft,
                  on ? Palette::ink : Palette::pencil, on);
}

void RetuneToggle::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()))
        attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
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
                      juce::Justification::centred, active ? Palette::onAccent : Palette::graphite, active, 0.04f);

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

//==============================================================================
//  EqOverlay
//==============================================================================
namespace
{
    constexpr float eqMinHz = 40.0f, eqMaxHz = 12000.0f;     // = axe du spectrum
    constexpr float grabZone = 7.0f;

    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " khz" : juce::String (juce::roundToInt (hz)) + " hz";
    }
}

EqOverlay::EqOverlay (juce::RangedAudioParameter& low, juce::RangedAudioParameter& high)
    : lowParam (low), highParam (high)
{
    lowAttachment  = std::make_unique<juce::ParameterAttachment> (low,  [this] (float v) { lowHz = v;  repaint(); });
    highAttachment = std::make_unique<juce::ParameterAttachment> (high, [this] (float v) { highHz = v; repaint(); });
    lowAttachment->sendInitialUpdate();
    highAttachment->sendInitialUpdate();
}

float EqOverlay::yFor (float hz) const noexcept
{
    const float t = std::log (juce::jlimit (eqMinHz, eqMaxHz, hz) / eqMinHz) / std::log (eqMaxHz / eqMinHz);
    return (float) getHeight() * (1.0f - t);
}

float EqOverlay::hzFor (float y) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, 1.0f - y / juce::jmax (1.0f, (float) getHeight()));
    return eqMinHz * std::pow (eqMaxHz / eqMinHz, t);
}

EqOverlay::Grab EqOverlay::hitTestGrab (float y) const noexcept
{
    const float yl = yFor (lowHz), yh = yFor (highHz);
    const float dl = std::abs (y - yl), dh = std::abs (y - yh);
    if (dl <= grabZone || dh <= grabZone)
    {
        if (dl < dh) return Grab::low;
        if (dh < dl) return Grab::high;
        return y >= yl ? Grab::low : Grab::high;      // confondues : selon le côté
    }
    const bool full = lowHz <= eqMinHz * 1.01f && highHz >= eqMaxHz * 0.99f;
    if (! full && y > yh && y < yl)
        return Grab::band;
    return Grab::none;
}

void EqOverlay::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float yl = yFor (lowHz), yh = yFor (highHz);
    const bool lowOn = lowHz > eqMinHz * 1.01f, highOn = highHz < eqMaxHz * 0.99f;

    // Zones coupées : papier voilé + hachures fines (comme une zone barrée sur une planche).
    auto shade = [&] (juce::Rectangle<float> r)
    {
        if (r.getHeight() <= 0.5f) return;
        g.setColour (Palette::paper.withAlpha (0.72f));
        g.fillRect (r);
        g.setColour (Palette::pencil.withAlpha (0.35f));
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 9.0f)
            g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 0.5f);
        g.restoreState();
    };
    if (highOn) shade ({ b.getX(), b.getY(), b.getWidth(), yh });
    if (lowOn)  shade ({ b.getX(), yl, b.getWidth(), b.getBottom() - yl });

    auto handle = [&] (float y, bool on, bool active, bool isLow)
    {
        const float yy = juce::jlimit (1.0f, b.getHeight() - 1.0f, y);
        g.setColour ((on || active) ? Palette::ink : Palette::pencil.withAlpha (0.6f));
        g.drawLine (b.getX(), yy, b.getRight(), yy, active ? 1.4f : 1.0f);

        // Poignée : petite pastille à droite, avec ↕.
        const juce::Rectangle<float> tab (b.getRight() - 30.0f, juce::jlimit (2.0f, b.getHeight() - 14.0f, yy - 6.0f), 22.0f, 12.0f);   // toujours dans le cadre
        g.setColour (active ? Palette::accent : Palette::paper);
        g.fillRoundedRectangle (tab, 3.0f);
        g.setColour ((on || active) ? Palette::ink : Palette::pencil);
        g.drawRoundedRectangle (tab, 3.0f, 0.8f);
        const auto c = tab.getCentre();
        juce::Path arrows;
        arrows.addTriangle (c.x - 3.0f, c.y - 1.2f, c.x + 3.0f, c.y - 1.2f, c.x, c.y - 4.2f);
        arrows.addTriangle (c.x - 3.0f, c.y + 1.2f, c.x + 3.0f, c.y + 1.2f, c.x, c.y + 4.2f);
        g.fillPath (arrows);

        // Valeur : pendant le geste ou au survol, et dès que la poignée coupe.
        if (on || active)
        {
            const auto text = hzText (isLow ? lowHz : highHz);
            const float ty = isLow ? juce::jmin (yy + 4.0f, b.getHeight() - 16.0f) : juce::jmax (yy - 16.0f, 2.0f);
            const juce::Rectangle<float> box (b.getRight() - 104.0f, ty, 68.0f, 13.0f);
            g.setColour (Palette::paper.withAlpha (0.92f));
            g.fillRect (box);
            g.setColour (Palette::ink);
            g.drawRect (box, 0.6f);
            drawMonoText (g, (isLow ? "low " : "high ") + text, box, 8.5f, juce::Justification::centred, Palette::ink);
        }
    };
    handle (yh, highOn, hover == Grab::high || dragging == Grab::high || dragging == Grab::band, false);
    handle (yl, lowOn,  hover == Grab::low  || dragging == Grab::low  || dragging == Grab::band, true);
}

void EqOverlay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTestGrab (e.position.y);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h == Grab::none ? juce::MouseCursor::NormalCursor
                  : h == Grab::band ? juce::MouseCursor::DraggingHandCursor
                                    : juce::MouseCursor::UpDownResizeCursor);
}

void EqOverlay::mouseExit (const juce::MouseEvent&)
{
    if (hover != Grab::none) { hover = Grab::none; repaint(); }
}

void EqOverlay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitTestGrab (e.position.y);
    dragStartY = e.position.y;
    dragStartLowY = yFor (lowHz);
    dragStartHighY = yFor (highHz);
    if (dragging == Grab::low || dragging == Grab::band)  lowAttachment->beginGesture();
    if (dragging == Grab::high || dragging == Grab::band) highAttachment->beginGesture();
    repaint();
}

void EqOverlay::mouseDrag (const juce::MouseEvent& e)
{
    const float dy = e.position.y - dragStartY;
    constexpr float minRatio = 1.06f;                       // les deux poignées ne se croisent pas
    switch (dragging)
    {
        case Grab::low:
            lowAttachment->setValueAsPartOfGesture (juce::jmin (hzFor (dragStartLowY + dy), highHz / minRatio));
            break;
        case Grab::high:
            highAttachment->setValueAsPartOfGesture (juce::jmax (hzFor (dragStartHighY + dy), lowHz * minRatio));
            break;
        case Grab::band:
        {
            // La plage glisse sans changer de largeur (bornée par les bords).
            const float h = (float) getHeight();
            const float d = juce::jlimit (-dragStartHighY, h - dragStartLowY, dy);
            lowAttachment->setValueAsPartOfGesture (hzFor (dragStartLowY + d));
            highAttachment->setValueAsPartOfGesture (hzFor (dragStartHighY + d));
            break;
        }
        case Grab::none: break;
    }
}

void EqOverlay::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Grab::low || dragging == Grab::band)  lowAttachment->endGesture();
    if (dragging == Grab::high || dragging == Grab::band) highAttachment->endGesture();
    dragging = Grab::none;
    repaint();
}

void EqOverlay::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-clic sur une poignée : elle revient en butée ; ailleurs : tout est remis à zéro.
    const auto h = hitTestGrab (e.position.y);
    if (h != Grab::high) lowAttachment->setValueAsCompleteGesture (eqMinHz);
    if (h != Grab::low)  highAttachment->setValueAsCompleteGesture (eqMaxHz);
}
