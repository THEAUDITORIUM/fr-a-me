/*
  ==============================================================================
    fr(a)me — FrameLookAndFeel.h

    Langage graphique "planche d'analyse sonore", thème unique garden : fond
    blanc, encre de mousse, accent vert d'herbe. Typographie monospace
    fine (IBM Plex Mono, embarquée), minuscules, lettres espacées.
  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Palette unique « garden » (3.0) : fond blanc, encre de mousse, vert d'herbe,
    touches de rose ; collage végétal et croquis au crayon. */
namespace Palette
{
    inline const juce::Colour paper      { 0xfffafbf7 };  // fond (blanc)
    inline const juce::Colour ink        { 0xff1d2a1a };  // encre principale
    inline const juce::Colour graphite   { 0xff4b5a40 };  // texte secondaire
    inline const juce::Colour pencil     { 0xff94a083 };  // lignes fines, graduations
    inline const juce::Colour faint      { 0xffe3e8da };  // repères très légers
    inline const juce::Colour accent     { 0xffa9cc6e };  // accent (formes, surlignage)
    inline const juce::Colour accentInk  { 0xff5d8a2b };  // accent dense (traits fins)
    inline const juce::Colour scaleInk   { 0xff4e7f24 };  // encre des fréquences de la gamme
    inline const juce::Colour scoreBlack { 0xff1a2716 };  // fond de la partition graphique
    inline const juce::Colour scoreDot   { 0xffe7edd6 };  // points non recalés de la partition
    inline const juce::Colour onAccent   { 0xff1d2a1a };  // texte / traits posés SUR l'accent
    inline const juce::Colour petal      { 0xffe8a3b6 };  // touche de rose (fleurs des champs)
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

    void refreshColours();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameLookAndFeel)
};

/** Dessine du texte en minuscules, monospace espacé (helper pour paint()). */
void drawMonoText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   float height, juce::Justification just, juce::Colour colour,
                   bool regularWeight = false, float tracking = 0.12f);


/** Décor du thème « garden » (sobre) : trois aplats d'herbe couchée dans les
    marges, brin au crayon ; le fond reste blanc. */
void drawGardenDecor (juce::Graphics& g, juce::Rectangle<float> page);



/** Ligne de rappel façon planche annotée : étiquette, trait horizontal,
    puis trait vertical jusqu'au point visé. */
void drawCallout (juce::Graphics& g, const juce::String& label, juce::Point<float> labelLeft,
                  juce::Point<float> target, bool labelOnLeft = true);
