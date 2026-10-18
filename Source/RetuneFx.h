/*
  ==============================================================================
    fr(a)me — RetuneFx.h

    Modulations du RETUNE (2.2), appliquées au son vocodé, en série :
      1. PHASER  : 6 passe-tout du 1er ordre, balayage 180 Hz → 3.4 kHz
                   (LFO 0.21 Hz), rétroaction 0.5, gauche / droite en quadrature.
      2. FLANGER : retard 0.5 → 4.5 ms (LFO 0.13 Hz), rétroaction 0.62,
                   gauche / droite en opposition : le « jet » métallique.
      3. CHORUS  : 3 voix (11 / 16 / 23 ms, ±1.4 ms, LFO 0.33 / 0.47 / 0.61 Hz,
                   désaccord ≤ 9 cents : le son reste accordé)
                   réparties dans l'espace : largeur et épaisseur.
    Chaque effet garde son chemin direct : aucun retard sur le son, 0 latence.
    Aucune allocation ni verrou dans processFrame().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

class RetuneFx
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;

    /** Traite un échantillon stéréo (l, r) en place. 'mono' : un seul canal utile. */
    void processFrame (double& l, double& r, bool mono) noexcept;

private:
    double sampleRate = 44100.0;

    // Phaser
    static constexpr int phaserStages = 6;
    double apState[2][phaserStages] {};
    double phaserFb[2] {};
    double phaserLfo = 0.0, phaserInc = 0.0;
    double apCoef[2] { 0.0, 0.0 };
    int coefCounter = 0;

    // Lignes à retard (flanger + chorus)
    std::vector<float> flangeLine[2], chorusLine[2];
    int flangeSize = 1, chorusSize = 1, flangeWrite = 0, chorusWrite = 0;
    double flangeFb[2] {};
    double flangeLfo = 0.0, flangeInc = 0.0;
    double chorusLfo[3] {}, chorusInc[3] {};

    static double readLine (const std::vector<float>& line, int size, int write, double delaySamples) noexcept;
};
