#include "WarpEngine.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace warp
{

// ---------------------------------------------------------------------------
//  Onset detection function: log-compressed spectral flux
// ---------------------------------------------------------------------------
OnsetFunction computeOnsetFunction (const juce::AudioBuffer<float>& source, double sampleRate)
{
    OnsetFunction f;
    f.sampleRate = sampleRate;

    const int n  = source.getNumSamples();
    const int nc = juce::jmax (1, source.getNumChannels());

    f.mono.assign ((size_t) n, 0.0f);
    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        auto* d = source.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
            f.mono[(size_t) i] += d[i] / (float) nc;
    }

    // keep ~5.8 ms resolution whatever the sample rate
    const int order = sampleRate > 60000.0 ? 11 : 10;
    f.fftSize = 1 << order;
    f.hop     = f.fftSize / 4;

    f.hf.assign ((size_t) n, 0.0f);
    for (int i = 1; i < n; ++i) f.hf[(size_t) i] = f.mono[(size_t) i] - f.mono[(size_t) i - 1];

    // zero-pad the start so a hit on the very first sample is still detected
    f.offset = -f.fftSize;
    if (n < f.hop * 4)
        return f;

    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window ((size_t) f.fftSize, juce::dsp::WindowingFunction<float>::hann, false);

    const int bins = f.fftSize / 2;
    std::vector<float> frame ((size_t) f.fftSize * 2), prev ((size_t) bins, 0.0f);

    const int numFrames = 1 + (n - f.offset - f.fftSize / 2) / f.hop;
    f.odf.assign ((size_t) numFrames, 0.0f);

    for (int fr = 0; fr < numFrames; ++fr)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        const int start = fr * f.hop + f.offset;
        for (int i = 0; i < f.fftSize; ++i)
        {
            const int si = start + i;
            if (si >= 0 && si < n) frame[(size_t) i] = f.mono[(size_t) si];
        }
        window.multiplyWithWindowingTable (frame.data(), (size_t) f.fftSize);
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);

        float flux = 0.0f;
        for (int b = 1; b < bins; ++b)
        {
            const float mag = std::log1p (100.0f * frame[(size_t) b]);
            const float diff = mag - prev[(size_t) b];
            if (diff > 0.0f) flux += diff;
            prev[(size_t) b] = mag;
        }
        f.odf[(size_t) fr] = flux;
    }

    if (! f.odf.empty()) f.odf[0] = 0.0f; // first frame has no reference

    // remove slow trend, half-wave rectify, normalise
    const int w = 24;
    std::vector<float> out (f.odf.size());
    double acc = 0.0;
    std::vector<double> prefix (f.odf.size() + 1, 0.0);
    for (size_t i = 0; i < f.odf.size(); ++i) prefix[i + 1] = prefix[i] + f.odf[i];

    for (int i = 0; i < (int) f.odf.size(); ++i)
    {
        const int a = juce::jmax (0, i - w), b = juce::jmin ((int) f.odf.size(), i + w + 1);
        acc = (prefix[(size_t) b] - prefix[(size_t) a]) / (double) (b - a);
        out[(size_t) i] = juce::jmax (0.0f, f.odf[(size_t) i] - (float) acc * 0.5f);
    }

    const float mx = out.empty() ? 0.0f : *std::max_element (out.begin(), out.end());
    if (mx > 0.0f)
        for (auto& v : out) v /= mx;

    f.odf = std::move (out);
    return f;
}

// ---------------------------------------------------------------------------
//  Peak picking + sample-accurate refinement
// ---------------------------------------------------------------------------
static double refineOnset (const OnsetFunction& f, double roughPos)
{
    const auto& hf = f.hf;
    const double sr = f.sampleRate;
    const int n = (int) hf.size();
    const int block = 48;
    const int from = juce::jlimit (0, n - 1, (int) roughPos - (int) (0.022 * sr));
    const int to   = juce::jlimit (0, n - 1, (int) roughPos + (int) (0.022 * sr));
    const int nb = (to - from) / block;
    if (nb < 4) return juce::jlimit (0.0, (double) n, roughPos);

    std::vector<float> e ((size_t) nb, 0.0f);
    for (int k = 0; k < nb; ++k)
    {
        float s = 0.0f;
        for (int i = 0; i < block; ++i) { const float x = hf[(size_t) (from + k * block + i)]; s += x * x; }
        e[(size_t) k] = std::sqrt (s / block);
    }

    // block with the largest rise over the recent past (high-passed, so a
    // decaying kick tail does not fool it)
    int best = 1; float bestRise = -1.0f;
    for (int k = 1; k < nb; ++k)
    {
        float past = 0.0f;
        for (int j = juce::jmax (0, k - 3); j < k; ++j) past = juce::jmax (past, e[(size_t) j]);
        const float rise = e[(size_t) k] - past;
        if (rise > bestRise) { bestRise = rise; best = k; }
    }

    // walk back sample by sample to where the attack actually starts
    float past = 0.0f;
    for (int j = juce::jmax (0, best - 3); j < best; ++j) past = juce::jmax (past, e[(size_t) j]);
    const int blockStart = from + best * block;
    float pk = 0.0f;
    for (int i = blockStart; i < juce::jmin (n, blockStart + block); ++i) pk = juce::jmax (pk, std::abs (hf[(size_t) i]));
    const float thresh = past * 1.5f + (pk - past * 1.5f) * 0.2f;

    int start = blockStart + block - 1;
    for (int i = blockStart; i < juce::jmin (n, blockStart + block); ++i)
        if (std::abs (hf[(size_t) i]) > thresh) { start = i; break; }

    const int preRoll = (int) (0.0007 * sr); // keep the very first edge of the hit
    return (double) juce::jmax (0, start - preRoll);
}

std::vector<Onset> pickOnsets (const OnsetFunction& f, float sensitivity)
{
    std::vector<Onset> result;
    const int nf = (int) f.odf.size();
    if (nf < 3) return result;

    sensitivity = juce::jlimit (0.0f, 1.0f, sensitivity);
    const float delta  = juce::jmap (sensitivity, 0.30f, 0.025f);
    const int   w      = 12;
    const int   local  = 3;
    const double minGapSamples = 0.045 * f.sampleRate;

    double lastPos = -1e9;

    for (int i = 1; i < nf - 1; ++i)
    {
        const float v = f.odf[(size_t) i];
        if (v <= delta * 0.8f) continue;

        bool isMax = true;
        for (int k = juce::jmax (0, i - local); k <= juce::jmin (nf - 1, i + local); ++k)
            if (k != i && f.odf[(size_t) k] > v) { isMax = false; break; }
        if (! isMax) continue;

        // adaptive threshold: local median (robust to neighbouring hits)
        float win[2 * 12 + 1]; int cnt = 0;
        for (int k = juce::jmax (0, i - w); k <= juce::jmin (nf - 1, i + w); ++k) win[cnt++] = f.odf[(size_t) k];
        std::nth_element (win, win + cnt / 2, win + cnt);
        if (v < win[cnt / 2] * 1.2f + delta) continue;

        const double rough = (double) i * f.hop + f.offset + f.fftSize * 0.75;
        const double pos = refineOnset (f, rough);

        if (pos - lastPos < minGapSamples)
        {
            if (! result.empty() && v > result.back().strength)
                result.back() = { pos, v };   // keep the stronger of two close hits
            continue;
        }

        result.push_back ({ pos, v });
        lastPos = pos;
    }

    return result;
}

// ---------------------------------------------------------------------------
//  Tempo estimation: ODF autocorrelation, biased toward loop-length tempos
// ---------------------------------------------------------------------------
double estimateTempo (const OnsetFunction& f, int numSamples)
{
    const int nf = (int) f.odf.size();
    const double framesPerSec = f.sampleRate / f.hop;
    if (nf < 16) return 120.0;

    auto acf = [&] (double lag)
    {
        const int l0 = (int) lag; const double fr = lag - l0;
        double s = 0.0;
        for (int i = 0; i + l0 + 1 < nf; ++i)
        {
            const double b = f.odf[(size_t) (i + l0)] * (1.0 - fr) + f.odf[(size_t) (i + l0 + 1)] * fr;
            s += f.odf[(size_t) i] * b;
        }
        return s / (double) juce::jmax (1, nf - l0);
    };

    auto weight = [] (double bpm)
    {
        const double x = std::log2 (bpm / 120.0) / 0.9;
        return std::exp (-0.5 * x * x);
    };

    double bestBpm = 120.0, bestScore = -1.0;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.25)
    {
        const double lag = framesPerSec * 60.0 / bpm;
        if (lag * 2.0 >= nf) continue;
        double s = acf (lag) + 0.5 * acf (lag * 2.0);
        if (lag * 0.5 > 1.0) s += 0.25 * acf (lag * 0.5);
        s *= weight (bpm);
        if (s > bestScore) { bestScore = s; bestBpm = bpm; }
    }

    // Loops are usually cut to a whole number of bars: prefer such a tempo
    const double lenSec = numSamples / f.sampleRate;
    double chosen = bestBpm, chosenDist = 1e9;
    for (int beats : { 4, 8, 12, 16, 24, 32, 48, 64, 128 })
    {
        const double bpm = 60.0 * beats / lenSec;
        if (bpm < 65.0 || bpm > 190.0) continue;
        const double lag = framesPerSec * 60.0 / bpm;
        double s = acf (lag);
        if (lag * 2.0 < nf) s += 0.5 * acf (lag * 2.0);
        if (lag * 0.5 > 1.0) s += 0.25 * acf (lag * 0.5);
        s *= weight (bpm);
        const double dist = std::abs (std::log2 (bpm / bestBpm));
        if (s >= 0.6 * bestScore && dist < chosenDist) { chosen = bpm; chosenDist = dist; }
    }

    if (std::abs (chosen - std::round (chosen)) < 0.15) chosen = std::round (chosen);
    return std::round (chosen * 100.0) / 100.0;
}

// ---------------------------------------------------------------------------
//  Markers
// ---------------------------------------------------------------------------
std::vector<Marker> identityMarkers (const std::vector<Onset>& onsets, int numSamples)
{
    std::vector<Marker> m;
    m.push_back ({ 0.0, 0.0 });
    for (auto& o : onsets)
        if (o.pos > 1.0 && o.pos < numSamples - 1.0)
            m.push_back ({ o.pos, o.pos });
    m.push_back ({ (double) numSamples, (double) numSamples });
    return m;
}

std::vector<Marker> computeWarp (const std::vector<Onset>& onsets, int numSamples,
                                 double sr, const WarpSettings& s)
{
    std::vector<Marker> m;
    const double srcBeat = 60.0 / s.sourceBpm * sr;
    const double spb     = 60.0 / s.targetBpm * sr;       // output samples per beat
    const double k       = s.sourceBpm / s.targetBpm;      // dst length per src sample
    const double grid    = spb * 4.0 / (double) s.gridDiv;
    const double minGap  = juce::jmax (0.012 * sr, grid * 0.45 * s.amount);

    // anchor the downbeat on the first hit if it sits right at the start
    double anchor = 0.0;
    if (! onsets.empty() && onsets.front().pos < srcBeat * 0.125)
        anchor = onsets.front().pos;

    struct Cand { double src, dst; float strength; };
    std::vector<Cand> c;
    c.push_back ({ anchor, 0.0, 1e9f });

    for (auto& o : onsets)
    {
        if (o.pos <= anchor + 0.005 * sr || o.pos >= numSamples - 0.005 * sr) continue;

        const double ideal = (o.pos - anchor) * k;
        const double snap  = std::round (ideal / grid) * grid;
        const double dst   = ideal + (double) s.amount * (snap - ideal);

        if (dst <= c.back().dst + minGap)
        {
            // two hits want the same slot: keep the stronger one
            if (c.size() > 1 && o.strength > c.back().strength
                && dst > c[c.size() - 2].dst + minGap)
                c.back() = { o.pos, dst, o.strength };
            continue;
        }
        c.push_back ({ o.pos, dst, o.strength });
    }

    // end of the sample lands on a whole beat
    const double idealEnd = ((double) numSamples - anchor) * k;
    double beats = juce::jmax (1.0, std::round (idealEnd / spb));
    while (beats * spb <= c.back().dst + minGap) beats += 1.0;

    for (auto& x : c) m.push_back ({ x.src, x.dst });
    m.push_back ({ (double) numSamples, beats * spb });
    return m;
}

double dstToSrc (const std::vector<Marker>& m, double dst)
{
    if (m.size() < 2) return dst;
    size_t i = 0;
    while (i + 2 < m.size() && dst >= m[i + 1].dst) ++i;
    const auto& a = m[i]; const auto& b = m[i + 1];
    const double span = b.dst - a.dst;
    if (span <= 0.0) return a.src;
    return a.src + (dst - a.dst) * (b.src - a.src) / span;
}

double srcToDst (const std::vector<Marker>& m, double src)
{
    if (m.size() < 2) return src;
    size_t i = 0;
    while (i + 2 < m.size() && src >= m[i + 1].src) ++i;
    const auto& a = m[i]; const auto& b = m[i + 1];
    const double span = b.src - a.src;
    if (span <= 0.0) return a.dst;
    return a.dst + (src - a.src) * (b.dst - a.dst) / span;
}

// ---------------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------------
static float raisedCos (float t) { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * juce::jlimit (0.0f, 1.0f, t)); }

// Segment renderer: Beats mode — copy the slice as-is, fade where it is cut.
static void renderSliceBeats (const juce::AudioBuffer<float>& src, double sr,
                              int a, int b, int c, int lenDst, juce::AudioBuffer<float>& out)
{
    const int lenSrc = b - a;
    const int len = juce::jmin (lenSrc, lenDst);
    if (len <= 0) return;

    const bool truncated = lenSrc > lenDst;
    const int fade = truncated ? juce::jmin ((int) (0.010 * sr), len / 2)
                               : juce::jmin ((int) (0.003 * sr), len / 3);

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        auto* s = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
        auto* d = out.getWritePointer (ch);
        for (int i = 0; i < len; ++i)
        {
            const int di = c + i, si = a + i;
            if (di < 0 || di >= out.getNumSamples() || si >= src.getNumSamples()) continue;
            float g = 1.0f;
            if (i >= len - fade) g = 1.0f - raisedCos ((float) (i - (len - fade)) / (float) fade);
            d[di] = s[si] * g;
        }
    }
}

// Segment renderer: Flow mode — WSOLA (waveform-similarity overlap-add).
static void renderSliceFlow (const juce::AudioBuffer<float>& src, const std::vector<float>& mono, double sr,
                             int a, int b, int c, int lenDst, juce::AudioBuffer<float>& out)
{
    const int lenSrc = b - a;
    if (lenSrc <= 0 || lenDst <= 0) return;
    const double alpha = (double) lenDst / (double) lenSrc;
    const int n = src.getNumSamples();

    if (std::abs (alpha - 1.0) < 0.002)
    {
        renderSliceBeats (src, sr, a, b, c, lenDst, out);
        return;
    }

    const int N   = sr > 60000.0 ? 4096 : 2048;
    const int Hs  = N / 2;
    const int tol = N / 4;
    const double Ha = (double) Hs / alpha;

    std::vector<float> win ((size_t) N), win0 ((size_t) N);
    for (int i = 0; i < N; ++i)
    {
        win[(size_t) i]  = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
        win0[(size_t) i] = i < N / 2 ? 1.0f : win[(size_t) i]; // flat start keeps the transient intact
    }

    const int chans = out.getNumChannels();
    juce::AudioBuffer<float> acc (chans, lenDst + N);
    acc.clear();
    std::vector<float> wsum ((size_t) (lenDst + N), 0.0f);

    auto sampleAt = [&] (const float* p, int idx) { return (idx >= 0 && idx < n) ? p[idx] : 0.0f; };

    int prevPos = a;
    for (int j = 0; j * Hs < lenDst; ++j)
    {
        int pos;
        const int o0 = j * Hs;
        // never read the next hit into this slice (it would double the attack)
        const int maxPos = juce::jmax (a, b - juce::jmin (N, lenDst - o0) - 32);
        const int nominal = juce::jmin (maxPos, a + (int) std::lround (j * Ha));
        if (j == 0) pos = a;
        else
        {
            const int natural = prevPos + Hs;
            auto corr = [&] (int cand)
            {
                double s = 0.0;
                for (int i = 0; i < N; i += 8)
                {
                    const int x = natural + i, y = cand + i;
                    if (x >= n || y >= n || y < 0) break;
                    s += (double) mono[(size_t) x] * mono[(size_t) y];
                }
                return s;
            };

            int best = nominal; double bestS = -1e30;
            for (int kk = -tol; kk <= tol; kk += 8)
            {
                if (nominal + kk > maxPos) break;
                const double sc = corr (nominal + kk);
                if (sc > bestS) { bestS = sc; best = nominal + kk; }
            }
            const int coarse = best;
            for (int kk = -7; kk <= 7; ++kk)
            {
                if (coarse + kk > maxPos) break;
                const double sc = corr (coarse + kk);
                if (sc > bestS) { bestS = sc; best = coarse + kk; }
            }
            pos = juce::jlimit (0, juce::jmax (0, maxPos), best);
        }

        const auto& w = (j == 0) ? win0 : win;
        const int o = o0;
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* s = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
            auto* d = acc.getWritePointer (ch);
            for (int i = 0; i < N; ++i)
                d[o + i] += sampleAt (s, pos + i) * w[(size_t) i];
        }
        for (int i = 0; i < N; ++i) wsum[(size_t) (o + i)] += w[(size_t) i];
        prevPos = pos;
    }

    for (int ch = 0; ch < chans; ++ch)
    {
        auto* d = out.getWritePointer (ch);
        auto* s = acc.getReadPointer (ch);
        for (int i = 0; i < lenDst; ++i)
        {
            const int di = c + i;
            if (di < 0 || di >= out.getNumSamples()) continue;
            d[di] = s[i] / juce::jmax (wsum[(size_t) i], 1.0e-3f);
        }
    }
}

void render (const juce::AudioBuffer<float>& source, double sr,
             const std::vector<Marker>& m, Mode mode, juce::AudioBuffer<float>& out)
{
    const int chans = juce::jmax (1, source.getNumChannels());
    const int total = m.size() >= 2 ? (int) std::ceil (m.back().dst) : source.getNumSamples();
    out.setSize (chans, juce::jmax (1, total), false, true, false);
    out.clear();

    if (m.size() < 2 || source.getNumSamples() == 0) return;

    std::vector<float> mono;
    if (mode == Mode::Flow)
    {
        mono.assign ((size_t) source.getNumSamples(), 0.0f);
        for (int ch = 0; ch < source.getNumChannels(); ++ch)
        {
            auto* d = source.getReadPointer (ch);
            for (int i = 0; i < source.getNumSamples(); ++i) mono[(size_t) i] += d[i];
        }
    }

    const int preRoll = juce::jmax (16, (int) (0.0025 * sr));

    for (size_t i = 0; i + 1 < m.size(); ++i)
    {
        const int a = (int) std::lround (m[i].src);
        const int b = (int) std::lround (m[i + 1].src);
        const int c = (int) std::lround (m[i].dst);
        const int d = (int) std::lround (m[i + 1].dst);
        if (b <= a || d <= c) continue;

        if (mode == Mode::Beats) renderSliceBeats (source, sr, a, b, c, d - c, out);
        else                     renderSliceFlow  (source, mono, sr, a, b, c, d - c, out);

        // short crossfade just before each slice so the next transient is
        // never softened (the fade happens on the pre-attack material)
        if (i > 0)
        {
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* s = source.getReadPointer (juce::jmin (ch, source.getNumChannels() - 1));
                auto* o = out.getWritePointer (ch);
                for (int k = 0; k < preRoll; ++k)
                {
                    const int di = c - preRoll + k, si = a - preRoll + k;
                    if (di < 0 || si < 0 || di >= out.getNumSamples()) continue;
                    const float g = raisedCos ((float) k / (float) preRoll);
                    o[di] = o[di] * (1.0f - g) + s[si] * g;
                }
            }
        }
    }

    // tiny fade at the very end so loops never click
    const int endFade = juce::jmin (out.getNumSamples(), (int) (0.002 * sr));
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        auto* o = out.getWritePointer (ch);
        for (int k = 0; k < endFade; ++k)
            o[out.getNumSamples() - 1 - k] *= (float) k / (float) endFade;
    }
}

} // namespace warp
