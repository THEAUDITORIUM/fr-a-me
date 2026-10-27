#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "WarpEngine.h"

class SyncProcessor : public juce::AudioProcessor,
                      private juce::Timer
{
public:
    SyncProcessor();
    ~SyncProcessor() override;

    // ---- AudioProcessor ---------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SYNC"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- SYNC API (message thread) ----------------------------------------
    bool   loadFile (const juce::File& file, juce::String& error);
    void   clear();
    bool   hasSample() const { return source != nullptr; }

    void   warp();                         // tempo-stretch + quantise to grid
    void   setMarkerDst (int index, double dst);
    void   addMarkerAtDst (double dst);
    void   removeMarker (int index);

    void   setGridDiv (int div);
    void   setAmount (float a);
    void   setSourceBpm (double bpm);
    void   setTargetBpm (double bpm);
    void   setHostSync (bool on);
    void   setMode (warp::Mode m);
    void   setSensitivity (float s);

    bool   exportTo (const juce::File& file, juce::String& error);
    juce::File renderTempFileForDrag();

    double effectiveTargetBpm() const;
    void   pollHostTempo();                // auto re-warp when FL's tempo changes

    // ---- read-only state for the editor ------------------------------------
    const std::vector<warp::Marker>& getMarkers() const { return markers; }
    const warp::WarpSettings& getSettings() const { return settings; }
    std::shared_ptr<const juce::AudioBuffer<float>> getRendered() const;
    std::shared_ptr<const juce::AudioBuffer<float>> getSource() const { return source; }
    double  getFileSampleRate() const { return fileSampleRate; }
    juce::File getFile() const { return sourceFile; }
    bool    isWarped() const { return warped; }
    bool    isHostSync() const { return hostSync; }
    warp::Mode getMode() const { return mode; }
    float   getSensitivity() const { return sensitivity; }
    double  getHostBpm() const { return hostBpm.load(); }
    bool    isHostPlaying() const { return hostPlaying.load(); }
    int     getRenderVersion() const { return renderVersion.load(); }
    double  getPlayheadDst() const { return playhead.load(); } // < 0 when stopped
    bool    isRendering() const { return renderBusy.load(); }

    std::atomic<bool> preview { false };
    std::atomic<bool> listenB { true };    // A = original, B = warped
    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override { pollHostTempo(); }
    void redetect();
    void requestRender();
    void applyWarpSettingsChange();

    juce::AudioFormatManager formats;

    std::shared_ptr<const juce::AudioBuffer<float>> source;
    double fileSampleRate = 44100.0;
    juce::File sourceFile;

    warp::OnsetFunction onsetFn;
    std::vector<warp::Onset> onsets;
    std::vector<warp::Marker> markers;
    warp::WarpSettings settings;
    warp::Mode mode = warp::Mode::Beats;
    float sensitivity = 0.55f;
    bool  hostSync = true;
    bool  warped = false;
    double lastAutoWarpHostBpm = 0.0;

    // rendered audio, swapped in from the render thread
    std::shared_ptr<const juce::AudioBuffer<float>> rendered;
    juce::SpinLock renderLock;
    std::atomic<int>  renderVersion { 0 };
    std::atomic<int>  renderGeneration { 0 };
    std::atomic<bool> renderBusy { false };
    juce::ThreadPool  pool { 1 };

    // playback
    double hostSampleRate = 44100.0;
    double previewPos = 0.0;
    bool   wasPreviewing = false;
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool>   hostPlaying { false };
    std::atomic<double> playhead { -1.0 };
    std::atomic<double> renderedBpm { 120.0 };
    std::atomic<double> sourceBpmAtomic { 120.0 };
    std::atomic<float>* levelParam = nullptr;
    float smoothedGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncProcessor)
};
