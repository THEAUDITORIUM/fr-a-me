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
    knob.onValueChange = [this, area] { repaint (area.expanded (40, 50)); };
    addAndMakeVisible (knob);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramID, knob);
}

void FrameCanvas::resized()
{
    inputBand.setBounds    (soundArea.toNearestInt());     // superposées
    outputBand.setBounds   (soundArea.toNearestInt());
    eqView.setBounds       (spectrumArea.toNearestInt());
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
}

//==============================================================================
void FrameCanvas::paint (juce::Graphics& g)
{
    using J = juce::Justification;
    g.fillAll (Palette::paper);
    drawGardenDecor (g, getLocalBounds().toFloat(), false);   // fond garden allégé

    const float left = soundArea.getX(), right = soundArea.getRight();

    // ---- En-tête : titre à gauche, scan key et gain à droite --------------------
    drawMonoText (g, "fr(a)me", { left, 24.0f, 260.0f, 40.0f }, 34.0f, J::centredLeft, Palette::ink, true, 0.22f);
    {
        const auto gr = gainArea.toFloat();
        const float gv = (float) gainKnob.getValue();
        drawMonoText (g, "GAIN", { gr.getRight() + 8.0f, gr.getY() + 6.0f, 70.0f, 18.0f }, 14.0f, J::centredLeft, Palette::ink, true, 0.08f);
        drawMonoText (g, juce::String (gv > 0.05f ? "+" : "") + juce::String (gv, 1) + " db",
                      { gr.getRight() + 8.0f, gr.getY() + 26.0f, 70.0f, 16.0f }, 12.5f, J::centredLeft, Palette::graphite);
    }

    // ---- Bandes : titre bien lisible, légende simple ---------------------------
    auto bandTitle = [&] (const char* text, juce::Rectangle<float> band)
    {
        drawMonoText (g, text, { band.getX(), band.getY() - 26.0f, 400.0f, 20.0f }, 15.0f, J::centredLeft, Palette::ink, true, 0.08f);
    };
    bandTitle ("SOUND", soundArea);
    bandTitle ("SPECTRUM EQUALIZER", spectrumArea);

    auto legend = [&] (float x, float y, const char* text, juce::Colour c)
    {
        g.setColour (c);
        g.fillEllipse (x, y - 4.5f, 9.0f, 9.0f);
        drawMonoText (g, text, { x + 15.0f, y - 8.0f, 100.0f, 16.0f }, 12.5f, J::centredLeft, Palette::graphite);
    };
    legend (right - 220.0f, soundArea.getY() - 16.0f, "dry", Palette::ink.withAlpha (0.35f));
    legend (right - 110.0f, soundArea.getY() - 16.0f, "in key", Palette::accent);

    // ---- Cartes de contrôles ------------------------------------------------------
    auto card = [&] (juce::Rectangle<float> r, const char* title)
    {
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (Palette::faint.darker (0.08f));
        g.drawRoundedRectangle (r, 14.0f, 1.2f);
        g.setColour (Palette::accent);
        g.fillEllipse (r.getX() + 16.0f, r.getY() + 21.0f, 8.0f, 8.0f);
        drawMonoText (g, title, { r.getX() + 32.0f, r.getY() + 14.0f, r.getWidth() - 40.0f, 22.0f }, 17.0f,
                      J::centredLeft, Palette::ink, true, 0.1f);
    };
    card (harmoCard,  "HARMO");
    card (reverbCard, "REVERB");
    card (gateCard,   "GATE");
    card (keyCard,    "KEY");

    // Valeurs des deux gros boutons, en gros.
    auto value = [&] (const juce::String& t, juce::Rectangle<float> c)
    {
        drawMonoText (g, t, { c.getX(), c.getBottom() - 62.0f, c.getWidth(), 26.0f }, 20.0f, J::centred, Palette::ink, true);
    };
    value (juce::String (juce::roundToInt (harmoKnob.getValue())) + " %", harmoCard);
    value (juce::String (juce::roundToInt (reverbKnob.getValue())) + " %", reverbCard);

    // Carte KEY : sous-titre de la tonique, et la tonalité en clair.
    drawMonoText (g, "tonic", { keyCard.getX() + 16.0f, (float) noteArea.getY() - 24.0f, 120.0f, 16.0f }, 12.0f,
                  J::centredLeft, Palette::graphite);
    {
        const int t = juce::jlimit (0, 11, noteRow.getNote());
        const bool minor = processor.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f;
        juce::String keyName (frame::noteNames[t]);
        keyName = keyName.toUpperCase() + (minor ? " minor" : " major");
        drawMonoText (g, keyName, { keyCard.getX(), keyCard.getBottom() - 46.0f, keyCard.getWidth(), 26.0f }, 20.0f,
                      J::centred, Palette::accentInk, true, 0.04f);
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
