/*
  ==============================================================================
    fr(a)me — SpectralEngine.h

    Moteur d'harmonisation spectrale.

    Principe (vocodeur de phase "phase-locked", d'après Laroche & Dolson,
    « New phase-vocoder techniques for pitch-shifting, harmonizing and other
    exotic effects », IEEE WASPAA 1999) :

      1. STFT : fenêtre de Hann, N = 4096 (44.1/48 kHz, adapté aux autres
         fréquences d'échantillonnage), recouvrement 75 % (hop = N/4).
      2. Le spectre "mid" (moyenne des canaux) sert à l'analyse : détection des
         pics, estimation de la fréquence réelle de chaque pic par l'écart de
         phase entre deux trames (vocodeur de phase).
      3. Le spectre est découpé en "régions d'influence" : chaque région
         contient un pic et les bins qui l'entourent jusqu'au creux suivant.
      4. Pour chaque pic on calcule la note de la gamme la plus proche, puis la
         région entière est déplacée d'un nombre entier de bins vers la cible
         et multipliée par un phaseur e^{jθ}. θ est accumulé de trame en trame
         (θ += 2π·hop·(f_cible − f_réelle)/fs) en suivant l'ancêtre du pic dans
         la trame précédente : la fréquence perçue est exactement la cible, la
         cohérence de phase horizontale (dans le temps) et verticale (entre les
         bins d'un même partiel) est conservée — pas de "phasiness".
         Comme les bins sont déplacés tels quels, enveloppe, bruit et
         transitoires de chaque partiel sont préservés : on ne resynthétise
         pas, on recale.
      5. Le même déplacement / phaseur est appliqué à chaque canal : l'image
         stéréo est conservée.
      6. IFFT, fenêtre de synthèse de Hann, overlap-add.

    Contraintes temps réel : toutes les mémoires sont allouées dans prepare().
    process() n'alloue rien et ne prend aucun verrou. Les données de
    visualisation sont publiées dans une LockFreeQueue.
  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <complex>
#include <memory>
#include <vector>

#include "LockFreeQueue.h"

/** Une "photographie" d'une trame STFT, envoyée à l'interface. */
struct SpectralFrameInfo
{
    static constexpr int numSpectrumRows = 192;   // lignes du spectrogramme (échelle log)
    static constexpr int maxPeaks        = 48;    // pics affichés dans la partition

    float spectrum[numSpectrumRows] {};           // niveau 0..1 du spectre de sortie, du grave à l'aigu
    int   numPeaks = 0;
    float peakFreq[maxPeaks] {};                  // fréquence de sortie (Hz)
    float peakLevel[maxPeaks] {};                 // niveau 0..1
    bool  peakSnapped[maxPeaks] {};               // true = le pic a été recalé sur une note
};

class SpectralEngine
{
public:
    static constexpr float displayMinHz = 40.0f;      // bornes de l'axe fréquentiel affiché
    static constexpr float displayMaxHz = 12000.0f;

    SpectralEngine() = default;

    /** Alloue tout. À appeler hors du thread audio (prepareToPlay). */
    void prepare (double sampleRate, int maxNumChannels);

    /** Remet à zéro les tampons (pas d'allocation). */
    void reset() noexcept;

    /** Latence totale introduite, en échantillons. Un échantillon n'est
        complet qu'après le passage des 4 trames qui le recouvrent : la
        latence vaut exactement N (4096 à 44.1/48 kHz). Vérifiée par le banc
        de test (reconstruction à l'échantillon près). */
    int getLatencySamples() const noexcept { return fftSize; }

    int getFftSize() const noexcept { return fftSize; }

    /** Change la gamme (masque 12 bits). Appelé depuis le thread audio,
        au début de chaque bloc. Le changement est fondu sur ~80 ms. */
    void setScaleMask (uint16_t mask) noexcept;

    /** Proportion du recalage (1 = recalage complet, 0 = identité).
        Réservé aux tests / à l'usage interne : le plugin utilise 1. */
    void setCorrectionAmount (float amount) noexcept { correctionAmount = juce::jlimit (0.0f, 1.0f, amount); }

    /** Traite le bloc en place : en sortie, le signal 100 % traité ("wet"),
        retardé de getLatencySamples(). Nombre de canaux ≤ maxNumChannels. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    /** File des trames de visualisation (lue par l'UI). */
    LockFreeQueue<SpectralFrameInfo, 32>& getVisualQueue() noexcept { return visualQueue; }

private:
    using Complex = std::complex<float>;

    void processFrame() noexcept;
    float targetFrequencyFor (float hz) const noexcept;
    void publishVisualFrame (int numChannelsUsed) noexcept;

    // --- configuration -------------------------------------------------------
    double sampleRate = 44100.0;
    int fftOrder = 12, fftSize = 4096, hopSize = 1024, numBins = 2049;
    int numChannels = 2, activeChannels = 2;
    float binHz = 44100.0f / 4096.0f;
    float olaGain = 1.0f;
    float correctionAmount = 1.0f;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;

    // --- tampons d'entrée / sortie (un par canal) -----------------------------
    std::vector<std::vector<float>> inFifo;       // N échantillons
    std::vector<std::vector<float>> outFifo;      // N échantillons
    std::vector<std::vector<float>> outAccum;     // 2N, accumulateur overlap-add
    int rover = 0;                                // position d'écriture commune

    // --- espace de travail spectral ------------------------------------------
    std::vector<float> fftWork;                   // 2N floats
    std::vector<std::vector<Complex>> spectrumIn; // X_c[k]
    std::vector<std::vector<Complex>> spectrumOut;// Z_c[k]
    std::vector<float> magnitude, phase, prevPhase, trueBin;

    // --- pics et régions ------------------------------------------------------
    static constexpr int maxPeaksInternal = 512;
    std::vector<int>   peakBin;                   // bin du pic
    std::vector<int>   regionStart, regionEnd;    // région [start, end]
    std::vector<float> peakTheta;                 // θ accumulé du pic
    std::vector<float> peakTargetHz, peakTrueHz;
    std::vector<int>   peakShift;                 // décalage entier (bins)
    std::vector<int>   peakOrder;                 // tri des pics pour l'affichage
    int numPeaks = 0;

    std::vector<int>   prevOwner;                 // pour chaque bin : index du pic qui le possédait à la trame précédente (-1 sinon)
    std::vector<float> prevTheta;                 // θ des pics de la trame précédente
    std::vector<float> prevPeakTrueHz;            // fréquence réelle des pics précédents
    std::vector<int>   prevTonalAge, peakTonalAge;// nb de trames où le partiel est stable
    std::vector<float> prevMagnitude;             // spectre mid précédent (détection d'attaque)
    int prevNumPeaks = 0;
    bool firstFrame = true;

    // --- gamme et fondu de gamme ---------------------------------------------
    uint16_t currentMask = 0, previousMask = 0;
    float scaleFade = 1.0f;                       // 0 → 1 : de previousMask vers currentMask
    float scaleFadeStep = 0.25f;

    // --- visualisation --------------------------------------------------------
    LockFreeQueue<SpectralFrameInfo, 32> visualQueue;
    SpectralFrameInfo scratchInfo;
    std::vector<int> rowBinStart, rowBinEnd;      // correspondance ligne log → bins

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralEngine)
};
