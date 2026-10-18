/*
  ==============================================================================
    fr(a)me — GrainShifter.cpp
  ==============================================================================
*/

#include "GrainShifter.h"
#include <cmath>

void GrainShifter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    grainLen = juce::jmax (64, (int) std::lround (0.012 * sampleRate));
    hop = grainLen / 2;
    sharpRise = juce::jmax (4, (int) std::lround (0.0005 * sampleRate));
    dieStep = 1.0f / (float) juce::jmax (8, (int) std::lround (0.001 * sampleRate));
    attackCore = juce::jmax (8, (int) std::lround (0.002 * sampleRate));
    window.resize ((size_t) grainLen);
    for (int i = 0; i < grainLen; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) grainLen);
    size = 1;
    while (size < grainLen * 4) size <<= 1;              // ≥ 2 × grain à ×2 de vitesse
    for (auto& l : line) l.assign ((size_t) size, 0.0f);
    reset();
}

void GrainShifter::reset() noexcept
{
    for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
    for (auto& g : grains) g = {};
    writeCount = 0;
    sinceHop = 0;
    lastOnset = -1000000;
}

void GrainShifter::startGrain (bool sharp, double ratio) noexcept
{
    for (auto& g : grains)
        if (! g.live)
        {
            g.live = true;
            g.age = 0;
            g.dying = false;
            g.fadeOut = 1.0f;
            g.sharp = sharp;
            // Retard de départ minimal : vers le haut, la lecture plus rapide
            // doit rester derrière l'écriture pendant toute la durée du grain.
            const double d0 = juce::jmax (0.0, (ratio - 1.0) * (double) grainLen) + 1.0;
            g.readPos = (double) writeCount - d0;
            return;
        }
}

void GrainShifter::process (float* const* data, int numChannels, int numSamples, double ratio, const uint8_t* onset) noexcept
{
    const int nch = juce::jmin (numChannels, 2);
    if (nch <= 0 || numSamples <= 0 || size < 8)
        return;
    ratio = juce::jlimit (0.49, 2.01, ratio);
    const int mask = size - 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const int w = (int) (writeCount & mask);
        for (int c = 0; c < nch; ++c)
            line[c][(size_t) w] = std::isfinite (data[c][i]) ? data[c][i] : 0.0f;
        ++writeCount;

        if (onset != nullptr && onset[i] != 0)
        {
            // Attaque : les grains en cours s'éteignent, un grain neuf part tout de suite.
            for (auto& g : grains) if (g.live) g.dying = true;
            lastOnset = writeCount - 1;
            startGrain (true, ratio);
            sinceHop = 0;
        }
        else if (++sinceHop >= hop)
        {
            sinceHop = 0;
            startGrain (false, ratio);
        }

        float out[2] = { 0.0f, 0.0f };
        for (auto& g : grains)
        {
            if (! g.live) continue;
            float wv = window[(size_t) g.age];
            if (g.sharp && g.age < grainLen / 2)                 // front raide, puis queue de Hann
                wv = juce::jmin (1.0f, (float) (g.age + 1) / (float) sharpRise);
            if (g.dying) { g.fadeOut -= dieStep; if (g.fadeOut <= 0.0f) { g.live = false; continue; } wv *= g.fadeOut; }

            // lecture interpolée
            const double rp = g.readPos;
            // Vers le haut, les grains relisent un peu de passé : le cœur de la
            // dernière attaque n'est relu que par son propre grain (pas d'écho).
            if (! g.sharp)
            {
                const double rel = rp - (double) lastOnset;
                if (rel > -0.25 * attackCore && rel < (double) attackCore)
                    wv *= 0.1f;
            }
            const long long i0 = (long long) std::floor (rp);
            const float fr = (float) (rp - (double) i0);
            const int a = (int) (i0 & mask), b = (int) ((i0 + 1) & mask);
            for (int c = 0; c < nch; ++c)
                out[c] += wv * (line[c][(size_t) a] + fr * (line[c][(size_t) b] - line[c][(size_t) a]));

            g.readPos += ratio;
            if (++g.age >= grainLen) g.live = false;
        }
        for (int c = 0; c < nch; ++c)
            data[c][i] = out[c];
    }
}
