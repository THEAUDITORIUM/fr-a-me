/*
  ==============================================================================
    fr(a)me — RetuneFx.cpp
  ==============================================================================
*/

#include "RetuneFx.h"
#include <cmath>

namespace
{
   #ifndef RT_PHASER_MIX
    #define RT_PHASER_MIX 0.75
   #endif
   #ifndef RT_FLANGE_MIX
    #define RT_FLANGE_MIX 0.6
   #endif
   #ifndef RT_CHORUS_MIX
    #define RT_CHORUS_MIX 0.55
   #endif
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    constexpr double phaserLoHz = 180.0, phaserHiHz = 3400.0, phaserRate = 0.21, phaserFeedback = 0.5;
    constexpr double flangeMinMs = 0.5, flangeMaxMs = 4.5, flangeRate = 0.13, flangeFeedback = 0.62;
    constexpr double chorusBaseMs[3]  { 11.0, 16.0, 23.0 };
    constexpr double chorusDepthMs    = 1.4;             // désaccord max ≈ 9 cents : reste juste
    constexpr double chorusRate[3]    { 0.33, 0.47, 0.61 };
    constexpr double chorusPanL[3]    { 1.0, 0.25, 0.75 };   // voix réparties gauche / droite
    constexpr double chorusPanR[3]    { 0.25, 1.0, 0.75 };
}

void RetuneFx::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    flangeSize = (int) std::ceil ((flangeMaxMs + 1.0) * 0.001 * sampleRate) + 4;
    chorusSize = (int) std::ceil ((chorusBaseMs[2] + chorusDepthMs + 2.0) * 0.001 * sampleRate) + 4;
    for (auto& l : flangeLine) l.assign ((size_t) flangeSize, 0.0f);
    for (auto& l : chorusLine) l.assign ((size_t) chorusSize, 0.0f);
    phaserInc = twoPi * phaserRate / sampleRate;
    flangeInc = twoPi * flangeRate / sampleRate;
    for (int v = 0; v < 3; ++v)
        chorusInc[v] = twoPi * chorusRate[v] / sampleRate;
    reset();
}

void RetuneFx::reset() noexcept
{
    for (auto& ch : apState) for (auto& s : ch) s = 0.0;
    phaserFb[0] = phaserFb[1] = 0.0;
    flangeFb[0] = flangeFb[1] = 0.0;
    for (auto& l : flangeLine) std::fill (l.begin(), l.end(), 0.0f);
    for (auto& l : chorusLine) std::fill (l.begin(), l.end(), 0.0f);
    flangeWrite = chorusWrite = 0;
    phaserLfo = 0.0; flangeLfo = 1.3;
    for (int v = 0; v < 3; ++v) chorusLfo[v] = 2.1 * v;
    coefCounter = 0;
}

double RetuneFx::readLine (const std::vector<float>& line, int size, int write, double d) noexcept
{
    double pos = (double) write - d;
    while (pos < 0.0) pos += (double) size;
    int i0 = (int) pos;
    if (i0 >= size) { i0 -= size; pos -= (double) size; }
    const int i1 = (i0 + 1 == size) ? 0 : i0 + 1;
    const double fr = pos - (double) i0;
    return (double) line[(size_t) i0] + fr * ((double) line[(size_t) i1] - (double) line[(size_t) i0]);
}

void RetuneFx::processFrame (double& l, double& r, bool mono) noexcept
{
    if (flangeSize < 8 || chorusSize < 8)
        return;

    double x[2] = { l, mono ? l : r };

    // ---- 1. Phaser --------------------------------------------------------------
    if (coefCounter-- <= 0)
    {
        coefCounter = 8;
        for (int c = 0; c < 2; ++c)
        {
            const double s = 0.5 + 0.5 * std::sin (phaserLfo + (c == 0 ? 0.0 : juce::MathConstants<double>::halfPi));
            const double hz = phaserLoHz * std::pow (phaserHiHz / phaserLoHz, s);
            const double t = std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
            apCoef[c] = (t - 1.0) / (t + 1.0);
        }
    }
    phaserLfo += phaserInc;
    if (phaserLfo > twoPi) phaserLfo -= twoPi;

    for (int c = 0; c < 2; ++c)
    {
        double v = x[c] + phaserFeedback * phaserFb[c];
        for (int st = 0; st < phaserStages; ++st)
        {
            // passe-tout 1er ordre : y = a·v + s ; s = v − a·y
            const double y = apCoef[c] * v + apState[c][st];
            apState[c][st] = v - apCoef[c] * y;
            v = y;
        }
        phaserFb[c] = std::tanh (v);                       // rétroaction bornée
        x[c] = x[c] + RT_PHASER_MIX * (0.5 * (x[c] + v) - x[c]);   // encoches du phaser
    }

    // ---- 2. Flanger -------------------------------------------------------------
    for (int c = 0; c < 2; ++c)
    {
        const double s = 0.5 + 0.5 * std::sin (flangeLfo + (c == 0 ? 0.0 : juce::MathConstants<double>::pi));
        const double dMs = flangeMinMs + (flangeMaxMs - flangeMinMs) * s * s;   // passe plus de temps près de 0 : « jet »
        const double d = juce::jmax (1.0, dMs * 0.001 * sampleRate);
        const double delayed = readLine (flangeLine[c], flangeSize, flangeWrite, d);
        flangeLine[c][(size_t) flangeWrite] = (float) (x[c] + flangeFeedback * flangeFb[c]);
        flangeFb[c] = std::tanh (delayed);
        x[c] = (x[c] + RT_FLANGE_MIX * delayed) / (1.0 + 0.5 * RT_FLANGE_MIX);
    }
    flangeLfo += flangeInc;
    if (flangeLfo > twoPi) flangeLfo -= twoPi;

    // ---- 3. Chorus --------------------------------------------------------------
    for (int c = 0; c < 2; ++c)
        chorusLine[c][(size_t) chorusWrite] = (float) x[c];

    double wet[2] = { 0.0, 0.0 };
    for (int v = 0; v < 3; ++v)
    {
        const double dMs = chorusBaseMs[v] + chorusDepthMs * std::sin (chorusLfo[v]);
        const double d = dMs * 0.001 * sampleRate;
        // chaque voix lit le canal opposé à sa position : élargit l'image
        const double vl = readLine (chorusLine[0], chorusSize, chorusWrite, d);
        const double vr = readLine (chorusLine[1], chorusSize, chorusWrite, d + 0.37 * (v + 1) * 0.001 * sampleRate);
        wet[0] += chorusPanL[v] * vl;
        wet[1] += chorusPanR[v] * vr;
        chorusLfo[v] += chorusInc[v];
        if (chorusLfo[v] > twoPi) chorusLfo[v] -= twoPi;
    }
    for (int c = 0; c < 2; ++c)
        x[c] = (x[c] + RT_CHORUS_MIX * wet[c] / 1.4) / (1.0 + 0.35 * RT_CHORUS_MIX);

    if (++flangeWrite >= flangeSize) flangeWrite = 0;
    if (++chorusWrite >= chorusSize) chorusWrite = 0;

    l = std::isfinite (x[0]) ? x[0] : 0.0;
    r = std::isfinite (x[1]) ? x[1] : 0.0;
    if (! std::isfinite (phaserFb[0]) || ! std::isfinite (flangeFb[0]))
        reset();
}
