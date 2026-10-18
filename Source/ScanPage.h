/*
  ==============================================================================
    fr(a)me — ScanPage.h

    Page d'accueil « scan » (3.0) : à l'ouverture d'une nouvelle instance de
    fr(a)me, une page garden avec un gros bouton SCAN. On appuie, on joue la
    mélodie dans le DAW : la tonalité (tonique + majeur / mineur) est
    détectée et appliquée au plugin, puis la planche s'ouvre.
    Rouvrable à tout moment avec le petit bouton « scan key » de la planche.

      • repos      : « appuyez sur scan, puis jouez votre mélodie »
      • écoute     : anneau de progression (~6 s de son joué), tonalité en
                     direct ; un clic sur le bouton arrête et garde le résultat
      • terminé    : la tonalité trouvée, appliquée ; « rescan » ou ouverture
                     automatique de la planche après 2.5 s
  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class FrameAudioProcessor;

class ScanPage final : public juce::Component
{
public:
    explicit ScanPage (FrameAudioProcessor&);

    /** Appelé ~30 fois par seconde par la planche. */
    void update();

    /** Affiche la page (au repos). */
    void open();

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class State { idle, listening, done };

    juce::Rectangle<float> buttonBounds() const noexcept;
    juce::Rectangle<float> rescanBounds() const noexcept;
    juce::Rectangle<float> continueBounds() const noexcept;
    void close();
    void finish();

    FrameAudioProcessor& processor;
    State state = State::idle;
    float progress = 0.0f, pulse = 0.0f;
    int tonic = -1, mode = 0;
    int listeningTicks = 0, doneTicks = 0;
    bool hoverButton = false;
};
