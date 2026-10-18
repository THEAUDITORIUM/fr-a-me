/*
  ==============================================================================
    fr(a)me — GrainShifter.h

    Décaleur à grains courts pour le « reste » non tonal du PITCH (attaques,
    souffle, bruits) — 0 latence déclarée :
      • grains de 12 ms (fenêtre de Hann, recouvrement 50 %), lus à la vitesse
        du rapport de transposition ;
      • à chaque attaque détectée, les grains en cours s'éteignent en 1 ms et un
        grain neuf démarre AU MOMENT de l'attaque (front raide, pas de fondu
        d'entrée) : l'attaque sort une seule fois — pas de doublement — et à
        l'heure en transposition vers le bas ; vers le haut, avec au plus
        (rapport − 1) × 12 ms de décalage (3 ms à +7, 12 ms à +12).
    Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

class GrainShifter
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** Traite en place. onset[i] > 0 : attaque à l'échantillon i (peut être nul). */
    void process (float* const* data, int numChannels, int numSamples, double ratio, const uint8_t* onset) noexcept;

private:
    struct Grain
    {
        bool live = false;
        double readPos = 0.0;      // position de lecture (échantillons absolus)
        int age = 0;
        float fadeOut = 1.0f;      // extinction rapide (attaque suivante)
        bool dying = false;
        bool sharp = false;        // grain d'attaque : front raide
    };

    void startGrain (bool sharp, double ratio) noexcept;

    double sampleRate = 44100.0;
    std::vector<float> line[2];
    int size = 1;
    long long writeCount = 0;
    std::array<Grain, 6> grains {};
    std::vector<float> window;      // Hann, longueur grainLen
    int grainLen = 512, hop = 256, sinceHop = 0, sharpRise = 16;
    float dieStep = 0.02f;
    long long lastOnset = -1000000;   // position (écriture) de la dernière attaque
    int attackCore = 96;              // ~2 ms : cœur de l'attaque, jamais relu par un autre grain
};
