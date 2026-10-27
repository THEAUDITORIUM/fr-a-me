#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// ============================================================================
//  SYNC — warp engine
//  Transient detection, tempo estimation, grid quantisation and rendering.
//  All positions are expressed in samples at the file's native sample rate.
// ============================================================================
namespace warp
{

struct Onset
{
    double pos = 0.0;       // sample position of the attack in the source
    float strength = 0.0f;  // 0..1 relative onset strength
};

struct Marker
{
    double src = 0.0;       // position in the source sample
    double dst = 0.0;       // position in the warped (output) timeline
};

enum class Mode { Beats, Flow };

struct WarpSettings
{
    double sourceBpm = 120.0;
    double targetBpm = 120.0;
    int    gridDiv   = 16;      // 4 = 1/4, 8 = 1/8, 16 = 1/16, 32 = 1/32
    float  amount    = 1.0f;    // quantise strength 0..1
};

// Onset-detection function, computed once per sample and reused when the
// sensitivity changes or the tempo is estimated.
struct OnsetFunction
{
    std::vector<float> odf;     // one value per hop, normalised 0..1
    std::vector<float> mono;    // mono mix-down of the source
    std::vector<float> hf;      // first difference (attack emphasis) for refinement
    int    offset = 0;          // sample position of frame 0 (negative: zero-padded start)
    int    hop = 256;
    int    fftSize = 1024;
    double sampleRate = 44100.0;
};

OnsetFunction computeOnsetFunction (const juce::AudioBuffer<float>& source, double sampleRate);

// sensitivity 0 (only strong hits) .. 1 (everything)
std::vector<Onset> pickOnsets (const OnsetFunction& f, float sensitivity);

double estimateTempo (const OnsetFunction& f, int numSamples);

// Markers that leave the sample untouched (start, every onset, end).
std::vector<Marker> identityMarkers (const std::vector<Onset>& onsets, int numSamples);

// Tempo-stretch + quantise the onsets to the target grid.
std::vector<Marker> computeWarp (const std::vector<Onset>& onsets, int numSamples,
                                 double sampleRate, const WarpSettings& s);

// Renders the warped audio: every segment between two markers is time-stretched
// to its new length. Beats = short grains (drums), Flow/Tones = long grains.
void render (const juce::AudioBuffer<float>& source, double sampleRate,
             const std::vector<Marker>& markers, Mode mode,
             juce::AudioBuffer<float>& out);

double dstToSrc (const std::vector<Marker>& markers, double dst);
double srcToDst (const std::vector<Marker>& markers, double src);

} // namespace warp
