/*
  ==============================================================================
    fr(a)me — ZeroLatencyHarmonizer.h

    Traitement harmonique SANS AUCUNE LATENCE (0 échantillon), dans la lignée
    de la version 1.4, entièrement par filtres (comme un égaliseur en mode
    « zero latency ») :

      1. Peigne harmonique (3.0) : une cloche RBJ (double précision) sur
         CHAQUE note de la gamme de mi1 à mi8, portée à +27 dB, largeur
         ~90 cents dans le grave → ~50 cents dans l'aigu (plus large qu'avant :
         les cloches sonnent moins longtemps, pas de « lignes » qui traînent).
         Puis une compensation STATIQUE par octave (8 cloches larges, gains
         calculés à partir de la réponse du peigne) : les notes de la gamme
         ressortent exactement au niveau d'origine, l'espace entre elles est
         creusé. Pas de boucle de régulation sur les sons tonals → pas de
         pompage sur les sons tenus. Seul un rattrapage très lent, limité à
         +6 dB, remonte le niveau des sons non tonals (bruits, cymbales).
         Effet progressif de 150 Hz à 700 Hz, plein jusqu'à 6 kHz, puis atténué.
         (En amont, PartialSnapper a déjà recalé les partiels tenus sur la
         gamme : le peigne ne fait que finir le travail.)

      2. Harmoniques justes : chaque note de la gamme est isolée par un
         passe-bande étroit, puis ses harmoniques sont recréées : on forme le
         signal analytique z = A·e^{jθ} et on prend Re(z²)/A, Re(z³)/A²,
         Re(z⁴)/A³ — octave, douzième, double octave, d'amplitude A, sans
         composante continue, exactement en phase avec la note. ×2 et ×4 sont
         toujours dans la gamme ; ×3 n'est ajoutée que si la quinte l'est.
         Poussées au maximum en 3.0 : ×2 ≈ −7 dB, ×3 ≈ −11 dB, ×4 ≈ −14 dB sous la note.

    Changement de gamme : gains des cloches et des harmoniques glissés
    (~120 ms), sans clic. Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

class ZeroLatencyHarmonizer
{
public:
    static constexpr int lowNote  = 28;      // mi1 ≈ 41 Hz
    static constexpr int highNote = 112;     // mi8 ≈ 5.3 kHz
    static constexpr int numNotes = highNote - lowNote + 1;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setScale (uint16_t mask) noexcept;

    /** Traite en place (0 latence). amount 0…1 (le plugin : 1). */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] {}, z2[2] {};
        inline double tick (double x, int c) noexcept      // forme directe II transposée
        {
            const double y = b0 * x + z1[c];
            z1[c] = b1 * x - a1 * y + z2[c];
            z2[c] = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
    };

    struct Note
    {
        double hz = 0.0, q = 10.0, qCut = 2.0;   // cloche (note de la gamme) / creux (note hors gamme)
        bool inScale = false;
        float freqWeight = 0.0f;          // présence du peigne selon la fréquence (fixe)
        float harmWeight = 0.0f;          // présence des harmoniques selon la fréquence (fixe)
        Biquad bell;                      // cloche du peigne (recalculée quand le gain change)
        Biquad band;                      // passe-bande d'isolation pour les harmoniques
        float bellGainDb = 0.0f;          // gain courant (lissé)
        float bellTargetDb = 0.0f;
        float harmGain = 0.0f, harmTarget = 0.0f;
        bool  twelfthInScale = false;
        float twelfthGain = 0.0f;         // ×3 glissé (pas de saut quand la gamme change)
        double bPrev[2] {}, qPrev[2] {};  // échantillon précédent du signal analytique
        double cosW = 1.0, sinW = 1.0, cosTol = 1.0;
        float  gate[2] {};                // « la note reçue est bien centrée » (lissé)
        bool  active = false;             // dans la gamme (ou en train d'en sortir)
    };

    void updateBell (Note& n) noexcept;
    void setPeaking (Biquad& f, double hz, double q, float gainDb) const noexcept;
    void setBandpass (Biquad& f, double hz, double q) const noexcept;
    void setHighShelf (Biquad& f, double hz, float gainDb) const noexcept;
    void setHighpass (Biquad& f, double hz) const noexcept;
    void setLowpass (Biquad& f, double hz) const noexcept;

    double sampleRate = 44100.0;
    std::array<Note, numNotes> notes {};
    uint16_t mask = 0;
    float gainSmoothDb = 0.1f, gainSmoothLin = 0.001f, gateSmooth = 0.01f;
    int coefCounter = 0;

    // Harmoniques : filtre anti-continu (les polynômes pairs créent un peu de DC).
    double dcX[2] {}, dcY[2] {};
    double dcR = 0.999;

    // Compensation STATIQUE par octave (8 bandes, 88 Hz → 11 kHz) : calculée à
    // partir de la réponse du peigne pour que les notes de la gamme ressortent
    // exactement au niveau d'origine (0 dB) et que l'espace entre elles soit
    // creusé. Aucune boucle de régulation : pas de pompage sur les sons tenus.
    static constexpr int numComp = 8;
    Biquad compEq[numComp];
    float compGainDb[numComp] {}, compTargetDb[numComp] {};
    // Rattrapage doux (0 … +6 dB, très lent) : seulement quand un son non
    // tonal (bruit, cymbales) perd beaucoup d'énergie dans le peigne.
    Biquad measIn[numComp], measOut[numComp];
    float eIn[numComp] {}, eOut[numComp] {}, makeupDb[numComp] {};
    float levelCoef = 0.0001f, makeupRate = 0.001f;

    double compHz[numComp] {};
    void computeCompensation() noexcept;
    double peakingDb (double centreHz, double q, double gainDb, double atHz) const noexcept;
};
