/*
  ==============================================================================
    fr(a)me — CutEq.cpp
  ==============================================================================
*/

#include "CutEq.h"
#include <cmath>

namespace
{
    // Butterworth 4e ordre : deux étages de Q 0.5412 et 1.3066 (k = 1/Q).
    constexpr double k1 = 1.0 / 0.54119610, k2 = 1.0 / 1.30656296;
}

void CutEq::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    freqCoef = 1.0 - std::exp (-1.0 / (0.03 * sampleRate));        // glissement ~30 ms
    onCoef   = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
    reset();
}

void CutEq::reset() noexcept
{
    for (auto& f : hp) f.clear();
    for (auto& f : lp) f.clear();
    lowNow = minHz; highNow = maxHz;
    hpOn = lpOn = 0.0f;
}

void CutEq::process (float* const* data, int numChannels, int numSamples, float lowHz, float highHz) noexcept
{
    const int nch = juce::jmin (numChannels, 2);
    if (nch <= 0 || numSamples <= 0)
        return;

    if (! std::isfinite (lowHz))  lowHz = minHz;
    if (! std::isfinite (highHz)) highHz = maxHz;
    lowHz  = juce::jlimit (minHz, maxHz, lowHz);
    highHz = juce::jlimit (minHz, maxHz, highHz);
    if (highHz < lowHz * 1.05f)                       // plage minimale : ~1 demi-ton
        highHz = juce::jmin (maxHz, lowHz * 1.05f);

    const bool hpWanted = lowHz  > minHz * 1.01f;
    const bool lpWanted = highHz < maxHz * 0.99f;

    // Rien à faire : on ne touche pas au son (filtres gardés à l'écoute pour repartir sans clic).
    if (! hpWanted && ! lpWanted && hpOn < 1.0e-4f && lpOn < 1.0e-4f)
    {
        hpOn = lpOn = 0.0f;
        lowNow = minHz; highNow = maxHz;
        for (auto& f : hp) f.clear();
        for (auto& f : lp) f.clear();
        return;
    }

    const double nyq = 0.49 * sampleRate;
    double gLo = 0.0, gHi = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        // Glissement logarithmique des fréquences ; coefficients recalculés tous les 16 échantillons.
        lowNow  *= std::exp (freqCoef * std::log ((double) lowHz  / lowNow));
        highNow *= std::exp (freqCoef * std::log ((double) highHz / highNow));
        if ((i & 15) == 0)
        {
            gLo = std::tan (juce::MathConstants<double>::pi * juce::jmin (lowNow,  nyq) / sampleRate);
            gHi = std::tan (juce::MathConstants<double>::pi * juce::jmin (highNow, nyq) / sampleRate);
        }
        hpOn += ((hpWanted ? 1.0f : 0.0f) - hpOn) * onCoef;
        lpOn += ((lpWanted ? 1.0f : 0.0f) - lpOn) * onCoef;

        for (int c = 0; c < nch; ++c)
        {
            const double x = (double) data[c][i];
            double y = hp[1].tick (hp[0].tick (x, c, gLo, k1, true), c, gLo, k2, true);
            y = x + (double) hpOn * (y - x);
            double z = lp[1].tick (lp[0].tick (y, c, gHi, k1, false), c, gHi, k2, false);
            z = y + (double) lpOn * (z - y);
            data[c][i] = std::isfinite (z) ? (float) z : 0.0f;
        }
    }

    for (int c = 0; c < 2; ++c)
        if (! std::isfinite (hp[1].ic1[c]) || ! std::isfinite (lp[1].ic1[c]))
        {
            for (auto& f : hp) f.clear();
            for (auto& f : lp) f.clear();
        }
}
