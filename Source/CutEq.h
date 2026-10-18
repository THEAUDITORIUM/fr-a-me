/*
  ==============================================================================
    fr(a)me — CutEq.h

    EQ « isoler / couper » façon Chroma (2.2) : deux poignées sur le spectrum
    fixent une plage de fréquences gardée ; tout ce qui est en dessous de
    « low » et au-dessus de « high » est coupé (24 dB/octave, Butterworth).
    0 latence : filtres à variables d'état (TPT / Simper), stables même quand
    les fréquences bougent (glissement lissé, sans zip). Poignée en butée
    (40 Hz en bas, 12 kHz en haut) = filtre contourné (fondu de 20 ms) :
    aucun effet tant qu'on n'y touche pas.

    Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class CutEq
{
public:
    static constexpr float minHz = 40.0f;      // = bornes de l'axe du spectrum
    static constexpr float maxHz = 12000.0f;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Traite en place (1 ou 2 canaux). */
    void process (float* const* data, int numChannels, int numSamples, float lowHz, float highHz) noexcept;

private:
    struct Svf
    {
        double ic1[2] {}, ic2[2] {};
        void clear() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0; }
        // Renvoie passe-haut ou passe-bas selon 'high'.
        inline double tick (double x, int c, double g, double k, bool high) noexcept
        {
            const double a1 = 1.0 / (1.0 + g * (g + k));
            const double v3 = x - ic2[c];
            const double v1 = a1 * ic1[c] + g * a1 * v3;
            const double v2 = ic2[c] + g * v1;
            ic1[c] = 2.0 * v1 - ic1[c];
            ic2[c] = 2.0 * v2 - ic2[c];
            return high ? x - k * v1 - v2 : v2;
        }
    };

    double sampleRate = 44100.0;
    Svf hp[2], lp[2];                  // 2 × 2e ordre = 4e ordre (24 dB/oct)
    double lowNow = minHz, highNow = maxHz;
    double freqCoef = 0.01;
    float hpOn = 0.0f, lpOn = 0.0f, onCoef = 0.001f;
};
