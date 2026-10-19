/*
  ==============================================================================
    fr(a)me — ScanPage.cpp
  ==============================================================================
*/

#include "ScanPage.h"
#include "PluginProcessor.h"
#include "FrameLookAndFeel.h"
#include "Scale.h"

namespace
{
    constexpr float pageW = 1000.0f;
    juce::String keyName (int tonic, int mode)
    {
        if (tonic < 0 || tonic > 11) return "-";
        return juce::String (frame::noteNames[tonic]) + (mode == 1 ? " minor" : " major");
    }
}

ScanPage::ScanPage (FrameAudioProcessor& p) : processor (p)
{
    setInterceptsMouseClicks (true, false);
}

juce::Rectangle<float> ScanPage::buttonBounds() const noexcept
{
    return { pageW * 0.5f - 120.0f, 230.0f, 240.0f, 240.0f };
}

juce::Rectangle<float> ScanPage::altBounds (int i) const noexcept { return { pageW * 0.5f - 190.0f + 200.0f * (float) i, 556.0f, 170.0f, 28.0f }; }
juce::Rectangle<float> ScanPage::rescanBounds() const noexcept   { return { pageW * 0.5f - 190.0f, 600.0f, 170.0f, 34.0f }; }
juce::Rectangle<float> ScanPage::continueBounds() const noexcept { return { pageW * 0.5f + 20.0f, 600.0f, 170.0f, 34.0f }; }
juce::Rectangle<float> ScanPage::skipBounds() const noexcept     { return { pageW * 0.5f - 95.0f, 640.0f, 190.0f, 38.0f }; }

void ScanPage::open()
{
    state = State::idle;
    progress = 0.0f;
    tonic = -1;
    listeningTicks = doneTicks = 0;
    setVisible (true);
    toFront (false);
    repaint();
}

void ScanPage::close()
{
    if (processor.isScanHolding() || processor.getKeyDetector().isScanning())
        processor.cancelScan();                     // l'effet revient
    processor.setIntroDone (true);
    processor.setKeyReady (true);                   // scan terminé ou « skip scan » : l'effet s'active
    setVisible (false);
    if (onClose) onClose();
}

void ScanPage::finish()
{
    auto& kd = processor.getKeyDetector();
    for (int i = 0; i < 2; ++i) { altTonic[i] = kd.getAltTonic (i); altMode[i] = kd.getAltMode (i); }
    // Résultat appliqué au plugin (tonique + mode).
    if (tonic >= 0)
        processor.applyDetectedKey (tonic, mode);   // tonalité réglée, puis l'effet revient
    else
        processor.cancelScan();
    state = State::done;
    doneTicks = 0;
    repaint();
}

void ScanPage::update()
{
    if (! isVisible()) return;
    pulse += 0.06f;

    auto& kd = processor.getKeyDetector();
    if (state == State::listening)
    {
        ++listeningTicks;
        progress = kd.getProgress();
        tonic = kd.getTonic();
        mode = kd.getMode();
        if (kd.isDone())
            finish();
        repaint();
    }
    else if (state == State::done)
    {
        if (++doneTicks > 150)         // ~5 s : la planche s'ouvre d'elle-même
            close();
        else
            repaint (continueBounds().expanded (4.0f).toNearestInt());
    }
}

void ScanPage::mouseMove (const juce::MouseEvent& e)
{
    const bool over = buttonBounds().contains (e.position)
                   || (state == State::done && (rescanBounds().contains (e.position) || continueBounds().contains (e.position)
                                                || altBounds (0).contains (e.position) || altBounds (1).contains (e.position)))
                   || (state != State::done && skipBounds().contains (e.position));
    setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    const bool hs = state != State::done && skipBounds().contains (e.position);
    if (hs != hoverSkip) { hoverSkip = hs; repaint (skipBounds().expanded (4.0f).toNearestInt()); }
    const bool hb = buttonBounds().contains (e.position);
    if (hb != hoverButton) { hoverButton = hb; repaint(); }
}

void ScanPage::mouseUp (const juce::MouseEvent& e)
{
    auto& kd = processor.getKeyDetector();
    const auto pos = e.position;

    if (state == State::done)
    {
        for (int i = 0; i < 2; ++i)
            if (altTonic[i] >= 0 && altBounds (i).contains (pos))
            {
                // Une autre proposition : elle devient la tonalité, l'ancienne passe en alternative.
                std::swap (tonic, altTonic[i]);
                std::swap (mode, altMode[i]);
                processor.applyDetectedKey (tonic, mode);
                doneTicks = -150;                  // laisse le temps d'écouter (~5 s de plus)
                repaint();
                return;
            }
        if (rescanBounds().contains (pos) || buttonBounds().contains (pos))
        {
            processor.startScan();
            state = State::listening; listeningTicks = 0; progress = 0.0f; tonic = -1;
            repaint();
        }
        else if (continueBounds().contains (pos))
            close();
        return;
    }

    if (buttonBounds().contains (pos))
    {
        if (state == State::idle)
        {
            processor.startScan();
            state = State::listening; listeningTicks = 0; progress = 0.0f; tonic = -1;
        }
        else    // en écoute : arrêt ; on garde le résultat s'il y a assez de son
        {
            kd.cancel();
            if (progress > 0.25f && tonic >= 0) finish();
            else { processor.cancelScan(); state = State::idle; }
        }
        repaint();
        return;
    }

    // « skip » : entrer sans scanner.
    if (skipBounds().contains (pos))
        close();
}

void ScanPage::paint (juce::Graphics& g)
{
    using J = juce::Justification;
    const auto page = getLocalBounds().toFloat();
    g.fillAll (Palette::paper);
    drawGardenDecor (g, page);

    // En-tête (comme la planche).
    drawMonoText (g, "fr(a)me", { 0.0f, 60.0f, pageW, 40.0f }, 34.0f, J::centred, Palette::ink, true, 0.28f);
    drawMonoText (g, "( spectral harmoniser )", { 0.0f, 104.0f, pageW, 16.0f }, 11.0f, J::centred, Palette::graphite);
    drawMonoText (g, "plate 00", { 80.0f, 22.0f, 100.0f, 14.0f }, 10.0f, J::centredLeft, Palette::pencil);

    // Le gros bouton.
    const auto b = buttonBounds();
    const auto c = b.getCentre();
    const float r = b.getWidth() * 0.5f;

    // graduations autour (comme les potentiomètres)
    for (int i = 0; i < 60; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 60.0f;
        const bool major = i % 5 == 0;
        const float r0 = r + 8.0f, r1 = r + (major ? 16.0f : 12.0f);
        g.setColour (major ? Palette::ink : Palette::pencil);
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), major ? 0.9f : 0.6f);
    }

    if (state == State::done)
    {
        g.setColour (Palette::accent);
        g.fillEllipse (b);
    }
    else if (state == State::listening)
    {
        const float breathe = 0.18f + 0.10f * std::sin (pulse * 2.0f);
        g.setColour (Palette::accent.withAlpha (breathe));
        g.fillEllipse (b);
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r + 2.0f, r + 2.0f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * juce::jmax (0.002f, progress), true);
        g.setColour (Palette::accentInk);
        g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else if (hoverButton)
    {
        g.setColour (Palette::accent.withAlpha (0.18f));
        g.fillEllipse (b);
    }
    g.setColour (Palette::ink);
    g.drawEllipse (b, 1.2f);
    g.setColour (Palette::pencil);
    g.drawEllipse (b.reduced (14.0f), 0.6f);

    if (state == State::idle)
    {
        drawMonoText (g, "scan", b.withTrimmedBottom (20.0f), 40.0f, J::centred, Palette::ink, true, 0.25f);
        drawMonoText (g, "key finder", b.withTrimmedTop (70.0f), 11.0f, J::centred, Palette::graphite);
    }
    else if (state == State::listening)
    {
        drawMonoText (g, "listening", b.withTrimmedBottom (30.0f), 22.0f, J::centred, Palette::ink, true, 0.2f);
        drawMonoText (g, juce::String (juce::roundToInt (progress * 100.0f)) + " %", b.withTrimmedTop (40.0f), 13.0f, J::centred, Palette::graphite);
    }
    else
    {
        drawMonoText (g, keyName (tonic, mode), b.withTrimmedBottom (20.0f), 30.0f, J::centred, Palette::onAccent, true, 0.12f);
        drawMonoText (g, "applied", b.withTrimmedTop (70.0f), 11.0f, J::centred, Palette::onAccent);
    }

    // Texte sous le bouton.
    juce::String line1, line2;
    if (state == State::idle)
    {
        line1 = "press scan, then play your melody in your daw";
        line2 = "fr(a)me finds the key and sets tonic & mode";
    }
    else if (state == State::listening)
    {
        if (progress <= 0.0f && listeningTicks > 60)
            line1 = "no sound yet - press play in your daw";
        else
            line1 = "keep playing ...";
        line2 = tonic >= 0 ? "key : " + keyName (tonic, mode) : juce::String ("key : -");
        line2 << "      ( click the circle to stop )";
    }
    else
    {
        line1 = "tonic & mode set : " + keyName (tonic, mode);
        line2 = "not quite ? try one of these :";
    }
    drawMonoText (g, line1, { 0.0f, 506.0f, pageW, 18.0f }, 13.0f, J::centred, Palette::ink);
    drawMonoText (g, line2, { 0.0f, 530.0f, pageW, 16.0f }, 10.5f, J::centred, Palette::graphite);

    if (state == State::done)
    {
        auto pill = [&] (juce::Rectangle<float> rr, const juce::String& t, bool filled)
        {
            if (filled) { g.setColour (Palette::accent); g.fillRoundedRectangle (rr, 17.0f); }
            g.setColour (Palette::ink);
            g.drawRoundedRectangle (rr, 17.0f, 0.9f);
            drawMonoText (g, t, rr, 12.0f, J::centred, Palette::ink, filled);
        };
        for (int i = 0; i < 2; ++i)
            if (altTonic[i] >= 0)
            {
                const auto ab = altBounds (i);
                g.setColour (Palette::pencil);
                g.drawRoundedRectangle (ab, 14.0f, 0.7f);
                drawMonoText (g, "or  " + keyName (altTonic[i], altMode[i]), ab, 11.0f, J::centred, Palette::graphite);
            }
        pill (rescanBounds(), "rescan", false);
        pill (continueBounds(), "open fr(a)me  >", true);
        // petite jauge de l'ouverture automatique
        const auto cb = continueBounds();
        g.setColour (Palette::accentInk);
        g.fillRect (cb.getX() + 17.0f, cb.getBottom() + 6.0f, (cb.getWidth() - 34.0f) * juce::jlimit (0.0f, 1.0f, (float) doneTicks / 150.0f), 1.5f);
    }
    else
    {
        // « skip scan » : bien visible ; entre dans la planche, effet actif.
        const auto sb = skipBounds();
        g.setColour (hoverSkip ? Palette::accent : Palette::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (sb, sb.getHeight() * 0.5f);
        g.setColour (Palette::ink);
        g.drawRoundedRectangle (sb, sb.getHeight() * 0.5f, 1.1f);
        drawMonoText (g, "back to fr(a)me  >", sb, 13.0f, J::centred, Palette::ink, true, 0.08f);
    }
}
