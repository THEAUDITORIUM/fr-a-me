/*
  ==============================================================================
    fr(a)me — KeyDetector.cpp
  ==============================================================================
*/

#include "KeyDetector.h"
#include <cmath>

namespace
{
    // Profils de tonalité d'Albrecht & Shanahan (2013), tonique = indice 0.
    constexpr double majorProfile[12] = { 0.238, 0.006, 0.111, 0.006, 0.137, 0.094, 0.016, 0.214, 0.009, 0.080, 0.008, 0.081 };
    constexpr double minorProfile[12] = { 0.220, 0.006, 0.104, 0.123, 0.019, 0.103, 0.012, 0.214, 0.062, 0.022, 0.061, 0.052 };

    double correlation (const std::array<double, 12>& x, const double* profile, int shift) noexcept
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i) { mx += x[(size_t) i]; my += profile[i]; }
        mx /= 12.0; my /= 12.0;
        double num = 0.0, dx = 0.0, dy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = x[(size_t) ((i + shift) % 12)] - mx, b = profile[i] - my;
            num += a * b; dx += a * a; dy += b * b;
        }
        return (dx > 0.0 && dy > 0.0) ? num / std::sqrt (dx * dy) : 0.0;
    }
}

void KeyDetector::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    fftOrder = sampleRate > 60000.0 ? 14 : 13;            // ~0.17 s, résolution ~5 Hz
    fftSize = 1 << fftOrder;
    hop = fftSize / 2;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
    ring.assign ((size_t) fftSize, 0.0f);
    work.assign ((size_t) fftSize * 2, 0.0f);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
    ringPos = sinceHop = 0;
    resetAccumulators();
}

void KeyDetector::resetAccumulators() noexcept
{
    chroma.fill (0.0);
    bassChroma.fill (0.0);
    activeSeconds = 0.0;
    progress.store (0.0f);
    confidence.store (0.0f);
    tonic.store (-1);
}

void KeyDetector::push (const float* left, const float* right, int numSamples) noexcept
{
    if (cancelRequested.exchange (false))
        scanning.store (false);
    if (startRequested.exchange (false))
    {
        resetAccumulators();
        std::fill (ring.begin(), ring.end(), 0.0f);
        sinceHop = 0;
        done.store (false);
        scanning.store (true);
    }
    if (! scanning.load() || left == nullptr || fft == nullptr || ring.empty())
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float v = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        if (! std::isfinite (v)) v = 0.0f;
        ring[(size_t) ringPos] = v;
        ringPos = (ringPos + 1) & (fftSize - 1);
        if (++sinceHop >= hop)
        {
            sinceHop = 0;
            analyseFrame();
            if (! scanning.load())
                return;
        }
    }
}

void KeyDetector::analyseFrame() noexcept
{
    // Trame fenêtrée (la plus ancienne en premier).
    double energy = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const float v = ring[(size_t) ((ringPos + i) & (fftSize - 1))];
        energy += (double) v * v;
        work[(size_t) i] = v * window[(size_t) i];
    }
    std::fill (work.begin() + fftSize, work.end(), 0.0f);

    // Silence (< −55 dBFS) : la trame ne compte pas.
    if (energy / fftSize < 3.2e-6)
        return;

    fft->performFrequencyOnlyForwardTransform (work.data(), true);

    const double binHz = sampleRate / fftSize;
    const int kLo = juce::jmax (2, (int) (70.0 / binHz)), kHi = juce::jmin (fftSize / 2 - 2, (int) (5000.0 / binHz));
    float maxMag = 0.0f;
    for (int k = kLo; k <= kHi; ++k)
        maxMag = juce::jmax (maxMag, work[(size_t) k]);
    if (maxMag <= 0.0f)
        return;

    std::array<double, 12> frame {}, bass {};
    double frameSum = 0.0, bassSum = 0.0;
    for (int k = kLo; k <= kHi; ++k)
    {
        const float m = work[(size_t) k];
        if (m < 0.01f * maxMag || m <= work[(size_t) k - 1] || m < work[(size_t) k + 1])
            continue;                                      // pics seulement, à −40 dB du plus fort au plus

        // Interpolation parabolique (sur le log) de la fréquence du pic.
        const double a = std::log (work[(size_t) k - 1] + 1e-12), b = std::log (m + 1e-12), c = std::log (work[(size_t) k + 1] + 1e-12);
        const double den = a - 2.0 * b + c;
        const double off = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
        const double hz = (k + juce::jlimit (-0.5, 0.5, off)) * binHz;
        const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const double nearest = std::round (midi);
        const double dev = midi - nearest;
        double w = (double) m * std::pow (std::cos (juce::MathConstants<double>::pi * dev), 2.0);
        const int pc = ((int) nearest % 12 + 12) % 12;
        frame[(size_t) pc] += w;
        frameSum += w;
        if (hz < 220.0) { bass[(size_t) pc] += w; bassSum += w; }   // les basses portent souvent la tonique
    }
    if (frameSum <= 0.0)
        return;

    for (int i = 0; i < 12; ++i)
    {
        chroma[(size_t) i] += frame[(size_t) i] / frameSum;   // chaque trame jouée compte autant
        if (bassSum > 0.0) bassChroma[(size_t) i] += bass[(size_t) i] / bassSum;
    }
    activeSeconds += (double) hop / sampleRate;

    int t = -1, m = 0;
    float conf = 0.0f;
    bestKey (chroma, bassChroma, t, m, conf);
    tonic.store (t);
    mode.store (m);
    confidence.store (conf);

    const float p = (float) juce::jlimit (0.0, 1.0, activeSeconds / (double) targetSeconds);
    progress.store (p);
    if (p >= 1.0f)
    {
        scanning.store (false);
        done.store (true);
    }
}

void KeyDetector::bestKey (const std::array<double, 12>& c, const std::array<double, 12>& bass,
                           int& tonicOut, int& modeOut, float& confidenceOut) noexcept
{
    constexpr int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    double total = 0.0;
    for (double v : c) total += v;
    if (total <= 0.0) { tonicOut = -1; modeOut = 0; confidenceOut = 0.0f; return; }

    // 1. La gamme : part d'énergie dans chacune des 12 gammes diatoniques.
    double best = -1.0, second = -1.0;
    int bestSet = 0;
    for (int t = 0; t < 12; ++t)
    {
        double in = 0.0;
        for (int s : majorSteps) in += c[(size_t) ((t + s) % 12)];
        const double frac = in / total;
        if (frac > best) { second = best; best = frac; bestSet = t; }
        else if (frac > second) second = frac;
    }

    // 2. La tonique : majeur (bestSet) ou relatif mineur (bestSet + 9),
    //    profils de tonalité + basse (racines des accords).
    const int majT = bestSet, minT = (bestSet + 9) % 12;
    double bassTotal = 0.0;
    for (double v : bass) bassTotal += v;
    const double bMaj = bassTotal > 0.0 ? bass[(size_t) majT] / bassTotal : 0.0;
    const double bMin = bassTotal > 0.0 ? bass[(size_t) minT] / bassTotal : 0.0;
    const double scoreMaj = correlation (c, majorProfile, majT) + 1.5 * bMaj + 0.5 * c[(size_t) majT] / total;
    const double scoreMin = correlation (c, minorProfile, minT) + 1.5 * bMin + 0.5 * c[(size_t) minT] / total;

    if (scoreMin > scoreMaj) { tonicOut = minT; modeOut = 1; }
    else                     { tonicOut = majT; modeOut = 0; }
    confidenceOut = (float) juce::jlimit (0.0, 1.0, (best - second) * 8.0);
}
