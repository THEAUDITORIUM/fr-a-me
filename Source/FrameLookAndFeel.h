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

/** Thèmes : « sun » (planche de base, accent jaune, collage solaire) et
    « garden » (collage végétal : verts mousse, croquis au crayon — thème par défaut). */
enum class Theme { sun = 0, garden = 1 };

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
                paper = juce::Colour (0xfff0eee4); ink = juce::Colour (0xff1e2a1a);
                graphite = juce::Colour (0xff4f5c45); pencil = juce::Colour (0xff97a189);
                faint = juce::Colour (0xffd8dccb); accent = juce::Colour (0xffa9cc62);
                accentInk = juce::Colour (0xff6c962e); scaleInk = juce::Colour (0xff5f8f2a);
                scoreBlack = juce::Colour (0xff1b2617); scoreDot = juce::Colour (0xffe9edd8);
                onAccent = juce::Colour (0xff1e2a1a);
                break;
            case Theme::sun:
            default:
                paper = juce::Colour (0xfff4f3ef); ink = juce::Colour (0xff111111);
                graphite = juce::Colour (0xff5c5c5a); pencil = juce::Colour (0xff9c9b97);
                faint = juce::Colour (0xffdcdbd5); accent = juce::Colour (0xffeee97a);
                accentInk = juce::Colour (0xffcbbf3c); scaleInk = juce::Colour (0xffc8b52c);
                scoreBlack = juce::Colour (0xff0b0b0b); scoreDot = juce::Colour (0xffffffff);
                onAccent = juce::Colour (0xff111111);
                break;
        }
    }

    // « light » (anciennes sessions) = sun ; tout le reste (dont l'ancien « dark ») = garden.
    inline Theme fromName (const juce::String& n) { return (n == "sun" || n == "light") ? Theme::sun : Theme::garden; }
    inline juce::String toName (Theme t)         { return t == Theme::sun ? "sun" : "garden"; }
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

/** Icônes de thème (soleil, petit arbre), dessinées au trait fin. */
void drawThemeIcon (juce::Graphics& g, Theme t, juce::Rectangle<float> area, bool selected);

/** Décor du thème « garden » : grain de papier, aplats de collage végétal,
    brin dessiné au crayon (inspiré des planches de référence). */
void drawGardenDecor (juce::Graphics& g, juce::Rectangle<float> page);

/** Décor du thème « sun » : même principe (grain, papiers déchirés dans les
    marges, petit croquis en haut à gauche), en tons solaires. */
void drawSunDecor (juce::Graphics& g, juce::Rectangle<float> page);

/** Ligne de rappel façon planche annotée : étiquette, trait horizontal,
    puis trait vertical jusqu'au point visé. */
void drawCallout (juce::Graphics& g, const juce::String& label, juce::Point<float> labelLeft,
                  juce::Point<float> target, bool labelOnLeft = true);
