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

/** Thèmes : « floral » (rose pétale : lavis d'aquarelle, pétales veinés,
    éclaboussures d'encre) et « garden » (prairie : herbes couchées par le
    vent, collage végétal, croquis au crayon — thème par défaut). */
enum class Theme { floral = 0, garden = 1 };

namespace Palette
{
    // Couleurs courantes (modifiées par apply()). Lues au moment du dessin.
    inline juce::Colour paper      { 0xfff4f3ef };  // fond
    inline juce::Colour ink        { 0xff111111 };  // encre principale
    inline juce::Colour graphite   { 0xff5c5c5a };  // texte secondaire
    inline juce::Colour pencil     { 0xff9c9b97 };  // lignes fines, graduations
    inline juce::Colour faint      { 0xffdcdbd5 };  // repères très légers
    inline juce::Colour accent     { 0xffeee97a };  // accent (formes, surlignage)
    inline juce::Colour accentInk  { 0xffcbbf3c };  // accent dense (traits fins)
    inline juce::Colour scaleInk   { 0xffc8b52c };  // encre des fréquences de la gamme (spectrogramme)
    inline juce::Colour scoreBlack { 0xff0b0b0b };  // fond de la partition graphique
    inline juce::Colour scoreDot   { 0xffffffff };  // points non recalés de la partition
    inline juce::Colour onAccent   { 0xff111111 };  // texte / traits posés SUR l'accent (toujours lisible)
    inline Theme current = Theme::garden;

    inline void apply (Theme t)
    {
        current = t;
        switch (t)
        {
            case Theme::garden:
                // prairie : vert d'herbe, papier de dune, encre de mousse
                paper = juce::Colour (0xffeff0e3); ink = juce::Colour (0xff1d2a1a);
                graphite = juce::Colour (0xff4b5a40); pencil = juce::Colour (0xff94a083);
                faint = juce::Colour (0xffd6dcc6); accent = juce::Colour (0xff9cc35c);
                accentInk = juce::Colour (0xff5d8a2b); scaleInk = juce::Colour (0xff4e7f24);
                scoreBlack = juce::Colour (0xff1a2716); scoreDot = juce::Colour (0xffe7edd6);
                onAccent = juce::Colour (0xff1d2a1a);
                break;
            case Theme::floral:
            default:
                // rose pétale : papier blanc rosé, encre prune, accent rose, cœur framboise
                paper = juce::Colour (0xfff8f2f1); ink = juce::Colour (0xff3b1a26);
                graphite = juce::Colour (0xff6f4b57); pencil = juce::Colour (0xffbb98a2);
                faint = juce::Colour (0xffefdfe2); accent = juce::Colour (0xfff2a9bd);
                accentInk = juce::Colour (0xffcf5a7d); scaleInk = juce::Colour (0xffbd3d66);
                scoreBlack = juce::Colour (0xff2b1119); scoreDot = juce::Colour (0xfffbe9ee);
                onAccent = juce::Colour (0xff3b1a26);
                break;
        }
    }

    // « sun » / « light » (anciennes sessions) = floral ; tout le reste = garden.
    inline Theme fromName (const juce::String& n) { return (n == "floral" || n == "sun" || n == "light") ? Theme::floral : Theme::garden; }
    inline juce::String toName (Theme t)         { return t == Theme::floral ? "floral" : "garden"; }
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

    /** Recolore les composants standard après un changement de thème. */
    void refreshColours();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameLookAndFeel)
};

/** Dessine du texte en minuscules, monospace espacé (helper pour paint()). */
void drawMonoText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   float height, juce::Justification just, juce::Colour colour,
                   bool regularWeight = false, float tracking = 0.12f);

/** Icônes de thème (fleur, petit arbre), dessinées au trait fin. */
void drawThemeIcon (juce::Graphics& g, Theme t, juce::Rectangle<float> area, bool selected);

/** Décor du thème « garden » : grain de papier, aplats de collage végétal,
    brin dessiné au crayon (inspiré des planches de référence). */
void drawGardenDecor (juce::Graphics& g, juce::Rectangle<float> page);

/** Décor du thème « floral » : lavis d'aquarelle rose, pétales veinés dans les
    marges, éclaboussures d'encre rouille, petite fleur au crayon. */
void drawFloralDecor (juce::Graphics& g, juce::Rectangle<float> page);

/** Ligne de rappel façon planche annotée : étiquette, trait horizontal,
    puis trait vertical jusqu'au point visé. */
void drawCallout (juce::Graphics& g, const juce::String& label, juce::Point<float> labelLeft,
                  juce::Point<float> target, bool labelOnLeft = true);
