/*
  ==============================================================================
    fr(a)me — PartialSnapper.h

    Recalage des partiels sur la gamme, SANS LATENCE (3.0) : le cœur de l'effet
    « chroma » sur les sons tenus (mélodies, nappes, accords).

    Pour chaque note de la gamme (la2 → ~8 kHz), une bande analytique (deux
    résonateurs complexes en cascade) couvre l'espace jusqu'aux notes voisines
    de la gamme. Dans chaque bande :
      • on mesure la fréquence réelle du partiel qui s'y trouve (rotation du
        signal analytique d'un échantillon à l'autre, moyennée ~25 ms) et sa
        stabilité (cohérence de phase) ;
      • si la bande « possède » bien ce partiel (fréquence dans sa zone) et qu'il
        est tonal, le partiel est REMPLACÉ par lui-même décalé en fréquence pour
        tomber exactement sur la note : estimation P = z / H(ω̂) (la réponse du
        filtre à la fréquence mesurée est compensée), puis
        y = x + w · Re(P · (c − 1)), c étant un phaseur qui tourne à la
        différence note − partiel.
    Un partiel déjà juste n'est pas modifié (c = 1 → y = x exactement) ; un
    partiel faux est déplacé (pas seulement atténué) : les notes hors gamme
    deviennent des notes de la gamme, les notes « entre deux » sont recentrées.
    Les attaques et les bruits (cohérence faible) passent tels quels.
    Le vibrato plus rapide que ~25 ms est gardé (seule la hauteur moyenne est
    recalée) : le son reste vivant.

    Changement de gamme : fondu croisé de 60 ms entre deux banques.
    Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

class PartialSnapper
{
public:
    static constexpr int lowNote  = 45;      // la2 (110 Hz)
    static constexpr int highNote = 120;     // ~8.4 kHz
    static constexpr int maxBands = highNote - lowNote + 1;

    void prepare (double sampleRate);
    void reset() noexcept;
    void setScale (uint16_t mask) noexcept;

    /** Traite en place. amount 0…1 : force du recalage (lissée). */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept;

private:
    using cd = std::complex<double>;

    struct Band
    {
        double hz = 0.0, w = 0.0;            // fréquence de la note, rad/éch.
        cd pole {}, rot {};                  // pôle du résonateur, e^{jW}
        double gain = 0.0, radius = 0.0;     // gain d'entrée (1 − r) et r
        double deadZone = 0.0;               // ±6 cents, en rotation par échantillon
        cd lo {}, hi {};                     // bornes de la zone (e^{jω})
        cd s1[2] {}, s2[2] {};               // états des deux résonateurs, par canal
        cd zPrev {};                         // sortie « mid » précédente
        cd dFast {}, dSlow {};               // rotation moyenne (rapide / lente)
        double mFast = 0.0;                  // énergie moyenne |z|²
        cd lagCorr {};                       // corrélation z(t)·z*(t − L) (tonalité)
        double lagEnergy = 0.0;
        cd corr { 1.0, 0.0 };                // phaseur de correction c
        cd step { 1.0, 0.0 };                // incrément de c par échantillon
        cd hInv { 1.0, 0.0 };                // 1 / H(ω̂)
        float weight = 0.0f, weightTarget = 0.0f;
        float freqWeight = 0.0f;
    };

    struct Bank
    {
        std::array<Band, maxBands> bands {};
        int count = 0;
        std::vector<std::complex<float>> history;   // maxBands × historySize (sortie « mid » passée)
    };
    static constexpr int historySize = 1024;
    int lag = 384, writePos = 0;

    void buildBank (Bank&, uint16_t mask) noexcept;
    void runBank (Bank&, const double* in, int nch, double* delta, bool updateControl, float amount) noexcept;

    double sampleRate = 44100.0;
    Bank banks[2];
    int current = 0;
    float crossfade = 1.0f, crossfadeStep = 0.001f;
    uint16_t mask = 0;
    bool hasBank = false;
    double fastCoef = 0.01, slowCoef = 0.001;
    float weightCoef = 0.01f, amountSmoothed = 0.0f, amountCoef = 0.001f;
    int counter = 0;
};
