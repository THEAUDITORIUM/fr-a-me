/*
  ==============================================================================
    fr(a)me — AmbientReverb.cpp
  ==============================================================================
*/

#include "AmbientReverb.h"
#include <cmath>

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // Longueurs de base des lignes (ms), premières entre elles une fois converties.
    constexpr float lineMs[8] = { 37.1f, 43.7f, 51.3f, 59.9f, 67.3f, 77.9f, 89.1f, 101.3f };
    constexpr float diffuserMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };

    inline float softClip (float x) noexcept { return std::tanh (x); }
}

float AmbientReverb::nextRandom() noexcept
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) (rng >> 8) * (1.0f / 16777216.0f);
}

void AmbientReverb::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const float msToSamples = (float) sampleRate * 0.001f;

    for (int i = 0; i < numLines; ++i)
    {
        // taille × 1.6 au maximum + modulation : on prévoit large
        baseDelay[(size_t) i] = lineMs[i] * msToSamples;
        lines[(size_t) i].setMax ((int) (baseDelay[(size_t) i] * 1.7f) + 64);
        lfoRate[(size_t) i] = 0.07f + 0.045f * (float) i;          // 0.07 → 0.39 Hz
        lfoPhase[(size_t) i] = twoPi * (float) i / (float) numLines;
    }

    for (int i = 0; i < numDiffusers; ++i)
    {
        diffuserL[(size_t) i].setSize ((int) (diffuserMs[i] * msToSamples));
        diffuserR[(size_t) i].setSize ((int) (diffuserMs[i] * 1.13f * msToSamples));
        diffuserL[(size_t) i].g = diffuserR[(size_t) i].g = (i < 2) ? 0.7f : 0.6f;
    }

    preDelayL.setMax ((int) (0.06 * sampleRate));
    preDelayR.setMax ((int) (0.06 * sampleRate));

    cloudL.assign ((size_t) (2.5 * sampleRate), 0.0f);
    cloudR.assign ((size_t) (2.5 * sampleRate), 0.0f);

    reset();
}

void AmbientReverb::reset() noexcept
{
    for (auto& l : lines) l.clear();
    for (auto& d : diffuserL) d.clear();
    for (auto& d : diffuserR) d.clear();
    preDelayL.clear(); preDelayR.clear();
    std::fill (cloudL.begin(), cloudL.end(), 0.0f);
    std::fill (cloudR.begin(), cloudR.end(), 0.0f);
    cloudWrite = 0;
    for (auto& g : grains) g.active = false;
    lowpassState.fill (0.0f);
    grainClock = 0.0f;
    grainFeedL = grainFeedR = 0.0f;
    amountSmoothed = 0.0f;
}

void AmbientReverb::process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept
{
    const int n = buffer.getNumSamples();
    if (n <= 0 || cloudL.empty())
        return;

    amount = juce::jlimit (0.0f, 1.0f, std::isfinite (amount) ? amount : 0.0f);
    float* L = buffer.getWritePointer (0);
    float* R = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    const float sr = (float) sampleRate;
    const float smooth = 1.0f - std::exp (-1.0f / (0.05f * sr));
    const int cloudSize = (int) cloudL.size();

    for (int i = 0; i < n; ++i)
    {
        amountSmoothed += (amount - amountSmoothed) * smooth;
        const float a = amountSmoothed;

        const float inL = L[i];
        const float inR = R != nullptr ? R[i] : inL;

        if (a < 1.0e-4f)
        {
            // Réverbe coupée : les lignes se vident (pas de vieille queue qui
            // ressurgirait quand on la remonte).
            for (auto& l : lines) l.push (0.0f);
            continue;
        }

        // --- paramètres dérivés de amount --------------------------------------
        const float t60   = 1.5f + 10.5f * a * a;                 // 1.5 s → 12 s
        const float size  = 0.75f + 0.85f * a;                    // taille de la pièce
        const float wet   = 0.6f * std::pow (a, 0.7f);
        const float damp  = 0.35f - 0.2f * a;                     // aigus plus doux sur les petites valeurs
        const float grainsPerSecond = 3.0f + 22.0f * a;
        const float grainFeed = 0.3f * a;                         // grains réinjectés dans la reverb
        const float grainOut  = 0.18f * a;                        // et un peu en direct

        // --- entrée : pré-délai + diffusion -------------------------------------
        preDelayL.push (inL + grainFeedL * grainFeed);
        preDelayR.push (inR + grainFeedR * grainFeed);
        const float pre = (0.012f + 0.03f * a) * sr;
        float xL = preDelayL.read (pre), xR = preDelayR.read (pre);
        for (int d = 0; d < numDiffusers; ++d)
        {
            xL = diffuserL[(size_t) d].process (xL);
            xR = diffuserR[(size_t) d].process (xR);
        }

        // --- FDN : lecture des 8 lignes (longueurs modulées) ----------------------
        float out[numLines];
        for (int l = 0; l < numLines; ++l)
        {
            float& ph = lfoPhase[(size_t) l];
            ph += twoPi * lfoRate[(size_t) l] / sr;
            if (ph > twoPi) ph -= twoPi;
            const float delay = baseDelay[(size_t) l] * size + 6.0f * std::sin (ph);
            out[l] = lines[(size_t) l].read (delay);
        }

        // Matrice de Hadamard 8×8 (orthogonale, normalisée) : mélange sans perte.
        float h[numLines];
        for (int l = 0; l < numLines; ++l) h[l] = out[l];
        for (int len = 1; len < numLines; len <<= 1)
            for (int s = 0; s < numLines; s += len << 1)
                for (int k = s; k < s + len; ++k)
                {
                    const float u = h[k], v = h[k + len];
                    h[k] = u + v; h[k + len] = u - v;
                }
        const float norm = 0.35355339f;                           // 1/√8

        // Normalisation d'énergie : une queue longue accumule ~1/(1−g²) ; on
        // compense à l'entrée pour que le niveau de la reverb reste constant
        // quelle que soit sa longueur (pas d'emballement sur un son tenu).
        const float meanLenSeconds = 66.0f * 0.001f * size;
        const float gMean = std::pow (10.0f, -3.0f * meanLenSeconds / t60);
        const float inGain = std::sqrt (juce::jmax (0.02f, 1.0f - gMean * gMean)) * 1.4f;

        // Gain de boucle par ligne pour le T60 voulu, amortissement des aigus.
        for (int l = 0; l < numLines; ++l)
        {
            const float lenSeconds = baseDelay[(size_t) l] * size / sr;
            const float g = juce::jmin (0.9995f, std::pow (10.0f, -3.0f * lenSeconds / t60));
            float y = h[l] * norm * g;
            float& lp = lowpassState[(size_t) l];
            lp += (y - lp) * (1.0f - damp);
            y = lp;
            const float in = (l & 1) ? xR : xL;
            lines[(size_t) l].push (y + in * inGain);
        }

        // Sorties stéréo : combinaisons de signes différentes (décorrélées).
        float revL = (out[0] - out[1] + out[2] - out[3] + out[4] - out[5] + out[6] - out[7]) * 0.25f;
        float revR = (out[0] + out[1] - out[2] - out[3] + out[4] + out[5] - out[6] - out[7]) * 0.25f;

        // --- nuage granulaire ---------------------------------------------------
        cloudL[(size_t) cloudWrite] = revL;
        cloudR[(size_t) cloudWrite] = revR;
        if (++cloudWrite >= cloudSize) cloudWrite = 0;

        grainClock += grainsPerSecond / sr;
        if (grainClock >= 1.0f)
        {
            grainClock -= 1.0f;
            for (auto& g : grains)
            {
                if (g.active) continue;
                const float r = nextRandom();
                g.rate = r < 0.55f ? 1.0f : (r < 0.85f ? 2.0f : 0.5f);   // unisson / octave ↑ / octave ↓
                g.length = (int) ((0.08f + 0.27f * nextRandom()) * sr);
                // départ entre 0.1 s et 1.6 s dans le passé (place pour lire à ×2)
                const float back = (0.1f + 1.5f * nextRandom()) * sr + (float) g.length * g.rate;
                g.readPos = (float) cloudWrite - juce::jmin (back, (float) cloudSize - 2.0f);
                while (g.readPos < 0.0f) g.readPos += (float) cloudSize;
                const float pan = nextRandom();
                g.gainL = std::cos (pan * 1.5707963f);
                g.gainR = std::sin (pan * 1.5707963f);
                g.age = 0;
                g.active = true;
                break;
            }
        }

        float gL = 0.0f, gR = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float t = (float) g.age / (float) g.length;
            const float env = 0.5f - 0.5f * std::cos (twoPi * t);     // Hann

            int i0 = (int) g.readPos;
            if (i0 >= cloudSize) { i0 -= cloudSize; g.readPos -= (float) cloudSize; }
            if (i0 < 0) { i0 = 0; g.readPos = 0.0f; }
            const int i1 = (i0 + 1) % cloudSize;
            const float fr = g.readPos - (float) i0;
            const float sL = cloudL[(size_t) i0] + fr * (cloudL[(size_t) i1] - cloudL[(size_t) i0]);
            const float sR = cloudR[(size_t) i0] + fr * (cloudR[(size_t) i1] - cloudR[(size_t) i0]);
            const float mono = 0.5f * (sL + sR);

            gL += mono * env * g.gainL;
            gR += mono * env * g.gainR;

            g.readPos += g.rate;
            while (g.readPos >= (float) cloudSize) g.readPos -= (float) cloudSize;
            if (++g.age >= g.length) g.active = false;
        }
        gL *= 0.6f; gR *= 0.6f;
        // Réinjection bornée (tanh) : le nuage ne peut pas s'emballer.
        grainFeedL = softClip (gL);
        grainFeedR = softClip (gR);

        const float oL = revL * wet + gL * grainOut;
        const float oR = revR * wet + gR * grainOut;

        L[i] = inL + (std::isfinite (oL) ? oL : 0.0f);
        if (R != nullptr) R[i] = inR + (std::isfinite (oR) ? oR : 0.0f);
    }
}
