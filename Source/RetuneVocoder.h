/*
  ==============================================================================
    fr(a)me — RetuneVocoder.h

    RETUNE (2.1) : vocodeur robotique façon Vocodex, dont les porteuses sont
    les notes de la gamme. 0 latence (filtres IIR + suivi d'enveloppe causal).
    Aucune note n'est retirée : toutes les bandes sonnent (sélectivité douce),
    chaque porteuse est « robotique » (fondamentale + octave), les bandes sont
    réparties gauche / droite, et une part du son d'origine est gardée pour
    conserver glissés, textures et variations. Le niveau suit le son d'origine.

      • Pour chaque note de la gamme de mi1 (41 Hz) à do9 (8.4 kHz), une bande
        d'analyse couvre tout l'espace jusqu'aux notes de la gamme voisines
        (bornes au milieu, en échelle logarithmique) : toute l'énergie du son
        est attribuée à la note de la gamme la plus proche (recalage complet).
      • L'enveloppe de chaque bande (amplitude du signal analytique, attaque
        ~1 ms, relâchement ~4 ms) pilote un oscillateur sinusoïdal accordé
        EXACTEMENT sur la note (A4 = 440 Hz) : le son ressort comme une nappe
        de notes justes qui suit le relief du son d'origine — l'aspect
        « vocodeur ».
      • Sélectivité : chaque bande est comparée à ses voisines (±½ octave) et
        élevée au carré relativement à elles : seules les notes de la gamme
        les plus proches de ce que joue vraiment le son ressortent.
      • Aigus légèrement en retrait (−5 dB vers 8 kHz), niveau calé sur les
        exemples de référence fournis (niveau identique, bandes à ±2 dB).
      • Changement de gamme : fondu croisé de ~60 ms entre l'ancienne et la
        nouvelle banque, sans clic.

    Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

class RetuneVocoder
{
public:
    static constexpr int lowNote  = 28;      // mi1
    static constexpr int highNote = 127;     // sol9 (limité par la fréquence d'échantillonnage)
    static constexpr int maxBands = highNote - lowNote + 1;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setScale (uint16_t mask) noexcept;

    /** Remplace le contenu du tampon par sa version « retune » (0 latence).
        amount 0…1 : fondu entre le son d'entrée et le son vocodé (lissé). */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept;

private:
    struct Biquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] {}, z2[2] {};
        inline double tick (double x, int c) noexcept
        {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
    };

    struct Band
    {
        int note = 0;
        double hz = 0.0;
        Biquad bp1, bp2;               // passe-bande 4e ordre (2 × 2e ordre)
        double cosW = 1.0, sinW = 1.0; // pour la composante en quadrature
        double prev[2] {};
        float env[2] {};
        double phase = 0.0, phaseInc = 0.0, sinNow = 0.0, sin2Now = 0.0, cosNow = 1.0, cos2Now = 1.0;
        float gain = 1.0f;             // gain de sortie (inclinaison + normalisation)
        float panL = 1.0f, panR = 1.0f;// position stéréo de la bande
        bool  buzzOk = true;           // l'octave de la porteuse reste sous Nyquist
    };

    struct Bank
    {
        std::array<Band, maxBands> bands {};
        std::array<float, maxBands> shaped {};
        int count = 0;
    };

    void buildBank (Bank& bank, uint16_t mask) noexcept;
    void runBank (Bank& bank, const double* in, int nch, double* out) noexcept;

    double sampleRate = 44100.0;
    Bank banks[2];
    int current = 0;
    float crossfade = 1.0f, crossfadeStep = 0.001f;   // 0 → 1 : de l'ancienne banque vers la nouvelle
    uint16_t mask = 0;
    bool hasBank = false;

    float envAttack = 0.5f, envRelease = 0.01f;
    float expansion = 2.0f;                          // sélectivité (notes jouées vs fond)
    float amountSmoothed = 0.0f, amountCoef = 0.001f;
    float eIn = 0.0f, eVoc = 0.0f, levelGain = 1.0f, levelCoef = 0.0001f, levelUp = 0.001f;
};
