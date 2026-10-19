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
#include "TranceGate.h"
#include "PartialSnapper.h"
#include "KeyDetector.h"
#include "ProEq.h"
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
    inline constexpr const char* gateOn = "gateOn";        // GATE : marche / arrêt (arrêt au lancement)
    inline constexpr const char* gatePreset = "gatePreset"; // GATE : motif (15)
    inline constexpr const char* reverb = "reverb";
    inline constexpr const char* gain   = "gain";
    inline constexpr const char* mode = "mode";
    inline constexpr const char* note = "note";
    inline constexpr const char* eqLow  = "eqLow";    // EQ : coupe basse (Hz)
    inline constexpr const char* eqHigh = "eqHigh";   // EQ : coupe haute (Hz)
    inline constexpr const char* eqBellHz[3] { "eqB1f", "eqB2f", "eqB3f" };   // EQ : cloches (Hz)
    inline constexpr const char* eqBellDb[3] { "eqB1g", "eqB2g", "eqB3g" };   //      gain (dB)
    inline constexpr const char* eqBellQ [3] { "eqB1q", "eqB2q", "eqB3q" };   //      largeur (Q)
}

class FrameAudioProcessor final : public juce::AudioProcessor
{
public:
    FrameAudioProcessor();

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

    /** SCAN : détection de tonalité (lancée et lue par l'interface). */
    KeyDetector& getKeyDetector() noexcept { return keyDetector; }
    /** Lance un scan : pendant l'écoute, la sortie est le son d'origine seul. */
    void startScan();
    /** Arrête un scan sans rien appliquer : l'effet revient. */
    void cancelScan();
    /** Applique la tonalité trouvée puis rend l'effet (fondu). */
    void applyDetectedKey (int tonic, int mode);
    bool isScanHolding() const noexcept { return scanHold.load(); }
    /** GATE : pas joué (0 … 15), −1 si éteint. */
    int getGateStep() const noexcept { return gateStep.load(); }
    /** 3.2 : l'effet ne s'active qu'une fois une tonalité trouvée par un scan
        terminé. Avant : la sortie est le son d'origine, tel quel (0 latence).
        Sauvegardé avec le projet. */
    bool isKeyReady() const noexcept { return keyReady.load(); }
    void setKeyReady (bool);
    /** Page d'accueil (scan) déjà passée pour cette instance (sauvegardé avec le projet). */
    bool isIntroDone() const;
    void setIntroDone (bool);

    /** Réglages courants de l'EQ (lus aussi par l'interface pour la courbe). */
    ProEq::Settings getEqSettings() const noexcept;
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
    KeyDetector keyDetector;              // SCAN : tonalité de la mélodie jouée
    std::atomic<bool> scanHold { false }; // pendant un scan : sortie = son d'origine
    std::atomic<bool> keyReady { false }; // aucun scan terminé : sortie = son d'origine
    float holdSmoothed = 0.0f, holdIn = 0.01f, holdOut = 0.001f;
    PartialSnapper snapper;               // recalage des partiels tenus sur la gamme (0 latence)
    ZeroLatencyHarmonizer harmonizer;     // traitement harmonique, 0 latence
    TranceGate gate;                      // GATE : coupe le son sur les temps (16 pas, calé sur le DAW)
    std::atomic<float>* gateOnParam = nullptr;
    std::atomic<float>* gatePresetParam = nullptr;
    double blockPpq = 0.0, ppqPerSample = 0.0, freePpq = 0.0;   // position du DAW pour le bloc en cours
    std::atomic<int> gateStep { -1 };     // pas joué (interface), −1 si gate éteint
    ProEq proEq;                          // EQ façon Pro-Q (partie spectrum), juste avant le limiteur
    std::atomic<float>* eqLowParam = nullptr;
    std::atomic<float>* eqHighParam = nullptr;
    std::atomic<float>* eqBellHzParam[3] {};
    std::atomic<float>* eqBellDbParam[3] {};
    std::atomic<float>* eqBellQParam[3] {};
    juce::AudioBuffer<float> outBuffer;   // sortie avant EQ + limiteur

    std::atomic<float>* harmoParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* gainParam = nullptr;
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
