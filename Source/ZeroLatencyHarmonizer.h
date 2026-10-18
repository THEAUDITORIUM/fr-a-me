/*
  ==============================================================================
    fr(a)me — ZeroLatencyHarmonizer.h

    Traitement harmonique SANS AUCUNE LATENCE (0 échantillon), dans la lignée
    de la version 1.4, entièrement par filtres (comme un égaliseur en mode
    « zero latency ») :

      1. Peigne harmonique : une cascade de cloches étroites (RBJ peaking EQ,
         double précision), une sur CHAQUE note de la gamme de mi1 à mi8.
         Les notes de la gamme sont portées à +21 dB, puis un plateau de gain
         dynamique (≈ 250 Hz – 9 kHz, boucle fermée) ramène le niveau de cette zone à
         celui du son entrant (le grave n'est pas touché) : contraste de la 1.4
         (notes renforcées, espace entre elles creusé), sans analyse ni retard.
         Largeur : ~40 cents dans le grave → ~22 cents dans l'aigu ; effet
         progressif de 150 Hz à 700 Hz, plein jusqu'à 6 kHz, puis atténué.

      2. Harmoniques justes : chaque note de la gamme est isolée par un
         passe-bande étroit, puis
         ses harmoniques sont recréées : on forme le signal analytique
         z = A·e^{jθ} (composante en quadrature exacte à la fréquence de la
         note) et on prend Re(z²)/A, Re(z³)/A², Re(z⁴)/A³ — octave, douzième,
         double octave, d'amplitude A, sans composante continue. Exactement en phase
         avec la note, elles naissent et meurent avec elle. ×2 et ×4 sont
         toujours dans la gamme ; ×3 n'est ajoutée que si la quinte l'est.
         Niveaux après peigne (la 1.4 poussée d'un cran) : ×2 ≈ −7 dB,
        ×3 ≈ −9 dB, ×4 ≈ −11 dB sous la note.

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
        double hz = 0.0, q = 10.0;
        float freqWeight = 0.0f;          // présence du peigne selon la fréquence (fixe)
        float harmWeight = 0.0f;          // présence des harmoniques selon la fréquence (fixe)
        Biquad bell;                      // cloche du peigne (recalculée quand le gain change)
        Biquad band;                      // passe-bande d'isolation pour les harmoniques
        float bellGainDb = 0.0f;          // gain courant (lissé)
        float bellTargetDb = 0.0f;
        float harmGain = 0.0f, harmTarget = 0.0f;
        bool  twelfthInScale = false;
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

    // Compensation dans la zone du peigne (cloche large dynamique) : le grave
    // et l'extrême aigu ne sont pas touchés, comme dans la 1.4.
    Biquad measIn, measOut, measInLP, measOutLP;    // mesure 220 Hz – 9 kHz
    Biquad compShelfLo, compShelfHi;                // plateau de compensation 250 Hz – 9 kHz
    float eIn = 0.0f, eOut = 0.0f, levelCoef = 0.0001f;
    float compDb = 0.0f, compUp = 0.01f, compDown = 0.001f;
    float eFullIn = 0.0f, eFullOut = 0.0f, levelGain = 1.0f;   // volume global (±3 dB)
};
