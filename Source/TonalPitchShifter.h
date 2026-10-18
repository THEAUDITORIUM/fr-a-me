/*
  ==============================================================================
    fr(a)me — TonalPitchShifter.h

    PITCH (3.1), sans latence : transposition « partiel par partiel ».

    Le son est séparé en deux parts, à chaque instant :
      • la part TONALE : une banque de ~90 bandes analytiques (un demi-ton
        chacune, plus larges dans le grave) mesure la fréquence réelle du
        partiel qui sonne dans chaque bande ; s'il est stable (phase cohérente
        sur 8 ms) et que la bande le « possède », il est retiré du son et remis
        à ratio × sa fréquence exacte (rotation de phase continue, aucune
        fenêtre, aucun grain) : la transposition est juste au cent près, les
        harmoniques gardent leurs rapports, pas de flottement ni d'écho ;
      • le RESTE (attaques, souffle, bruits, ce qui n'est pas tonal) : transposé
        par des grains courts (GrainShifter) redémarrés à chaque attaque —
        les transitoires restent nets, une seule fois, à l'heure ;
      • quand le son est surtout tonal, ce reste (erreur d'estimation) est
        atténué pour ne pas « battre » avec les partiels transposés.
    À 0 demi-ton : contourné, son strictement inchangé.

    Aucune allocation ni verrou dans process().
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <complex>
#include <vector>
#include <memory>
#include "GrainShifter.h"

class TonalPitchShifter
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    /** semitones : −12 … +12. Traite en place. */
    void process (juce::AudioBuffer<float>& buffer, int numChannels, float semitones) noexcept;

private:
    using cd = std::complex<double>;
    static constexpr int maxBands = 128;
    static constexpr int historySize = 1024;

    // Données « lentes » d'une bande (calcul de contrôle, tous les 16 échantillons).
    struct Band
    {
        double hz = 0.0, w = 0.0;
        cd rot {};
        double gain = 0.0, radius = 0.0;
        cd lo {}, hi {};
    };

    // État « chaud » en tableaux (une case par bande) : les boucles par
    // échantillon sont vectorisées par le compilateur.
    struct Hot
    {
        alignas (32) double pr[maxBands] {}, pi[maxBands] {}, g[maxBands] {};
        alignas (32) double sr[4][2][maxBands] {}, si[4][2][maxBands] {};
        alignas (32) double zpr[maxBands] {}, zpi[maxBands] {};
        alignas (32) double dsr[maxBands] {}, dsi[maxBands] {}, dfr[maxBands] {}, dfi[maxBands] {};
        alignas (32) double lcr[maxBands] {}, lci[maxBands] {}, le[maxBands] {};
        alignas (32) double cr[maxBands] {}, ci[maxBands] {}, str[maxBands] {}, sti[maxBands] {};
        alignas (32) double hr[maxBands] {}, hi[maxBands] {};
        alignas (32) double w[maxBands] {}, wt[maxBands] {};
    };

    void buildBands() noexcept;

    double sampleRate = 44100.0;
    std::array<Band, maxBands> bands {};
    std::unique_ptr<Hot> hot = std::make_unique<Hot>();
    int count = 0;
    std::vector<std::complex<float>> history;
    int lag = 384, writePos = 0, counter = 0;
    double slowCoef = 0.001, fastCoef = 0.01;
    float weightCoef = 0.01f;
    double ratioSmoothed = 1.0, ratioCoef = 0.001;
    float active = 0.0f, activeCoef = 0.001f;

    // Reste (non tonal) : décaleur temporel classique, et détecteur d'attaques.
    GrainShifter residualShifter;          // grains courts, redémarrés à chaque attaque
    std::vector<uint8_t> onsetFlags;
    juce::AudioBuffer<float> residual, residualDry, dryCopy, transientCurve;
    float envFast = 0.0f, envSlow = 0.0f, transient = 0.0f;
    int keepCountdown = 0, keepLength = 240;   // attaque laissée intacte pendant ~5 ms
    bool armed = true;
    float eTonal = 0.0f, eAll = 0.0f, duckCoef = 0.001f;   // part tonale du son (pour atténuer les restes d'estimation)
    float envFastAtt = 0.1f, envFastRel = 0.01f, envSlowCoef = 0.001f, transientRel = 0.001f;
};
