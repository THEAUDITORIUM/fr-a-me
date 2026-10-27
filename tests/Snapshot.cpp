// Renders the editor to PNG (empty state + loaded/warped state) for visual QA.
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

static void shoot (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argv[1]);
    SyncProcessor p;
    p.prepareToPlay (44100.0, 512);
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        shoot (*ed, out.getChildFile ("ui_empty.png"));
    }
    juce::String err;
    if (! p.loadFile (juce::File (argv[2]), err)) { std::printf ("load failed: %s\n", err.toRawUTF8()); return 1; }
    while (p.isRendering()) juce::Thread::sleep (5);
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        juce::Thread::sleep (50);
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        shoot (*ed, out.getChildFile ("ui_loaded.png"));
    }
    p.setHostSync (false);
    p.setTargetBpm (128.0);
    p.setGridDiv (8);
    p.warp();
    juce::Thread::sleep (30);
    while (p.isRendering()) juce::Thread::sleep (5);
    p.preview = true;
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    for (int i = 0; i < 260; ++i) p.processBlock (buf, midi);
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        shoot (*ed, out.getChildFile ("ui_warped.png"));
    }
    // zoom with the mouse wheel + drag a marker (live stretch)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto wheelAt = [&] (juce::Point<float> pos, float dy)
        {
            juce::MouseEvent me (src, pos, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, ed.get(), ed.get(),
                                 juce::Time::getCurrentTime(), pos, juce::Time::getCurrentTime(), 0, false);
            juce::MouseWheelDetails w { 0.0f, dy, false, false, false };
            ed->mouseWheelMove (me, w);
        };
        for (int i = 0; i < 4; ++i) wheelAt ({ 500.0f, 140.0f }, 0.125f);   // 4 notches in, around x=500
        // drag marker 4 by half a beat to the right — the audio around it must stretch
        const double spb = 60.0 / p.getSettings().targetBpm * p.getFileSampleRate();
        p.setMarkerDst (4, p.getMarkers()[4].dst + spb * 0.3);
        juce::Thread::sleep (30);
        while (p.isRendering()) juce::Thread::sleep (5);
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        shoot (*ed, out.getChildFile ("ui_zoomed.png"));
    }

    // state round-trip (what FL Studio saves in the project)
    juce::MemoryBlock mb;
    p.setMarkerDst (3, p.getMarkers()[3].dst + 1000.0);
    p.getStateInformation (mb);
    SyncProcessor q;
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = q.getMarkers().size() == p.getMarkers().size();
    for (size_t i = 0; same && i < p.getMarkers().size(); ++i)
        same = std::abs (q.getMarkers()[i].dst - p.getMarkers()[i].dst) < 0.01;
    std::printf ("state round-trip: %s  (warped %d, target %.2f, markers %zu)\n", same ? "OK" : "MISMATCH",
                 (int) q.isWarped(), q.getSettings().targetBpm, q.getMarkers().size());
    while (q.isRendering() || p.isRendering()) juce::Thread::sleep (5);
    std::printf ("rendered lengths: %d vs %d\n", q.getRendered()->getNumSamples(), p.getRendered()->getNumSamples());
    juce::String e2;
    std::printf ("export: %s\n", p.exportTo (out.getChildFile ("export_test.wav"), e2) ? "OK" : e2.toRawUTF8());
    std::printf ("ok\n");
    return 0;
}
