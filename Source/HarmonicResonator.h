/*
  ==============================================================================
    fr(a)me — HarmonicResonator.h

    Banque de résonances harmoniques, « façon Pro-Q à +30 dB sur les notes »,
    mais automatique et cohérente avec la gamme choisie.

      • Un résonateur passe-bande très étroit (2 pôles, double précision) sur
        CHAQUE note de la gamme, de mi1 (41 Hz) à mi8 (5.3 kHz) : le son, même
        un clap, un hi-hat ou du souffle, se met à chanter sur ces notes.
      • Largeur réglée par un temps de résonance (~0.1–0.4 s, plus long dans le
        grave) : les notes « sonnent » comme des cordes sympathiques.
      • Cohérence tonale : chaque note est pondérée selon son DEGRÉ dans la gamme.
        L'accord de la tonalité domine — tonique 1.0, quinte 0.9, tierce 0.85
        (mineure en mineur, majeure en majeur) — puis 6te 0.55, 4te / 7e 0.5,
        2de 0.45. Les notes hors gamme ne résonnent jamais. Ex. : en la mineur,
        la / do / mi dominent ; en mi majeur, mi / sol# / si.
      • Pondération en fréquence : présence maximale 150 Hz – 3 kHz, atténuée
        dans l'extrême grave et l'extrême aigu (pas de boue, pas de sifflement).
      • Changement de gamme : les poids glissent (~120 ms), les résonateurs qui
        sortent de la gamme s'éteignent sans clic.
      • Part des résonances bornée à ~+8 dB au-dessus du son entrant (suivi
        rapide ~50 ms) : le son d'origine reste toujours présent, même quand
        une note tenue tombe pile sur un résonateur ; puis volume global
        compensé en douceur (~300 ms).

    Pas d'allocation ni de verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

class HarmonicResonator
{
public:
    static constexpr int lowNote  = 28;      // mi1 ≈ 41 Hz
    static constexpr int highNote = 112;     // mi8 ≈ 5.3 kHz
    static constexpr int numNotes = highNote - lowNote + 1;

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Gamme : masque 12 bits + tonique (0 = do … 11 = si). */
    void setScale (uint16_t mask, int tonic) noexcept;

    /** Ajoute les résonances au signal, en place. amount 0…1 (le plugin : 1). */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept;

    /** Poids tonal d'une classe de hauteur (pour les tests et l'interface). */
    float getDegreeWeight (int pitchClass) const noexcept { return degreeWeight[(size_t) (((pitchClass % 12) + 12) % 12)]; }

private:
    struct Resonator
    {
        double b0 = 0, a1 = 0, a2 = 0;         // passe-bande à gain unité au centre (b1 = 0, b2 = −b0)
        double x1[2] {}, x2[2] {}, y1[2] {}, y2[2] {};
        float freqWeight = 0.0f;               // pondération en fréquence (fixe)
        float gain = 0.0f;                     // poids courant (lissé)
        float target = 0.0f;                   // poids visé (degré × fréquence)
        bool  sleeping = true;                 // gain nul et état vidé : on saute le calcul
    };

    void updateTargets() noexcept;

    double sampleRate = 44100.0;
    std::array<Resonator, numNotes> res {};
    std::array<float, 12> degreeWeight {};
    uint16_t mask = 0;
    int tonic = 0;
    float gainSmooth = 0.001f;

    // Compensation de niveau
    float energyIn = 0.0f, energyOut = 0.0f, levelGain = 1.0f, energyCoef = 0.0001f;

    // Limitation de la part des résonances (suivi rapide)
    float fastIn = 0.0f, fastRes = 0.0f, resLimit = 1.0f;
    float fastCoef = 0.001f, limitAttack = 0.01f, limitRelease = 0.001f;
};
