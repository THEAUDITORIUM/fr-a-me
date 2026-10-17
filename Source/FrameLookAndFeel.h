/*
  ==============================================================================
    fr(a)me — FrameLookAndFeel.h

    Langage graphique "planche d'analyse sonore" : blanc cassé, encre noire,
    niveaux de gris, un seul accent jaune citron pâle. Typographie monospace
    fine (IBM Plex Mono, embarquée), minuscules, lettres espacées.
  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    inline const juce::Colour paper      { 0xfff4f3ef };  // fond blanc cassé
    inline const juce::Colour ink        { 0xff111111 };  // encre noire
    inline const juce::Colour graphite   { 0xff5c5c5a };  // texte secondaire
    inline const juce::Colour pencil     { 0xff9c9b97 };  // lignes fines, graduations
    inline const juce::Colour faint      { 0xffdcdbd5 };  // repères très légers
    inline const juce::Colour accent     { 0xffeee97a };  // jaune citron pâle (formes, surlignage)
    inline const juce::Colour accentInk  { 0xffcbbf3c };  // jaune plus dense (traits fins sur fond clair)
    inline const juce::Colour scoreBlack { 0xff0b0b0b };  // fond de la partition graphique
}

class FrameLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FrameLookAndFeel();

    /** Police monospace fine (light) ou normale, avec espacement des lettres. */
    static juce::Font mono (float height, bool regularWeight = false, float tracking = 0.12f);

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override { return mono (12.0f); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameLookAndFeel)
};

/** Dessine du texte en minuscules, monospace espacé (helper pour paint()). */
void drawMonoText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   float height, juce::Justification just, juce::Colour colour,
                   bool regularWeight = false, float tracking = 0.12f);

/** Ligne de rappel façon planche annotée : étiquette, trait horizontal,
    puis trait vertical jusqu'au point visé. */
void drawCallout (juce::Graphics& g, const juce::String& label, juce::Point<float> labelLeft,
                  juce::Point<float> target, bool labelOnLeft = true);
