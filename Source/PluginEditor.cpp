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
      gatePanel  (p.apvts, [&p] (int index) { p.loadGatePreset (index); }),
      scanPage   (p),
      noteRow    (*p.apvts.getParameter (ParamIDs::note))
{
    addAndMakeVisible (inputBand);
    addAndMakeVisible (outputBand);
    addAndMakeVisible (eqView);
    addAndMakeVisible (partialBand);

    setupKnob (harmoKnob,  "harmo",  100.0, knobArea,   harmoAttachment,  ParamIDs::harmo);
    setupKnob (reverbKnob, "reverb",  30.0, reverbArea, reverbAttachment, ParamIDs::reverb);
    setupKnob (gainKnob,   "gain",     0.0, gainArea,   gainAttachment,   ParamIDs::gain);
    gainKnob.setMouseDragSensitivity (300);
    addAndMakeVisible (gatePanel);

    addAndMakeVisible (modeToggle);
    addAndMakeVisible (noteRow);
    addAndMakeVisible (scanButton);
    scanButton.onClick = [this] { scanPage.open(); };

    // Page de scan : seulement à la demande (bouton scan key), plus au lancement.
    addChildComponent (scanPage);
    scanPage.onClose = [this] { repaint(); };

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
    inputBand.setBounds    (soundArea.toNearestInt());     // superposées
    outputBand.setBounds   (soundArea.toNearestInt());
    eqView.setBounds       (spectrumArea.toNearestInt());
    partialBand.setBounds  (partialArea.toNearestInt());
    harmoKnob.setBounds  (knobArea);
    gatePanel.setBounds  (gateArea);
    reverbKnob.setBounds (reverbArea);
    gainKnob.setBounds   (gainArea);
    modeToggle.setBounds (modeArea);
    scanButton.setBounds (scanArea);
    noteRow.setBounds   (noteArea);
    scanPage.setBounds  (getLocalBounds());
}

void FrameCanvas::refresh()
{
    scanPage.update();

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
    gatePanel.setStep (processor.getGateStep());

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
    drawGardenDecor (g, getLocalBounds().toFloat());

    const float left = soundArea.getX(), right = soundArea.getRight();

    // ---- En-tête -------------------------------------------------------------
    drawMonoText (g, "fr(a)me", { 0.0f, 16.0f, (float) designWidth, 34.0f }, 28.0f, J::centred, Palette::ink, true, 0.28f);
    drawMonoText (g, "( spectral harmoniser )", { 0.0f, 50.0f, (float) designWidth, 16.0f }, 11.0f, J::centred, Palette::graphite);
    drawMonoText (g, "plate 01", { left, 22.0f, 200.0f, 14.0f }, 10.0f, J::centredLeft, Palette::pencil);
    // Petit bouton gain (en haut à droite) : nom et valeur dessous.
    {
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
    bandLabel ("sound",    soundArea);
    bandLabel ("spectrum equalizer", spectrumArea);
    bandLabel ("partials", partialArea);

    // Petites légendes à droite des étiquettes (pastille + mot).
    auto legend = [&] (juce::Rectangle<float> band, float x, const char* text, juce::Colour c, bool filled)
    {
        const float y = band.getY() - 9.5f;
        g.setColour (c);
        if (filled) g.fillEllipse (x, y - 3.0f, 6.0f, 6.0f);
        else        g.drawEllipse (x, y - 3.0f, 6.0f, 6.0f, 0.8f);
        drawMonoText (g, text, { x + 12.0f, y - 6.0f, 90.0f, 12.0f }, 9.5f, J::centredLeft, Palette::graphite);
    };
    legend (soundArea,   right - 214.0f, "dry",      Palette::ink.withAlpha (0.35f), true);
    legend (soundArea,   right - 104.0f, "in key",   Palette::accent, true);
    legend (partialArea, right - 214.0f, "detected", Palette::ink, false);
    legend (partialArea, right - 104.0f, "snapped",  Palette::accentInk, true);

    // ---- Zone des contrôles ---------------------------------------------------
    g.setColour (Palette::pencil);
    for (float x = left; x < right; x += 6.0f)                 // ligne pointillée de séparation
        g.drawHorizontalLine ((int) separatorY, x, juce::jmin (x + 2.0f, right));

    const auto knobC = knobArea.toFloat().getCentre();
    const auto reverbC = reverbArea.toFloat().getCentre();
    drawCallout (g, "harmo",  { left, calloutY },   { knobC.x,   (float) knobArea.getY() + 2.0f }, true);
    drawCallout (g, "reverb", { 228.0f, calloutY }, { reverbC.x, (float) reverbArea.getY() + 2.0f }, true);
    drawCallout (g, "gate",   { 400.0f, calloutY }, { (float) gateArea.getCentreX(), (float) gateArea.getY() - 2.0f }, true);
    drawCallout (g, "mode",   { 672.0f, calloutY }, { (float) modeArea.getCentreX(), (float) modeArea.getY() - 2.0f }, true);
    drawCallout (g, "tonic",  { 672.0f, (float) modeArea.getBottom() + 8.0f },
                 { (float) noteArea.getX() + noteArea.getWidth() * ((float) noteRow.getNote() + 0.5f) / 12.0f,
                   (float) noteArea.getY() - 2.0f }, true);

    // Valeurs sous les trois gros boutons.
    auto value = [&] (const juce::String& t, juce::Point<float> c, const juce::Rectangle<int>& area)
    {
        drawMonoText (g, t, { c.x - 55.0f, (float) area.getBottom() + 2.0f, 110.0f, 16.0f }, 12.0f, J::centred, Palette::ink, true);
    };
    value (juce::String (juce::roundToInt (harmoKnob.getValue())) + " %", knobC, knobArea);
    value (juce::String (juce::roundToInt (reverbKnob.getValue())) + " %", reverbC, reverbArea);
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
    setLookAndFeel (&lookAndFeel);
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
