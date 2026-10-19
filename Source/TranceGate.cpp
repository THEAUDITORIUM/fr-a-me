/*
  ==============================================================================
    fr(a)me — TranceGate.cpp
  ==============================================================================
*/

#include "TranceGate.h"
#include <algorithm>
#include <cmath>

namespace
{
    using P = TranceGate::Preset;
    // Un mot par motif ; 16 pas (doubles croches), niveau 0 … 1.
    const P presets[TranceGate::numPresets] =
    {
        { "bloom",    0.90f, { 1, 0, 0, 1,   0, 0, 1, 0,   0, 1, .4f, 0,   1, 0, 1, 0 } },
        { "pulse",    0.85f, { 1, 0, 1, 0,   1, 0, 1, 0,   1, 0, 1, 0,     1, 0, 1, 0 } },
        { "flutter",  0.50f, { 1, 1, 1, 1,   1, 1, 1, 1,   1, 1, 1, 1,     1, 1, 1, 1 } },
        { "gallop",   0.80f, { 1, 0, 1, 1,   1, 0, 1, 1,   1, 0, 1, 1,     1, 0, 1, 1 } },
        { "offbeat",  0.90f, { 0, 0, 1, 0,   0, 0, 1, 0,   0, 0, 1, 0,     0, 0, 1, 0 } },
        { "pump",     1.00f, { .1f, .5f, .8f, 1,  .1f, .5f, .8f, 1,  .1f, .5f, .8f, 1,  .1f, .5f, .8f, 1 } },
        { "chop",     0.90f, { 1, 1, 0, 0,   1, 1, 0, 0,   1, 1, 0, 0,     1, 1, 0, 0 } },
        { "morse",    0.70f, { 1, 0, 1, 1,   0, 1, 0, 1,   1, 0, 1, 0,     1, 1, 0, 1 } },
        { "dotted",   0.90f, { 1, 0, 0, 1,   0, 0, 1, 0,   0, 1, 0, 0,     1, 0, 0, 1 } },
        { "tresillo", 0.90f, { 1, 0, 0, 1,   0, 0, 1, 0,   1, 0, 0, 1,     0, 0, 1, 0 } },
        { "heart",    0.80f, { 1, 0, .6f, 0, 0, 0, 0, 0,   1, 0, .6f, 0,   0, 0, 0, 0 } },
        { "ripple",   0.85f, { 1, .7f, .45f, .25f,  1, .7f, .45f, .25f,  1, .7f, .45f, .25f,  1, .7f, .45f, .25f } },
        { "swell",    0.85f, { .25f, .5f, .75f, 1,  .25f, .5f, .75f, 1,  .25f, .5f, .75f, 1,  .25f, .5f, .75f, 1 } },
        { "sparkle",  0.60f, { 1, 0, 1, 1,   0, 1, 1, 0,   1, 0, 1, 1,     0, 1, 1, 1 } },
        { "drift",    0.95f, { 1, 0, 0, 0,   0, 0, 1, 0,   0, 0, 1, 0,     0, 0, 0, 0 } },
    };
}

const TranceGate::Preset& TranceGate::preset (int index) noexcept
{
    return presets[std::clamp (index, 0, numPresets - 1)];
}

void TranceGate::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    attack    = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
    release   = 1.0f - std::exp (-1.0f / (0.004f * (float) sampleRate));
    depthCoef = 1.0f - std::exp (-1.0f / (0.020f * (float) sampleRate));
    reset();
}

void TranceGate::reset() noexcept
{
    env = 1.0f;
    depth = 0.0f;
    step = 0;
}

float TranceGate::next (double ppq, bool on, const std::array<float, numSteps>& steps, float length) noexcept
{
    if (! std::isfinite (ppq)) ppq = 0.0;
    length = std::isfinite (length) ? std::clamp (length, 0.05f, 1.0f) : 0.9f;

    const double pos = ppq * 4.0;                          // en doubles croches
    const double fl = std::floor (pos);
    const double phase = pos - fl;
    long long idx = (long long) fl % numSteps;
    if (idx < 0) idx += numSteps;
    step = (int) idx;

    float lvl = steps[(size_t) idx];
    lvl = std::isfinite (lvl) ? std::clamp (lvl, 0.0f, 1.0f) : 0.0f;
    const float target = phase < (double) length ? lvl : 0.0f;
    env += (target - env) * (target > env ? attack : release);

    depth += ((on ? 1.0f : 0.0f) - depth) * depthCoef;
    if (depth < 1.0e-5f) depth = 0.0f;
    if (! std::isfinite (env)) env = 1.0f;

    return 1.0f - depth * (1.0f - env);
}
