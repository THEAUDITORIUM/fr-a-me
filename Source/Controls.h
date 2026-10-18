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
#include <functional>
#include <array>

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

/** Petit bouton « scan key » de la planche : rouvre la page de scan. */
class ScanButton final : public juce::Component
{
public:
    ScanButton();
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
private:
    bool hover = false;
};

/** EQ façon Pro-Q, dans la partie spectrum : fréquences de gauche à droite
    (20 Hz → 20 kHz, échelle log), gain de haut en bas (±24 dB affichés).
    Derrière la courbe : l'analyseur du son de sortie et les toniques de la
    gamme. Cinq points à tirer :
      • coupe basse et coupe haute (gauche / droite, 24 dB/oct) ;
      • 3 cloches (gauche / droite = fréquence, haut / bas = gain) ;
    molette sur un point = largeur (Q) ; double-clic = remise à zéro. */
class EqView final : public juce::Component
{
public:
    static constexpr int numNodes = 5;           // 0 = coupe basse, 1-3 = cloches, 4 = coupe haute

    EqView (juce::AudioProcessorValueTreeState& apvts);

    void pushSpectrum (const float* levels, int numRows, float rowMinHz, float rowMaxHz) noexcept;
    void setScale (uint16_t mask, int tonic);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr float displayDb = 24.0f;

private:
    float xFor (float hz) const noexcept;
    float hzFor (float x) const noexcept;
    float yFor (float db) const noexcept;
    float dbFor (float y) const noexcept;
    juce::Point<float> nodePos (int node) const noexcept;
    int nodeAt (juce::Point<float> p) const noexcept;
    juce::String nodeText (int node) const;

    struct Attach
    {
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };
    Attach freqA[numNodes], gainA[numNodes], qA[numNodes];   // gain / Q : cloches seulement

    float freq[numNodes] {}, gain[numNodes] {}, q[numNodes] {};
    std::array<float, 256> analyser {};
    int analyserRows = 0;
    float analyserMinHz = 40.0f, analyserMaxHz = 12000.0f;
    uint16_t scaleMask = 0;
    int tonic = 0;

    int hover = -1, dragging = -1;
};
