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
      modeToggle (*p.apvts.getParameter (ParamIDs::mode)),
      noteRow    (*p.apvts.getParameter (ParamIDs::note))
{
    addAndMakeVisible (inputBand);
    addAndMakeVisible (outputBand);
    addAndMakeVisible (spectrumBand);
    addAndMakeVisible (partialBand);

    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    mixKnob.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    mixKnob.setDoubleClickReturnValue (true, 100.0);
    mixKnob.setMouseDragSensitivity (220);
    mixKnob.setTitle ("mix");
    mixKnob.onValueChange = [this] { repaint (knobArea.expanded (40, 30)); };
    addAndMakeVisible (mixKnob);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.apvts, ParamIDs::mix, mixKnob);

    colorKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    colorKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    colorKnob.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    colorKnob.setDoubleClickReturnValue (true, 100.0);
    colorKnob.setMouseDragSensitivity (260);
    colorKnob.setTitle ("color");
    colorKnob.onValueChange = [this] { repaint (colorArea.expanded (40, 30)); };
    addAndMakeVisible (colorKnob);
    colorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.apvts, ParamIDs::color, colorKnob);

    addAndMakeVisible (modeToggle);
    addAndMakeVisible (noteRow);

    setSize (designWidth, designHeight);
    refresh();
}

void FrameCanvas::resized()
{
    inputBand.setBounds    (inputArea.toNearestInt());
    outputBand.setBounds   (outputArea.toNearestInt());
    spectrumBand.setBounds (spectrumArea.toNearestInt());
    partialBand.setBounds  (partialArea.toNearestInt());
    mixKnob.setBounds   (knobArea);
    colorKnob.setBounds (colorArea);
    modeToggle.setBounds (modeArea);
    noteRow.setBounds   (noteArea);
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
        spectrumBand.push (frameInfo);
        partialBand.push (frameInfo);
    }

    // 3. Gamme courante (surlignage du spectrogramme, points sous les notes)
    const uint16_t mask = processor.getCurrentScaleMask();
    spectrumBand.setScaleMask (mask);
    noteRow.setScaleMask (mask);
    if (mask != shownMask)
    {
        shownMask = mask;
        repaint();          // pied de page, étiquettes de fréquence, rappels
    }

    inputBand.repaint();
    outputBand.repaint();
    spectrumBand.repaint();
    partialBand.repaint();
}

//==============================================================================
void FrameCanvas::paint (juce::Graphics& g)
{
    using J = juce::Justification;
    g.fillAll (Palette::paper);

    const float left = inputArea.getX(), right = inputArea.getRight();

    // ---- En-tête -------------------------------------------------------------
    drawMonoText (g, "fr(a)me", { 0.0f, 16.0f, (float) designWidth, 34.0f }, 28.0f, J::centred, Palette::ink, true, 0.28f);
    drawMonoText (g, "( spectral harmoniser )", { 0.0f, 50.0f, (float) designWidth, 16.0f }, 11.0f, J::centred, Palette::graphite);
    drawMonoText (g, "plate 01", { left, 22.0f, 200.0f, 14.0f }, 10.0f, J::centredLeft, Palette::pencil);
    drawMonoText (g, "a4 = 440 hz", { right - 200.0f, 22.0f, 200.0f, 14.0f }, 10.0f, J::centredRight, Palette::pencil);

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
    sideNote ("12 khz", spectrumArea.getY() + 4.0f);
    sideNote ("40 hz",  spectrumArea.getBottom() - 4.0f);
    sideNote ("12 khz", partialArea.getY() + 4.0f);
    sideNote ("40 hz",  partialArea.getBottom() - 4.0f);

    drawFrequencyLabels (g, spectrumArea);

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
        const float t = FreqAxis::toNorm ((float) frame::midiToHz (midi));
        const float y = spectrumArea.getBottom() - t * spectrumArea.getHeight();
        drawCallout (g, "scale frequencies", { right - 280.0f, spectrumArea.getY() - 8.0f },
                     { right - 110.0f, y }, true);
        g.setColour (Palette::ink);
        g.fillEllipse (right - 112.0f, y - 2.0f, 4.0f, 4.0f);
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
    const auto colorC = colorArea.toFloat().getCentre();
    drawCallout (g, "mix  -  dry / wet", { left, 616.0f }, { knobC.x, (float) knobArea.getY() + 2.0f }, true);
    drawCallout (g, "color", { 240.0f, 616.0f }, { colorC.x, (float) colorArea.getY() + 2.0f }, true);
    drawCallout (g, "mode", { 382.0f, 616.0f }, { (float) modeArea.getCentreX(), (float) modeArea.getY() - 2.0f }, true);
    drawCallout (g, "tonic", { 620.0f, 616.0f },
                 { (float) noteArea.getX() + noteArea.getWidth() * ((float) noteRow.getNote() + 0.5f) / 12.0f,
                   (float) noteArea.getY() - 2.0f }, true);

    // Valeur du MIX sous le potentiomètre
    const auto mixText = juce::String (juce::roundToInt (mixKnob.getValue())) + " %";
    drawMonoText (g, mixText, { knobC.x - 60.0f, (float) knobArea.getBottom() + 2.0f, 120.0f, 16.0f },
                  12.0f, J::centred, Palette::ink, true);

    // Valeur du COLOR ; au-delà de 100 % la queue de résonance s'active.
    const int colorValue = juce::roundToInt (colorKnob.getValue());
    drawMonoText (g, juce::String (colorValue) + " %" + (colorValue > 100 ? "  + tail" : ""),
                  { colorC.x - 70.0f, (float) colorArea.getBottom() + 2.0f, 140.0f, 16.0f },
                  12.0f, J::centred, Palette::ink, true);

    // ---- Pied de page : récapitulatif « rapport scientifique » ----------------
    const int note = juce::jlimit (0, 11, noteRow.getNote());
    const juce::String scaleName = juce::String (frame::noteNames[note]) + (modeToggle.getMode() == 0 ? " major" : " minor");
    const int latency = processor.getLatencyInSamples();
    const double sr = processor.getCurrentSampleRate();

    g.setColour (Palette::faint);
    g.drawHorizontalLine (728, left, right);
    drawMonoText (g, "scale : " + scaleName, { left, 734.0f, 300.0f, 14.0f }, 9.5f, J::centredLeft, Palette::graphite);
    drawMonoText (g, "stft " + juce::String (latency) + " / hann / 75 % overlap",
                  { 0.0f, 734.0f, (float) designWidth, 14.0f }, 9.5f, J::centred, Palette::graphite);
    drawMonoText (g, "latency : " + juce::String (latency) + " smp  (" + juce::String (1000.0 * latency / sr, 1) + " ms)",
                  { right - 300.0f, 734.0f, 300.0f, 14.0f }, 9.5f, J::centredRight, Palette::graphite);
}

void FrameCanvas::drawTimeAxis (juce::Graphics& g, juce::Rectangle<float> span)
{
    // Axe temporel commun aux bandes : « maintenant » à droite.
    const double sr = processor.getCurrentSampleRate();
    const double seconds = (double) WaveBand::historySize * (processor.getLatencyInSamples() / 8.0) / sr;

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

void FrameCanvas::drawFrequencyLabels (juce::Graphics& g, juce::Rectangle<float> band)
{
    // À gauche du spectrogramme : la tonique de chaque octave, petit trait jaune.
    const int tonic = juce::jlimit (0, 11, noteRow.getNote());
    for (int octave = 1; octave <= 9; ++octave)
    {
        const double midi = 12.0 * (octave + 1) + tonic;
        const float hz = (float) frame::midiToHz (midi);
        if (hz < SpectralEngine::displayMinHz || hz > SpectralEngine::displayMaxHz)
            continue;

        const float y = band.getBottom() - FreqAxis::toNorm (hz) * band.getHeight();
        g.setColour (Palette::accentInk);
        g.drawLine (band.getX() - 10.0f, y, band.getX() - 2.0f, y, 1.2f);
        drawMonoText (g, juce::String (frame::noteNames[tonic]) + juce::String (octave),
                      { band.getX() - 52.0f, y - 6.0f, 38.0f, 12.0f }, 9.0f,
                      juce::Justification::centredRight, Palette::graphite);
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
    setResizeLimits (650, (int) (650 / ratio), 1700, (int) (1700 / ratio));
    setSize (920, (int) (920 / ratio));

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
