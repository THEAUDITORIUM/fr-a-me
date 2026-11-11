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

    FrameAudioProcessor& processor;

    WaveBand inputBand  { WaveBand::Source::input };
    WaveBand outputBand { WaveBand::Source::output };
    EqView eqView;                        // partie spectrum : EQ façon Pro-Q + analyseur

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

    // Couche fixe (fond, puits, cartes en relief, titres) : dessinée une fois,
    // gardée en image — les ombres douces ne coûtent rien à l'affichage.
    void drawStatic (juce::Graphics&);
    juce::Image staticLayer;
    float staticScale = 0.0f;

    // Géométrie (unités de conception) — 3.7 : planche allégée, textes plus gros.
    // En-tête (titre, scan key, gain), deux bandes (sound, spectrum equalizer),
    // puis quatre cartes de contrôles : HARMO, REVERB, GATE, KEY.
    const juce::Rectangle<float> soundArea    { 80.0f, 120.0f, 840.0f,  72.0f };
    const juce::Rectangle<float> spectrumArea { 80.0f, 240.0f, 840.0f, 180.0f };
    const juce::Rectangle<int>   scanArea     { 598, 26, 192, 42 };
    const juce::Rectangle<int>   gainArea     { 806, 22, 50, 50 };

    const juce::Rectangle<float> harmoCard    {  80.0f, 450.0f, 140.0f, 278.0f };
    const juce::Rectangle<float> reverbCard   { 236.0f, 450.0f, 140.0f, 278.0f };
    const juce::Rectangle<float> gateCard     { 392.0f, 450.0f, 240.0f, 278.0f };
    const juce::Rectangle<float> keyCard      { 648.0f, 450.0f, 272.0f, 278.0f };

    const juce::Rectangle<int>   knobArea     {  95, 528, 110, 110 };
    const juce::Rectangle<int>   reverbArea   { 251, 528, 110, 110 };
    const juce::Rectangle<int>   gateArea     { 404, 510, 216, 206 };
    const juce::Rectangle<int>   modeArea     { 660, 512, 248, 44 };
    const juce::Rectangle<int>   noteArea     { 660, 602, 248, 64 };

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
