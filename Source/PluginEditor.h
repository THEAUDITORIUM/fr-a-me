/*
  ==============================================================================
    fr(a)me — PluginEditor.h

    Interface : une "planche" de 880 × 592 unités dessinée en vectoriel puis
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
    static constexpr int designWidth  = 880;
    static constexpr int designHeight = 592;

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

    // Géométrie (unités de conception) — 3.9 : compacte, sans cartes ; les
    // commandes sortent directement de la matière.
    const juce::Rectangle<float> soundArea    { 44.0f, 112.0f, 792.0f,  48.0f };
    const juce::Rectangle<float> spectrumArea { 44.0f, 212.0f, 792.0f, 118.0f };
    const juce::Rectangle<int>   scanArea     { 552, 18, 168, 50 };
    const juce::Rectangle<int>   gainArea     { 734, 21, 44, 44 };

    // Rangée du bas : quatre zones (titre au-dessus).
    const juce::Rectangle<float> harmoZone    {  36.0f, 374.0f, 140.0f, 210.0f };
    const juce::Rectangle<float> reverbZone   { 176.0f, 374.0f, 140.0f, 210.0f };
    const juce::Rectangle<float> gateZone     { 324.0f, 374.0f, 236.0f, 210.0f };
    const juce::Rectangle<float> keyZone      { 572.0f, 374.0f, 272.0f, 210.0f };

    const juce::Rectangle<int>   knobArea     {  54, 404, 104, 104 };
    const juce::Rectangle<int>   reverbArea   { 194, 404, 104, 104 };
    const juce::Rectangle<int>   gateArea     { 336, 398, 212, 180 };
    const juce::Rectangle<int>   modeArea     { 586, 404, 244, 42 };
    const juce::Rectangle<int>   noteArea     { 582, 478, 252, 60 };

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
