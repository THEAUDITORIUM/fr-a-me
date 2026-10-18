/*
  ==============================================================================
    fr(a)me — HarmonicResonator.cpp
  ==============================================================================
*/

#include "HarmonicResonator.h"
#include "Scale.h"
#include <cmath>

namespace
{
    inline float smoothStep (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }

    /** Poids d'un intervalle (en demi-tons depuis la tonique) : l'accord de la
        tonalité domine. Les tierces mineure (3) et majeure (4) ont le même poids :
        seule celle qui appartient à la gamme existera. */
    inline float intervalWeight (int interval) noexcept
    {
        switch (interval)
        {
            case 0:  return 1.00f;   // tonique
            case 7:  return 0.90f;   // quinte
            case 3:
            case 4:  return 0.85f;   // tierce (mineure ou majeure)
            case 8:
            case 9:  return 0.55f;   // sixte
            case 5:  return 0.50f;   // quarte
            case 10:
            case 11: return 0.50f;   // septième
            case 2:  return 0.45f;   // seconde
            default: return 0.40f;   // (autres : n'arrive pas dans majeur / mineur)
        }
    }

    // Force des résonances : un partiel exactement sur une note est porté ~+28 dB
    // avant compensation de niveau (l'équivalent des cloches Pro-Q à +30 dB).
    constexpr float resonanceGain = 25.0f;

    // Énergie maximale des résonances par rapport au son entrant (×6 ≈ +7.8 dB).
    constexpr float maxResonanceRatio = 6.0f;
}

void HarmonicResonator::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    for (int i = 0; i < numNotes; ++i)
    {
        auto& r = res[(size_t) i];
        const double hz = frame::midiToHz (lowNote + i);

        // Temps de résonance : plus long dans le grave (cordes), plus court dans l'aigu.
        const double tau = juce::jlimit (0.10, 0.40, 0.30 * std::pow (hz / 200.0, -0.25));
        const double q = juce::MathConstants<double>::pi * hz * tau;
        const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (hz, sampleRate * 0.45) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        r.b0 = alpha / a0;                       // gain unité au centre
        r.a1 = -2.0 * std::cos (w0) / a0;
        r.a2 = (1.0 - alpha) / a0;

        // Présence maximale 150 Hz – 3 kHz.
        const float f = (float) hz;
        r.freqWeight = 0.25f + 0.75f * smoothStep ((f - 60.0f) / 90.0f) * (1.0f - 0.7f * smoothStep ((f - 3000.0f) / 3000.0f));

        // Inaudible au-delà de 0.45 × fs : on désactive.
        if (hz > sampleRate * 0.45)
            r.freqWeight = 0.0f;
    }

    gainSmooth   = 1.0f - std::exp (-1.0f / (0.12f  * (float) sampleRate));
    energyCoef   = 1.0f - std::exp (-1.0f / (0.3f   * (float) sampleRate));
    fastCoef     = 1.0f - std::exp (-1.0f / (0.05f  * (float) sampleRate));
    limitAttack  = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    limitRelease = 1.0f - std::exp (-1.0f / (0.08f  * (float) sampleRate));
    reset();
}

void HarmonicResonator::reset() noexcept
{
    for (auto& r : res)
    {
        for (int c = 0; c < 2; ++c) r.x1[c] = r.x2[c] = r.y1[c] = r.y2[c] = 0.0;
        r.gain = r.target;
        r.sleeping = r.gain <= 0.0f;
    }
    energyIn = energyOut = 0.0f;
    levelGain = 1.0f;
    fastIn = fastRes = 0.0f;
    resLimit = 1.0f;
}

void HarmonicResonator::setScale (uint16_t newMask, int newTonic) noexcept
{
    newTonic = ((newTonic % 12) + 12) % 12;
    if (newMask == mask && newTonic == tonic)
        return;
    mask = newMask;
    tonic = newTonic;
    updateTargets();
}

void HarmonicResonator::updateTargets() noexcept
{
    for (int pc = 0; pc < 12; ++pc)
        degreeWeight[(size_t) pc] = frame::maskContains (mask, pc) ? intervalWeight ((pc - tonic + 12) % 12) : 0.0f;

    for (int i = 0; i < numNotes; ++i)
    {
        auto& r = res[(size_t) i];
        r.target = degreeWeight[(size_t) ((lowNote + i) % 12)] * r.freqWeight;
        if (r.target > 0.0f)
            r.sleeping = false;
    }
}

void HarmonicResonator::process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept
{
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin (numChannels, buffer.getNumChannels(), 2);
    if (n <= 0 || nch <= 0 || mask == 0)
        return;

    amount = juce::jlimit (0.0f, 1.0f, std::isfinite (amount) ? amount : 0.0f);
    const float g = resonanceGain * amount;

    float* data[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < n; ++i)
    {
        double sum[2] = { 0.0, 0.0 };
        float inE = 0.0f;
        double xin[2];
        for (int c = 0; c < nch; ++c)
        {
            xin[c] = (double) data[c][i];
            inE += (float) (xin[c] * xin[c]);
        }

        for (auto& r : res)
        {
            if (r.sleeping)
                continue;

            r.gain += (r.target - r.gain) * gainSmooth;

            for (int c = 0; c < nch; ++c)
            {
                // y = b0·(x − x2) − a1·y1 − a2·y2
                const double y = r.b0 * (xin[c] - r.x2[c]) - r.a1 * r.y1[c] - r.a2 * r.y2[c];
                r.x2[c] = r.x1[c]; r.x1[c] = xin[c];
                r.y2[c] = r.y1[c]; r.y1[c] = y;
                sum[c] += y * (double) r.gain;
            }

            // Résonateur sorti de la gamme et éteint : on le met en veille.
            if (r.target <= 0.0f && r.gain < 1.0e-4f)
            {
                r.gain = 0.0f;
                r.sleeping = true;
                for (int c = 0; c < 2; ++c) r.x1[c] = r.x2[c] = r.y1[c] = r.y2[c] = 0.0;
            }
        }

        // Part des résonances bornée par rapport au son entrant (suivi rapide,
        // ~50 ms) : jamais plus de ~+8 dB au-dessus de lui, même quand une note
        // tenue tombe pile sur un résonateur. Le son d'origine reste toujours là.
        float resE = 0.0f;
        for (int c = 0; c < nch; ++c) resE += (float) (sum[c] * sum[c]);
        fastIn  += (inE  - fastIn)  * fastCoef;
        fastRes += (resE * g * g - fastRes) * fastCoef;
        float limit = 1.0f;
        if (fastRes > 1.0e-14f)
            limit = juce::jmin (1.0f, std::sqrt (maxResonanceRatio * fastIn / fastRes));
        resLimit += (limit - resLimit) * (limit < resLimit ? limitAttack : limitRelease);
        if (! std::isfinite (resLimit)) resLimit = 1.0f;

        float out[2];
        float outE = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            out[c] = (float) (xin[c] + (double) (g * resLimit) * sum[c]);
            outE += out[c] * out[c];
        }

        // Compensation douce du volume global (~300 ms) : le perçu ne saute pas.
        energyIn  += (inE  - energyIn)  * energyCoef;
        energyOut += (outE - energyOut) * energyCoef;
        const float target = energyOut > 1.0e-12f ? juce::jlimit (0.25f, 1.25f, std::sqrt (energyIn / energyOut)) : 1.0f;
        // remonte vite (~50 ms : pas de creux quand une note s'arrête), redescend en douceur (~300 ms)
        levelGain += (target - levelGain) * (target > levelGain ? fastCoef : energyCoef);
        if (! std::isfinite (levelGain)) levelGain = 1.0f;

        for (int c = 0; c < nch; ++c)
        {
            const float v = out[c] * levelGain;
            data[c][i] = std::isfinite (v) ? v : 0.0f;
        }
    }

    // Sécurité : un état invalide est vidé (jamais de NaN qui persiste).
    for (auto& r : res)
        for (int c = 0; c < 2; ++c)
            if (! std::isfinite (r.y1[c]) || ! std::isfinite (r.y2[c]))
            {
                r.x1[c] = r.x2[c] = r.y1[c] = r.y2[c] = 0.0;
            }
}
