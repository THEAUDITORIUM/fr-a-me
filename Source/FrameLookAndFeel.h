/*
  ==============================================================================
    fr(a)me — FrameLookAndFeel.h

    Identité 3.8 « pure » : tout en blanc, en relief. Les surfaces sont
    sculptées dans un même matériau mat — en saillie (ombre douce en bas à
    droite, lumière en haut à gauche) ou en creux (ombre intérieure) —, avec
    un grain fin et une lumière qui s'étale depuis le centre. Aucune couleur :
    des gris froids très doux. Typographie Poppins fine (embarquée, licence
    OFL), en capitales très espacées.
  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Palette « pure » : blancs et gris froids. */
namespace Palette
{
    inline const juce::Colour paper      { 0xffe9ebee };  // matériau (blanc mat)
    inline const juce::Colour ink        { 0xff2f3540 };  // texte principal
    inline const juce::Colour graphite   { 0xff6d7480 };  // texte secondaire
    inline const juce::Colour pencil     { 0xffa4abb5 };  // repères, texte discret
    inline const juce::Colour faint      { 0xffd8dbe0 };  // lignes très légères
    inline const juce::Colour accent     { 0xffbcc2ca };  // surfaces remplies (barres, zones)
    inline const juce::Colour accentInk  { 0xff4a5260 };  // traits actifs (arcs, courbes)
    inline const juce::Colour scaleInk   { 0xff8d95a1 };  // fréquences de la gamme
    inline const juce::Colour scoreBlack { 0xff2f3540 };
    inline const juce::Colour scoreDot   { 0xffe9ebee };
    inline const juce::Colour onAccent   { 0xff2f3540 };  // texte posé sur une surface active
    inline const juce::Colour petal      { 0xffffffff };
    inline const juce::Colour light      { 0xffffffff };  // lumière (haut gauche)
    inline const juce::Colour shade      { 0xffa9b0ba };  // ombre (bas droite)
}

/** Relief : formes en saillie ou en creux dans le matériau blanc. */
namespace Relief
{
    /** Forme en saillie (bouton, carte) ; depth ≈ hauteur en pixels. */
    void raised (juce::Graphics&, const juce::Path&, float depth, bool pressed = false);
    /** Forme en creux (puits, rainure). */
    void inset (juce::Graphics&, const juce::Path&, float depth);
    void raised (juce::Graphics&, juce::Rectangle<float>, float cornerRadius, float depth, bool pressed = false);
    void inset (juce::Graphics&, juce::Rectangle<float>, float cornerRadius, float depth);
    void raisedEllipse (juce::Graphics&, juce::Rectangle<float>, float depth, bool pressed = false);
    void insetEllipse (juce::Graphics&, juce::Rectangle<float>, float depth);
}

/** Fond : matériau blanc, lumière au centre qui s'éteint vers les bords, grain fin. */
void drawPureBackground (juce::Graphics& g, juce::Rectangle<float> page);

class FrameLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FrameLookAndFeel();

    /** Poppins fine (light) ou normale, avec espacement des lettres. */
    static juce::Font mono (float height, bool regularWeight = false, float tracking = 0.22f);

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override { return mono (12.0f); }

    void refreshColours();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameLookAndFeel)
};

/** Dessine du texte en capitales, Poppins fine très espacée (helper pour paint()). */
void drawMonoText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   float height, juce::Justification just, juce::Colour colour,
                   bool regularWeight = false, float tracking = 0.22f);
