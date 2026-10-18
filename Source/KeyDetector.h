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
      • 1) La GAMME (les 7 notes) : parmi les 12 gammes diatoniques, celle qui
        contient le plus d'énergie du morceau (c'est elle que le peigne et le
        recalage utilisent) ; 2) la TONIQUE dans cette gamme : majeur ou
        relatif mineur, départagés par la basse et les profils de tonalité
        d'Albrecht & Shanahan (2013). L'écart avec la gamme voisine donne la
        confiance.
      • Le scan s'arrête de lui-même après ~6 s de son réellement joué (les
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
    static constexpr float targetSeconds = 6.0f;     // son joué nécessaire

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

    /** Calcul hors temps réel (tests) : tonalité d'un profil chroma. */
    static void bestKey (const std::array<double, 12>& chroma, const std::array<double, 12>& bass,
                         int& tonicOut, int& modeOut, float& confidenceOut) noexcept;

private:
    void analyseFrame() noexcept;
    void resetAccumulators() noexcept;

    double sampleRate = 44100.0;
    int fftOrder = 13, fftSize = 8192, hop = 4096;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> ring, window, work;
    int ringPos = 0, sinceHop = 0;
    std::array<double, 12> chroma {}, bassChroma {};
    double activeSeconds = 0.0;

    std::atomic<bool> startRequested { false }, cancelRequested { false };
    std::atomic<bool> scanning { false }, done { false };
    std::atomic<float> progress { 0.0f }, confidence { 0.0f };
    std::atomic<int> tonic { -1 }, mode { 0 };
};
