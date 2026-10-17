/*
  ==============================================================================
    fr(a)me — AmbientReverb.h

    Réverbe ambient avec nuage granulaire intégré.

      • Réseau de 8 lignes à retard en feedback (FDN, matrice de Hadamard),
        longueurs premières, modulation lente des longueurs (chorus : queue
        vivante, pas métallique), amortissement des aigus dans la boucle,
        diffusion d'entrée par 4 passe-tout, pré-délai.
      • Nuage granulaire « dans la reverb » : des grains (80–350 ms, fenêtre de
        Hann, position et panoramique aléatoires) sont prélevés dans la queue,
        joués à l'unisson, une octave au-dessus ou en dessous (toujours dans la
        gamme), puis RÉINJECTÉS dans la réverbe : ils s'y fondent au lieu de
        sonner comme un effet à part.
      • Placée AVANT le moteur spectral sur le chemin traité : toute la queue
        (grains compris) passe ensuite par le recalage, les harmoniques et le
        peigne COLOR → une réverbe entièrement dans la tonalité.

    Un seul réglage : amount (0…1) = niveau, longueur (T60 1.5 → 12 s),
    taille et densité du nuage. Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

class AmbientReverb
{
public:
    static constexpr double maxTailSeconds = 13.0;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Ajoute la réverbe au signal, en place (out = in + wet). amount 0…1. */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept;

private:
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;
    static constexpr int maxGrains = 32;

    struct DelayLine
    {
        std::vector<float> data;
        int write = 0;
        void setMax (int n) { data.assign ((size_t) n + 4, 0.0f); write = 0; }
        void clear() noexcept { std::fill (data.begin(), data.end(), 0.0f); write = 0; }
        void push (float x) noexcept { data[(size_t) write] = x; if (++write >= (int) data.size()) write = 0; }
        float read (float delay) const noexcept           // retard fractionnaire (interpolation linéaire)
        {
            const int size = (int) data.size();
            float pos = (float) write - delay - 1.0f;
            while (pos < 0.0f) pos += (float) size;
            // (pos peut valoir exactement `size` après l'arrondi flottant : on replie.)
            int i0 = (int) pos;
            if (i0 >= size) { i0 -= size; pos -= (float) size; }
            const int i1 = (i0 + 1) % size;
            const float frac = pos - (float) i0;
            return data[(size_t) i0] + frac * (data[(size_t) i1] - data[(size_t) i0]);
        }
    };

    struct Allpass
    {
        std::vector<float> data;
        int pos = 0;
        float g = 0.6f;
        void setSize (int n) { data.assign ((size_t) juce::jmax (1, n), 0.0f); pos = 0; }
        void clear() noexcept { std::fill (data.begin(), data.end(), 0.0f); pos = 0; }
        float process (float x) noexcept
        {
            const float d = data[(size_t) pos];
            const float y = -g * x + d;
            data[(size_t) pos] = x + g * y;
            if (++pos >= (int) data.size()) pos = 0;
            return y;
        }
    };

    struct Grain
    {
        bool active = false;
        float readPos = 0.0f;       // position dans le tampon du nuage (échantillons)
        float rate = 1.0f;          // 0.5, 1 ou 2 : octaves, toujours dans la gamme
        int age = 0, length = 1;
        float gainL = 0.7f, gainR = 0.7f;
    };

    float nextRandom() noexcept;

    double sampleRate = 44100.0;
    std::array<DelayLine, numLines> lines;
    std::array<float, numLines> baseDelay {}, lfoPhase {}, lfoRate {}, lowpassState {};
    std::array<Allpass, numDiffusers> diffuserL, diffuserR;
    DelayLine preDelayL, preDelayR;

    // Nuage granulaire : tampon circulaire stéréo de la queue (2.5 s).
    std::vector<float> cloudL, cloudR;
    int cloudWrite = 0;
    std::array<Grain, maxGrains> grains {};
    float grainClock = 0.0f;
    float grainFeedL = 0.0f, grainFeedR = 0.0f;   // grains réinjectés (échantillon précédent)

    float amountSmoothed = 0.0f;
    uint32_t rng = 0x9E3779B9u;
};
