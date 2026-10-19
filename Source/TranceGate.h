/*
  ==============================================================================
    fr(a)me — TranceGate.h

    GATE (3.3) : coupe le son sur les temps, en 16 pas par mesure (doubles
    croches), calé sur le tempo et la position du DAW. 15 motifs prêts à
    l'emploi ; désactivé au lancement, on l'allume quand on veut.

      • chaque pas a un niveau (0 … 1) ; il s'ouvre au début du pas et se
        referme après une part du pas propre au motif (longueur) ;
      • enveloppe sans clic : ouverture ~1 ms, fermeture ~4 ms ;
      • allumer / éteindre / changer de motif : fondu de 20 ms ;
      • transport arrêté : le gate tourne seul au tempo du DAW (120 par défaut).
    0 latence : un simple gain par échantillon, aucune allocation.
  ==============================================================================
*/

#pragma once

#include <array>

class TranceGate
{
public:
    static constexpr int numSteps = 16;
    static constexpr int numPresets = 15;

    struct Preset
    {
        const char* name;
        float length;                              // part du pas ouverte (0 … 1)
        std::array<float, numSteps> steps;         // niveau de chaque pas
    };

    static const Preset& preset (int index) noexcept;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Gain pour un échantillon. ppq : position en noires (temps du DAW). */
    float next (double ppq, bool on, int presetIndex) noexcept;

    /** Pas joué (0 … 15), pour l'interface. */
    int getStep() const noexcept { return step; }

private:
    double sampleRate = 44100.0;
    float env = 1.0f, depth = 0.0f;
    float attack = 0.02f, release = 0.005f, depthCoef = 0.001f;
    int step = 0, shownPreset = 0;
    float swap = 1.0f;                             // fondu au changement de motif
};
