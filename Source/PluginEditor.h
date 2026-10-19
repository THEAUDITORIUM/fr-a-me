/*
  ==============================================================================
    fr(a)me — PluginEditor.h

    Interface : une "planche" de 1000 × 760 unités dessinée en vectoriel puis
    mise à l'échelle (redimensionnement libre à proportions fixes).
  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "PluginProcessor.h"
#include "FrameLookAndFeel.h"
#include "Visualisers.h"
#include "Controls.h"
#include "ScanPage.h"

/** Le contenu à taille fixe (coordonnées de conception). */
class FrameCanvas final : public juce::Component
{
public:
    static constexpr int designWidth  = 1000;
    static constexpr int designHeight = 760;

    explicit FrameCanvas (FrameAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Ouvre la page de scan (comme le bouton scan key). */
    void openScan() { scanPage.open(); }

    /** Appelé par le timer de l'éditeur : vide les FIFO et rafraîchit. */
    void refresh();


private:
    void drawTimeAxis (juce::Graphics&, juce::Rectangle<float> span);

    FrameAudioProcessor& processor;

    WaveBand inputBand  { WaveBand::Source::input };
    WaveBand outputBand { WaveBand::Source::output };
    EqView eqView;                        // partie spectrum : EQ façon Pro-Q + analyseur
    PartialScore partialBand;

    juce::Slider harmoKnob, reverbKnob, gainKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> harmoAttachment, reverbAttachment, gainAttachment;

    void setupKnob (juce::Slider& knob, const juce::String& name, double resetValue,
                    juce::Rectangle<int> area, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                    const char* paramID);
    ModeToggle modeToggle;
    GatePanel gatePanel;                  // GATE (à la place de pitch)
    ScanButton scanButton;
    ScanPage scanPage;                    // page d'accueil « scan » (par-dessus la planche)
    NoteRow noteRow;

    uint16_t shownMask = 0;

    // Géométrie (unités de conception) — 3.2 : planche épurée.
    // Une seule bande « sound » (son brut en gris, son dans la tonalité en vert
    // par-dessus), le spectrum (EQ) et la partition des partiels.
    const juce::Rectangle<float> soundArea    { 80.0f, 116.0f, 840.0f, 116.0f };
    const juce::Rectangle<float> spectrumArea { 80.0f, 266.0f, 840.0f, 160.0f };
    const juce::Rectangle<float> partialArea  { 80.0f, 458.0f, 840.0f,  70.0f };   // 3.4 : plus basse, les contrôles montent
    // En haut à droite : scan key, puis le petit bouton gain.
    const juce::Rectangle<int>   scanArea     { 742, 18, 124, 26 };
    const juce::Rectangle<int>   gainArea     { 884, 12, 40, 40 };
    // Contrôles (3.4) : trois groupes aérés — son (harmo, reverb), gate, tonalité
    // (mode au-dessus de la tonique).
    static constexpr float separatorY = 550.0f, calloutY = 570.0f;
    const juce::Rectangle<int>   knobArea     { 104, 594, 100, 100 };
    const juce::Rectangle<int>   reverbArea   { 254, 594, 100, 100 };
    const juce::Rectangle<int>   gateArea     { 412, 590, 196, 118 };
    const juce::Rectangle<int>   modeArea     { 712, 590, 170, 36 };
    const juce::Rectangle<int>   noteArea     { 672, 658, 248, 50 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameCanvas)
};

//==============================================================================
class FrameAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit FrameAudioProcessorEditor (FrameAudioProcessor&);
    ~FrameAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { canvas.refresh(); }

    FrameLookAndFeel lookAndFeel;
    FrameCanvas canvas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameAudioProcessorEditor)
};
