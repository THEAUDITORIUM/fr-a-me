/*
  ==============================================================================
    fr(a)me — SpectralEngine.cpp
    Voir SpectralEngine.h pour la description de l'algorithme.
  ==============================================================================
*/

#include "SpectralEngine.h"
#include "Scale.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr float pi    = juce::MathConstants<float>::pi;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    /** Ramène un angle dans [-π, π]. */
    inline float wrapPhase (float x) noexcept
    {
        return x - twoPi * std::floor ((x + pi) / twoPi);
    }

    /** Courbe en S pour le fondu de gamme (évite une cassure de pente). */
    inline float smoothStep (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }
}

//==============================================================================
void SpectralEngine::prepare (double newSampleRate, int maxNumChannels)
{
    sampleRate  = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    numChannels = juce::jmax (1, maxNumChannels);
    activeChannels = numChannels;

    // Fenêtre de ~43–46 ms (2048 à 44.1/48 kHz, 4096 à 88.2/96 kHz, 8192 au-delà) :
    // latence divisée par deux par rapport aux versions ≤ 1.8, l'effet suit le
    // son de près ; le vocodeur de phase garde une estimation fine des fréquences.
    fftOrder = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
    fftSize  = 1 << fftOrder;
    hopSize  = fftSize / 4;                       // recouvrement 75 %
    numBins  = fftSize / 2 + 1;
    binHz    = (float) (sampleRate / fftSize);

    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    // Fenêtre de Hann périodique (somme parfaite en overlap-add).
    window.assign ((size_t) fftSize, 0.0f);
    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (twoPi * (float) n / (float) fftSize);

    // Gain de normalisation : analyse + synthèse appliquent chacune la fenêtre,
    // la somme des w² décalées de hop vaut 1.5 pour Hann à 75 %.
    {
        // moyenne de Σ w²(n + m·hop) sur une période de hop
        double acc = 0.0;
        for (int n = 0; n < hopSize; ++n)
        {
            double s = 0.0;
            for (int m = n; m < fftSize; m += hopSize)
                s += (double) window[(size_t) m] * window[(size_t) m];
            acc += s;
        }
        olaGain = (float) (1.0 / (acc / hopSize));
    }

    auto alloc2D = [this] (std::vector<std::vector<float>>& v, int size)
    {
        v.assign ((size_t) numChannels, std::vector<float> ((size_t) size, 0.0f));
    };
    alloc2D (inFifo,   fftSize);
    alloc2D (outFifo,  fftSize);
    alloc2D (outAccum, fftSize * 2);

    fftWork.assign ((size_t) fftSize * 2, 0.0f);
    spectrumIn.assign  ((size_t) numChannels, std::vector<Complex> ((size_t) numBins));
    spectrumOut.assign ((size_t) numChannels, std::vector<Complex> ((size_t) numBins));
    magnitude.assign ((size_t) numBins, 0.0f);
    phase.assign     ((size_t) numBins, 0.0f);
    prevPhase.assign ((size_t) numBins, 0.0f);
    trueBin.assign   ((size_t) numBins, 0.0f);

    peakBin.assign      (maxPeaksInternal, 0);
    regionStart.assign  (maxPeaksInternal, 0);
    regionEnd.assign    (maxPeaksInternal, 0);
    peakTheta.assign    (maxPeaksInternal, 0.0f);
    peakTargetHz.assign (maxPeaksInternal, 0.0f);
    peakTrueHz.assign   (maxPeaksInternal, 0.0f);
    peakShift.assign    (maxPeaksInternal, 0);
    peakOrder.assign    (maxPeaksInternal, 0);
    prevOwner.assign ((size_t) numBins, -1);
    prevTheta.assign (maxPeaksInternal, 0.0f);
    peakAncestor.assign (maxPeaksInternal, -1);
    harmState.assign (maxPeaksInternal, HarmState {});
    prevHarmState.assign (maxPeaksInternal, HarmState {});
    prevPeakTrueHz.assign (maxPeaksInternal, 0.0f);
    prevTonalAge.assign (maxPeaksInternal, 0);
    peakTonalAge.assign (maxPeaksInternal, 0);
    prevMagnitude.assign ((size_t) numBins, 0.0f);

    // Durée du fondu lors d'un changement de gamme : ~100 ms.
    scaleFadeStep = juce::jlimit (0.05f, 1.0f, (float) hopSize / (float) (0.1 * sampleRate));

    combCurveDb.assign ((size_t) numBins, 0.0f);
    combPrevCurveDb.assign ((size_t) numBins, 0.0f);
    combGain.assign ((size_t) numBins, 1.0f);
    combMask = combPrevMask = 0xFFFF;                    // force le premier calcul

    // Lignes du spectrogramme (axe logarithmique) → plages de bins.
    rowBinStart.assign (SpectralFrameInfo::numSpectrumRows, 0);
    rowBinEnd.assign   (SpectralFrameInfo::numSpectrumRows, 0);
    const float ratio = displayMaxHz / displayMinHz;
    for (int r = 0; r < SpectralFrameInfo::numSpectrumRows; ++r)
    {
        const float f0 = displayMinHz * std::pow (ratio, (float) r       / SpectralFrameInfo::numSpectrumRows);
        const float f1 = displayMinHz * std::pow (ratio, (float) (r + 1) / SpectralFrameInfo::numSpectrumRows);
        const int b0 = juce::jlimit (1, numBins - 1, (int) std::floor (f0 / binHz + 0.5f));
        const int b1 = juce::jlimit (b0, numBins - 1, (int) std::floor (f1 / binHz + 0.5f));
        rowBinStart[(size_t) r] = b0;
        rowBinEnd[(size_t) r]   = b1;
    }

    reset();
}

void SpectralEngine::reset() noexcept
{
    for (auto& v : inFifo)   std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : outFifo)  std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : outAccum) std::fill (v.begin(), v.end(), 0.0f);
    std::fill (prevPhase.begin(), prevPhase.end(), 0.0f);
    std::fill (prevOwner.begin(), prevOwner.end(), -1);
    std::fill (prevTheta.begin(), prevTheta.end(), 0.0f);
    for (auto& h : harmState) h.valid = false;
    for (auto& h : prevHarmState) h.valid = false;
    std::fill (prevTonalAge.begin(), prevTonalAge.end(), 0);
    std::fill (prevMagnitude.begin(), prevMagnitude.end(), 0.0f);
    prevNumPeaks = 0;

    rover = fftSize - hopSize;
    combNorm = 1.0f;

    numPeaks = 0;
    firstFrame = true;
    previousMask = currentMask;
    scaleFade = 1.0f;
}

void SpectralEngine::setScaleMask (uint16_t mask) noexcept
{
    if (mask == currentMask)
        return;

    if (currentMask == 0)          // première gamme : pas de fondu
    {
        currentMask = previousMask = mask;
        scaleFade = 1.0f;
        return;
    }

    previousMask = currentMask;    // fondu de l'ancienne gamme vers la nouvelle
    currentMask  = mask;
    scaleFade    = 0.0f;
}

//==============================================================================
void SpectralEngine::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const int nch = juce::jmin (buffer.getNumChannels(), numChannels);
    const int numSamples = buffer.getNumSamples();
    if (nch <= 0 || fft == nullptr)
        return;

    activeChannels = nch;
    const int fill = fftSize - hopSize;   // remplissage de la FIFO d'entrée (≠ latence totale)

    float* channelData[8] {};
    for (int c = 0; c < juce::jmin (nch, 8); ++c)
        channelData[c] = buffer.getWritePointer (c);

    for (int i = 0; i < numSamples; ++i)
    {
        // Écrit l'entrée dans la FIFO et lit la sortie déjà reconstruite.
        for (int c = 0; c < nch; ++c)
        {
            float* d = channelData[c];
            inFifo[(size_t) c][(size_t) rover] = std::isfinite (d[i]) ? d[i] : 0.0f;
            d[i] = outFifo[(size_t) c][(size_t) (rover - fill)];
        }

        if (++rover >= fftSize)
        {
            rover = fill;
            processFrame();

            for (int c = 0; c < nch; ++c)
            {
                auto& acc = outAccum[(size_t) c];
                auto& in  = inFifo[(size_t) c];

                // Les "hop" premiers échantillons de l'accumulateur sont terminés.
                std::memcpy (outFifo[(size_t) c].data(), acc.data(), sizeof (float) * (size_t) hopSize);
                // Décale l'accumulateur et la FIFO d'entrée de hop échantillons.
                std::memmove (acc.data(), acc.data() + hopSize, sizeof (float) * (size_t) fftSize);
                std::fill (acc.begin() + fftSize, acc.end(), 0.0f);
                std::memmove (in.data(), in.data() + hopSize, sizeof (float) * (size_t) fill);
            }
        }
    }
}

//==============================================================================
float SpectralEngine::targetFrequencyFor (float hz) const noexcept
{
    // On ne touche ni l'infra-grave ni l'extrême aigu (souffle, air, cymbales).
    const float maxHz = juce::jmin (16000.0f, (float) sampleRate * 0.45f);
    if (hz < 25.0f || hz > maxHz || currentMask == 0)
        return hz;

    const double midi = frame::hzToMidi (hz);
    const double keep = 0.5;                       // moitié de l'écart gardée (mesuré sur Chroma)
    double target = frame::chromaTargetMidi (midi, currentMask, keep);

    if (scaleFade < 1.0f)
    {
        const double old = frame::chromaTargetMidi (midi, previousMask, keep);
        target = old + (target - old) * (double) smoothStep (scaleFade);
    }

    // Recalage partiel (interne/tests) : interpolation en demi-tons.
    target = midi + (target - midi) * (double) correctionAmount;
    return (float) frame::midiToHz (target);
}

void SpectralEngine::processFrame() noexcept
{
    const int N = fftSize;
    const int H = hopSize;
    const int nch = activeChannels;
    const float expectedAdvance = twoPi * (float) H / (float) N;   // avance de phase par bin et par hop
    const float invNch = 1.0f / (float) nch;

    // ---- 1. Analyse : fenêtrage + FFT de chaque canal ------------------------
    for (int c = 0; c < nch; ++c)
    {
        const float* in = inFifo[(size_t) c].data();
        for (int n = 0; n < N; ++n)
            fftWork[(size_t) n] = in[n] * window[(size_t) n];
        std::fill (fftWork.begin() + N, fftWork.end(), 0.0f);

        fft->performRealOnlyForwardTransform (fftWork.data(), true);

        auto& X = spectrumIn[(size_t) c];
        for (int k = 0; k < numBins; ++k)
            X[(size_t) k] = { fftWork[(size_t) (2 * k)], fftWork[(size_t) (2 * k + 1)] };
    }

    // ---- 2. Spectre "mid" : amplitude, phase, fréquence réelle ---------------
    float maxMag = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        Complex m = spectrumIn[0][(size_t) k];
        for (int c = 1; c < nch; ++c)
            m += spectrumIn[(size_t) c][(size_t) k];
        m *= invNch;

        const float mag = std::abs (m);
        const float ph  = std::arg (m);
        magnitude[(size_t) k] = mag;
        maxMag = juce::jmax (maxMag, mag);

        // Vocodeur de phase : l'écart à l'avance de phase attendue donne la
        // déviation de fréquence par rapport au centre du bin.
        const float delta = wrapPhase (ph - prevPhase[(size_t) k] - (float) k * expectedAdvance);
        trueBin[(size_t) k] = firstFrame ? (float) k : (float) k + delta / expectedAdvance;
        prevPhase[(size_t) k] = ph;
        phase[(size_t) k] = ph;
    }

    // ---- 3. Détection des pics ----------------------------------------------
    // Seuil : −80 dB sous le maximum de la trame et −100 dBFS absolu.
    const float fullScale = (float) N * 0.25f;            // amplitude d'un sinus 0 dBFS
    const float threshold = juce::jmax (maxMag * 1.0e-4f, fullScale * 1.0e-5f);

    numPeaks = 0;
    for (int k = 2; k < numBins - 2 && numPeaks < maxPeaksInternal; ++k)
    {
        const float m = magnitude[(size_t) k];
        if (m > threshold
            && m >  magnitude[(size_t) (k - 1)] && m >= magnitude[(size_t) (k + 1)])
        {
            peakBin[(size_t) numPeaks++] = k;
        }
    }

    // ---- 4. Régions d'influence (limites au creux entre deux pics) -----------
    for (int p = 0; p < numPeaks; ++p)
    {
        regionStart[(size_t) p] = 0;
        regionEnd[(size_t) p]   = numBins - 1;
    }
    for (int p = 1; p < numPeaks; ++p)
    {
        int minBin = peakBin[(size_t) (p - 1)] + 1;
        for (int k = minBin; k < peakBin[(size_t) p]; ++k)
            if (magnitude[(size_t) k] < magnitude[(size_t) minBin])
                minBin = k;

        regionEnd[(size_t) (p - 1)] = minBin - 1;
        regionStart[(size_t) p]     = minBin;
    }

    // ---- 5. Cible, décalage entier et phaseur de chaque pic ------------------
    // Un pic n'est recalé que s'il est "tonal" : sa fréquence réelle est stable
    // d'une trame à l'autre (même partiel suivi). Le bruit et les attaques
    // (pics instables ou qui viennent d'apparaître) passent sans décalage et
    // avec leur phase d'origine : les transitoires restent intacts.
    const float onsetRatio = 2.0f;                         // +6 dB d'une trame à l'autre = attaque
    // Tolérance de stabilité en Hz, indépendante de la taille de fenêtre (≈ celle
    // de la 1.4) : une fenêtre plus courte ne rend pas le suivi plus laxiste.
    const float stableToleranceHz = juce::jmin (binHz, 11.0f);
    for (int p = 0; p < numPeaks; ++p)
    {
        const int k = peakBin[(size_t) p];
        const float trueHz = juce::jmax (0.0f, trueBin[(size_t) k] * binHz);

        // Ancêtre = pic qui possédait ce bin à la trame précédente.
        const int ancestor = firstFrame ? -1 : prevOwner[(size_t) k];
        const bool hasAncestor = ancestor >= 0 && ancestor < prevNumPeaks;

        const bool stable = hasAncestor
                         && std::abs (trueHz - prevPeakTrueHz[(size_t) ancestor]) < stableToleranceHz
                         && std::abs (trueBin[(size_t) k] - (float) k) < 1.5f;
        const bool onset  = magnitude[(size_t) k] > onsetRatio * prevMagnitude[(size_t) k];

        const int age = (stable && ! onset) ? juce::jmin (1000, prevTonalAge[(size_t) ancestor] + 1) : 0;
        peakTonalAge[(size_t) p] = age;
        peakAncestor[(size_t) p] = (age >= 1) ? ancestor : -1;

        const float target = (age >= 1) ? targetFrequencyFor (trueHz) : trueHz;

        peakTrueHz[(size_t) p]   = trueHz;
        peakTargetHz[(size_t) p] = target;
        peakShift[(size_t) p]    = (int) std::lround ((target - trueHz) / binHz);

        // θ prolonge celui de l'ancêtre (phase continue dans le temps) ;
        // nouveau partiel, bruit ou attaque → θ = 0 (phase d'origine).
        float theta = (age >= 1) ? prevTheta[(size_t) ancestor] : 0.0f;
        theta += twoPi * (float) H * (target - trueHz) / (float) sampleRate;
        peakTheta[(size_t) p] = wrapPhase (theta);
    }

    // Mémorise la propriété des bins et l'état des pics pour la trame suivante.
    std::fill (prevOwner.begin(), prevOwner.end(), -1);
    for (int p = 0; p < numPeaks; ++p)
    {
        for (int k = regionStart[(size_t) p]; k <= regionEnd[(size_t) p]; ++k)
            prevOwner[(size_t) k] = p;
        prevTheta[(size_t) p]      = peakTheta[(size_t) p];
        prevPeakTrueHz[(size_t) p] = peakTrueHz[(size_t) p];
        prevTonalAge[(size_t) p]   = peakTonalAge[(size_t) p];
    }
    prevNumPeaks = numPeaks;
    std::copy (magnitude.begin(), magnitude.end(), prevMagnitude.begin());

    // L'état des harmoniques de la trame précédente devient « précédent ».
    std::swap (harmState, prevHarmState);
    for (auto& h : harmState) h.valid = false;

    // ---- 6. Déplacement des régions (même opération sur chaque canal) --------
    for (int c = 0; c < nch; ++c)
    {
        const auto& X = spectrumIn[(size_t) c];
        auto& Z = spectrumOut[(size_t) c];

        if (numPeaks == 0)
        {
            std::copy (X.begin(), X.end(), Z.begin());   // silence / bruit très faible : inchangé
            continue;
        }

        std::fill (Z.begin(), Z.end(), Complex {});

        for (int p = 0; p < numPeaks; ++p)
        {
            const Complex rot = std::polar (1.0f, peakTheta[(size_t) p]);
            const int shift = peakShift[(size_t) p];
            const int s = juce::jmax (regionStart[(size_t) p], -shift);
            const int e = juce::jmin (regionEnd[(size_t) p], numBins - 1 - shift);

            for (int k = s; k <= e; ++k)
                Z[(size_t) (k + shift)] += X[(size_t) k] * rot;
        }

    }

    // ---- 6b. Harmoniques justes (octaves, quintes de la gamme) ----------------
    addHarmonics (nch);

    // ---- 6c. Peigne harmonique « COLOR » -------------------------------------
    applyColourComb (nch);

    for (int c = 0; c < nch; ++c)
    {
        // DC et Nyquist doivent rester réels pour une IFFT réelle.
        auto& Z = spectrumOut[(size_t) c];
        Z[0] = { Z[0].real(), 0.0f };
        Z[(size_t) (numBins - 1)] = { Z[(size_t) (numBins - 1)].real(), 0.0f };
    }

    // ---- 7. Resynthèse : IFFT, fenêtre de synthèse, overlap-add --------------
    for (int c = 0; c < nch; ++c)
    {
        const auto& Z = spectrumOut[(size_t) c];
        for (int k = 0; k < numBins; ++k)
        {
            fftWork[(size_t) (2 * k)]     = Z[(size_t) k].real();
            fftWork[(size_t) (2 * k + 1)] = Z[(size_t) k].imag();
        }
        std::fill (fftWork.begin() + 2 * numBins, fftWork.end(), 0.0f);

        fft->performRealOnlyInverseTransform (fftWork.data());

        float* acc = outAccum[(size_t) c].data();
        for (int n = 0; n < N; ++n)
            acc[n] += window[(size_t) n] * fftWork[(size_t) n] * olaGain;
    }

    publishVisualFrame (nch);

    firstFrame = false;
    if (scaleFade < 1.0f)
        scaleFade = juce::jmin (1.0f, scaleFade + scaleFadeStep);
}

//==============================================================================
//  Peigne harmonique « COLOR »
//==============================================================================
void SpectralEngine::setColour (float c) noexcept
{
    colour = juce::jlimit (0.0f, 1.0f, c);
}

void SpectralEngine::computeCombCurve (uint16_t mask, std::vector<float>& curveDb) const noexcept
{
    // Courbe en dB, par bin, pour une profondeur de 100 % :
    //   +6.5 dB sur les notes de la gamme (pics étroits, ~15 cents),
    //   −9.5 dB entre elles (la 1.4 poussée un cran plus loin, à la demande), actif de ~250 Hz à ~12 kHz (pleine action 700 Hz–6 kHz),
    //   puis niveau global compensé.
    // Calé en comparant fr(a)me et Chroma sur le même extrait (démo vidéo) :
    // écart moyen par note ≈ 1.5 dB entre 600 Hz et 4 kHz, niveau identique.
    constexpr float peakDb = 6.5f, floorDb = -9.5f, sigmaCents = 15.0f;

    for (int k = 0; k < numBins; ++k)
    {
        const float hz = (float) k * binHz;
        const float lowRamp  = smoothStep ((hz - 250.0f) / 450.0f);
        const float highRamp = 1.0f - smoothStep ((hz - 6000.0f) / 6000.0f);
        const float w = lowRamp * highRamp;

        if (w <= 0.0f || mask == 0)
        {
            curveDb[(size_t) k] = 0.0f;
            continue;
        }

        const double midi = frame::hzToMidi (hz);
        const float distCents = (float) std::abs (midi - frame::nearestScaleMidi (midi, mask)) * 100.0f;

        // Un bin couvre une plage de fréquences : on élargit la cloche d'autant,
        // pour qu'un bin centré près d'une note reçoive bien le renfort.
        const float binCents = 600.0f * std::log2 ((hz + 0.5f * binHz) / juce::jmax (1.0f, hz - 0.5f * binHz));
        const float sigma = std::sqrt (sigmaCents * sigmaCents + binCents * binCents);
        const float bump = std::exp (-0.5f * (distCents / sigma) * (distCents / sigma));

        curveDb[(size_t) k] = w * (peakDb * bump + floorDb * (1.0f - bump));
    }
}

void SpectralEngine::applyColourComb (int nch) noexcept
{
    const float depth = colour;                               // 0…1
    if (depth <= 0.0f || currentMask == 0)
        return;

    // Courbes mises en cache : recalculées seulement si la gamme change.
    if (combMask != currentMask)      { computeCombCurve (currentMask,  combCurveDb);     combMask = currentMask; }
    if (combPrevMask != previousMask) { computeCombCurve (previousMask, combPrevCurveDb); combPrevMask = previousMask; }

    const float fade = smoothStep (scaleFade);
    const float dbToLin = 0.11512925f;                        // ln(10)/20

    float before = 0.0f, after = 0.0f;
    for (int k = 0; k < numBins; ++k)
    {
        float db = combCurveDb[(size_t) k];
        if (fade < 1.0f)
            db = combPrevCurveDb[(size_t) k] + (db - combPrevCurveDb[(size_t) k]) * fade;

        const float g = std::exp (db * depth * dbToLin);
        combGain[(size_t) k] = g;

        if (std::abs (db) > 1.0e-6f)
            for (int c = 0; c < nch; ++c)
            {
                const float e = std::norm (spectrumOut[(size_t) c][(size_t) k]);
                before += e;
                after  += e * g * g;
            }
    }

    // Compensation de niveau (comme Chroma : le volume global ne bouge pas).
    // Lissée de trame en trame pour ne pas pomper.
    const float target = after > 1.0e-12f ? juce::jlimit (0.25f, 4.0f, std::sqrt (before / after)) : 1.0f;
    combNorm += (target - combNorm) * 0.35f;
    if (! std::isfinite (combNorm)) combNorm = 1.0f;

    for (int k = 0; k < numBins; ++k)
    {
        if (std::abs (combCurveDb[(size_t) k]) < 1.0e-6f && std::abs (combPrevCurveDb[(size_t) k]) < 1.0e-6f)
            continue;
        const float g = combGain[(size_t) k] * combNorm;
        for (int c = 0; c < nch; ++c)
            spectrumOut[(size_t) c][(size_t) k] *= g;
    }
}

//==============================================================================
//  Harmoniques justes
//==============================================================================
void SpectralEngine::addSinusoid (std::vector<Complex>& Z, float k0, float amplitude, float phase0) const noexcept
{
    // Spectre exact d'une sinusoïde fenêtrée par Hann (fenêtre de N points) :
    //   X[k] = A/2 · e^{j(φ − πδ)} · N·(½ sinc δ + ¼ sinc(δ−1) + ¼ sinc(δ+1)),  δ = k − k0
    // sur ±4 bins autour de k0 (au-delà, < −90 dB).
    const int kc = (int) std::lround (k0);
    const float scale = 0.5f * amplitude * (float) fftSize;

    auto sinc = [] (float x) { return std::abs (x) < 1.0e-6f ? 1.0f : std::sin (pi * x) / (pi * x); };

    for (int k = juce::jmax (1, kc - 4); k <= juce::jmin (numBins - 2, kc + 4); ++k)
    {
        const float d = (float) k - k0;
        const float w = 0.5f * sinc (d) + 0.25f * (sinc (d - 1.0f) + sinc (d + 1.0f));
        Z[(size_t) k] += std::polar (scale * w, phase0 - pi * d);
    }
}

void SpectralEngine::addHarmonics (int nch) noexcept
{
    // Pour chaque partiel tonal (donc recalé dans la gamme), on ajoute ses
    // harmoniques justes : octave (×2), double octave (×4) et, si la quinte
    // est dans la gamme, la douzième (×3). Leur phase est celle du partiel
    // multipliée par le rang : elles naissent et meurent exactement avec lui
    // (aucune queue, transitoires intacts), comme une saturation… mais
    // uniquement sur des notes de la gamme. Dosées par COLOR.
    if (colour <= 0.0f || currentMask == 0 || numPeaks == 0)
        return;

    const float fullScale = (float) fftSize * 0.25f;
    const float maxHz = juce::jmin (16000.0f, (float) sampleRate * 0.45f);
    auto sinc = [] (float x) { return std::abs (x) < 1.0e-6f ? 1.0f : std::sin (pi * x) / (pi * x); };

    // Les partiels les plus forts d'abord (au plus 40).
    int count = 0;
    for (int p = 0; p < numPeaks; ++p)
        if (peakTonalAge[(size_t) p] >= 1 && magnitude[(size_t) peakBin[(size_t) p]] > fullScale * 1.0e-3f)
            peakOrder[(size_t) count++] = p;
    const int used = juce::jmin (count, 40);
    std::partial_sort (peakOrder.begin(), peakOrder.begin() + used, peakOrder.begin() + count,
                       [this] (int a, int b) { return magnitude[(size_t) peakBin[(size_t) a]] > magnitude[(size_t) peakBin[(size_t) b]]; });

    struct Rank { int mult; float gain; };
    const Rank ranks[numHarmRanks] = { { 2, 0.48f }, { 3, 0.30f }, { 4, 0.20f } };
    const float hopSeconds = (float) hopSize / (float) sampleRate;

    for (int i = 0; i < used; ++i)
    {
        const int p = peakOrder[(size_t) i];
        const float f0 = peakTargetHz[(size_t) p];
        const float k0 = f0 / binHz;
        const int kc = (int) std::lround (k0);
        if (kc < 2 || kc >= numBins - 2)
            continue;

        const float d = (float) kc - k0;
        const float w = 0.5f * sinc (d) + 0.25f * (sinc (d - 1.0f) + sinc (d + 1.0f));
        if (w < 0.2f)
            continue;

        const int note = (int) std::lround (frame::hzToMidi (f0));
        const int anc = peakAncestor[(size_t) p];
        const HarmState* prev = (anc >= 0 && anc < maxPeaksInternal && prevHarmState[(size_t) anc].valid)
                                  ? &prevHarmState[(size_t) anc] : nullptr;
        HarmState& st = harmState[(size_t) p];
        st.valid = true;

        for (int r = 0; r < numHarmRanks; ++r)
        {
            const float fh = f0 * (float) ranks[r].mult;
            const bool skipTwelfth = (ranks[r].mult == 3 && ! frame::maskContains (currentMask, note + 19));

            for (int c = 0; c < nch; ++c)
            {
                // Amplitude et phase du partiel dans CE canal (bin le plus proche).
                const Complex zp = spectrumOut[(size_t) c][(size_t) kc];
                const float ampNow = 2.0f * std::abs (zp) / ((float) fftSize * w);
                const float phiNow = wrapPhase ((std::arg (zp) + pi * d) * (float) ranks[r].mult);

                // Phase CONTINUE : prolongée depuis la trame précédente quand le
                // partiel est suivi (aucun saut d'une trame à l'autre = aucun
                // grésillement) ; sinon calée sur la phase mesurée × rang.
                // Amplitude lissée sur ~2 trames (pas de vibration d'amplitude).
                float ph = phiNow, amp = ampNow;
                if (prev != nullptr)
                {
                    ph = wrapPhase (prev->phase[r][c] + twoPi * fh * hopSeconds);
                    amp   = 0.5f * (prev->amp[r][c] + ampNow);
                }
                st.phase[r][c] = ph;
                st.amp[r][c]   = amp;

                if (fh <= maxHz && ! skipTwelfth)
                    addSinusoid (spectrumOut[(size_t) c], fh / binHz, amp * ranks[r].gain * colour, ph);
            }
        }
    }
}

//==============================================================================
void SpectralEngine::publishVisualFrame (int nch) noexcept
{
    auto& info = scratchInfo;
    const float fullScale = (float) fftSize * 0.25f;
    const float invNch = 1.0f / (float) nch;

    auto toLevel = [fullScale] (float mag)
    {
        // −84 dBFS → 0, −6 dBFS → 1
        const float db = juce::Decibels::gainToDecibels (mag / fullScale, -120.0f);
        return juce::jlimit (0.0f, 1.0f, (db + 84.0f) / 78.0f);
    };

    // Spectre de sortie (mid) sur l'axe log.
    for (int r = 0; r < SpectralFrameInfo::numSpectrumRows; ++r)
    {
        float m = 0.0f;
        for (int k = rowBinStart[(size_t) r]; k <= rowBinEnd[(size_t) r]; ++k)
        {
            Complex z = spectrumOut[0][(size_t) k];
            for (int c = 1; c < nch; ++c)
                z += spectrumOut[(size_t) c][(size_t) k];
            m = juce::jmax (m, std::abs (z) * invNch);
        }
        info.spectrum[r] = toLevel (m);
    }

    // Les pics les plus forts dans la plage affichée.
    int count = 0;
    for (int p = 0; p < numPeaks; ++p)
        if (peakTargetHz[(size_t) p] >= displayMinHz && peakTargetHz[(size_t) p] <= displayMaxHz)
            peakOrder[(size_t) count++] = p;

    const int shown = juce::jmin (count, SpectralFrameInfo::maxPeaks);
    std::partial_sort (peakOrder.begin(), peakOrder.begin() + shown, peakOrder.begin() + count,
                       [this] (int a, int b) { return magnitude[(size_t) peakBin[(size_t) a]]
                                                    > magnitude[(size_t) peakBin[(size_t) b]]; });

    info.numPeaks = shown;
    for (int i = 0; i < shown; ++i)
    {
        const int p = peakOrder[(size_t) i];
        info.peakFreq[i]  = peakTargetHz[(size_t) p];
        info.peakLevel[i] = toLevel (magnitude[(size_t) peakBin[(size_t) p]]);
        const float cents = 1200.0f * std::log2 (juce::jmax (1.0f, peakTargetHz[(size_t) p])
                                                 / juce::jmax (1.0f, peakTrueHz[(size_t) p]));
        info.peakSnapped[i] = std::abs (cents) > 8.0f;
    }

    visualQueue.push (info);
}
