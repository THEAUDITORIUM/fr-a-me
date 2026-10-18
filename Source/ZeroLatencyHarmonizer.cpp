/*
  ==============================================================================
    fr(a)me — ZeroLatencyHarmonizer.cpp
  ==============================================================================
*/

#include "ZeroLatencyHarmonizer.h"
#include "Scale.h"
#include <cmath>
#include <complex>

namespace
{
    inline float smoothStep (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }

   #ifndef ZLH_BELL
    #define ZLH_BELL 27.0f
   #endif
   #ifndef ZLH_WIDTH
    #define ZLH_WIDTH 2.2      // cloches plus larges : sonnent moins longtemps (pas de traînées)
   #endif
   #ifndef ZLH_CUT
    #define ZLH_CUT 0.0f        // creux sur les notes hors gamme (dB)
   #endif
   #ifndef ZLH_CUTW
    #define ZLH_CUTW 80.0       // largeur des creux (cents)
   #endif
   #ifndef ZLH_MAKEUP
    #define ZLH_MAKEUP 6.0f     // rattrapage maximal (dB) pour les sons non tonals
   #endif
   #ifndef ZLH_H2
    #define ZLH_H2 0.5f
   #endif
   #ifndef ZLH_H3
    #define ZLH_H3 0.25f
   #endif
   #ifndef ZLH_H4
    #define ZLH_H4 0.15f
   #endif
    constexpr float bellBoostDb   = ZLH_BELL;   // renfort des notes
    constexpr float cutDepthDb    = ZLH_CUT;    // creux sur les notes hors gamme
    constexpr float harmGain2     = ZLH_H2;     // ×2 (octave)
    constexpr float harmGain3     = ZLH_H3;     // ×3 (douzième, si la quinte est dans la gamme)
    constexpr float harmGain4     = ZLH_H4;     // ×4 (double octave)
}

void ZeroLatencyHarmonizer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const double nyquistLimit = sampleRate * 0.45;

    for (int i = 0; i < numNotes; ++i)
    {
        auto& n = notes[(size_t) i];
        n.hz = frame::midiToHz (lowNote + i);

        // Largeur de la cloche : ~40 cents dans le grave → ~22 cents dans l'aigu.
        const double t = juce::jlimit (0.0, 1.0, std::log2 (n.hz / 150.0) / std::log2 (2000.0 / 150.0));
        const double cents = ZLH_WIDTH * (40.0 + (22.0 - 40.0) * t);
        const double bwOct = cents / 1200.0;
        n.q = 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * bwOct));
        n.qCut = 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * (ZLH_CUTW / 1200.0)));

        const float f = (float) n.hz;
        n.freqWeight = smoothStep ((f - 150.0f) / 550.0f) * (1.0f - 0.6f * smoothStep ((f - 6000.0f) / 6000.0f));
        // Harmoniques : dès le grave (fondamentales à partir de ~60 Hz), comme la 1.4.
        n.harmWeight = smoothStep ((f - 45.0f) / 60.0f) * (1.0f - 0.7f * smoothStep ((f - 3000.0f) / 3000.0f));
        if (n.hz > nyquistLimit) n.freqWeight = n.harmWeight = 0.0f;

        // Passe-bande d'isolation (gain 1 au centre), un peu plus large que la cloche.
        {
            const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (n.hz, nyquistLimit) / sampleRate;
            const double alpha = std::sin (w0) / (2.0 * n.q * 0.8);
            const double a0 = 1.0 + alpha;
            n.band.b0 = alpha / a0;  n.band.b1 = 0.0;  n.band.b2 = -alpha / a0;
            n.band.a1 = -2.0 * std::cos (w0) / a0;     n.band.a2 = (1.0 - alpha) / a0;
            n.cosW = std::cos (w0);
            n.sinW = juce::jmax (1.0e-6, std::sin (w0));
            // tolérance ±50 cents exprimée en rotation par échantillon
            const double tol = w0 * (std::exp2 (50.0 / 1200.0) - 1.0);
            n.cosTol = std::cos (tol);
        }
        n.bellGainDb = 0.0f;
        updateBell (n);
    }

    gainSmoothDb  = 1.0f - std::exp (-32.0f / (0.12f * (float) sampleRate));   // mises à jour toutes les 32 éch.
    gainSmoothLin = 1.0f - std::exp (-1.0f  / (0.12f * (float) sampleRate));
    gateSmooth    = 1.0f - std::exp (-1.0f  / (0.008f * (float) sampleRate));
    for (int k = 0; k < numComp; ++k)
    {
        compHz[k] = juce::jmin (88.4 * std::exp2 ((double) k), sampleRate * 0.4);
        setBandpass (measIn[k],  compHz[k], 1.41);
        setBandpass (measOut[k], compHz[k], 1.41);
    }
    levelCoef  = 1.0f - std::exp (-1.0f  / (0.4f * (float) sampleRate));

    makeupRate = 1.0f - std::exp (-32.0f / (0.6f * (float) sampleRate));
    dcR = 1.0 - juce::MathConstants<double>::twoPi * 20.0 / sampleRate;

    const uint16_t m = mask;
    mask = 0;
    setScale (m);
    reset();
}

void ZeroLatencyHarmonizer::reset() noexcept
{
    for (auto& n : notes)
    {
        n.bell.clear();
        n.band.clear();
        n.bPrev[0] = n.bPrev[1] = 0.0;
        n.qPrev[0] = n.qPrev[1] = 0.0;
        n.gate[0] = n.gate[1] = 0.0f;
        n.bellGainDb = n.bellTargetDb;
        n.harmGain = n.harmTarget;
        updateBell (n);
        n.active = std::abs (n.bellTargetDb) > 0.0f || n.harmTarget > 0.0f;
    }
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;

    for (int k = 0; k < numComp; ++k)
    {
        compGainDb[k] = compTargetDb[k];
        makeupDb[k] = 0.0f;
        eIn[k] = eOut[k] = 0.0f;
        setPeaking (compEq[k], compHz[k], 1.0, compGainDb[k]);
        compEq[k].clear(); measIn[k].clear(); measOut[k].clear();
    }
    coefCounter = 0;
}

void ZeroLatencyHarmonizer::setScale (uint16_t newMask) noexcept
{
    if (newMask == mask)
        return;
    mask = newMask;

    for (int i = 0; i < numNotes; ++i)
    {
        auto& n = notes[(size_t) i];
        const int midi = lowNote + i;
        const bool in = frame::maskContains (mask, midi);
        n.inScale = in;
        n.bellTargetDb = (in ? bellBoostDb : -cutDepthDb) * n.freqWeight;   // cloche sur la gamme, creux hors gamme
        n.harmTarget   = in ? n.harmWeight : 0.0f;
        n.twelfthInScale = frame::maskContains (mask, midi + 19);
        if (in || std::abs (n.bellTargetDb) > 0.0f) n.active = true;
    }
    computeCompensation();
}

double ZeroLatencyHarmonizer::peakingDb (double centreHz, double q, double gainDb, double atHz) const noexcept
{
    if (std::abs (gainDb) < 1.0e-4) return 0.0;
    Biquad f;
    setPeaking (f, centreHz, q, (float) gainDb);
    const double w = juce::MathConstants<double>::twoPi * atHz / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    const auto num = f.b0 + f.b1 * z1 + f.b2 * z2;
    const auto den = 1.0 + f.a1 * z1 + f.a2 * z2;
    return 20.0 * std::log10 (std::abs (num) / std::abs (den));
}

void ZeroLatencyHarmonizer::computeCompensation() noexcept
{
    // Réponse du peigne (gains cibles) sur les notes de la gamme de chaque octave,
    // puis gains des 8 cloches larges de compensation résolus par itérations :
    // les notes de la gamme retombent à 0 dB.
    constexpr int maxPts = 16;
    double ptHz[numComp][maxPts] {}, combDb[numComp][maxPts] {};
    int cnt[numComp] {};
    for (int k = 0; k < numComp; ++k)
    {
        const double lo = compHz[k] / std::sqrt (2.0), hi = compHz[k] * std::sqrt (2.0);
        for (int i = 0; i < numNotes && cnt[k] < maxPts; ++i)
        {
            const auto& n = notes[(size_t) i];
            if (n.hz < lo || n.hz >= hi || ! n.inScale || n.freqWeight <= 0.0f) continue;
            double r = 0.0;
            for (const auto& m : notes)
                if (std::abs (m.bellTargetDb) > 0.0f && std::abs (std::log2 (m.hz / n.hz)) < 1.5)
                    r += peakingDb (m.hz, m.bellTargetDb >= 0.0f ? m.q : m.qCut, m.bellTargetDb, n.hz);
            ptHz[k][cnt[k]] = n.hz;
            combDb[k][cnt[k]] = r;
            ++cnt[k];
        }
    }

    double g[numComp] {};
    for (int iter = 0; iter < 12; ++iter)
        for (int k = 0; k < numComp; ++k)
        {
            if (cnt[k] == 0) { g[k] = 0.0; continue; }
            double mean = 0.0;
            for (int p = 0; p < cnt[k]; ++p)
            {
                double v = combDb[k][p];
                for (int j = 0; j < numComp; ++j)
                    v += peakingDb (compHz[j], 1.0, g[j], ptHz[k][p]);
                mean += v;
            }
            mean /= (double) cnt[k];
            g[k] = juce::jlimit (-30.0, 6.0, g[k] - 0.7 * mean);
        }
    for (int k = 0; k < numComp; ++k)
        compTargetDb[k] = (float) g[k];
}

void ZeroLatencyHarmonizer::setPeaking (Biquad& f, double hz, double q, float gainDb) const noexcept
{
    const double A = std::pow (10.0, (double) gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (hz, sampleRate * 0.45) / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double cw = std::cos (w0);
    const double a0 = 1.0 + alpha / A;
    f.b0 = (1.0 + alpha * A) / a0;  f.b1 = (-2.0 * cw) / a0;  f.b2 = (1.0 - alpha * A) / a0;
    f.a1 = (-2.0 * cw) / a0;        f.a2 = (1.0 - alpha / A) / a0;
}

void ZeroLatencyHarmonizer::setHighShelf (Biquad& f, double hz, float gainDb) const noexcept
{
    // RBJ high-shelf, pente S = 1
    const double A = std::pow (10.0, (double) gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (hz, sampleRate * 0.45) / sampleRate;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / 2.0 * std::sqrt (2.0);
    const double sq = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cw + sq;
    f.b0 = A * ((A + 1.0) + (A - 1.0) * cw + sq) / a0;
    f.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
    f.b2 = A * ((A + 1.0) + (A - 1.0) * cw - sq) / a0;
    f.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
    f.a2 = ((A + 1.0) - (A - 1.0) * cw - sq) / a0;
}

void ZeroLatencyHarmonizer::setHighpass (Biquad& f, double hz) const noexcept
{
    const double w0 = juce::MathConstants<double>::twoPi * hz / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * 0.7071);
    const double cw = std::cos (w0), a0 = 1.0 + alpha;
    f.b0 = (1.0 + cw) / 2.0 / a0;  f.b1 = -(1.0 + cw) / a0;  f.b2 = (1.0 + cw) / 2.0 / a0;
    f.a1 = -2.0 * cw / a0;         f.a2 = (1.0 - alpha) / a0;
}

void ZeroLatencyHarmonizer::setLowpass (Biquad& f, double hz) const noexcept
{
    const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (hz, sampleRate * 0.45) / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * 0.7071);
    const double cw = std::cos (w0), a0 = 1.0 + alpha;
    f.b0 = (1.0 - cw) / 2.0 / a0;  f.b1 = (1.0 - cw) / a0;  f.b2 = (1.0 - cw) / 2.0 / a0;
    f.a1 = -2.0 * cw / a0;         f.a2 = (1.0 - alpha) / a0;
}

void ZeroLatencyHarmonizer::setBandpass (Biquad& f, double hz, double q) const noexcept
{
    const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (hz, sampleRate * 0.45) / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    f.b0 = alpha / a0;  f.b1 = 0.0;  f.b2 = -alpha / a0;
    f.a1 = -2.0 * std::cos (w0) / a0;  f.a2 = (1.0 - alpha) / a0;
}

void ZeroLatencyHarmonizer::updateBell (Note& n) noexcept
{
    // Cloche RBJ (peaking EQ) au gain courant.
    const double A = std::pow (10.0, (double) n.bellGainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (n.hz, sampleRate * 0.45) / sampleRate;
    const double alpha = std::sin (w0) / (2.0 * (n.bellGainDb >= 0.0f ? n.q : n.qCut));
    const double cw = std::cos (w0);
    const double a0 = 1.0 + alpha / A;
    n.bell.b0 = (1.0 + alpha * A) / a0;
    n.bell.b1 = (-2.0 * cw) / a0;
    n.bell.b2 = (1.0 - alpha * A) / a0;
    n.bell.a1 = (-2.0 * cw) / a0;
    n.bell.a2 = (1.0 - alpha / A) / a0;
}

void ZeroLatencyHarmonizer::process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept
{
    const int len = buffer.getNumSamples();
    const int nch = juce::jmin (numChannels, buffer.getNumChannels(), 2);
    if (len <= 0 || nch <= 0)
        return;

    amount = juce::jlimit (0.0f, 1.0f, std::isfinite (amount) ? amount : 0.0f);
    float* data[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };
    const double nyq = sampleRate * 0.45;

    for (int i = 0; i < len; ++i)
    {
        // Glissement des gains de cloche (coefficients recalculés toutes les 32 éch.).
        if ((coefCounter++ & 31) == 0)
        {
            for (auto& n : notes)
            {
                if (! n.active) continue;
                const float diff = n.bellTargetDb * amount - n.bellGainDb;
                if (std::abs (diff) > 0.005f)
                {
                    n.bellGainDb += diff * gainSmoothDb;
                    updateBell (n);
                }
                if (std::abs (n.bellTargetDb) <= 0.0f && std::abs (n.bellGainDb) < 0.01f && n.harmTarget <= 0.0f && n.harmGain < 1.0e-4f)
                {
                    n.bellGainDb = 0.0f; n.harmGain = 0.0f;
                    updateBell (n); n.bell.clear(); n.band.clear(); n.bPrev[0] = n.bPrev[1] = 0.0;
                    n.active = false;                         // sorti de la gamme : en veille
                }
            }
        }

        float out[2] {};


        for (int c = 0; c < nch; ++c)
        {
            const double x = (double) data[c][i];

            // 1. Harmoniques justes, calculées sur le son d'origine.
            double harm = 0.0;
            for (auto& n : notes)
            {
                if (! n.active || (n.harmGain <= 1.0e-4f && n.harmTarget <= 0.0f)) continue;
                if (c == 0)
                {
                    n.harmGain += (n.harmTarget - n.harmGain) * gainSmoothLin;
                    n.twelfthGain += ((n.twelfthInScale ? 1.0f : 0.0f) - n.twelfthGain) * gainSmoothLin;
                }

                // Isolation de la note, puis composante en quadrature (exacte pour
                // une sinusoïde à la fréquence de la note) → signal analytique z = A·e^{jθ}.
                const double b = n.band.tick (x, c);
                const double qd = (n.bPrev[c] - b * n.cosW) / n.sinW;

                // Garde « note centrée » : si la fréquence instantanée s'écarte de
                // plus de ±50 cents de la note, ce n'est que la fuite d'une note
                // voisine : pas d'harmoniques (sinon fausses harmoniques → son sale).
                const double A2 = b * b + qd * qd;
                const double Ap2 = n.bPrev[c] * n.bPrev[c] + n.qPrev[c] * n.qPrev[c];
                float gateTarget = 0.0f;
                if (A2 > 1.0e-14 && Ap2 > 1.0e-14)
                {
                    const double dot   = b * n.bPrev[c] + qd * n.qPrev[c];
                    const double cross = qd * n.bPrev[c] - b * n.qPrev[c];
                    // cos(écart) > cosTol, sans racine carrée : lhs > 0 et lhs² > cosTol²·A²·A'²
                    const double lhs = dot * n.cosW + cross * n.sinW;
                    gateTarget = (lhs > 0.0 && lhs * lhs > n.cosTol * n.cosTol * A2 * Ap2) ? 1.0f : 0.0f;
                }
                n.gate[c] += (gateTarget - n.gate[c]) * gateSmooth;
                n.bPrev[c] = b;
                n.qPrev[c] = qd;

                if (A2 > 1.0e-14 && n.hz * 2.0 < nyq && n.gate[c] > 1.0e-3f)
                {
                    const double A = std::sqrt (A2);
                    // Re(z²)/A, Re(z³)/A², Re(z⁴)/A³ : harmoniques d'amplitude A, en phase.
                    const double y2 = b * b, q2 = qd * qd;
                    double h = harmGain2 * (y2 - q2) / A;
                    if (n.twelfthGain > 1.0e-4f && n.hz * 3.0 < nyq)
                        h += harmGain3 * (double) n.twelfthGain * (b * (y2 - 3.0 * q2)) / A2;
                    if (n.hz * 4.0 < nyq)
                        h += harmGain4 * (y2 * y2 - 6.0 * y2 * q2 + q2 * q2) / (A2 * A);
                    harm += h * (double) (n.harmGain * n.gate[c]);
                }
            }

            // Anti-continu léger sur les harmoniques (sécurité).
            const double hp = harm - dcX[c] + dcR * dcY[c];
            dcX[c] = harm; dcY[c] = hp;

            // 2. Peigne : cascade de cloches sur (son + harmoniques).
            double y = x + (double) amount * hp;
            for (auto& n : notes)
                if (n.active && std::abs (n.bellGainDb) > 0.005f)
                    y = n.bell.tick (y, c);
            out[c] = (float) y;

        }

        // 3. Compensation par octave : statique (les notes de la gamme à 0 dB)
        //    + rattrapage lent limité à +6 dB (sons non tonals uniquement).
        if ((coefCounter & 31) == 1)
            for (int k = 0; k < numComp; ++k)
            {
                if (eOut[k] > 1.0e-16f && eIn[k] > 1.0e-16f)
                {
                    const float errDb = juce::jlimit (-6.0f, 6.0f, 10.0f * std::log10 (eIn[k] / eOut[k]));
                    makeupDb[k] = juce::jlimit (0.0f, ZLH_MAKEUP, makeupDb[k] + errDb * makeupRate);
                }
                if (! std::isfinite (makeupDb[k])) makeupDb[k] = 0.0f;
                const float target = compTargetDb[k] * amount;
                compGainDb[k] += (target - compGainDb[k]) * gainSmoothDb;
                setPeaking (compEq[k], compHz[k], 1.0, compGainDb[k] + makeupDb[k] * amount);
            }

        float inE[numComp] {}, outE[numComp] {};
        for (int c = 0; c < nch; ++c)
        {
            const double xin = (double) data[c][i];
            double v = (double) out[c];
            for (int k = 0; k < numComp; ++k)
                v = compEq[k].tick (v, c);
            for (int k = 0; k < numComp; ++k)
            {
                const double a = measIn[k].tick (xin, c), o = measOut[k].tick (v, c);
                inE[k] += (float) (a * a);
                outE[k] += (float) (o * o);
            }
            data[c][i] = std::isfinite (v) ? (float) v : 0.0f;
        }
        for (int k = 0; k < numComp; ++k)
        {
            eIn[k]  += (inE[k]  - eIn[k])  * levelCoef;
            eOut[k] += (outE[k] - eOut[k]) * levelCoef;
            if (! std::isfinite (eIn[k]) || ! std::isfinite (eOut[k])) eIn[k] = eOut[k] = 0.0f;
        }
    }

    // Sécurité : tout état invalide est vidé.
    for (auto& n : notes)
        for (int c = 0; c < 2; ++c)
            if (! std::isfinite (n.bell.z1[c]) || ! std::isfinite (n.band.z1[c]))
            {
                n.bell.clear(); n.band.clear();
            }
}
