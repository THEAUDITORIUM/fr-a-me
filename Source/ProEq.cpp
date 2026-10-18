/*
  ==============================================================================
    fr(a)me — ProEq.cpp
  ==============================================================================
*/

#include "ProEq.h"
#include <cmath>

namespace
{
    // Butterworth 4e ordre : deux étages de Q 0.5412 et 1.3066 (k = 1/Q).
    constexpr double k1 = 1.0 / 0.54119610, k2 = 1.0 / 1.30656296;

    float sanitise (float v, float lo, float hi, float fallback) noexcept
    {
        return std::isfinite (v) ? juce::jlimit (lo, hi, v) : fallback;
    }
}

void ProEq::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    smoothCoef = 1.0 - std::exp (-16.0 / (0.03 * sampleRate));    // ~30 ms, appliqué tous les 16 échantillons
    onCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
    reset();
}

void ProEq::reset() noexcept
{
    for (auto& f : hp) f.clear();
    for (auto& f : lp) f.clear();
    for (auto& f : bell) f.clear();
    hpOn = lpOn = 0.0f;
    first = true;
}

void ProEq::process (float* const* data, int numChannels, int numSamples, Settings t) noexcept
{
    const int nch = juce::jmin (numChannels, 2);
    if (nch <= 0 || numSamples <= 0)
        return;

    // Réglages assainis.
    t.lowHz  = sanitise (t.lowHz,  minHz, maxHz, minHz);
    t.highHz = sanitise (t.highHz, minHz, maxHz, maxHz);
    bool anyBell = false;
    for (int b = 0; b < numBells; ++b)
    {
        t.bellHz[(size_t) b] = sanitise (t.bellHz[(size_t) b], minHz, maxHz, defaultBellHz[b]);
        t.bellDb[(size_t) b] = sanitise (t.bellDb[(size_t) b], -maxGainDb, maxGainDb, 0.0f);
        t.bellQ [(size_t) b] = sanitise (t.bellQ [(size_t) b], minQ, maxQ, 1.0f);
    }
    if (first) { now = t; first = false; }
    for (int b = 0; b < numBells; ++b)
        anyBell = anyBell || std::abs (t.bellDb[(size_t) b]) > 0.01f || std::abs (now.bellDb[(size_t) b]) > 0.01f;

    const bool hpWanted = lowCutOn (t.lowHz), lpWanted = highCutOn (t.highHz);

    // Tout est neutre : on ne touche pas au son.
    if (! hpWanted && ! lpWanted && ! anyBell && hpOn < 1.0e-4f && lpOn < 1.0e-4f)
    {
        reset();
        now = t;
        first = false;
        return;
    }

    const double nyq = 0.45 * sampleRate;
    const double pi = juce::MathConstants<double>::pi;
    double gLo = 0.0, gHi = 0.0;
    double gB[numBells] {}, kB[numBells] {}, mB[numBells] {};

    auto logGlide = [this] (float& v, float target)
    {
        v = (float) ((double) v * std::exp (smoothCoef * std::log ((double) target / (double) v)));
    };

    for (int i = 0; i < numSamples; ++i)
    {
        if ((i & 15) == 0)
        {
            logGlide (now.lowHz, t.lowHz);
            logGlide (now.highHz, t.highHz);
            gLo = std::tan (pi * juce::jmin ((double) now.lowHz,  nyq) / sampleRate);
            gHi = std::tan (pi * juce::jmin ((double) now.highHz, nyq) / sampleRate);
            for (int b = 0; b < numBells; ++b)
            {
                logGlide (now.bellHz[(size_t) b], t.bellHz[(size_t) b]);
                logGlide (now.bellQ [(size_t) b], t.bellQ [(size_t) b]);
                now.bellDb[(size_t) b] += (float) smoothCoef * (t.bellDb[(size_t) b] - now.bellDb[(size_t) b]);
                const double A = std::pow (10.0, (double) now.bellDb[(size_t) b] / 40.0);
                gB[b] = std::tan (pi * juce::jmin ((double) now.bellHz[(size_t) b], nyq) / sampleRate);
                kB[b] = 1.0 / ((double) now.bellQ[(size_t) b] * A);
                mB[b] = kB[b] * (A * A - 1.0);
            }
        }
        hpOn += ((hpWanted ? 1.0f : 0.0f) - hpOn) * onCoef;
        lpOn += ((lpWanted ? 1.0f : 0.0f) - lpOn) * onCoef;

        for (int c = 0; c < nch; ++c)
        {
            const double x = (double) data[c][i];
            double v1, v2;

            // Coupe basse
            hp[0].tick (x, c, gLo, k1, v1, v2);
            const double h1 = x - k1 * v1 - v2;
            hp[1].tick (h1, c, gLo, k2, v1, v2);
            double y = x + (double) hpOn * ((h1 - k2 * v1 - v2) - x);

            // Cloches
            for (int b = 0; b < numBells; ++b)
            {
                bell[b].tick (y, c, gB[b], kB[b], v1, v2);
                y += mB[b] * v1;
            }

            // Coupe haute
            lp[0].tick (y, c, gHi, k1, v1, v2);
            lp[1].tick (v2, c, gHi, k2, v1, v2);
            y = y + (double) lpOn * (v2 - y);

            data[c][i] = std::isfinite (y) ? (float) y : 0.0f;
        }
    }

    bool bad = false;
    for (int c = 0; c < 2; ++c)
    {
        bad = bad || ! std::isfinite (hp[1].ic1[c]) || ! std::isfinite (lp[1].ic1[c]);
        for (auto& f : bell) bad = bad || ! std::isfinite (f.ic1[c]);
    }
    if (bad)
        reset();
}

float ProEq::magnitudeDb (float hz, const Settings& s) noexcept
{
    double db = 0.0;
    if (lowCutOn (s.lowHz))
    {
        const double r = std::pow ((double) hz / (double) s.lowHz, 8.0);
        db += 10.0 * std::log10 (r / (1.0 + r) + 1.0e-30);
    }
    if (highCutOn (s.highHz))
    {
        const double r = std::pow ((double) hz / (double) s.highHz, 8.0);
        db += 10.0 * std::log10 (1.0 / (1.0 + r));
    }
    for (int b = 0; b < numBells; ++b)
    {
        const double g = (double) s.bellDb[(size_t) b];
        if (std::abs (g) < 0.01) continue;
        const double A = std::pow (10.0, g / 40.0), Q = (double) s.bellQ[(size_t) b];
        const double w = (double) hz / (double) s.bellHz[(size_t) b];
        // H(s) = (s² + s·A/Q + 1) / (s² + s/(A·Q) + 1), s = j·w
        const double re = 1.0 - w * w;
        const double num = re * re + std::pow (w * A / Q, 2.0);
        const double den = re * re + std::pow (w / (A * Q), 2.0);
        db += 10.0 * std::log10 (num / den);
    }
    return (float) juce::jmax (-120.0, db);
}
