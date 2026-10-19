/*
  ==============================================================================
    fr(a)me — PartialSnapper.cpp
  ==============================================================================
*/

#include "PartialSnapper.h"
#include "Scale.h"
#include <cmath>
#include <cstdio>
#ifdef PS_DEBUG
static int debugCounter = 0;
#endif

namespace
{
   #ifndef PS_BW
    #define PS_BW 1.0            // largeur des bandes (1 = −3 dB aux bornes de la zone)
   #endif
   #ifndef PS_SLOW
    #define PS_SLOW 0.006        // moyenne de la fréquence mesurée (s) : suit le vibrato, recale tout
   #endif
   #ifndef PS_FAST
    #define PS_FAST 0.006        // fenêtre de cohérence (s)
   #endif
   #ifndef PS_LOWHZ
    #define PS_LOWHZ 50.0        // le recalage commence ici (plein au double)
   #endif
   #ifndef PS_KMIN
    #define PS_KMIN 0.45f        // tonalité en dessous de laquelle rien n'est recalé (bruit, attaques)
   #endif
   #ifndef PS_LAG
    #define PS_LAG 0.008         // écart de la mesure de tonalité (s) : un partiel reste cohérent, un bruit non
   #endif
   #ifndef PS_KSPAN
    #define PS_KSPAN 0.2f
   #endif

    inline float smoothStep (float x) noexcept
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        return x * x * (3.0f - 2.0f * x);
    }
    inline double cross (std::complex<double> a, std::complex<double> b) noexcept
    {
        return a.real() * b.imag() - a.imag() * b.real();
    }
}

void PartialSnapper::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    fastCoef = 1.0 - std::exp (-1.0 / (PS_FAST * sampleRate));
    slowCoef = 1.0 - std::exp (-1.0 / (PS_SLOW * sampleRate));
    weightCoef = 1.0f - std::exp (-1.0f / (0.008f * (float) sampleRate));
    amountCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));
    crossfadeStep = 1.0f / (0.06f * (float) sampleRate);
    lag = juce::jlimit (16, historySize - 1, (int) std::lround (PS_LAG * sampleRate));
    for (auto& bank : banks)
        bank.history.assign ((size_t) (maxBands * historySize), {});

    const uint16_t m = mask;
    hasBank = false;
    mask = 0;
    setScale (m);
    reset();
}

void PartialSnapper::reset() noexcept
{
    for (auto& bank : banks)
        for (int i = 0; i < bank.count; ++i)
        {
            auto& b = bank.bands[(size_t) i];
            b.s1[0] = b.s1[1] = b.s2[0] = b.s2[1] = {};
            b.zPrev = {}; b.dFast = {}; b.dSlow = {}; b.mFast = 0.0;
            b.lagCorr = {}; b.lagEnergy = 0.0;
            b.corr = { 1.0, 0.0 }; b.hInv = { 1.0, 0.0 }; b.step = { 1.0, 0.0 };
            b.weight = b.weightTarget = 0.0f;
        }
    for (auto& bank : banks)
        std::fill (bank.history.begin(), bank.history.end(), std::complex<float> {});
    writePos = 0;
    crossfade = 1.0f;
    amountSmoothed = 0.0f;
    counter = 0;
}

void PartialSnapper::setScale (uint16_t newMask) noexcept
{
    if (hasBank && newMask == mask)
        return;
    mask = newMask;
    if (! hasBank)
    {
        buildBank (banks[current], mask);
        banks[1 - current].count = 0;
        hasBank = true;
        crossfade = 1.0f;
        return;
    }
    current = 1 - current;
    buildBank (banks[current], mask);
    crossfade = 0.0f;
}

void PartialSnapper::buildBank (Bank& bank, uint16_t m) noexcept
{
    bank.count = 0;
    if ((m & 0x0FFF) == 0)
        return;

    int notes[maxBands + 30];
    int n = 0;
    for (int midi = lowNote - 12; midi <= highNote + 12; ++midi)
        if (frame::maskContains (m, midi))
            notes[n++] = midi;

    const double twoPi = juce::MathConstants<double>::twoPi;
    const double nyq = 0.45 * sampleRate;

    for (int i = 1; i + 1 < n; ++i)
    {
        const int note = notes[i];
        if (note < lowNote || note > highNote) continue;
        const double hz = frame::midiToHz (note);
        const double loHz = frame::midiToHz (0.5 * (notes[i - 1] + note));
        const double hiHz = frame::midiToHz (0.5 * (notes[i + 1] + note));
        if (hiHz > nyq) break;

        auto& b = bank.bands[(size_t) bank.count++];
        b.hz = hz;
        b.w = twoPi * hz / sampleRate;
        const double wLo = twoPi * loHz / sampleRate, wHi = twoPi * hiHz / sampleRate;
        const double halfWidth = 0.5 * (wHi - wLo);
        // Deux pôles identiques : −3 dB à Δω = 0.644·(1 − r) ⇒ (1 − r) = demi-largeur / 0.644.
        const double oneMinusR = juce::jlimit (1.0e-5, 0.5, PS_BW * halfWidth / 0.6436);
        b.radius = 1.0 - oneMinusR;
        b.gain = oneMinusR;
        b.rot = std::polar (1.0, b.w);
        b.deadZone = std::sin (b.w * (std::exp2 (6.0 / 1200.0) - 1.0));
        b.pole = b.radius * b.rot;
        // Zones élargies de 30 % de part et d'autre : un partiel posé pile entre
        // deux notes (son accordé un quart de ton à côté) reste recalé ; la
        // bande la plus forte des deux le prend (voir « partagé »).
        b.lo = std::polar (1.0, wLo - 0.3 * (b.w - wLo));
        b.hi = std::polar (1.0, wHi + 0.3 * (wHi - b.w));
        b.estW = b.w;
        b.s1[0] = b.s1[1] = b.s2[0] = b.s2[1] = {};
        b.zPrev = {}; b.dFast = {}; b.dSlow = {}; b.mFast = 0.0;
        b.lagCorr = {}; b.lagEnergy = 0.0;
        b.corr = { 1.0, 0.0 }; b.hInv = { 1.0, 0.0 }; b.step = { 1.0, 0.0 };
        b.weight = b.weightTarget = 0.0f;
        if (! bank.history.empty())
            std::fill (bank.history.begin() + (bank.count - 1) * historySize, bank.history.begin() + bank.count * historySize, std::complex<float> {});

        // Progressif dans le grave (110 → 200 Hz), allégé au-dessus de 5 kHz.
        const float f = (float) hz;
        b.freqWeight = smoothStep ((f - (float) PS_LOWHZ) / (float) PS_LOWHZ) * (1.0f - 0.8f * smoothStep ((f - 5000.0f) / 4000.0f));
    }
}

void PartialSnapper::runBank (Bank& bank, const double* in, int nch, double* delta, bool updateControl, float amount) noexcept
{
    for (int i = 0; i < bank.count; ++i)
    {
        auto& b = bank.bands[(size_t) i];

        // 1. Résonateurs complexes (signal analytique de la bande), par canal.
        //    (arithmétique complexe écrite à la main : plus rapide que std::complex)
        const double pr = b.pole.real(), pi = b.pole.imag(), g = b.gain;
        for (int c = 0; c < nch; ++c)
        {
            const double a1r = b.s1[c].real(), a1i = b.s1[c].imag();
            const double n1r = pr * a1r - pi * a1i + g * in[c];
            const double n1i = pr * a1i + pi * a1r;
            const double a2r = b.s2[c].real(), a2i = b.s2[c].imag();
            b.s1[c] = { n1r, n1i };
            b.s2[c] = { pr * a2r - pi * a2i + g * n1r, pr * a2i + pi * a2r + g * n1i };
        }
        const cd z = nch > 1 ? 0.5 * (b.s2[0] + b.s2[1]) : b.s2[0];

        // 2. Rotation d'un échantillon à l'autre = fréquence instantanée.
        //    (|d| ≈ |z|² : la cohérence se calcule sans racine carrée)
        const double zr = z.real(), zi = z.imag(), qr = b.zPrev.real(), qi = b.zPrev.imag();
        const double dr = zr * qr + zi * qi, di = zi * qr - zr * qi;     // z · conj(zPrev)
        b.zPrev = z;
        b.dFast = { b.dFast.real() + fastCoef * (dr - b.dFast.real()), b.dFast.imag() + fastCoef * (di - b.dFast.imag()) };
        b.dSlow = { b.dSlow.real() + slowCoef * (dr - b.dSlow.real()), b.dSlow.imag() + slowCoef * (di - b.dSlow.imag()) };
        b.mFast += fastCoef * (zr * zr + zi * zi - b.mFast);

        // Tonalité : corrélation avec la sortie d'il y a L (~8 ms). Un partiel
        // (même avec vibrato) reste corrélé ; un bruit de bande perd sa phase.
        if (! bank.history.empty())
        {
            auto* h = bank.history.data() + (size_t) i * historySize;
            const auto old = h[(writePos - lag) & (historySize - 1)];
            h[writePos] = { (float) zr, (float) zi };
            const double orr = old.real(), oi = old.imag();
            b.lagCorr = { b.lagCorr.real() + slowCoef * (zr * orr + zi * oi - b.lagCorr.real()),
                          b.lagCorr.imag() + slowCoef * (zi * orr - zr * oi - b.lagCorr.imag()) };
            b.lagEnergy += slowCoef * (zr * zr + zi * zi - b.lagEnergy);
        }

        // 3. Correction : tourner de (note − partiel) à chaque échantillon
        //    (incrément u recalculé tous les 16 échantillons, la mesure étant lente).
        {
            const double cr = b.corr.real(), ci = b.corr.imag(), sr = b.step.real(), si = b.step.imag();
            b.corr = { cr * sr - ci * si, cr * si + ci * sr };
        }

        if (updateControl)
        {
            const double ns = std::abs (b.dSlow);
            b.step = ns > 1.0e-30 ? b.rot * std::conj (b.dSlow) / ns : cd (1.0, 0.0);
            // Zone morte : un partiel à moins de ~6 cents de la note est laissé tel
            // quel (pas de micro-rotation due aux fuites des bandes voisines).
            if (std::abs (b.step.imag()) < b.deadZone)
                b.step = { 1.0, 0.0 };
            const cd u = b.step;

            b.corr /= std::abs (b.corr);
            if (! std::isfinite (b.corr.real())) b.corr = { 1.0, 0.0 };

            // Réponse du filtre à la fréquence mesurée : H = (g / (1 − r·u))².
            cd hi = (1.0 - b.radius * u) / b.gain;
            hi *= hi;
            const double mag = std::abs (hi);
            b.hInv = mag > 4.0 ? hi * (4.0 / mag) : hi;

            // La bande « possède »-t-elle ce partiel ? (fréquence dans sa zone)
            const cd dHat = ns > 1.0e-30 ? b.dSlow / ns : cd (1.0, 0.0);
            b.estW = std::arg (dHat);
            bool owned = ns > 1.0e-30 && cross (b.lo, dHat) >= 0.0 && cross (dHat, b.hi) >= 0.0;
            // Partagé : si la bande voisine mesure le même partiel (±10 cents) et
            // le reçoit plus fort, c'est elle qui le recale (jamais deux fois).
            for (int j : { i - 1, i + 1 })
                if (owned && j >= 0 && j < bank.count)
                {
                    const auto& nb = bank.bands[(size_t) j];
                    if (std::abs (nb.estW - b.estW) < 0.006 * b.estW && nb.lagEnergy > b.lagEnergy)
                        owned = false;
                }
            const float coherence = b.lagEnergy > 1.0e-30 ? (float) (std::abs (b.lagCorr) / b.lagEnergy) : 0.0f;
            // Dominance : une bande qui ne reçoit que les fuites de ses voisines
            // (plus fortes qu'elle) ne recale rien — sinon un mélange de deux
            // notes justes voisines serait déplacé à tort.
            double neighbour = 0.0;
            if (i > 0)              neighbour = juce::jmax (neighbour, bank.bands[(size_t) i - 1].lagEnergy);
            if (i + 1 < bank.count) neighbour = juce::jmax (neighbour, bank.bands[(size_t) i + 1].lagEnergy);
            const float dominance = neighbour > 1.0e-30 ? smoothStep ((float) ((b.lagEnergy / neighbour - 0.25) / 0.35)) : 1.0f;
            b.weightTarget = owned ? amount * b.freqWeight * dominance * smoothStep ((coherence - PS_KMIN) / PS_KSPAN) : 0.0f;

            // Inactive : le phaseur repart de 1 (reprise sans saut de phase).
            if (b.weight < 1.0e-3f && b.weightTarget <= 0.0f)
                b.corr = { 1.0, 0.0 };
        }

        b.weight += weightCoef * (b.weightTarget - b.weight);
       #ifdef PS_DEBUG
        if (updateControl && b.weight > 0.05f && (debugCounter++ % 97) == 0)
            std::printf ("%.0f Hz band: w %.3f tgt %.3f coh %.3f est %.1f Hz corr %.2f,%.2f\n", b.hz, b.weight, b.weightTarget, b.lagEnergy > 0 ? std::abs (b.lagCorr) / b.lagEnergy : 0.0, std::arg (b.dSlow) * sampleRate / juce::MathConstants<double>::twoPi,
                         b.corr.real(), b.corr.imag());
       #endif

        // 4. Remplacement du partiel par sa version recalée.
        if (b.weight > 1.0e-4f)
        {
            const double kr = b.corr.real() - 1.0, ki = b.corr.imag();
            const double hr = b.hInv.real(), hi = b.hInv.imag();
            for (int c = 0; c < nch; ++c)
            {
                const double sr = b.s2[c].real(), si = b.s2[c].imag();
                // P = 2·z / H : le résonateur complexe ne garde que la moitié
                // « fréquences positives » d'un signal réel (A·cos → A/2·e^{jωt}).
                const double pr2 = 2.0 * (sr * hr - si * hi), pi2 = 2.0 * (sr * hi + si * hr);
                delta[c] += (double) b.weight * (pr2 * kr - pi2 * ki);           // Re(P · (c − 1))
            }
        }
    }
}

void PartialSnapper::process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept
{
    const int len = buffer.getNumSamples();
    const int nch = juce::jmin (numChannels, buffer.getNumChannels(), 2);
    if (len <= 0 || nch <= 0)
        return;

    amount = juce::jlimit (0.0f, 1.0f, std::isfinite (amount) ? amount : 0.0f);
   #ifdef PS_DISABLE
    amount = 0.0f; return;
   #endif
    float* data[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < len; ++i)
    {
        amountSmoothed += (amount - amountSmoothed) * amountCoef;
        const bool control = (counter++ & 15) == 0;

        double in[2] = { 0.0, 0.0 };
        for (int c = 0; c < nch; ++c)
            in[c] = std::isfinite (data[c][i]) ? (double) data[c][i] : 0.0;

        double dNew[2] = { 0.0, 0.0 }, dOld[2] = { 0.0, 0.0 };
        runBank (banks[current], in, nch, dNew, control, amountSmoothed);
        if (crossfade < 1.0f)
        {
            runBank (banks[1 - current], in, nch, dOld, control, amountSmoothed);
            crossfade = juce::jmin (1.0f, crossfade + crossfadeStep);
        }
        const double xf = (double) crossfade;
        writePos = (writePos + 1) & (historySize - 1);

        for (int c = 0; c < nch; ++c)
        {
            const double y = in[c] + dNew[c] * xf + dOld[c] * (1.0 - xf);
            data[c][i] = std::isfinite (y) ? (float) y : 0.0f;
        }
    }

    // Sécurité : état invalide → vidé.
    for (auto& bank : banks)
        for (int i = 0; i < bank.count; ++i)
        {
            auto& b = bank.bands[(size_t) i];
            if (! std::isfinite (b.s2[0].real()) || ! std::isfinite (b.s2[1].real()) || ! std::isfinite (b.dSlow.real()))
            {
                b.s1[0] = b.s1[1] = b.s2[0] = b.s2[1] = {};
                b.zPrev = {}; b.dFast = {}; b.dSlow = {}; b.mFast = 0.0;
                b.corr = { 1.0, 0.0 }; b.weight = 0.0f;
            }
        }
}
