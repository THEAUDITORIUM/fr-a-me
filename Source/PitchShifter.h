/*
  ==============================================================================
    fr(a)me — PitchShifter.h

    PITCH (−12 … +12 demi-tons), façon « elastic pitch » : la hauteur change,
    la durée et le tempo ne bougent pas.

    Décaleur à lignes à retard (deux têtes de lecture qui glissent en sens
    inverse de la transposition, fondues en sin² / cos² : somme d'énergie
    constante). Quand une tête repart, son point de départ est choisi par
    corrélation pour reprendre en phase avec l'autre (type SOLA) : hauteur
    exacte, pas de saut de phase, pas de grésillement. Fenêtre ~45 ms : pas de latence déclarée, le son reste calé
    sur le tempo (le retard moyen des têtes est d'environ une demi-fenêtre,
    comme dans les décaleurs temps réel des DAW).
    À 0 demi-ton, le module est contourné (fondu de 30 ms) : aucun effet,
    aucun retard. Le changement de transposition est lissé (pas de saut).

    Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

class PitchShifter
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** semitones : −12 … +12. Traite en place. */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, float semitones) noexcept;

private:
    double sampleRate = 44100.0;
    std::vector<float> line[2];
    int size = 1, write = 0;
    double window = 2000.0;           // longueur de fenêtre (échantillons)
    double phase = 0.0;               // position 0…1 de la tête A dans la fenêtre
    double ratioSmoothed = 1.0;
    double offsetA = 0.0, offsetB = 0.0;   // décalages de phase des deux têtes
    int maxOffset = 768, corrLen = 480;
    double bestOffset (double baseDelay, double otherDelay) const noexcept;
    float active = 0.0f, activeCoef = 0.001f, ratioCoef = 0.001f;
};
