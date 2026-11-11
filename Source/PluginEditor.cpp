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
void FrameCanvas::drawStatic (juce::Graphics& g)
{
    using J = juce::Justification;
    const auto page = getLocalBounds().toFloat();
    drawPureBackground (g, page);

    const float left = soundArea.getX(), right = soundArea.getRight();

    // En-tête : le nom, fin et très espacé.
    drawMonoText (g, "fr(a)me", { left, 22.0f, 400.0f, 46.0f }, 30.0f, J::centredLeft, Palette::ink, false, 0.62f);

    // Bandes : puits creusés dans la matière.
    Relief::inset (g, soundArea.expanded (10.0f, 8.0f), 16.0f, 6.0f);
    Relief::inset (g, spectrumArea.expanded (10.0f, 8.0f), 16.0f, 6.0f);
    auto bandTitle = [&] (const char* text, juce::Rectangle<float> band)
    {
        drawMonoText (g, text, { band.getX(), band.getY() - 34.0f, 500.0f, 18.0f }, 13.0f, J::centredLeft, Palette::graphite, false, 0.42f);
    };
    bandTitle ("sound", soundArea);
    bandTitle ("spectrum equalizer", spectrumArea);
    auto legend = [&] (float x, float y, const char* text, juce::Colour c)
    {
        g.setColour (c);
        g.fillEllipse (x, y - 4.0f, 8.0f, 8.0f);
        drawMonoText (g, text, { x + 15.0f, y - 8.0f, 110.0f, 16.0f }, 11.0f, J::centredLeft, Palette::pencil, false, 0.3f);
    };
    legend (right - 236.0f, soundArea.getY() - 25.0f, "dry",    Palette::pencil);
    legend (right - 120.0f, soundArea.getY() - 25.0f, "in key", Palette::accentInk);

    // Cartes en relief, titres centrés.
    auto card = [&] (juce::Rectangle<float> r, const char* title)
    {
        Relief::raised (g, r, 24.0f, 9.0f);
        drawMonoText (g, title, { r.getX(), r.getY() + 20.0f, r.getWidth(), 20.0f }, 13.5f, J::centred, Palette::graphite, false, 0.48f);
    };
    card (harmoCard,  "harmo");
    card (reverbCard, "reverb");
    card (gateCard,   "gate");
    card (keyCard,    "key");
    drawMonoText (g, "tonic", { keyCard.getX(), (float) noteArea.getY() - 26.0f, keyCard.getWidth(), 16.0f }, 11.0f,
                  J::centred, Palette::pencil, false, 0.4f);
}

void FrameCanvas::paint (juce::Graphics& g)
{
    using J = juce::Justification;

    // Couche fixe, à la résolution réelle de l'écran.
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! staticLayer.isValid() || std::abs (scale - staticScale) > 0.01f)
    {
        staticScale = scale;
        staticLayer = juce::Image (juce::Image::ARGB, juce::roundToInt (designWidth * scale), juce::roundToInt (designHeight * scale), true);
        juce::Graphics sg (staticLayer);
        sg.addTransform (juce::AffineTransform::scale (scale));
        drawStatic (sg);
    }
    g.drawImage (staticLayer, getLocalBounds().toFloat());

    // Gain : valeur sous le petit bouton.
    {
        const auto gr = gainArea.toFloat();
        const float gv = (float) gainKnob.getValue();
        drawMonoText (g, "gain", { gr.getRight() + 10.0f, gr.getY() + 8.0f, 80.0f, 16.0f }, 11.0f, J::centredLeft, Palette::graphite, false, 0.4f);
        drawMonoText (g, juce::String (gv > 0.05f ? "+" : "") + juce::String (gv, 1) + " db",
                      { gr.getRight() + 10.0f, gr.getY() + 26.0f, 80.0f, 16.0f }, 12.0f, J::centredLeft, Palette::ink, false, 0.2f);
    }

    // Valeurs des deux gros boutons.
    auto value = [&] (const juce::String& t, juce::Rectangle<float> c)
    {
        drawMonoText (g, t, { c.getX(), c.getBottom() - 64.0f, c.getWidth(), 28.0f }, 21.0f, J::centred, Palette::ink, false, 0.2f);
    };
    value (juce::String (juce::roundToInt (harmoKnob.getValue())) + " %", harmoCard);
    value (juce::String (juce::roundToInt (reverbKnob.getValue())) + " %", reverbCard);

    // Carte KEY : la tonalité en clair.
    {
        const int t = juce::jlimit (0, 11, noteRow.getNote());
        const bool minor = processor.apvts.getRawParameterValue (ParamIDs::mode)->load() > 0.5f;
        drawMonoText (g, juce::String (frame::noteNames[t]) + (minor ? " minor" : " major"),
                      { keyCard.getX(), keyCard.getBottom() - 50.0f, keyCard.getWidth(), 28.0f }, 19.0f,
                      J::centred, Palette::ink, false, 0.3f);
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
