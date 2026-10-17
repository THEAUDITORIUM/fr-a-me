/*
  ==============================================================================
    fr(a)me — Controls.h

    Contrôles intégrés au langage de la planche :
      • ModeToggle : bascule texte « major / minor », l'option active surlignée
      • NoteRow    : rangée de 12 cases, la tonique surlignée en jaune, un point
                     sous chaque note appartenant à la gamme
    Chacun est relié à son paramètre par une juce::ParameterAttachment :
    automation, undo de l'hôte et gestes (begin/end) sont gérés.
  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class ModeToggle final : public juce::Component
{
public:
    explicit ModeToggle (juce::RangedAudioParameter& param);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    int getMode() const noexcept { return mode; }

private:
    juce::Rectangle<float> optionBounds (int index) const;

    int mode = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

class NoteRow final : public juce::Component
{
public:
    explicit NoteRow (juce::RangedAudioParameter& param);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setScaleMask (uint16_t newMask);
    int getNote() const noexcept { return note; }

private:
    int cellAt (juce::Point<float> p) const;
    juce::Rectangle<float> cellBounds (int index) const;

    int note = 0, hover = -1;
    uint16_t mask = 0;
    bool dragging = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
