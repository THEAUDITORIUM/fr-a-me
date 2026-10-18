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

/** Le contenu à taille fixe (coordonnées de conception). */
class FrameCanvas final : public juce::Component
{
public:
    static constexpr int designWidth  = 1000;
    static constexpr int designHeight = 760;

    explicit FrameCanvas (FrameAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Appelé par le timer de l'éditeur : vide les FIFO et rafraîchit. */
    void refresh();

    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    /** Applique un thème (palette, spectrogramme, look-and-feel, sauvegarde). */
    void applyTheme (Theme t);
    std::function<void()> onThemeChanged;

private:
    void drawTimeAxis (juce::Graphics&, juce::Rectangle<float> span);

    FrameAudioProcessor& processor;

    WaveBand inputBand  { WaveBand::Source::input };
    WaveBand outputBand { WaveBand::Source::output };
    EqView eqView;                        // partie spectrum : EQ façon Pro-Q + analyseur
    PartialScore partialBand;

    juce::Slider harmoKnob, reverbKnob, gainKnob, pitchKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> harmoAttachment, reverbAttachment, gainAttachment, pitchAttachment;

    void setupKnob (juce::Slider& knob, const juce::String& name, double resetValue,
                    juce::Rectangle<int> area, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                    const char* paramID);
    ModeToggle modeToggle;
    NoteRow noteRow;

    uint16_t shownMask = 0;

    // Géométrie (unités de conception)
    const juce::Rectangle<float> inputArea    { 80.0f, 118.0f, 840.0f,  70.0f };
    const juce::Rectangle<float> outputArea   { 80.0f, 214.0f, 840.0f,  70.0f };
    const juce::Rectangle<float> spectrumArea { 80.0f, 310.0f, 840.0f, 140.0f };
    const juce::Rectangle<float> partialArea  { 80.0f, 476.0f, 840.0f, 100.0f };
    const juce::Rectangle<int>   gainArea     { 884, 12, 40, 40 };   // petit bouton gain, en haut à droite
    const juce::Rectangle<int>   pitchArea    { 812, 12, 40, 40 };   // petit bouton pitch, à côté de gain
    juce::Rectangle<float> themeIconArea (Theme t) const
    {
        const float slot = (t == Theme::garden) ? 0.0f : 1.0f;              // garden (défaut) à gauche, floral à droite
        return { 702.0f + 36.0f * slot, 20.0f, 24.0f, 24.0f };
    }
    const juce::Rectangle<int>   knobArea     { 140, 630, 92, 92 };
    const juce::Rectangle<int>   reverbArea   { 290, 630, 92, 92 };
    const juce::Rectangle<int>   modeArea     { 430, 656, 170, 36 };
    const juce::Rectangle<int>   noteArea     { 630, 648, 290, 50 };

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
