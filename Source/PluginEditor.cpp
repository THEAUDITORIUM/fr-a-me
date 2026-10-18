/*
  ==============================================================================
    fr(a)me — PluginEditor.cpp
  ==============================================================================
*/

#include "PluginEditor.h"
#include "Scale.h"

//==============================================================================
//  FrameCanvas
//==============================================================================
FrameCanvas::FrameCanvas (FrameAudioProcessor& p)
    : processor (p),
      eqView     (p.apvts),
      modeToggle (*p.apvts.getParameter (ParamIDs::mode)),
      noteRow    (*p.apvts.getParameter (ParamIDs::note))
{
    Palette::apply (Palette::fromName (p.getTheme()));

    addAndMakeVisible (inputBand);
    addAndMakeVisible (outputBand);
    addAndMakeVisible (eqView);
    addAndMakeVisible (partialBand);

    setupKnob (harmoKnob,  "harmo",  100.0, knobArea,   harmoAttachment,  ParamIDs::harmo);
    setupKnob (reverbKnob, "reverb",  30.0, reverbArea, reverbAttachment, ParamIDs::reverb);
    setupKnob (gainKnob,   "gain",     0.0, gainArea,   gainAttachment,   ParamIDs::gain);
    gainKnob.setMouseDragSensitivity (300);
    setupKnob (pitchKnob,  "pitch",    0.0, pitchArea,  pitchAttachment,  ParamIDs::pitch);
    pitchKnob.setMouseDragSensitivity (300);

    addAndMakeVisible (modeToggle);
    addAndMakeVisible (noteRow);

    setSize (designWidth, designHeight);
    refresh();
}

void FrameCanvas::setupKnob (juce::Slider& knob, const juce::String& name, double resetValue,
                             juce::Rectangle<int> area,
                             std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                             const char* paramID)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    knob.setDoubleClickReturnValue (true, resetValue);
    knob.setMouseDragSensitivity (240);
    knob.setTitle (name);
    knob.onValueChange = [this, area] { repaint (area.expanded (50, 30)); };
    addAndMakeVisible (knob);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramID, knob);
}

void FrameCanvas::resized()
{
    inputBand.setBounds    (inputArea.toNearestInt());
    outputBand.setBounds   (outputArea.toNearestInt());
    eqView.setBounds       (spectrumArea.toNearestInt());
    partialBand.setBounds  (partialArea.toNearestInt());
    harmoKnob.setBounds  (knobArea);
    pitchKnob.setBounds  (pitchArea);
    reverbKnob.setBounds (reverbArea);
    gainKnob.setBounds   (gainArea);
    modeToggle.setBounds (modeArea);
    noteRow.setBounds   (noteArea);
}

void FrameCanvas::applyTheme (Theme t)
{
    Palette::apply (t);
    processor.setTheme (Palette::toName (t));
    if (onThemeChanged) onThemeChanged();
    repaint();
    for (auto* child : getChildren())
        child->repaint();
}

void FrameCanvas::mouseUp (const juce::MouseEvent& e)
{
    for (auto t : { Theme::garden, Theme::floral })
        if (themeIconArea (t).expanded (3.0f).contains (e.position) && t != Palette::current)
        {
            applyTheme (t);
            return;
        }
}

void FrameCanvas::mouseMove (const juce::MouseEvent& e)
{
    bool over = false;
    for (auto t : { Theme::garden, Theme::floral })
        over = over || themeIconArea (t).expanded (3.0f).contains (e.position);
    setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void FrameCanvas::refresh()
{
    // 1. Formes d'onde
    WaveSlice slice;
    int guard = 0;
    while (guard++ < 4096 && processor.getWaveQueue().pop (slice))
    {
        inputBand.push (slice);
        outputBand.push (slice);
    }

    // 2. Trames spectrales
    SpectralFrameInfo frameInfo;
    guard = 0;
    while (guard++ < 64 && processor.getSpectralQueue().pop (frameInfo))
    {
        eqView.pushSpectrum (frameInfo.spectrum, SpectralFrameInfo::numSpectrumRows,
                             SpectralEngine::displayMinHz, SpectralEngine::displayMaxHz);
        partialBand.push (frameInfo);
    }

    // 3. Gamme courante (surlignage du spectrogramme, points sous les notes)
    const uint16_t mask = processor.getCurrentScaleMask();
    eqView.setScale (mask, noteRow.getNote());
    noteRow.setScaleMask (mask);
    if (mask != shownMask)
    {
        shownMask = mask;
        repaint();          // pied de page, étiquettes de fréquence, rappels
    }

    inputBand.repaint();
    outputBand.repaint();
    eqView.repaint();
    partialBand.repaint();
}

//==============================================================================
void FrameCanvas::paint (juce::Graphics& g)
{
    using J = juce::Justification;
    g.fillAll (Palette::paper);
    if (Palette::current == Theme::garden)
        drawGardenDecor (g, getLocalBounds().toFloat());
    else
        drawFloralDecor (g, getLocalBounds().toFloat());

    const float left = inputArea.getX(), right = inputArea.getRight();

    // ---- En-tête -------------------------------------------------------------
    drawMonoText (g, "fr(a)me", { 0.0f, 16.0f, (float) designWidth, 34.0f }, 28.0f, J::centred, Palette::ink, true, 0.28f);
    drawMonoText (g, "( spectral harmoniser )", { 0.0f, 50.0f, (float) designWidth, 16.0f }, 11.0f, J::centred, Palette::graphite);
    drawMonoText (g, "plate 01", { left, 22.0f, 200.0f, 14.0f }, 10.0f, J::centredLeft, Palette::pencil);
    // Thèmes : petit arbre (garden, défaut) à gauche, fleur (floral) à droite.
    for (auto t : { Theme::garden, Theme::floral })
        drawThemeIcon (g, t, themeIconArea (t), t == Palette::current);
    drawMonoText (g, Palette::toName (Palette::current), { 684.0f, 46.0f, 96.0f, 12.0f }, 8.5f, J::centred, Palette::graphite);

    // Petits boutons pitch et gain (en haut à droite) : nom et valeur dessous.
    {
        const auto pr = pitchArea.toFloat();
        const int st = juce::roundToInt (pitchKnob.getValue());
        drawMonoText (g, "pitch " + juce::String (st > 0 ? "+" : "") + juce::String (st) + " st",
                      { pr.getCentreX() - 45.0f, pr.getBottom() + 2.0f, 90.0f, 12.0f }, 8.5f, J::centred, Palette::ink);
        const auto gr = gainArea.toFloat();
        const float gv = (float) gainKnob.getValue();
        drawMonoText (g, "gain " + juce::String (gv > 0.05f ? "+" : "") + juce::String (gv, 1) + " db",
                      { gr.getCentreX() - 45.0f, gr.getBottom() + 2.0f, 90.0f, 12.0f }, 8.5f, J::centred, Palette::ink);
    }

    drawTimeAxis (g, { left, 92.0f, right - left, 1.0f });

    // ---- Étiquettes des bandes (façon « sound sections ») ---------------------
    auto bandLabel = [&] (const char* text, juce::Rectangle<float> band)
    {
        drawMonoText (g, text, { band.getX(), band.getY() - 16.0f, 300.0f, 13.0f }, 11.5f, J::centredLeft, Palette::ink);
    };
    bandLabel ("input",    inputArea);
    bandLabel ("output",   outputArea);
    bandLabel ("spectrum", spectrumArea);
    bandLabel ("partials", partialArea);

    // Échelles à droite des bandes
    auto sideNote = [&] (const juce::String& text, float y)
    {
        drawMonoText (g, text, { right + 8.0f, y - 6.0f, 70.0f, 12.0f }, 9.0f, J::centredLeft, Palette::graphite);
    };
    sideNote ("+1.0", inputArea.getY() + 4.0f);
    sideNote ("-1.0", inputArea.getBottom() - 4.0f);
    sideNote ("+1.0", outputArea.getY() + 4.0f);
    sideNote ("-1.0", outputArea.getBottom() - 4.0f);
    sideNote ("+24 db", spectrumArea.getY() + 10.0f);
    sideNote ("0 db",   spectrumArea.getCentreY());
    sideNote ("-24 db", spectrumArea.getBottom() - 10.0f);
    sideNote ("12 khz", partialArea.getY() + 4.0f);
    sideNote ("40 hz",  partialArea.getBottom() - 4.0f);


    // ---- Rappels annotés sur les bandes (façon « highway / tree ») ------------
    drawCallout (g, "dry signal", { right - 210.0f, inputArea.getY() - 8.0f },
                 { right - 120.0f, inputArea.getCentreY() - 10.0f }, true);
    drawCallout (g, "in key", { right - 190.0f, outputArea.getY() - 8.0f },
                 { right - 120.0f, outputArea.getCentreY() - 10.0f }, true);

    {
        // Le rappel « scale frequencies » vise la ligne de la tonique la plus
        // proche de 440 Hz : il suit la tonalité choisie.
        const int tonic = juce::jlimit (0, 11, noteRow.getNote());
        double midi = 60.0 + tonic;
        if (midi < 64.0) midi += 12.0;
        // Même échelle que l'EQ (20 Hz → 20 kHz, marge de 8 unités de chaque côté).
        const float t = std::log ((float) frame::midiToHz (midi) / ProEq::minHz) / std::log (ProEq::maxHz / ProEq::minHz);
        const float x = spectrumArea.getX() + 8.0f + t * (spectrumArea.getWidth() - 16.0f);
        drawCallout (g, "scale frequencies", { x - 190.0f, spectrumArea.getY() - 8.0f },
                     { x, spectrumArea.getY() }, true);
    }

    // Légende de la partition
    {
        const float y = partialArea.getY() - 9.5f;
        g.setColour (Palette::ink);
        g.drawEllipse (right - 214.0f, y - 3.0f, 6.0f, 6.0f, 0.8f);
        drawMonoText (g, "detected", { right - 202.0f, y - 6.0f, 80.0f, 12.0f }, 9.5f, J::centredLeft, Palette::graphite);
        g.setColour (Palette::accentInk);
        g.fillEllipse (right - 104.0f, y - 3.0f, 6.0f, 6.0f);
        drawMonoText (g, "snapped", { right - 92.0f, y - 6.0f, 80.0f, 12.0f }, 9.5f, J::centredLeft, Palette::graphite);
    }

    // ---- Zone des contrôles ---------------------------------------------------
    g.setColour (Palette::pencil);
    for (float x = left; x < right; x += 6.0f)                 // ligne pointillée de séparation
        g.drawHorizontalLine (596, x, juce::jmin (x + 2.0f, right));

    const auto knobC = knobArea.toFloat().getCentre();
    const auto reverbC = reverbArea.toFloat().getCentre();
    drawCallout (g, "harmo", { left, 616.0f }, { knobC.x, (float) knobArea.getY() + 2.0f }, true);
    drawCallout (g, "reverb", { 236.0f, 616.0f }, { reverbC.x, (float) reverbArea.getY() + 2.0f }, true);
    drawCallout (g, "mode", { 420.0f, 616.0f }, { (float) modeArea.getCentreX(), (float) modeArea.getY() - 2.0f }, true);
    drawCallout (g, "tonic", { 630.0f, 616.0f },
                 { (float) noteArea.getX() + noteArea.getWidth() * ((float) noteRow.getNote() + 0.5f) / 12.0f,
                   (float) noteArea.getY() - 2.0f }, true);

    // Valeur du MIX sous le potentiomètre
    const auto mixText = juce::String (juce::roundToInt (harmoKnob.getValue())) + " %";
    drawMonoText (g, mixText, { knobC.x - 55.0f, (float) knobArea.getBottom() + 2.0f, 110.0f, 16.0f },
                  12.0f, J::centred, Palette::ink, true);
    drawMonoText (g, juce::String (juce::roundToInt (reverbKnob.getValue())) + " %",
                  { reverbC.x - 55.0f, (float) reverbArea.getBottom() + 2.0f, 110.0f, 16.0f },
                  12.0f, J::centred, Palette::ink, true);
}

void FrameCanvas::drawTimeAxis (juce::Graphics& g, juce::Rectangle<float> span)
{
    // Axe temporel commun aux bandes : « maintenant » à droite.
    const double sr = processor.getCurrentSampleRate();
    const double seconds = (double) WaveBand::historySize * (processor.getAnalysisFftSize() / 8.0) / sr;

    g.setColour (Palette::ink);
    g.drawHorizontalLine ((int) span.getY(), span.getX(), span.getRight());

    const int whole = (int) std::floor (seconds);
    for (int s = 0; s <= whole; ++s)
    {
        const float x = span.getRight() - (float) (s / seconds) * span.getWidth();
        g.drawVerticalLine ((int) x, span.getY() - 4.0f, span.getY() + 1.0f);
        drawMonoText (g, s == 0 ? "now" : juce::String (-s) + " s", { x - 30.0f, span.getY() - 17.0f, 60.0f, 11.0f },
                      8.5f, juce::Justification::centred, Palette::graphite);

        // demi-secondes
        const float xh = span.getRight() - (float) ((s + 0.5) / seconds) * span.getWidth();
        if (xh > span.getX())
            g.drawVerticalLine ((int) xh, span.getY() - 2.0f, span.getY() + 1.0f);
    }
}

//==============================================================================
//  FrameAudioProcessorEditor
//==============================================================================
FrameAudioProcessorEditor::FrameAudioProcessorEditor (FrameAudioProcessor& p)
    : AudioProcessorEditor (&p), canvas (p)
{
    lookAndFeel.refreshColours();                // thème du projet (appliqué par le canevas)
    setLookAndFeel (&lookAndFeel);
    canvas.onThemeChanged = [this] { lookAndFeel.refreshColours(); repaint(); };
    addAndMakeVisible (canvas);

    // Redimensionnable, proportions de la planche conservées.
    setResizable (true, true);
    const double ratio = (double) FrameCanvas::designWidth / FrameCanvas::designHeight;
    getConstrainer()->setFixedAspectRatio (ratio);
    setResizeLimits (520, (int) (520 / ratio), 1500, (int) (1500 / ratio));
    setSize (720, (int) (720 / ratio));          // fenêtre plus compacte par défaut

    startTimerHz (30);
}

FrameAudioProcessorEditor::~FrameAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void FrameAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::paper);
}

void FrameAudioProcessorEditor::resized()
{
    // Toute la planche est mise à l'échelle : rendu vectoriel net à toute taille.
    const float scale = (float) getWidth() / (float) FrameCanvas::designWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));
    canvas.setBounds (0, 0, FrameCanvas::designWidth, FrameCanvas::designHeight);
}
