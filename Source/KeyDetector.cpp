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
    fine.fill (0.0);
    fineBass.fill (0.0);
    tuningCents = 0.0;
    activeSeconds = 0.0;
    progress.store (0.0f);
    confidence.store (0.0f);
    tonic.store (-1);
    for (auto& a : altTonic) a.store (-1);
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

    // 1. Pics du spectre (fréquence affinée par interpolation parabolique).
    constexpr int maxPeaks = 80;
    double pf[maxPeaks], pm[maxPeaks];
    int np = 0;
    for (int k = kLo; k <= kHi && np < maxPeaks; ++k)
    {
        const float m = work[(size_t) k];
        if (m < 0.01f * maxMag || m <= work[(size_t) k - 1] || m < work[(size_t) k + 1])
            continue;                                      // pics seulement, à −40 dB du plus fort au plus
        const double a = std::log (work[(size_t) k - 1] + 1e-12), b = std::log (m + 1e-12), c = std::log (work[(size_t) k + 1] + 1e-12);
        const double den = a - 2.0 * b + c;
        const double off = std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
        pf[np] = (k + juce::jlimit (-0.5, 0.5, off)) * binHz;
        pm[np] = (double) m;
        ++np;
    }
    if (np == 0)
        return;

    // 2. Harmoniques écartées : un pic qui tombe sur ×3, ×5, ×6, ×7 d'un pic
    //    plus grave et au moins aussi fort n'est qu'un harmonique (il pousserait
    //    vers la quinte ou la tierce) ; ×2 et ×4 tombent sur la même note.
    double weight[maxPeaks];
    for (int j = 0; j < np; ++j)
    {
        weight[j] = pm[j];
        for (int i = 0; i < np; ++i)
        {
            if (i == j || pf[i] >= pf[j] || pm[i] < 0.5 * pm[j]) continue;
            const double r = pf[j] / pf[i];
            const int nearest = (int) std::lround (r);
            if (nearest >= 3 && nearest <= 7 && nearest != 4 && std::abs (r - (double) nearest) < 0.025 * nearest)
            {
                weight[j] *= 0.25;
                break;
            }
        }
    }

    // 3. Histogrammes fins (10 cents) : l'accordage réel est estimé à la fin.
    std::array<double, fineBins> frame {}, bass {};
    double frameSum = 0.0, bassSum = 0.0;
    for (int j = 0; j < np; ++j)
    {
        const double cents = 100.0 * (69.0 + 12.0 * std::log2 (pf[j] / 440.0));
        int bin = (int) std::floor (cents / 10.0);
        bin = ((bin % fineBins) + fineBins) % fineBins;
        const double w = std::sqrt (weight[j]);           // racine : les notes faibles comptent aussi
        frame[(size_t) bin] += w;
        frameSum += w;
        if (pf[j] < 220.0) { bass[(size_t) bin] += w; bassSum += w; }   // les basses portent souvent la tonique
    }
    if (frameSum <= 0.0)
        return;

    for (int i = 0; i < fineBins; ++i)
    {
        fine[(size_t) i] += frame[(size_t) i] / frameSum;    // chaque trame jouée compte autant
        if (bassSum > 0.0) fineBass[(size_t) i] += bass[(size_t) i] / bassSum;
    }
    fold (fine, fineBass, chroma, bassChroma, tuningCents);
    activeSeconds += (double) hop / sampleRate;

    std::array<Candidate, 3> top;
    rankKeys (chroma, bassChroma, top);
    tonic.store (top[0].tonic);
    mode.store (top[0].mode);
    for (int i = 0; i < 2; ++i)
    {
        altTonic[i].store (top[(size_t) i + 1].tonic);
        altMode[i].store (top[(size_t) i + 1].mode);
    }
    confidence.store (top[1].tonic >= 0 ? (float) juce::jlimit (0.0, 1.0, (top[0].score - top[1].score) * 4.0) : 0.0f);

    const float p = (float) juce::jlimit (0.0, 1.0, activeSeconds / (double) targetSeconds);
    progress.store (p);
    if (p >= 1.0f)
    {
        scanning.store (false);
        done.store (true);
    }
}

void KeyDetector::fold (const std::array<double, fineBins>& f, const std::array<double, fineBins>& fb,
                        std::array<double, 12>& c, std::array<double, 12>& b, double& tuning) noexcept
{
    // Accordage : le décalage (−50 … +50 cents, par pas de 10) qui concentre le
    // plus d'énergie au centre des notes.
    double best = -1.0; int bestOff = 0;
    for (int off = -5; off < 5; ++off)
    {
        double e = 0.0;
        for (int pc = 0; pc < 12; ++pc)
            for (int d = -1; d <= 1; ++d)
            {
                const int bin = (((pc * 10 + off + d) % fineBins) + fineBins) % fineBins;
                e += f[(size_t) bin] * (d == 0 ? 1.0 : 0.5);
            }
        if (e > best) { best = e; bestOff = off; }
    }
    tuning = bestOff * 10.0;
    // Repliement sur 12 notes, centré sur l'accordage trouvé (±50 cents).
    c.fill (0.0); b.fill (0.0);
    for (int i = 0; i < fineBins; ++i)
    {
        const double centsRel = (i + 0.5) * 10.0 - tuning;
        const int pc = ((int) std::floor (centsRel / 100.0 + 0.5) % 12 + 12) % 12;
        c[(size_t) pc] += f[(size_t) i];
        b[(size_t) pc] += fb[(size_t) i];
    }
}

void KeyDetector::rankKeys (const std::array<double, 12>& c, const std::array<double, 12>& bass,
                            std::array<Candidate, 3>& top) noexcept
{
    // Score de chaque tonalité (24) : ressemblance au profil de tonalité
    // + part de l'énergie dans sa gamme + poids de la tonique à la basse.
    // Réglé sur un banc de 260 morceaux de test (mélodies seules, accords,
    // arpèges, pentatoniques, 5 timbres, accordages décalés jusqu'à ±45 cents).
    constexpr int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (auto& t : top) t = {};
    double total = 0.0, bassTotal = 0.0;
    for (double v : c) total += v;
    for (double v : bass) bassTotal += v;
    if (total <= 0.0) return;

    std::array<double, 12> soft {};                                // racine : comprime les écarts de niveau
    for (int i = 0; i < 12; ++i) soft[(size_t) i] = std::sqrt (juce::jmax (0.0, c[(size_t) i] / total));

    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            const int root = m == 0 ? t : (t + 3) % 12;            // gamme majeure relative
            double in = 0.0;
            for (int st : majorSteps) in += c[(size_t) ((root + st) % 12)];
            const double score = correlation (soft, m == 0 ? majorProfile : minorProfile, t)
                               + 1.0 * in / total
                               + 0.3 * (bassTotal > 0.0 ? bass[(size_t) t] / bassTotal : 0.0);
            Candidate cand { t, m, score };
            for (int i = 0; i < 3; ++i)
                if (top[(size_t) i].tonic < 0 || score > top[(size_t) i].score)
                {
                    for (int j = 2; j > i; --j) top[(size_t) j] = top[(size_t) j - 1];
                    top[(size_t) i] = cand;
                    break;
                }
        }
}

void KeyDetector::bestKey (const std::array<double, 12>& c, const std::array<double, 12>& bass,
                           int& tonicOut, int& modeOut, float& confidenceOut) noexcept
{
    std::array<Candidate, 3> top;
    rankKeys (c, bass, top);
    tonicOut = top[0].tonic;
    modeOut = top[0].mode;
    confidenceOut = top[1].tonic >= 0 ? (float) juce::jlimit (0.0, 1.0, (top[0].score - top[1].score) * 4.0) : 0.0f;
}
