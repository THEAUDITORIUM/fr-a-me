// Offline test: builds a humanised drum loop, warps it, checks the result.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/WarpEngine.h"
#include <random>
#include <cstdio>

static void addKick (juce::AudioBuffer<float>& b, int at, double sr, float amp)
{
    double ph = 0.0;
    for (int i = 0; i < (int) (0.9 * sr) && at + i < b.getNumSamples(); ++i)
    {
        const double t = i / sr;
        const double f = 45.0 + 110.0 * std::exp (-t * 30.0);
        ph += 2.0 * juce::MathConstants<double>::pi * f / sr;
        const float v = (float) (std::sin (ph) * std::exp (-t * 9.0)) * amp;
        for (int ch = 0; ch < b.getNumChannels(); ++ch) b.addSample (ch, at + i, v);
    }
}

static void addNoiseHit (juce::AudioBuffer<float>& b, int at, double sr, float amp, double decay, std::mt19937& rng)
{
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    float lp = 0.0f;
    for (int i = 0; i < (int) (0.25 * sr) && at + i < b.getNumSamples(); ++i)
    {
        const float n = u (rng);
        lp = lp * 0.3f + n * 0.7f;
        const float v = (n - lp) * (float) std::exp (-(i / sr) * decay) * amp;
        for (int ch = 0; ch < b.getNumChannels(); ++ch) b.addSample (ch, at + i, v);
    }
}

static void writeWav (const juce::AudioBuffer<float>& b, double sr, const juce::File& f)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (f.createOutputStream().release(), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static int toneTest (const juce::File& outDir)
{
    const double sr = 48000.0;
    const int len = (int) (4.0 * sr);
    juce::AudioBuffer<float> b (2, len);
    for (int i = 0; i < len; ++i)
    {
        const double t = i / sr;
        const float v = (float) (0.25 * std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t)
                               + 0.20 * std::sin (2 * juce::MathConstants<double>::pi * 277.18 * t)
                               + 0.18 * std::sin (2 * juce::MathConstants<double>::pi * 329.63 * t + 0.4 * std::sin (6.0 * t)));
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    float inJump = 0.0f;
    for (int i = 1; i < len; ++i) inJump = std::max (inJump, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));

    for (double ratio : { 0.75, 1.3 })
    {
        std::vector<warp::Marker> m { { 0.0, 0.0 }, { len * 0.37, len * 0.37 * ratio }, { (double) len, len * ratio } };
        juce::AudioBuffer<float> out;
        warp::render (b, sr, m, warp::Mode::Flow, out);
        float outJump = 0.0f, rmsIn = b.getRMSLevel (0, 0, len), rmsOut = out.getRMSLevel (0, 0, out.getNumSamples());
        for (int i = 1; i < out.getNumSamples() - 200; ++i) outJump = std::max (outJump, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        std::printf ("[Tone x%.2f] len %.2fs  max step in %.4f out %.4f  rms in %.3f out %.3f\n", ratio, out.getNumSamples() / sr, inJump, outJump, rmsIn, rmsOut);
        writeWav (out, sr, outDir.getChildFile ("test_tone_flow_x" + juce::String (ratio) + ".wav"));
    }
    return 0;
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[2]) == "tone") return toneTest (juce::File (argv[1]));
    const double sr = 44100.0;
    const double bpm = argc > 2 ? juce::String (argv[2]).getDoubleValue() : 97.0;
    const double beat = 60.0 / bpm * sr;
    const int bars = argc > 3 ? juce::String (argv[3]).getIntValue() : 2;
    const int len = (int) std::round (beat * 4 * bars);
    juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();

    std::mt19937 rng (7);
    std::normal_distribution<double> jitter (0.0, 0.012 * sr); // ~12 ms of sloppy timing

    juce::AudioBuffer<float> loop (2, len);
    loop.clear();
    std::vector<double> truth;

    for (int e = 0; e < bars * 8; ++e)  // 8th notes
    {
        const double ideal = e * beat * 0.5;
        const int at = juce::jmax (0, (int) std::round (ideal + (e == 0 ? 0.0 : jitter (rng))));
        truth.push_back (at);
        const int inBar = e % 8;
        if (inBar == 0 || inBar == 3 || inBar == 5) addKick (loop, at, sr, 0.8f);
        if (inBar == 2 || inBar == 6) addNoiseHit (loop, at, sr, 0.6f, 14.0, rng);
        addNoiseHit (loop, at, sr, inBar % 2 ? 0.18f : 0.25f, 60.0, rng);
    }
    writeWav (loop, sr, outDir.getChildFile ("test_input_97bpm.wav"));

    // ---- analysis ------------------------------------------------------------
    auto fn = warp::computeOnsetFunction (loop, sr);
    auto onsets = warp::pickOnsets (fn, 0.55f);
    const double est = warp::estimateTempo (fn, len);
    std::printf ("Tempo: truth %.2f  estimated %.2f\n", bpm, est);
    std::printf ("Onsets: truth %zu  detected %zu\n", truth.size(), onsets.size());

    int matched = 0; double errSum = 0.0, errMax = 0.0;
    for (double t : truth)
    {
        double best = 1e9;
        for (auto& o : onsets) best = std::min (best, std::abs (o.pos - t));
        if (best < 0.02 * sr) { ++matched; errSum += best; errMax = std::max (errMax, best); }
    }
    for (size_t ti = 0; ti < truth.size(); ++ti)
    {
        double best = 1e9;
        for (auto& o : onsets) best = std::min (best, std::abs (o.pos - truth[ti]));
        if (best >= 0.02 * sr) std::printf ("  missed #%zu (8th %zu in bar) nearest %.1f ms\n", ti, ti % 8, best / sr * 1000.0);
    }
    for (auto& o : onsets)
    {
        double best = 1e9; size_t bi = 0;
        for (size_t ti = 0; ti < truth.size(); ++ti) if (std::abs (o.pos - truth[ti]) < best) { best = std::abs (o.pos - truth[ti]); bi = ti; }
        if (best >= 0.02 * sr) std::printf ("  false onset at %.1f ms (%.1f ms after truth #%zu, 8th %zu) strength %.3f\n", o.pos / sr * 1000.0, (o.pos - truth[bi]) / sr * 1000.0, bi, bi % 8, o.strength);
    }
    for (float sens : { 0.25f, 0.55f, 0.85f })
        std::printf ("  sens %.2f -> %zu onsets\n", sens, warp::pickOnsets (fn, sens).size());
    std::printf ("Matched %d/%zu  mean err %.2f ms  max err %.2f ms\n", matched, truth.size(),
                 errSum / juce::jmax (1, matched) / sr * 1000.0, errMax / sr * 1000.0);

    // ---- warp to 128 bpm, 1/8 grid --------------------------------------------
    warp::WarpSettings s;
    s.sourceBpm = est; s.targetBpm = 128.0; s.gridDiv = 8; s.amount = 1.0f;
    auto markers = warp::computeWarp (onsets, len, sr, s);
    const double outBeat = 60.0 / 128.0 * sr;
    std::printf ("Markers: %zu  output length %.3f beats\n", markers.size(), markers.back().dst / outBeat);

    for (auto mode : { warp::Mode::Beats, warp::Mode::Flow })
    {
        juce::AudioBuffer<float> out;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        warp::render (loop, sr, markers, mode, out);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

        auto fo = warp::computeOnsetFunction (out, sr);
        auto oo = warp::pickOnsets (fo, 0.55f);
        double maxDev = 0.0, sumDev = 0.0;
        for (auto& o : oo)
        {
            const double grid = outBeat * 0.5;
            const double dev = std::abs (o.pos - std::round (o.pos / grid) * grid);
            maxDev = std::max (maxDev, dev); sumDev += dev;
        }
        float peak = out.getMagnitude (0, out.getNumSamples());
        std::printf ("[%s] render %.1f ms  len %.3f beats  onsets %zu  grid dev mean %.2f ms max %.2f ms  peak %.2f\n",
                     mode == warp::Mode::Beats ? "Beats" : "Flow ", ms, out.getNumSamples() / outBeat, oo.size(),
                     sumDev / juce::jmax<size_t> (1, oo.size()) / sr * 1000.0, maxDev / sr * 1000.0, peak);
        writeWav (out, sr, outDir.getChildFile (mode == warp::Mode::Beats ? "test_out_beats_128.wav" : "test_out_flow_128.wav"));
    }
    return 0;
}

