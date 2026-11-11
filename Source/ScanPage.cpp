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
    return { pageW * 0.5f - 115.0f, 236.0f, 230.0f, 230.0f };
}

juce::Rectangle<float> ScanPage::altBounds (int i) const noexcept { return { pageW * 0.5f - 200.0f + 210.0f * (float) i, 568.0f, 190.0f, 38.0f }; }
juce::Rectangle<float> ScanPage::rescanBounds() const noexcept   { return { pageW * 0.5f - 200.0f, 630.0f, 190.0f, 44.0f }; }
juce::Rectangle<float> ScanPage::continueBounds() const noexcept { return { pageW * 0.5f + 10.0f, 630.0f, 190.0f, 44.0f }; }
juce::Rectangle<float> ScanPage::skipBounds() const noexcept     { return { pageW * 0.5f - 105.0f, 640.0f, 210.0f, 46.0f }; }

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
    const auto b = buttonBounds();
    const auto c = b.getCentre();
    const float r = b.getWidth() * 0.5f;

    // Couche fixe : matière blanche, nom, encoches pressées autour du disque.
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! staticLayer.isValid() || std::abs (scale - staticScale) > 0.01f)
    {
        staticScale = scale;
        staticLayer = juce::Image (juce::Image::ARGB, juce::roundToInt (page.getWidth() * scale), juce::roundToInt (page.getHeight() * scale), true);
        juce::Graphics sg (staticLayer);
        sg.addTransform (juce::AffineTransform::scale (scale));
        drawPureBackground (sg, page);
        drawMonoText (sg, "fr(a)me", { 0.0f, 56.0f, pageW, 46.0f }, 30.0f, J::centred, Palette::ink, false, 0.62f);
        for (int i = 0; i < 12; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 12.0f;
            juce::Path notch;
            notch.addRoundedRectangle (-4.0f, -(r + 44.0f), 8.0f, 22.0f, 4.0f);
            notch.applyTransform (juce::AffineTransform::rotation (a).translated (c.x, c.y));
            Relief::inset (sg, notch, 3.0f);
        }
        // socle creusé dans lequel le disque est posé
        Relief::insetEllipse (sg, b.expanded (12.0f), 7.0f);
    }
    g.drawImage (staticLayer, page);

    // Le disque : en saillie au repos, enfoncé une fois la tonalité trouvée.
    const bool pressed = state == State::done || (state == State::idle && hoverButton);
    Relief::raisedEllipse (g, b, pressed ? 6.0f : 12.0f, pressed);

    if (state == State::listening)
    {
        // anneau creusé dans le disque, la progression s'y dessine
        const float ringR = r - 22.0f;
        juce::Path ring;
        ring.addEllipse (c.x - ringR, c.y - ringR, ringR * 2.0f, ringR * 2.0f);
        juce::Path inner;
        inner.addEllipse (c.x - ringR + 10.0f, c.y - ringR + 10.0f, (ringR - 10.0f) * 2.0f, (ringR - 10.0f) * 2.0f);
        ring.addPath (inner);
        ring.setUsingNonZeroWinding (false);
        Relief::inset (g, ring, 3.0f);
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ringR - 5.0f, ringR - 5.0f, 0.0f, 0.0f,
                           juce::MathConstants<float>::twoPi * juce::jmax (0.002f, progress), true);
        g.setColour (Palette::accentInk.withAlpha (0.75f + 0.25f * std::sin (pulse * 2.0f)));
        g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (state == State::idle)
    {
        drawMonoText (g, "scan", b.withTrimmedBottom (14.0f), 30.0f, J::centred, Palette::ink, false, 0.6f);
        drawMonoText (g, "key", b.withTrimmedTop (70.0f), 12.0f, J::centred, Palette::pencil, false, 0.5f);
    }
    else if (state == State::listening)
    {
        drawMonoText (g, "listening", b.withTrimmedBottom (26.0f), 15.0f, J::centred, Palette::graphite, false, 0.45f);
        drawMonoText (g, juce::String (juce::roundToInt (progress * 100.0f)) + " %", b.withTrimmedTop (34.0f), 22.0f, J::centred, Palette::ink, false, 0.2f);
    }
    else
    {
        drawMonoText (g, keyName (tonic, mode), b.withTrimmedBottom (16.0f), 22.0f, J::centred, Palette::ink, false, 0.32f);
        drawMonoText (g, "applied", b.withTrimmedTop (64.0f), 11.0f, J::centred, Palette::pencil, false, 0.5f);
    }

    // Texte sous le disque (écoute / résultat seulement).
    juce::String line1, line2;
    if (state == State::listening)
    {
        line1 = (progress <= 0.0f && listeningTicks > 60) ? "no sound yet - press play in your daw" : "keep playing";
        line2 = tonic >= 0 ? keyName (tonic, mode) : juce::String ("-");
    }
    else if (state == State::done)
        line1 = "not quite ?  try :";
    drawMonoText (g, line1, { 0.0f, 516.0f, pageW, 20.0f }, 12.0f, J::centred, Palette::graphite, false, 0.36f);
    drawMonoText (g, line2, { 0.0f, 540.0f, pageW, 18.0f }, 12.0f, J::centred, Palette::pencil, false, 0.3f);

    auto pill = [&] (juce::Rectangle<float> rr, const juce::String& t, bool hot)
    {
        Relief::raised (g, rr, rr.getHeight() * 0.5f, 4.0f, hot);
        drawMonoText (g, t, rr, 11.5f, J::centred, Palette::ink, false, 0.32f);
    };
    if (state == State::done)
    {
        for (int i = 0; i < 2; ++i)
            if (altTonic[i] >= 0)
                pill (altBounds (i), keyName (altTonic[i], altMode[i]), false);
        pill (rescanBounds(), "rescan", false);
        pill (continueBounds(), "open fr(a)me", false);
        const auto cb = continueBounds();
        g.setColour (Palette::accentInk.withAlpha (0.6f));
        g.fillRoundedRectangle (cb.getX() + 18.0f, cb.getBottom() + 9.0f,
                                (cb.getWidth() - 36.0f) * juce::jlimit (0.0f, 1.0f, (float) doneTicks / 150.0f), 2.0f, 1.0f);
    }
    else
        pill (skipBounds(), "back to fr(a)me", hoverSkip);
}
