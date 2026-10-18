/*
  ==============================================================================
    fr(a)me — ZeroLatencyHarmonizer.cpp
  ==============================================================================
*/

#include "ZeroLatencyHarmonizer.h"
#include "Scale.h"
#include <cmath>

namespace
{
    inline float smoothStep (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }

    constexpr float bellBoostDb   = 21.0f;   // renfort des notes (contraste final ≈ 17 dB, comme la 1.4 poussée)
    constexpr float harmGain2     = 0.13f;   // ×2 (octave)
    constexpr float harmGain3     = 0.031f;   // ×3 (douzième, si la quinte est dans la gamme)
    constexpr float harmGain4     = 0.019f;   // ×4 (double octave)
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
        const double cents = 40.0 + (22.0 - 40.0) * t;
        const double bwOct = cents / 1200.0;
        n.q = 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * bwOct));

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
    levelCoef     = 1.0f - std::exp (-1.0f  / (0.3f   * (float) sampleRate));
    compUp        = 1.0f - std::exp (-32.0f / (0.05f  * (float) sampleRate));
    compDown      = 1.0f - std::exp (-32.0f / (0.3f   * (float) sampleRate));
    setHighpass (measIn,  220.0);   setLowpass (measInLP,  9000.0);
    setHighpass (measOut, 220.0);   setLowpass (measOutLP, 9000.0);
    setHighShelf (compShelfLo, 250.0, 0.0f);
    setHighShelf (compShelfHi, 9000.0, 0.0f);
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
        n.active = n.bellTargetDb > 0.0f || n.harmTarget > 0.0f;
    }
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0;
    eIn = eOut = 0.0f;
    eFullIn = eFullOut = 0.0f;
    levelGain = 1.0f;
    compDb = 0.0f;
    setHighShelf (compShelfLo, 250.0, 0.0f);
    setHighShelf (compShelfHi, 9000.0, 0.0f);
    compShelfLo.clear(); compShelfHi.clear();
    measIn.clear(); measOut.clear(); measInLP.clear(); measOutLP.clear();
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
        n.bellTargetDb = in ? bellBoostDb * n.freqWeight : 0.0f;
        n.harmTarget   = in ? n.harmWeight : 0.0f;
        n.twelfthInScale = frame::maskContains (mask, midi + 19);
        if (in) n.active = true;
    }
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
    const double alpha = std::sin (w0) / (2.0 * n.q);
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
                if (n.bellTargetDb <= 0.0f && n.bellGainDb < 0.01f && n.harmTarget <= 0.0f && n.harmGain < 1.0e-4f)
                {
                    n.bellGainDb = 0.0f; n.harmGain = 0.0f;
                    updateBell (n); n.bell.clear(); n.band.clear(); n.bPrev[0] = n.bPrev[1] = 0.0;
                    n.active = false;                         // sorti de la gamme : en veille
                }
            }
        }

        float inE = 0.0f, outE = 0.0f;
        float out[2] {};

        for (int c = 0; c < nch; ++c)
        {
            const double x = (double) data[c][i];
            inE += (float) (x * x);

            // 1. Harmoniques justes, calculées sur le son d'origine.
            double harm = 0.0;
            for (auto& n : notes)
            {
                if (! n.active || (n.harmGain <= 1.0e-4f && n.harmTarget <= 0.0f)) continue;
                if (c == 0) n.harmGain += (n.harmTarget - n.harmGain) * gainSmoothLin;

                // Isolation de la note, puis composante en quadrature (exacte pour
                // une sinusoïde à la fréquence de la note) → signal analytique z = A·e^{jθ}.
                const double b = n.band.tick (x, c);
                const double qd = (n.bPrev[c] - b * n.cosW) / n.sinW;

                // Garde « note centrée » : la rotation de z d'un échantillon à
                // l'autre donne la fréquence instantanée. Si elle s'écarte de plus
                // de ±50 cents de la note, ce que la bande reçoit n'est que la fuite
                // d'une note voisine : on ne fabrique pas d'harmoniques (sinon de
                // fausses harmoniques interfèrent avec les vraies → son sale).
                const double A2 = b * b + qd * qd;
                const double Ap2 = n.bPrev[c] * n.bPrev[c] + n.qPrev[c] * n.qPrev[c];
                float gateTarget = 0.0f;
                if (A2 > 1.0e-14 && Ap2 > 1.0e-14)
                {
                    const double dot   = b * n.bPrev[c] + qd * n.qPrev[c];
                    const double cross = qd * n.bPrev[c] - b * n.qPrev[c];
                    const double cosDev = (dot * n.cosW + cross * n.sinW) / std::sqrt (A2 * Ap2);
                    gateTarget = cosDev > n.cosTol ? 1.0f : 0.0f;
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
                    if (n.twelfthInScale && n.hz * 3.0 < nyq)
                        h += harmGain3 * (b * (y2 - 3.0 * q2)) / A2;
                    if (n.hz * 4.0 < nyq)
                        h += harmGain4 * (y2 * y2 - 6.0 * y2 * q2 + q2 * q2) / (A2 * A);
                    harm += h * (double) (n.harmGain * n.gate[c]);
                }
            }

            // Anti-continu léger sur les harmoniques (sécurité).
            const double hp = harm - dcX[c] + dcR * dcY[c];
            dcX[c] = harm; dcY[c] = hp;

            // 2. Peigne : cascade de cloches sur (son + harmoniques), comme la 1.4
            //    où les harmoniques passaient aussi dans le peigne.
            double y = x + (double) amount * hp;
            for (auto& n : notes)
                if (n.active && n.bellGainDb > 0.005f)
                    y = n.bell.tick (y, c);

            out[c] = (float) y;

            // Mesure de l'énergie dans la zone du peigne (avant / après), pour
            // la compensation : seule cette zone est rééquilibrée, le grave reste intact.
            const double mi = measInLP.tick (measIn.tick (x, c), c);
            inE += (float) (mi * mi);
        }

        // Compensation : une cloche très large (≈ 250 Hz – 10 kHz) dont le gain
        // suit le rapport d'énergie de la zone du peigne. Remonte vite (~50 ms),
        // redescend en douceur (~300 ms) ; recalculée toutes les 32 échantillons.
        eIn  += (inE  - eIn)  * levelCoef;
        if ((coefCounter & 31) == 1)
        {
            // Intégrateur : corrige l'écart d'énergie mesuré après compensation.
            if (eOut > 1.0e-14f && eIn > 1.0e-14f)
            {
                const float errDb = juce::jlimit (-12.0f, 12.0f, 10.0f * std::log10 (eIn / eOut));
                compDb = juce::jlimit (-30.0f, 6.0f, compDb + errDb * (errDb > 0.0f ? compUp : compDown));
            }
            if (! std::isfinite (compDb)) compDb = 0.0f;
            setHighShelf (compShelfLo, 250.0,  compDb);     // plateau de gain compDb
            setHighShelf (compShelfHi, 9000.0, -compDb);    // … entre ~250 Hz et ~9 kHz
        }

        float fullIn = 0.0f, fullOut = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            const double xin = (double) data[c][i];
            const double v = compShelfHi.tick (compShelfLo.tick ((double) out[c], c), c);
            // boucle fermée : on mesure APRÈS la compensation → convergence exacte
            const double mo = measOutLP.tick (measOut.tick (v, c), c);
            outE += (float) (mo * mo);
            fullIn  += (float) (xin * xin);
            fullOut += (float) (v * v * (double) (levelGain * levelGain));
            const float y = (float) v * levelGain;            // volume global : correction douce (±3 dB)
            data[c][i] = std::isfinite (y) ? y : 0.0f;
        }
        eFullIn  += (fullIn  - eFullIn)  * levelCoef;
        eFullOut += (fullOut - eFullOut) * levelCoef;
        if ((coefCounter & 31) == 1 && eFullOut > 1.0e-14f && eFullIn > 1.0e-14f)
        {
            const float err = std::sqrt (eFullIn / eFullOut);
            levelGain = juce::jlimit (0.7f, 1.4f, levelGain * (1.0f + (err - 1.0f) * 0.02f));
            if (! std::isfinite (levelGain)) levelGain = 1.0f;
        }
        eOut += (outE - eOut) * levelCoef;
        if (! std::isfinite (eOut)) eOut = 0.0f;
    }

    // Sécurité : tout état invalide est vidé.
    for (auto& n : notes)
        for (int c = 0; c < 2; ++c)
            if (! std::isfinite (n.bell.z1[c]) || ! std::isfinite (n.band.z1[c]))
            {
                n.bell.clear(); n.band.clear();
            }
}
