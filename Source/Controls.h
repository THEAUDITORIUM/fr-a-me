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

/** Case à cocher « retune » : un petit carré au trait fin (jaune + coche
    quand activé) suivi du mot, comme une légende de planche. */
class RetuneToggle final : public juce::Component
{
public:
    explicit RetuneToggle (juce::RangedAudioParameter& param);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isOn() const noexcept { return on; }

private:
    bool on = false;
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

/** EQ du spectrum (façon Chroma) : deux poignées horizontales posées sur le
    spectrogramme. Tout ce qui est sous « low » et au-dessus de « high » est
    coupé (et assombri à l'écran). On tire une poignée pour la déplacer, on
    tire entre les deux pour faire glisser toute la plage ; double-clic :
    remise à zéro. La fréquence s'affiche pendant le geste. */
class EqOverlay final : public juce::Component
{
public:
    EqOverlay (juce::RangedAudioParameter& low, juce::RangedAudioParameter& high);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Grab { none, low, high, band };

    float yFor (float hz) const noexcept;
    float hzFor (float y) const noexcept;
    Grab hitTestGrab (float y) const noexcept;

    juce::RangedAudioParameter& lowParam;
    juce::RangedAudioParameter& highParam;
    std::unique_ptr<juce::ParameterAttachment> lowAttachment, highAttachment;
    float lowHz = 40.0f, highHz = 12000.0f;

    Grab hover = Grab::none, dragging = Grab::none;
    float dragStartY = 0.0f, dragStartLowY = 0.0f, dragStartHighY = 0.0f;
};
