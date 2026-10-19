/*
  ==============================================================================
    fr(a)me — Controls.cpp
  ==============================================================================
*/

#include "Controls.h"
#include "TranceGate.h"
#include "FrameLookAndFeel.h"
#include "Scale.h"
#include "ProEq.h"
#include "PluginProcessor.h"

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
//  GatePanel
//==============================================================================
GatePanel::GatePanel (juce::RangedAudioParameter& onParam, juce::RangedAudioParameter& presetParam)
{
    setTitle ("gate");
    onAttachment = std::make_unique<juce::ParameterAttachment> (onParam, [this] (float v)
    {
        on = v > 0.5f;
        repaint();
    });
    presetAttachment = std::make_unique<juce::ParameterAttachment> (presetParam, [this] (float v)
    {
        preset = juce::jlimit (0, TranceGate::numPresets - 1, juce::roundToInt (v));
        repaint();
    });
    onAttachment->sendInitialUpdate();
    presetAttachment->sendInitialUpdate();
}

juce::Rectangle<float> GatePanel::leftArrow() const noexcept  { return { 0.0f, 0.0f, 24.0f, 22.0f }; }
juce::Rectangle<float> GatePanel::rightArrow() const noexcept { return { (float) getWidth() - 24.0f, 0.0f, 24.0f, 22.0f }; }
juce::Rectangle<float> GatePanel::stepsArea() const noexcept  { return { 0.0f, 29.0f, (float) getWidth(), 38.0f }; }
juce::Rectangle<float> GatePanel::toggleOption (int index) const noexcept
{
    const float w = 46.0f, slash = 18.0f, x0 = ((float) getWidth() - (2.0f * w + slash)) * 0.5f;
    return { x0 + (float) index * (w + slash), 76.0f, w, 22.0f };
}

void GatePanel::setStep (int s)
{
    if (s != step) { step = s; repaint (stepsArea().expanded (2.0f, 6.0f).toNearestInt()); }
}

void GatePanel::paint (juce::Graphics& g)
{
    using J = juce::Justification;
    const auto& p = TranceGate::preset (preset);

    // ‹ nom ›
    auto arrow = [&] (juce::Rectangle<float> r, bool left, bool hot)
    {
        const auto c = r.getCentre();
        juce::Path a;
        a.startNewSubPath (c.x + (left ? 3.0f : -3.0f), c.y - 6.0f);
        a.lineTo (c.x + (left ? -3.0f : 3.0f), c.y);
        a.lineTo (c.x + (left ? 3.0f : -3.0f), c.y + 6.0f);
        if (hot) { g.setColour (Palette::accent.withAlpha (0.45f)); g.fillEllipse (r.withSizeKeepingCentre (20.0f, 20.0f)); }
        g.setColour (Palette::ink);
        g.strokePath (a, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    arrow (leftArrow(), true, hover == 0);
    arrow (rightArrow(), false, hover == 1);
    drawMonoText (g, p.name, { 24.0f, 0.0f, (float) getWidth() - 48.0f, 22.0f }, 13.0f, J::centred, Palette::ink, true, 0.06f);

    // 16 pas : fente fine, barre verte à la hauteur du niveau.
    const auto sa = stepsArea();
    const float cw = sa.getWidth() / (float) TranceGate::numSteps;
    for (int i = 0; i < TranceGate::numSteps; ++i)
    {
        const auto slot = juce::Rectangle<float> (sa.getX() + (float) i * cw + 1.0f, sa.getY(), cw - 2.0f, sa.getHeight());
        const float lvl = p.steps[(size_t) i];
        const bool current = on && i == step;
        g.setColour (Palette::faint);
        g.drawRoundedRectangle (slot, 1.5f, 0.6f);
        if (lvl > 0.0f)
        {
            const auto bar = slot.withTop (slot.getBottom() - lvl * slot.getHeight());
            g.setColour (current ? Palette::accentInk : (on ? Palette::accent : Palette::accent.withAlpha (0.4f)));
            g.fillRoundedRectangle (bar, 1.5f);
        }
        else
        {
            g.setColour (Palette::pencil);
            g.fillRect (slot.getX() + 1.0f, slot.getBottom() - 1.5f, slot.getWidth() - 2.0f, 1.0f);
        }
        // temps forts (1, 5, 9, 13) : petit point dessous ; pas joué : point vert plus gros
        if (current)
        {
            g.setColour (Palette::accentInk);
            g.fillEllipse (slot.getCentreX() - 2.0f, sa.getBottom() + 3.0f, 4.0f, 4.0f);
        }
        else if (i % 4 == 0)
        {
            g.setColour (Palette::pencil);
            g.fillEllipse (slot.getCentreX() - 1.0f, sa.getBottom() + 4.0f, 2.0f, 2.0f);
        }
    }

    // off / on (même écriture que major / minor)
    const char* labels[2] = { "off", "on" };
    for (int i = 0; i < 2; ++i)
    {
        const auto r = toggleOption (i);
        const bool active = (i == 1) == on;
        if (active)
        {
            g.setColour (Palette::accent);
            g.fillRect (r.reduced (2.0f, r.getHeight() * 0.16f));
        }
        else if (hover == 2 + i)
        {
            g.setColour (Palette::accent.withAlpha (0.25f));
            g.fillRect (r.reduced (2.0f, r.getHeight() * 0.16f));
        }
        drawMonoText (g, labels[i], r, 13.0f, J::centred, active ? Palette::onAccent : Palette::pencil, active);
    }
    const auto t0 = toggleOption (0), t1 = toggleOption (1);
    drawMonoText (g, "/", { t0.getRight(), t0.getY(), t1.getX() - t0.getRight(), t0.getHeight() }, 13.0f, J::centred, Palette::pencil);
}

void GatePanel::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    if (leftArrow().contains (e.position)) h = 0;
    else if (rightArrow().contains (e.position)) h = 1;
    else if (toggleOption (0).contains (e.position)) h = 2;
    else if (toggleOption (1).contains (e.position)) h = 3;
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != hover) { hover = h; repaint(); }
}

void GatePanel::mouseUp (const juce::MouseEvent& e)
{
    const auto pos = e.position;
    const int n = TranceGate::numPresets;
    if (leftArrow().contains (pos))
        presetAttachment->setValueAsCompleteGesture ((float) ((preset + n - 1) % n));
    else if (rightArrow().contains (pos))
        presetAttachment->setValueAsCompleteGesture ((float) ((preset + 1) % n));
    else if (toggleOption (0).contains (pos))
        onAttachment->setValueAsCompleteGesture (0.0f);
    else if (toggleOption (1).contains (pos))
        onAttachment->setValueAsCompleteGesture (1.0f);
    else if (stepsArea().contains (pos))
        onAttachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);   // clic sur le motif : bascule
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

        drawMonoText (g, frame::noteNames[i], r.withTrimmedTop (sharp ? 6.0f : 0.0f), 11.0f,
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
//  ScanButton
//==============================================================================
ScanButton::ScanButton()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("scan key");
}

void ScanButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float rr = b.getHeight() * 0.5f;
    if (hover) { g.setColour (Palette::accent.withAlpha (0.35f)); g.fillRoundedRectangle (b, rr); }
    g.setColour (Palette::ink);
    g.drawRoundedRectangle (b, rr, 0.8f);
    // pictogramme : cercles concentriques (écoute)
    const juce::Point<float> c (b.getX() + rr + 2.0f, b.getCentreY());
    g.fillEllipse (c.x - 1.6f, c.y - 1.6f, 3.2f, 3.2f);
    g.drawEllipse (c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f, 0.7f);
    g.setColour (Palette::accentInk);
    g.drawEllipse (c.x - 7.5f, c.y - 7.5f, 15.0f, 15.0f, 0.7f);
    drawMonoText (g, "scan key", b.withTrimmedLeft (rr * 2.0f + 2.0f), 11.0f, juce::Justification::centredLeft, Palette::ink);
}

void ScanButton::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

//==============================================================================
//  EqView (façon Pro-Q)
//==============================================================================
namespace
{
    constexpr float eqLoHz = ProEq::minHz, eqHiHz = ProEq::maxHz;

    juce::String hzLabel (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " khz" : juce::String (juce::roundToInt (hz)) + " hz";
    }
}

EqView::EqView (juce::AudioProcessorValueTreeState& apvts)
{
    auto bind = [this, &apvts] (Attach& a, const char* id, float& target)
    {
        a.param = apvts.getParameter (id);
        if (a.param == nullptr) return;
        a.attachment = std::make_unique<juce::ParameterAttachment> (*a.param, [this, &target] (float v) { target = v; repaint(); });
        a.attachment->sendInitialUpdate();
    };
    bind (freqA[0], ParamIDs::eqLow, freq[0]);
    bind (freqA[4], ParamIDs::eqHigh, freq[4]);
    for (int b = 0; b < ProEq::numBells; ++b)
    {
        bind (freqA[b + 1], ParamIDs::eqBellHz[b], freq[b + 1]);
        bind (gainA[b + 1], ParamIDs::eqBellDb[b], gain[b + 1]);
        bind (qA[b + 1],    ParamIDs::eqBellQ[b],  q[b + 1]);
    }
}

float EqView::xFor (float hz) const noexcept
{
    const float pad = 8.0f;
    const float t = std::log (juce::jlimit (eqLoHz, eqHiHz, hz) / eqLoHz) / std::log (eqHiHz / eqLoHz);
    return pad + t * ((float) getWidth() - 2.0f * pad);
}

float EqView::hzFor (float x) const noexcept
{
    const float pad = 8.0f;
    const float t = juce::jlimit (0.0f, 1.0f, (x - pad) / juce::jmax (1.0f, (float) getWidth() - 2.0f * pad));
    return eqLoHz * std::pow (eqHiHz / eqLoHz, t);
}

float EqView::yFor (float db) const noexcept
{
    const float h = (float) getHeight(), pad = 10.0f;
    return pad + (h - 2.0f * pad) * 0.5f * (1.0f - db / displayDb);
}

float EqView::dbFor (float y) const noexcept
{
    const float h = (float) getHeight(), pad = 10.0f;
    return displayDb * (1.0f - 2.0f * (y - pad) / juce::jmax (1.0f, h - 2.0f * pad));
}

juce::Point<float> EqView::nodePos (int n) const noexcept
{
    if (n == 0 || n == numNodes - 1)
    {
        // Coupe : point sur la ligne 0 dB, gardé dans le cadre en butée.
        return { juce::jlimit (8.0f, (float) getWidth() - 8.0f, xFor (freq[n])), yFor (0.0f) };
    }
    return { xFor (freq[n]), yFor (gain[n]) };
}

int EqView::nodeAt (juce::Point<float> p) const noexcept
{
    int best = -1;
    float bestD = 11.0f;
    for (int n = 0; n < numNodes; ++n)
    {
        const float d = nodePos (n).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = n; }
    }
    return best;
}

juce::String EqView::nodeText (int n) const
{
    if (n == 0) return ProEq::lowCutOn (freq[0])  ? "low cut " + hzLabel (freq[0]) : juce::String ("low cut off");
    if (n == 4) return ProEq::highCutOn (freq[4]) ? "high cut " + hzLabel (freq[4]) : juce::String ("high cut off");
    return "bell " + juce::String (n) + "  " + hzLabel (freq[n]) + "  " + (gain[n] > 0.05f ? "+" : "")
         + juce::String (gain[n], 1) + " db  q " + juce::String (q[n], 2);
}

void EqView::pushSpectrum (const float* levels, int numRows, float rowMinHz, float rowMaxHz) noexcept
{
    analyserRows = juce::jmin (numRows, (int) analyser.size());
    analyserMinHz = rowMinHz; analyserMaxHz = rowMaxHz;
    for (int r = 0; r < analyserRows; ++r)
    {
        const float v = juce::jlimit (0.0f, 1.0f, levels[r]);
        auto& a = analyser[(size_t) r];
        a = v > a ? v : a + (v - a) * 0.18f;          // montée immédiate, retombée douce
    }
}

void EqView::setScale (uint16_t mask, int newTonic)
{
    if (mask != scaleMask || newTonic != tonic)
    {
        scaleMask = mask; tonic = newTonic;
        repaint();
    }
}

void EqView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();

    // Grille : décades et repères de gain.
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xFor (f);
        g.setColour (Palette::faint);
        g.drawVerticalLine ((int) x, 0.0f, h);
        drawMonoText (g, f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                      { x + 2.0f, h - 13.0f, 30.0f, 11.0f }, 8.0f, juce::Justification::centredLeft, Palette::pencil);
    }
    for (float db : { -12.0f, 12.0f })
    {
        g.setColour (Palette::faint);
        g.drawHorizontalLine ((int) yFor (db), 0.0f, w);
    }

    // Toniques de la gamme (une par octave) : traits verticaux à l'encre de la gamme.
    for (int octave = 0; octave <= 9; ++octave)
    {
        const float hz = (float) frame::midiToHz (12.0 * (octave + 1) + tonic);
        if (hz < eqLoHz || hz > eqHiHz) continue;
        const float x = xFor (hz);
        g.setColour (Palette::scaleInk.withAlpha (0.45f));
        g.drawVerticalLine ((int) x, 12.0f, h);
        drawMonoText (g, juce::String (frame::noteNames[juce::jlimit (0, 11, tonic)]) + juce::String (octave),
                      { x - 14.0f, 1.0f, 28.0f, 10.0f }, 7.5f, juce::Justification::centred, Palette::accentInk);
    }

    // Analyseur (son de sortie) : surface pleine, du bas vers le niveau.
    if (analyserRows > 1)
    {
        juce::Path spec;
        const float ratio = analyserMaxHz / analyserMinHz;
        spec.startNewSubPath (xFor (analyserMinHz), h);
        for (int r = 0; r < analyserRows; ++r)
        {
            const float hz = analyserMinHz * std::pow (ratio, ((float) r + 0.5f) / (float) analyserRows);
            // lissage sur ±2 lignes (le grave de la FFT est en marches d'escalier)
            float sum = 0.0f; int cnt = 0;
            for (int k = juce::jmax (0, r - 2); k <= juce::jmin (analyserRows - 1, r + 2); ++k) { sum += analyser[(size_t) k]; ++cnt; }
            const float lvl = std::pow (sum / (float) cnt, 1.5f);
            spec.lineTo (xFor (hz), h - lvl * (h - 22.0f) * 0.85f);
        }
        spec.lineTo (xFor (analyserMaxHz), h);
        spec.closeSubPath();
        g.setColour (Palette::ink.withAlpha (0.07f));
        g.fillPath (spec);
        g.setColour (Palette::graphite.withAlpha (0.35f));
        g.strokePath (spec, juce::PathStrokeType (0.7f));
    }

    // Ligne 0 dB.
    g.setColour (Palette::pencil);
    g.drawHorizontalLine ((int) yFor (0.0f), 0.0f, w);

    // Courbe de l'EQ + surface entre la courbe et 0 dB (comme Pro-Q).
    {
        ProEq::Settings s;
        s.lowHz = freq[0]; s.highHz = freq[4];
        for (int k = 0; k < ProEq::numBells; ++k)
        {
            s.bellHz[(size_t) k] = freq[k + 1]; s.bellDb[(size_t) k] = gain[k + 1]; s.bellQ[(size_t) k] = q[k + 1];
        }
        juce::Path curve, area;
        const float y0 = yFor (0.0f);
        area.startNewSubPath (0.0f, y0);
        bool firstPoint = true;
        for (float x = 0.0f; x <= w; x += 1.5f)
        {
            const float db = juce::jlimit (-displayDb - 4.0f, displayDb + 4.0f, ProEq::magnitudeDb (hzFor (x), s));
            const float y = juce::jlimit (-2.0f, h + 2.0f, yFor (db));
            if (firstPoint) { curve.startNewSubPath (x, y); firstPoint = false; } else curve.lineTo (x, y);
            area.lineTo (x, y);
        }
        area.lineTo (w, y0);
        area.closeSubPath();
        g.setColour (Palette::accent.withAlpha (0.30f));
        g.fillPath (area);
        g.setColour (Palette::accentInk);
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Points.
    for (int n = 0; n < numNodes; ++n)
    {
        const auto p = nodePos (n);
        const bool active = (n == hover || n == dragging);
        const bool cut = (n == 0 || n == numNodes - 1);
        const bool engaged = cut ? (n == 0 ? ProEq::lowCutOn (freq[0]) : ProEq::highCutOn (freq[4])) : std::abs (gain[n]) > 0.05f;
        const float r = active ? 6.5f : 5.0f;
        const juce::Rectangle<float> box (p.x - r, p.y - r, 2.0f * r, 2.0f * r);
        g.setColour (engaged || active ? Palette::accent : Palette::paper);
        if (cut) g.fillRoundedRectangle (box, 2.0f); else g.fillEllipse (box);       // coupes : carrés, cloches : ronds
        g.setColour (engaged || active ? Palette::ink : Palette::graphite);
        if (cut)
        {
            g.drawRoundedRectangle (box, 2.0f, 1.0f);
            // petit pictogramme de pente : ⟋ (coupe basse) / ⟍ (coupe haute)
            const float d = r * 0.5f;
            if (n == 0) g.drawLine (p.x - d, p.y + d, p.x + d, p.y - d, 1.0f);
            else        g.drawLine (p.x - d, p.y - d, p.x + d, p.y + d, 1.0f);
        }
        else
        {
            g.drawEllipse (box, 1.0f);
            drawMonoText (g, juce::String (n), box, 7.5f, juce::Justification::centred,
                          engaged || active ? Palette::onAccent : Palette::graphite, true, 0.0f);
        }
    }

    // Étiquette du point survolé / tiré.
    const int shown = dragging >= 0 ? dragging : hover;
    if (shown >= 0)
    {
        const auto p = nodePos (shown);
        const auto text = nodeText (shown);
        const float tw = 9.0f + 6.2f * (float) text.length();
        float tx = juce::jlimit (2.0f, w - tw - 2.0f, p.x - tw * 0.5f);
        float ty = p.y - 26.0f < 2.0f ? p.y + 12.0f : p.y - 26.0f;
        const juce::Rectangle<float> box (tx, ty, tw, 14.0f);
        g.setColour (Palette::paper.withAlpha (0.94f));
        g.fillRect (box);
        g.setColour (Palette::ink);
        g.drawRect (box, 0.6f);
        drawMonoText (g, text, box, 8.5f, juce::Justification::centred, Palette::ink);
    }

    g.setColour (Palette::pencil);
    g.drawRect (b, 0.6f);
}

void EqView::mouseMove (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n != hover) { hover = n; repaint(); }
    setMouseCursor (n >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void EqView::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0) { hover = -1; repaint(); }
}

void EqView::mouseDown (const juce::MouseEvent& e)
{
    dragging = nodeAt (e.position);
    if (dragging < 0) return;
    if (freqA[dragging].attachment) freqA[dragging].attachment->beginGesture();
    if (gainA[dragging].attachment) gainA[dragging].attachment->beginGesture();
    repaint();
}

void EqView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    float hz = hzFor (e.position.x);
    if (dragging == 0) hz = juce::jmin (hz, freq[4] / 1.06f);                 // la coupe basse reste sous la haute
    if (dragging == 4) hz = juce::jmax (hz, freq[0] * 1.06f);
    if (freqA[dragging].attachment) freqA[dragging].attachment->setValueAsPartOfGesture (hz);
    if (gainA[dragging].attachment)
        gainA[dragging].attachment->setValueAsPartOfGesture (juce::jlimit (-ProEq::maxGainDb, ProEq::maxGainDb, dbFor (e.position.y)));
}

void EqView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
    {
        if (freqA[dragging].attachment) freqA[dragging].attachment->endGesture();
        if (gainA[dragging].attachment) gainA[dragging].attachment->endGesture();
    }
    dragging = -1;
    repaint();
}

void EqView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n < 0) return;
    if (n == 0) freqA[0].attachment->setValueAsCompleteGesture (ProEq::minHz);
    else if (n == 4) freqA[4].attachment->setValueAsCompleteGesture (ProEq::maxHz);
    else
    {
        gainA[n].attachment->setValueAsCompleteGesture (0.0f);
        qA[n].attachment->setValueAsCompleteGesture (1.0f);
    }
}

void EqView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int n = dragging >= 0 ? dragging : nodeAt (e.position);
    if (n <= 0 || n >= numNodes - 1 || ! qA[n].attachment) return;     // Q : cloches seulement
    const float factor = std::pow (1.25f, wheel.deltaY * 4.0f);
    qA[n].attachment->setValueAsCompleteGesture (juce::jlimit (ProEq::minQ, ProEq::maxQ, q[n] * factor));
}
