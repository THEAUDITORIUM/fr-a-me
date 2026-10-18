/*
  ==============================================================================
    fr(a)me — PitchShifter.cpp
  ==============================================================================
*/

#include "PitchShifter.h"
#include <cmath>

void PitchShifter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    window = 0.045 * sampleRate;
    maxOffset = (int) (0.016 * sampleRate);     // recherche de phase : jusqu'à ~16 ms (période de 62 Hz)
    corrLen   = (int) (0.010 * sampleRate);     // segment comparé : 10 ms
    size = (int) (window * 2.0) + maxOffset + corrLen + 16;
    for (auto& l : line)
        l.assign ((size_t) size, 0.0f);
    activeCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));
    ratioCoef  = 1.0f - std::exp (-1.0f / (0.05f * (float) sampleRate));
    reset();
}

void PitchShifter::reset() noexcept
{
    for (auto& l : line)
        std::fill (l.begin(), l.end(), 0.0f);
    write = 0;
    phase = 0.0;
    offsetA = offsetB = 0.0;
    ratioSmoothed = 1.0;
    active = 0.0f;
}

double PitchShifter::bestOffset (double baseDelay, double otherDelay) const noexcept
{
    // Compare le passé récent lu par l'autre tête avec celui que lirait la
    // tête qui repart, pour chaque décalage 0…maxOffset (pas de 2, puis affinage).
    auto sample = [this] (double d)
    {
        double pos = (double) write - d;
        while (pos < 0.0) pos += (double) size;
        int i0 = (int) pos;
        if (i0 >= size) i0 -= size;
        return (double) line[0][(size_t) i0] + (line[1].empty() ? 0.0 : (double) line[1][(size_t) i0]);
    };

    const int maxOff = juce::jmin (maxOffset, (int) ((double) size - baseDelay - (double) corrLen - 4.0));
    if (maxOff <= 0)
        return 0.0;

    double best = -1.0e30;
    int bestK = 0;
    auto score = [&] (int k)
    {
        double num = 0.0, e1 = 1.0e-12, e2 = 1.0e-12;
        for (int n = 0; n < corrLen; n += 2)
        {
            const double a = sample (otherDelay + n), b = sample (baseDelay + k + n);
            num += a * b; e1 += a * a; e2 += b * b;
        }
        return num / std::sqrt (e1 * e2);
    };
    for (int k = 0; k <= maxOff; k += 2)
    {
        const double sc = score (k);
        if (sc > best) { best = sc; bestK = k; }
    }
    if (bestK > 0)     { const double sc = score (bestK - 1); if (sc > best) { best = sc; bestK = bestK - 1; } }
    if (bestK < maxOff){ const double sc = score (bestK + 1); if (sc > best) { best = sc; bestK = bestK + 1; } }
    return (double) bestK;
}

void PitchShifter::process (juce::AudioBuffer<float>& buffer, int numChannels, float semitones) noexcept
{
    const int len = buffer.getNumSamples();
    const int nch = juce::jmin (numChannels, buffer.getNumChannels(), 2);
    if (len <= 0 || nch <= 0 || size < 8)
        return;

    semitones = juce::jlimit (-12.0f, 12.0f, std::isfinite (semitones) ? semitones : 0.0f);
    const bool wanted = std::abs (semitones) > 0.01f;
    const double targetRatio = std::exp2 ((double) semitones / 12.0);

    // Contourné et fondu terminé : on ne touche à rien (mais on remplit la
    // ligne pour pouvoir repartir sans trou).
    float* data[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < len; ++i)
    {
        active += ((wanted ? 1.0f : 0.0f) - active) * activeCoef;
        ratioSmoothed += (targetRatio - ratioSmoothed) * (double) ratioCoef;

        for (int c = 0; c < nch; ++c)
            line[c][(size_t) write] = std::isfinite (data[c][i]) ? data[c][i] : 0.0f;

        if (active > 1.0e-4f)
        {
            // La tête A est à un retard d = phase·W ; elle glisse de (1 − ratio)
            // par échantillon : ratio > 1 → le retard diminue (lecture plus rapide).
            const double prevA = phase, prevB = std::fmod (phase + 0.5, 1.0);
            phase -= (ratioSmoothed - 1.0) / window;
            phase -= std::floor (phase);
            const double phB = std::fmod (phase + 0.5, 1.0);

            // Quand une tête repart (saut de retard), son gain est nul : on
            // choisit alors son décalage pour qu'elle reprenne EN PHASE avec
            // l'autre tête (corrélation maximale) → pas de saut de phase, la
            // hauteur est exacte et le son ne « grésille » pas.
            if (std::abs (phase - prevA) > 0.5) offsetA = bestOffset (phase * window + 1.0, phB * window + 1.0 + offsetB);
            if (std::abs (phB - prevB) > 0.5)   offsetB = bestOffset (phB * window + 1.0, phase * window + 1.0 + offsetA);

            const double dA = phase * window + 1.0 + offsetA, dB = phB * window + 1.0 + offsetB;
            const double gA = std::sin (juce::MathConstants<double>::pi * phase);
            const double gB = std::sin (juce::MathConstants<double>::pi * phB);
            const double wA = gA * gA, wB = gB * gB;                 // somme = 1

            for (int c = 0; c < nch; ++c)
            {
                auto read = [&] (double d)
                {
                    double pos = (double) write - d;
                    while (pos < 0.0) pos += (double) size;
                    int i0 = (int) pos;
                    if (i0 >= size) { i0 -= size; pos -= (double) size; }
                    const int i1 = (i0 + 1) % size;
                    const double fr = pos - (double) i0;
                    return (double) line[c][(size_t) i0] + fr * ((double) line[c][(size_t) i1] - (double) line[c][(size_t) i0]);
                };
                const double shifted = wA * read (dA) + wB * read (dB);
                const double dry = (double) data[c][i];
                const double y = dry + (double) active * (shifted - dry);
                data[c][i] = std::isfinite (y) ? (float) y : 0.0f;
            }
        }
        else
        {
            active = 0.0f;
            phase = 0.0;
            offsetA = offsetB = 0.0;
        }

        if (++write >= size)
            write = 0;
    }
}
