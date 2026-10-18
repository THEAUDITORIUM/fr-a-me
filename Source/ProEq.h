/*
  ==============================================================================
    fr(a)me — ProEq.h

    EQ façon Pro-Q (2.3), dans la partie spectrum : fréquences de gauche à
    droite (20 Hz → 20 kHz), gain de haut en bas. Cinq points :
      • coupe basse  (24 dB/oct, Butterworth) — en butée à 20 Hz : coupée
      • 3 cloches    (±18 dB, Q 0.3 … 10)     — à 0 dB : exactement neutres
      • coupe haute  (24 dB/oct, Butterworth) — en butée à 20 kHz : coupée
    0 latence : filtres à variables d'état (TPT / Simper), stables quand les
    réglages bougent (fréquence, gain et Q glissés, sans zip).
    Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

class ProEq
{
public:
    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    static constexpr float maxGainDb = 18.0f;
    static constexpr float minQ = 0.3f, maxQ = 10.0f;
    static constexpr int numBells = 3;
    static constexpr float defaultBellHz[numBells] { 150.0f, 1000.0f, 6000.0f };

    struct Settings
    {
        float lowHz = minHz, highHz = maxHz;
        std::array<float, numBells> bellHz { 150.0f, 1000.0f, 6000.0f };
        std::array<float, numBells> bellDb { 0.0f, 0.0f, 0.0f };
        std::array<float, numBells> bellQ  { 1.0f, 1.0f, 1.0f };
    };

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Traite en place (1 ou 2 canaux). */
    void process (float* const* data, int numChannels, int numSamples, Settings target) noexcept;

    /** Réponse en dB à une fréquence (pour la courbe affichée ; prototype analogique). */
    static float magnitudeDb (float hz, const Settings& s) noexcept;

    static bool lowCutOn  (float hz) noexcept { return hz > minHz * 1.01f; }
    static bool highCutOn (float hz) noexcept { return hz < maxHz * 0.99f; }

private:
    struct Svf
    {
        double ic1[2] {}, ic2[2] {};
        void clear() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0; }
        // Renvoie (v1, v2) ; l'appelant combine en passe-haut / passe-bas / cloche.
        inline void tick (double x, int c, double g, double k, double& v1, double& v2) noexcept
        {
            const double a1 = 1.0 / (1.0 + g * (g + k));
            const double v3 = x - ic2[c];
            v1 = a1 * ic1[c] + g * a1 * v3;
            v2 = ic2[c] + g * v1;
            ic1[c] = 2.0 * v1 - ic1[c];
            ic2[c] = 2.0 * v2 - ic2[c];
        }
    };

    double sampleRate = 44100.0;
    Svf hp[2], lp[2], bell[numBells];
    Settings now;
    bool first = true;
    double smoothCoef = 0.01;
    float hpOn = 0.0f, lpOn = 0.0f, onCoef = 0.001f;
};
