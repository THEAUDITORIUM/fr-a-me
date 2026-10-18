/*
  ==============================================================================
    fr(a)me — KeyDetector.h

    SCAN (3.0) : détection de la tonalité (tonique + majeur / mineur) du son
    qui entre dans le plugin, pendant qu'on joue la mélodie dans le DAW.

      • Le son d'entrée (mono) est analysé par FFT (8192 points à 44.1 / 48 kHz,
        recouvrement 50 %, fenêtre de Hann), uniquement pendant un scan.
      • Les pics du spectre (70 Hz → 5 kHz) sont rangés par classe de hauteur
        (12 notes), pondérés par leur amplitude : profil « chroma » du morceau.
        Les basses comptent un peu plus (elles portent souvent la tonique).
      • (3.1) Accordage estimé (le sample n'est pas forcément en la 440) :
        histogramme fin à 10 cents, replié sur 12 notes autour de l'accordage
        trouvé. Les harmoniques (×3, ×5, ×6, ×7 d'un pic plus grave) sont
        écartées : elles poussaient la détection vers la quinte.
      • Score de chacune des 24 tonalités : ressemblance au profil de
        Albrecht & Shanahan + part de l'énergie dans sa gamme + poids de la
        tonique à la basse. Les deux suivantes sont proposées.
      • Le scan s'arrête de lui-même après ~8 s de son réellement joué (les
        silences ne comptent pas).

    Fil audio : push() ne fait que copier et, une fois par saut, une FFT
    (aucune allocation). Fil interface : start(), lecture des résultats
    (atomiques), sans verrou.
  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

class KeyDetector
{
public:
    static constexpr float targetSeconds = 8.0f;     // son joué nécessaire

    void prepare (double sampleRate);

    /** Fil interface : lance (ou relance) un scan. */
    void start() noexcept   { startRequested.store (true); }
    void cancel() noexcept  { cancelRequested.store (true); }

    /** Fil audio : son d'entrée (avant tout traitement). */
    void push (const float* left, const float* right, int numSamples) noexcept;

    // Lecture (fil interface)
    bool  isScanning() const noexcept   { return scanning.load(); }
    bool  isDone() const noexcept       { return done.load(); }
    float getProgress() const noexcept  { return progress.load(); }          // 0 … 1
    int   getTonic() const noexcept     { return tonic.load(); }             // 0 = do … 11 = si, −1 = inconnue
    int   getMode() const noexcept      { return mode.load(); }              // 0 = majeur, 1 = mineur
    float getConfidence() const noexcept{ return confidence.load(); }        // 0 … 1
    /** Deuxième et troisième tonalités les plus probables (−1 si aucune). */
    int   getAltTonic (int i) const noexcept { return altTonic[juce::jlimit (0, 1, i)].load(); }
    int   getAltMode (int i) const noexcept  { return altMode[juce::jlimit (0, 1, i)].load(); }

    struct Candidate { int tonic = -1, mode = 0; double score = -1.0e9; };
    static void rankKeys (const std::array<double, 12>& chroma, const std::array<double, 12>& bass,
                          std::array<Candidate, 3>& top) noexcept;

    /** Calcul hors temps réel (tests) : tonalité d'un profil chroma. */
    static void bestKey (const std::array<double, 12>& chroma, const std::array<double, 12>& bass,
                         int& tonicOut, int& modeOut, float& confidenceOut) noexcept;

    /** Tests : profils accumulés (12 notes, basses) et accordage estimé. */
    const std::array<double, 12>& debugChroma() const noexcept { return chroma; }
    const std::array<double, 12>& debugBass() const noexcept   { return bassChroma; }
    double debugTuning() const noexcept { return tuningCents; }

private:
    void analyseFrame() noexcept;
    void resetAccumulators() noexcept;

    double sampleRate = 44100.0;
    int fftOrder = 13, fftSize = 8192, hop = 4096;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> ring, window, work;
    int ringPos = 0, sinceHop = 0;
    static constexpr int fineBins = 120;                  // 10 cents
    std::array<double, 12> chroma {}, bassChroma {};
    std::array<double, fineBins> fine {}, fineBass {};
    double tuningCents = 0.0;
    static void fold (const std::array<double, fineBins>& f, const std::array<double, fineBins>& fb,
                      std::array<double, 12>& c, std::array<double, 12>& b, double& tuning) noexcept;
    double activeSeconds = 0.0;

    std::atomic<bool> startRequested { false }, cancelRequested { false };
    std::atomic<bool> scanning { false }, done { false };
    std::atomic<float> progress { 0.0f }, confidence { 0.0f };
    std::atomic<int> tonic { -1 }, mode { 0 };
    std::atomic<int> altTonic[2] { -1, -1 }, altMode[2] { 0, 0 };
};
