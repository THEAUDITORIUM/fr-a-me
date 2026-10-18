/*
  ==============================================================================
    fr(a)me — TonalPitchShifter.cpp
  ==============================================================================
*/

#include "TonalPitchShifter.h"
#include <cmath>
#include <cstdio>

namespace
{
   #ifndef TP_KMIN
    #define TP_KMIN 0.55f        // tonalité à partir de laquelle un partiel est transposé « proprement »
   #endif
   #ifndef TP_KSPAN
    #define TP_KSPAN 0.25f
   #endif
   #ifndef TP_SLOW
    #define TP_SLOW 0.006        // moyenne de la fréquence mesurée (s)
   #endif
   #ifndef TP_FAST
    #define TP_FAST 0.0015       // fréquence instantanée pour l'amplitude (vibrato suivi)
   #endif
   #ifndef TP_DUCK
    #define TP_DUCK 0.8f         // atténuation du reste quand le son est surtout tonal
   #endif
   #ifndef TP_KEEP
    #define TP_KEEP 0.005        // durée de l'attaque laissée intacte (s)
   #endif
   #ifndef TP_TRANS
    #define TP_TRANS 0.0f        // attaques : part laissée intacte (nette, à l'heure)
   #endif
   #ifndef TP_TREL
    #define TP_TREL 0.006f       // durée de l'attaque laissée intacte (s) : trop court pour qu'on y entende une hauteur
   #endif
   #ifndef TP_MINBW
    #define TP_MINBW 12.0        // écart minimal entre bandes (Hz) dans le grave
   #endif
   #ifndef TP_RESWIN
    #define TP_RESWIN 0.010      // fenêtre du décaleur du reste (s)
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

void TonalPitchShifter::prepare (double newSampleRate, int maxBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    slowCoef   = 1.0 - std::exp (-1.0 / (TP_SLOW * sampleRate));
    fastCoef   = 1.0 - std::exp (-1.0 / (TP_FAST * sampleRate));
    weightCoef = 1.0f - std::exp (-1.0f / (0.008f * (float) sampleRate));
    ratioCoef  = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));
    activeCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));
    envFastAtt  = 1.0f - std::exp (-1.0f / (0.0005f * (float) sampleRate));
    envFastRel  = 1.0f - std::exp (-1.0f / (0.008f  * (float) sampleRate));
    envSlowCoef = 1.0f - std::exp (-1.0f / (0.060f  * (float) sampleRate));
    transientRel = 1.0f - std::exp (-1.0f / (TP_TREL * (float) sampleRate));
    lag = juce::jlimit (16, historySize - 1, (int) std::lround (0.008 * sampleRate));
    duckCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
    keepLength = juce::jmax (16, (int) std::lround (TP_KEEP * sampleRate));
    history.assign ((size_t) (maxBands * historySize), {});
    const int blk = juce::jlimit (32, 8192, maxBlock);
    residual.setSize (2, blk, false, true, false);
    residualDry.setSize (2, blk, false, true, false);
    dryCopy.setSize (2, blk, false, true, false);
    transientCurve.setSize (1, blk, false, true, false);
    residualShifter.prepare (sampleRate);
    onsetFlags.assign ((size_t) blk, 0);
    buildBands();
    reset();
}

void TonalPitchShifter::buildBands() noexcept
{
    // Centres : tous les 12 Hz dans le grave (bandes assez larges pour réagir
    // vite), puis un par demi-ton jusqu'à ~16 kHz.
    double centres[maxBands + 2];
    int n = 0;
    double f = 40.0;
    const double top = juce::jmin (12000.0, 0.42 * sampleRate);   // au-dessus : le reste (décaleur à têtes) suffit
    while (f < top && n < maxBands + 2)
    {
        centres[n++] = f;
        const double semitone = f * (std::exp2 (1.0 / 12.0) - 1.0);
        f += juce::jmax ((double) TP_MINBW, semitone);
    }

    const double twoPi = juce::MathConstants<double>::twoPi;
    count = 0;
    for (int i = 1; i + 1 < n && count < maxBands; ++i)
    {
        auto& b = bands[(size_t) count++];
        b.hz = centres[i];
        b.w = twoPi * b.hz / sampleRate;
        const double loHz = 0.5 * (centres[i - 1] + centres[i]);
        const double hiHz = 0.5 * (centres[i] + centres[i + 1]);
        const double wLo = twoPi * loHz / sampleRate, wHi = twoPi * hiHz / sampleRate;
        // Quatre pôles identiques : −3 dB à Δω = 0.435·(1 − r) ; flancs raides
        // (une note voisine ne « fuit » presque pas dans la bande).
        const double oneMinusR = juce::jlimit (1.0e-5, 0.5, 0.5 * (wHi - wLo) / 0.4350);
        b.radius = 1.0 - oneMinusR;
        b.gain = oneMinusR;
        b.rot = std::polar (1.0, b.w);
        const cd pole = b.radius * b.rot;
        hot->pr[count - 1] = pole.real();
        hot->pi[count - 1] = pole.imag();
        hot->g[count - 1]  = b.gain;
        // La première et la dernière bande s'étendent jusqu'aux bords.
        b.lo = std::polar (1.0, i == 1 ? 0.0 : wLo);
        b.hi = std::polar (1.0, i + 2 == n ? juce::MathConstants<double>::pi * 0.99 : wHi);
    }
}

void TonalPitchShifter::reset() noexcept
{
    auto& H = *hot;
    for (int k = 0; k < maxBands; ++k)
    {
        for (int st = 0; st < 4; ++st) for (int c = 0; c < 2; ++c) H.sr[st][c][k] = H.si[st][c][k] = 0.0;
        H.zpr[k] = H.zpi[k] = H.dsr[k] = H.dsi[k] = H.dfr[k] = H.dfi[k] = 0.0;
        H.lcr[k] = H.lci[k] = H.le[k] = 0.0;
        H.cr[k] = 1.0; H.ci[k] = 0.0; H.str[k] = 1.0; H.sti[k] = 0.0;
        H.hr[k] = 1.0; H.hi[k] = 0.0; H.w[k] = H.wt[k] = 0.0;
    }
    std::fill (history.begin(), history.end(), std::complex<float> {});
    writePos = counter = 0;
    ratioSmoothed = 1.0;
    active = 0.0f;
    envFast = envSlow = transient = 0.0f;
    eTonal = eAll = 0.0f;
    keepCountdown = 0; armed = true;
    residualShifter.reset();
}

void TonalPitchShifter::process (juce::AudioBuffer<float>& buffer, int numChannels, float semitones) noexcept
{
    const int len = buffer.getNumSamples();
    const int nch = juce::jmin (numChannels, buffer.getNumChannels(), 2);
    if (len <= 0 || nch <= 0 || count == 0)
        return;

    semitones = juce::jlimit (-12.0f, 12.0f, std::isfinite (semitones) ? semitones : 0.0f);
    const bool wanted = std::abs (semitones) > 0.01f;
    const double targetRatio = std::exp2 ((double) semitones / 12.0);

    // Contourné et fondu terminé : rien à faire (son strictement inchangé).
    if (! wanted && active < 1.0e-4f)
    {
        if (active > 0.0f) reset();
        active = 0.0f;
        return;
    }

    float* data[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };
    const int chunk = juce::jmin (len, residual.getNumSamples());   // ≤ taille préparée

    for (int start = 0; start < len; start += chunk)
    {
        const int n = juce::jmin (chunk, len - start);
        float* res[2]  = { residual.getWritePointer (0), residual.getWritePointer (1) };
        float* keep[2] = { residualDry.getWritePointer (0), residualDry.getWritePointer (1) };
        float* dry[2]  = { dryCopy.getWritePointer (0), dryCopy.getWritePointer (1) };
        float* tc = transientCurve.getWritePointer (0);

        // Part tonale (transposée exactement) dans data, reste dans residual.
        for (int i = 0; i < n; ++i)
        {
            const int s = start + i;
            ratioSmoothed += (targetRatio - ratioSmoothed) * ratioCoef;
            const bool control = (counter++ & 15) == 0;

            double x[2] = { 0.0, 0.0 };
            for (int c = 0; c < nch; ++c)
            {
                x[c] = std::isfinite (data[c][s]) ? (double) data[c][s] : 0.0;
                dry[c][i] = (float) x[c];
            }

            double moved[2] = { 0.0, 0.0 }, removed[2] = { 0.0, 0.0 };
            auto& H = *hot;
            const int K = count;

            // 1. Résonateurs (4 pôles complexes en cascade), par canal — vectorisé.
            for (int c = 0; c < nch; ++c)
            {
                const double xc = x[c];
                double* __restrict s0r = H.sr[0][c]; double* __restrict s0i = H.si[0][c];
                for (int k = 0; k < K; ++k)
                {
                    const double ar = s0r[k], ai = s0i[k];
                    s0r[k] = H.pr[k] * ar - H.pi[k] * ai + H.g[k] * xc;
                    s0i[k] = H.pr[k] * ai + H.pi[k] * ar;
                }
                for (int st = 1; st < 4; ++st)
                {
                    const double* __restrict pr_ = H.sr[st - 1][c]; const double* __restrict pi_ = H.si[st - 1][c];
                    double* __restrict nr = H.sr[st][c]; double* __restrict ni = H.si[st][c];
                    for (int k = 0; k < K; ++k)
                    {
                        const double ar = nr[k], ai = ni[k];
                        nr[k] = H.pr[k] * ar - H.pi[k] * ai + H.g[k] * pr_[k];
                        ni[k] = H.pr[k] * ai + H.pi[k] * ar + H.g[k] * pi_[k];
                    }
                }
            }

            // 2. Fréquence instantanée (rotation de la sortie « mid »), phaseurs.
            const double* z3r0 = H.sr[3][0]; const double* z3i0 = H.si[3][0];
            const double* z3r1 = H.sr[3][nch > 1 ? 1 : 0]; const double* z3i1 = H.si[3][nch > 1 ? 1 : 0];
            for (int k = 0; k < K; ++k)
            {
                const double zr = 0.5 * (z3r0[k] + z3r1[k]), zi = 0.5 * (z3i0[k] + z3i1[k]);
                const double dr = zr * H.zpr[k] + zi * H.zpi[k], di = zi * H.zpr[k] - zr * H.zpi[k];
                H.zpr[k] = zr; H.zpi[k] = zi;
                H.dsr[k] += slowCoef * (dr - H.dsr[k]); H.dsi[k] += slowCoef * (di - H.dsi[k]);
                H.dfr[k] += fastCoef * (dr - H.dfr[k]); H.dfi[k] += fastCoef * (di - H.dfi[k]);
                const double cr = H.cr[k], ci = H.ci[k];
                H.cr[k] = cr * H.str[k] - ci * H.sti[k];
                H.ci[k] = cr * H.sti[k] + ci * H.str[k];
            }

            // 3. Tonalité : corrélation avec la sortie d'il y a ~8 ms.
            {
                const int rp = (writePos - lag) & (historySize - 1);
                for (int k = 0; k < K; ++k)
                {
                    auto* h = history.data() + (size_t) k * historySize;
                    const auto old = h[rp];
                    const double zr = H.zpr[k], zi = H.zpi[k];
                    h[writePos] = { (float) zr, (float) zi };
                    H.lcr[k] += slowCoef * (zr * old.real() + zi * old.imag() - H.lcr[k]);
                    H.lci[k] += slowCoef * (zi * old.real() - zr * old.imag() - H.lci[k]);
                    H.le[k]  += slowCoef * (zr * zr + zi * zi - H.le[k]);
                }
            }

            // 4. Contrôle (tous les 16 échantillons) : fréquence, appartenance, poids.
            if (control)
            {
                for (int k = 0; k < K; ++k)
                {
                    const auto& b = bands[(size_t) k];
                    const cd dS (H.dsr[k], H.dsi[k]);
                    const double ns = std::abs (dS);
                    const cd dHat = ns > 1.0e-30 ? dS / ns : cd (1.0, 0.0);
                    const double omega = std::arg (dHat);                       // fréquence mesurée (rad/éch.)
                    const cd stp = std::polar (1.0, (ratioSmoothed - 1.0) * omega);   // partiel → ratio × fréquence
                    H.str[k] = stp.real(); H.sti[k] = stp.imag();
                    const double cm = std::sqrt (H.cr[k] * H.cr[k] + H.ci[k] * H.ci[k]);
                    if (cm > 1.0e-12 && std::isfinite (cm)) { H.cr[k] /= cm; H.ci[k] /= cm; } else { H.cr[k] = 1.0; H.ci[k] = 0.0; }

                    const cd dF (H.dfr[k], H.dfi[k]);
                    const double nf = std::abs (dF);
                    const cd dFastHat = nf > 1.0e-30 ? dF / nf : dHat;
                    const cd u = b.rot * std::conj (dFastHat);                  // e^{j(W − ω)}, ω instantanée
                    cd hv = (1.0 - b.radius * u) / b.gain;
                    hv *= hv; hv *= hv;                                         // 1/H = ((1 − r·u) / g)⁴
                    const double mag = std::abs (hv);
                    if (mag > 6.0) hv *= 6.0 / mag;
                    H.hr[k] = hv.real(); H.hi[k] = hv.imag();

                    const bool owned = ns > 1.0e-30 && cross (b.lo, dHat) >= 0.0 && cross (dHat, b.hi) >= 0.0;
                    const float coherence = H.le[k] > 1.0e-30 ? (float) (std::sqrt (H.lcr[k] * H.lcr[k] + H.lci[k] * H.lci[k]) / H.le[k]) : 0.0f;
                    double neighbour = 0.0;
                    if (k > 0)     neighbour = juce::jmax (neighbour, H.le[k - 1]);
                    if (k + 1 < K) neighbour = juce::jmax (neighbour, H.le[k + 1]);
                    const float dominance = neighbour > 1.0e-30 ? smoothStep ((float) ((H.le[k] / neighbour - 0.25) / 0.35)) : 1.0f;
                    const bool fits = (ratioSmoothed * omega) < juce::MathConstants<double>::pi * 0.9;   // reste sous Nyquist
                    H.wt[k] = (owned && fits) ? (double) (dominance * smoothStep ((coherence - TP_KMIN) / TP_KSPAN)) : 0.0;
                    if (H.w[k] < 1.0e-3 && H.wt[k] <= 0.0) { H.cr[k] = 1.0; H.ci[k] = 0.0; }
                }
            }

            // 5. Partiels retirés du son et remis à leur nouvelle hauteur.
            for (int k = 0; k < K; ++k)
            {
                H.w[k] += (double) weightCoef * (H.wt[k] - H.w[k]);
                const double wk = H.w[k];
                if (wk <= 1.0e-4) continue;
                for (int c = 0; c < nch; ++c)
                {
                    const double sr = H.sr[3][c][k], si = H.si[3][c][k];
                    const double pr2 = 2.0 * (sr * H.hr[k] - si * H.hi[k]), pi2 = 2.0 * (sr * H.hi[k] + si * H.hr[k]);   // partiel estimé
                    removed[c] += wk * pr2;
                    moved[c]   += wk * (pr2 * H.cr[k] - pi2 * H.ci[k]);                         // Re(P · c)
                }
            }
            writePos = (writePos + 1) & (historySize - 1);

            for (int c = 0; c < nch; ++c)
            {
                res[c][i] = (float) (x[c] - removed[c]);
                data[c][s] = (float) moved[c];
            }
        }

        // Reste : détection d'attaques sur le reste lui-même, transposition
        // classique, attaques laissées nettes (et à l'heure).
        for (int i = 0; i < n; ++i)
        {
            // Attaque = le reste (non tonal) bondit nettement au-dessus du niveau
            // récent du son entier (et pas les petites fluctuations du reste).
            float a = std::abs (res[0][i]), d = std::abs (dry[0][i]);
            if (nch > 1) { a = juce::jmax (a, std::abs (res[1][i])); d = juce::jmax (d, std::abs (dry[1][i])); }
            envFast += (a - envFast) * (a > envFast ? envFastAtt : envFastRel);
            envSlow += (d - envSlow) * envSlowCoef;
            const float t = envSlow > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (envFast / envSlow - 1.5f) / 1.5f) : (envFast > 1.0e-4f ? 1.0f : 0.0f);
            transient = t > transient ? t : transient + (t - transient) * transientRel;
            // Attaque : ~5 ms laissées intactes (le clic arrive pile à l'heure),
            // trop court pour qu'on y entende la hauteur d'origine ; ensuite le
            // reste transposé prend le relais.
            if (armed && transient > 0.5f) { keepCountdown = keepLength; armed = false; }
            if (! armed && transient < 0.15f) armed = true;
            float k = 0.0f;
            if (keepCountdown > 0)
            {
                const int fade = keepLength / 4;
                k = keepCountdown > fade ? 1.0f : (float) keepCountdown / (float) fade;
                --keepCountdown;
            }
            tc[i] = k * TP_TRANS;
            onsetFlags[(size_t) i] = (keepCountdown == keepLength - 1) ? 1 : 0;   // départ d'un grain d'attaque
            for (int c = 0; c < nch; ++c) keep[c][i] = res[c][i];
        }
        residualShifter.process (res, nch, n, ratioSmoothed, onsetFlags.data());

        for (int i = 0; i < n; ++i)
        {
            const int s = start + i;
            active += ((wanted ? 1.0f : 0.0f) - active) * activeCoef;
            {
                const float m = data[0][s], d0 = dry[0][i];
                eTonal += (m * m - eTonal) * duckCoef;
                eAll   += (d0 * d0 - eAll) * duckCoef;
            }
            const float tonalFrac = eAll > 1.0e-12f ? juce::jlimit (0.0f, 1.0f, eTonal / eAll) : 0.0f;
            const float duck = 1.0f - TP_DUCK * smoothStep ((tonalFrac - 0.4f) / 0.5f);
            for (int c = 0; c < nch; ++c)
            {
               #if defined (TP_DBG_TONAL)
                const float r = 0.0f;
               #elif defined (TP_DBG_RESID)
                const float r = keep[c][i] * tc[i] + res[c][i] * (1.0f - tc[i]); data[c][s] = 0.0f;
               #elif defined (TP_DBG_RAWRES)
                const float r = keep[c][i];
               #else
                // Son très tonal : ce qui reste n'est surtout que l'erreur
                // d'estimation des partiels → atténué (sinon il bat avec eux).
                const float r = keep[c][i] * tc[i] + res[c][i] * (1.0f - tc[i]) * duck;
               #endif
                const float shifted = data[c][s] + r;
                const float y = dry[c][i] + active * (shifted - dry[c][i]);
                data[c][s] = std::isfinite (y) ? y : 0.0f;
               #ifdef TP_DBG_T
                if (c == 1) data[c][s] = tc[i];
                if (c == 0) data[c][s] = keep[0][i];
               #endif
            }
        }
    }

    // Sécurité : état invalide → vidé.
    for (int k = 0; k < count; ++k)
        if (! std::isfinite (hot->sr[3][0][k]) || ! std::isfinite (hot->dsr[k]))
        {
            reset();
            break;
        }
}
