/*
  ==============================================================================
    fr(a)me — PluginProcessor.h
    Processeur audio : paramètres (APVTS), latence, mélange dry/wet, échange
    des données de visualisation avec l'interface.
  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "SpectralEngine.h"
#include "LockFreeQueue.h"

/** Un "pixel" des bandes de forme d'onde : min / max sur un petit paquet
    d'échantillons, pour l'entrée et la sortie. */
struct WaveSlice
{
    float inMin = 0.0f, inMax = 0.0f, outMin = 0.0f, outMax = 0.0f;
};

namespace ParamIDs
{
    inline constexpr const char* mix   = "mix";
    inline constexpr const char* color = "color";
    inline constexpr const char* mode = "mode";
    inline constexpr const char* note = "note";
}

class FrameAudioProcessor final : public juce::AudioProcessor
{
public:
    FrameAudioProcessor();
    ~FrameAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using AudioProcessor::processBlock;          // la version double reste celle de JUCE
    using AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "fr(a)me"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return SpectralEngine::resonanceT60Seconds + 0.5; }   // queue de résonance

    int getNumPrograms() override    { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    /** Files lues par l'éditeur (thread UI). */
    LockFreeQueue<WaveSlice, 4096>& getWaveQueue() noexcept { return waveQueue; }
    LockFreeQueue<SpectralFrameInfo, 32>& getSpectralQueue() noexcept { return engine.getVisualQueue(); }

    /** Masque de la gamme courante (lu par l'UI pour le surlignage). */
    uint16_t getCurrentScaleMask() const noexcept;
    int getLatencyInSamples() const noexcept { return engine.getLatencySamples(); }
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }


private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void pushDryThroughDelay (const float* const* input, int numChannels, int numSamples) noexcept;
    void accumulateWave (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out,
                         int numChannels, int numSamples) noexcept;

    SpectralEngine engine;

    std::atomic<float>* mixParam  = nullptr;
    std::atomic<float>* colorParam = nullptr;
    float colourSmoothed = 1.0f;          // COLOR lissé de bloc en bloc
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* noteParam = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmoothed;

    // Tampons pré-alloués (prepareToPlay) : aucun malloc dans processBlock.
    juce::AudioBuffer<float> dryBuffer, wetBuffer;
    juce::AudioBuffer<float> dryDelay;     // ligne à retard du signal sec (= latence)
    int dryDelayWritePos = 0;
    int maxChunk = 512;

    // Accumulation des tranches de forme d'onde.
    WaveSlice currentSlice;
    int sliceCount = 0;
    int samplesPerWaveSlice = 512;         // = hop / 2 : deux tranches par trame STFT

    LockFreeQueue<WaveSlice, 4096> waveQueue;
    std::atomic<double> currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameAudioProcessor)
};
