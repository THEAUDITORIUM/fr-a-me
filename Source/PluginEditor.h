/*
  ==============================================================================
    fr(a)me — PluginEditor.h

    Interface : une "planche" de 1000 × 760 unités dessinée en vectoriel puis
    mise à l'échelle (redimensionnement libre à proportions fixes).
  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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

private:
    void drawTimeAxis (juce::Graphics&, juce::Rectangle<float> span);
    void drawFrequencyLabels (juce::Graphics&, juce::Rectangle<float> band);

    FrameAudioProcessor& processor;

    WaveBand inputBand  { WaveBand::Source::input };
    WaveBand outputBand { WaveBand::Source::output };
    SpectrogramBand spectrumBand;
    PartialScore partialBand;

    juce::Slider mixKnob, colorKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, colorAttachment;
    ModeToggle modeToggle;
    NoteRow noteRow;

    uint16_t shownMask = 0;

    // Géométrie (unités de conception)
    const juce::Rectangle<float> inputArea    { 80.0f, 118.0f, 840.0f,  70.0f };
    const juce::Rectangle<float> outputArea   { 80.0f, 214.0f, 840.0f,  70.0f };
    const juce::Rectangle<float> spectrumArea { 80.0f, 310.0f, 840.0f, 140.0f };
    const juce::Rectangle<float> partialArea  { 80.0f, 476.0f, 840.0f, 100.0f };
    const juce::Rectangle<int>   knobArea     { 132, 628, 96, 96 };
    const juce::Rectangle<int>   colorArea    { 272, 628, 96, 96 };
    const juce::Rectangle<int>   modeArea     { 412, 654, 180, 36 };
    const juce::Rectangle<int>   noteArea     { 620, 648, 300, 50 };

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
