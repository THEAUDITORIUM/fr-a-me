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
#include "AmbientReverb.h"
#include "ZeroLatencyHarmonizer.h"
#include "RetuneVocoder.h"
#include "PitchShifter.h"
#include "LockFreeQueue.h"

/** Un "pixel" des bandes de forme d'onde : min / max sur un petit paquet
    d'échantillons, pour l'entrée et la sortie. */
struct WaveSlice
{
    float inMin = 0.0f, inMax = 0.0f, outMin = 0.0f, outMax = 0.0f;
};

namespace ParamIDs
{
    inline constexpr const char* harmo = "harmo";   // ancien « mix »
    inline constexpr const char* pitch = "pitch";
    inline constexpr const char* reverb = "reverb";
    inline constexpr const char* gain   = "gain";
    inline constexpr const char* retune = "retune";
    inline constexpr const char* mode = "mode";
    inline constexpr const char* note = "note";
}

class FrameAudioProcessor final : public juce::AudioProcessor
{
public:
    FrameAudioProcessor();

    /** Thème de l'interface (« light », « dark », « garden »), sauvegardé avec le projet. */
    juce::String getTheme() const;
    void setTheme (const juce::String& theme);
    ~FrameAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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
    double getTailLengthSeconds() const override { return AmbientReverb::maxTailSeconds; }   // queue de la reverb

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
    int getLatencyInSamples() const noexcept { return 0; }                       // aucun retard
    int getAnalysisFftSize() const noexcept  { return engine.getFftSize(); }     // pour l'axe temporel
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

    /** Vu-mètres (dBFS crête, lissés) et état du garde-fou, pour l'interface. */
    float getInputLevelDb() const noexcept  { return inputLevelDb.load(); }
    float getOutputLevelDb() const noexcept { return outputLevelDb.load(); }
    bool  isDirectRestoreActive() const noexcept { return restoreActive.load(); }


private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void pushDryThroughDelay (const float* const* input, int numChannels, int numSamples) noexcept;
    void accumulateWave (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out,
                         int numChannels, int numSamples) noexcept;

    SpectralEngine engine;                // analyse pour l'affichage (ne touche pas au son)
    AmbientReverb reverb;                 // avant l'harmonie : la queue est harmonisée elle aussi
    ZeroLatencyHarmonizer harmonizer;     // traitement harmonique, 0 latence
    RetuneVocoder retuner;                // RETUNE (case à cocher) : vocodeur sur les notes de la gamme, 0 latence
    PitchShifter pitcher;                 // PITCH −12…+12 (elastic), en fin de chaîne

    std::atomic<float>* harmoParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* retuneParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
    float reverbSmoothed = 0.3f;          // REVERB lissé de bloc en bloc
    std::atomic<bool> prepared { false }; // prepareToPlay a été appelé
    bool engineNeedsReset = false;        // auto-réparation après une valeur invalide

    // Garde-fou « tout le son passe » : énergies lissées du son d'origine
    // (retardé) et du son traité ; si le traité tombe loin sous l'original,
    // le son direct est réinjecté dans le chemin traité.
    float guardDry = 0.0f, guardWet = 0.0f, directRestore = 0.0f;
    float guardCoef = 0.001f, restoreAttack = 0.1f, restoreRelease = 0.01f;
    uint32_t guardCounter = 0;

    float meterIn = 0.0f, meterOut = 0.0f, meterCoef = 0.001f;

    // Limiteur de sortie (gain lissé, sans écrêtage dur) : évite le grésillement
    // quand les renforts harmoniques poussent le niveau vers 0 dBFS.
    float limiterGain = 1.0f, limiterRelease = 0.001f;
    std::atomic<float> inputLevelDb { -100.0f }, outputLevelDb { -100.0f };
    std::atomic<bool> restoreActive { false };
    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* noteParam = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> harmoSmoothed;
    juce::AudioBuffer<float> preHarmoBuffer;     // signal avant l'harmonie (pour le dosage HARMO)

    // Tampons pré-alloués (prepareToPlay) : aucun malloc dans processBlock.
    juce::AudioBuffer<float> dryBuffer, wetBuffer;
    juce::AudioBuffer<float> analysisBuffer;   // copie pour l'analyse spectrale (affichage)
    int maxChunk = 512;

    // Accumulation des tranches de forme d'onde.
    WaveSlice currentSlice;
    int sliceCount = 0;
    int samplesPerWaveSlice = 512;         // = hop / 2 : deux tranches par trame STFT

    LockFreeQueue<WaveSlice, 4096> waveQueue;
    std::atomic<double> currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FrameAudioProcessor)
};
