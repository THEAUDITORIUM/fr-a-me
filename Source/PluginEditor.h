#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class SyncEditor : public juce::AudioProcessorEditor,
                   public juce::FileDragAndDropTarget,
                   public juce::DragAndDropContainer,
                   private juce::Timer
{
public:
    explicit SyncEditor (SyncProcessor&);
    ~SyncEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    enum class Hit
    {
        None, Name, Import, Export, Drag, A, B,
        Warp, Source, Half, Double, Mode, Target,
        Grid, Amount, Detect, Host, Preview,
        Strip, Wave, Marker
    };

    void timerCallback() override;
    Hit  hitTest (juce::Point<int> p, int& markerIndex) const;
    juce::String helpFor (Hit) const;
    void setStatus (const juce::String& s, double seconds = 3.0);

    void openImport();
    void openExport();
    void loadFile (const juce::File&);
    void startExternalDrag();
    void beginTextEdit (Hit which);
    void rebuildPeaks();

    double totalDst() const;
    float  xForDst (double dst) const;
    double dstForX (float x) const;
    double samplesPerBeatOut() const;

    juce::Font font (float size, bool medium = false) const;
    void text (juce::Graphics&, const juce::String&, juce::Rectangle<float> r, float size,
               juce::Colour c, bool medium = false, juce::Justification j = juce::Justification::centredLeft) const;
    void box (juce::Graphics&, juce::Rectangle<int> r, const juce::String& s, Hit h,
              bool on = false, bool enabled = true, juce::Justification j = juce::Justification::centred) const;
    void label (juce::Graphics&, juce::Rectangle<int> boxRect, const juce::String& s) const;

    SyncProcessor& proc;
    juce::Typeface::Ptr regular, medium;

    // layout
    juce::Rectangle<int> rTitle, rPanel, rName, rImport, rExport, rA, rB, rDrag,
                         rWarp, rSource, rHalf, rDouble, rMode, rTarget,
                         rGrid, rAmount, rDetect, rHost, rPreview,
                         rRuler, rStrip, rWave;

    // interaction
    Hit hover = Hit::None, pressed = Hit::None;
    int hoverMarker = -1, dragMarker = -1;
    juce::Point<int> downPos;
    double dragStartValue = 0.0;
    bool dragHover = false;
    bool externalDragStarted = false;

    // waveform cache (min / max per pixel column)
    std::vector<float> pkMin, pkMax;
    int peaksVersion = -1;
    double peaksTotal = 0.0;

    juce::String status;
    double statusUntil = 0.0;
    double lastPlayhead = -2.0;
    double lastHostBpm = 0.0;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TextEditor> textEdit;
    Hit textEditTarget = Hit::None;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncEditor)
};
