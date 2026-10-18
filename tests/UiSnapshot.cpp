/*
  ==============================================================================
    fr(a)me — capture de l'interface (outil de développement).

    Instancie le processeur, lui fait traiter quelques secondes d'un signal de
    démonstration (pad + basse + percussions), ouvre l'éditeur et enregistre
    une image PNG de l'interface. Usage : frame_ui_snapshot sortie.png [largeur]
  ==============================================================================
*/

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "frame_ui.png");
    const int width = argc > 2 ? juce::String (argv[2]).getIntValue() : 1000;

    FrameAudioProcessor proc;
    const double sr = 48000.0;
    const int block = 512;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    if (auto* note = proc.apvts.getParameter (ParamIDs::note)) note->setValueNotifyingHost (note->convertTo0to1 (9.0f)); // a
    if (auto* mode = proc.apvts.getParameter (ParamIDs::mode)) mode->setValueNotifyingHost (1.0f);                      // minor
    if (auto* pt = proc.apvts.getParameter (ParamIDs::pitch)) pt->setValueNotifyingHost (pt->convertTo0to1 (0.0f));

    // EQ du spectrum (optionnel) : sortie.png largeur thème low_hz high_hz
    if (argc > 5)
    {
        if (auto* lo = proc.apvts.getParameter (ParamIDs::eqLow))  lo->setValueNotifyingHost (lo->convertTo0to1 ((float) juce::String (argv[4]).getDoubleValue()));
        if (auto* hi = proc.apvts.getParameter (ParamIDs::eqHigh)) hi->setValueNotifyingHost (hi->convertTo0to1 ((float) juce::String (argv[5]).getDoubleValue()));
        // démonstration : cloche 2 à +6 dB vers 1.2 kHz, cloche 3 à −5 dB vers 5 kHz
        auto set = [&proc] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        set (ParamIDs::eqBellHz[1], 1200.0f); set (ParamIDs::eqBellDb[1], 6.0f);
        set (ParamIDs::eqBellHz[2], 5000.0f); set (ParamIDs::eqBellDb[2], -5.0f); set (ParamIDs::eqBellQ[2], 2.0f);
    }
    if (argc > 3) proc.setTheme (argv[3]);                                   // sun / garden
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (width, (int) std::round (width * 760.0 / 1000.0));
    auto* canvas = dynamic_cast<FrameCanvas*> (editor->getChildComponent (0));

    // Signal de démonstration : pad désaccordé, basse, kick / caisse claire.
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    juce::Random rng (11);
    const double pad[] = { 207.0, 263.0, 318.0, 401.0 };
    long n0 = 0;
    const int seconds = 10;
    for (int b = 0; b < (int) (seconds * sr / block); ++b)
    {
        for (int i = 0; i < block; ++i, ++n0)
        {
            const double t = (double) n0 / sr;
            double v = 0.0;
            const double swell = 0.6 + 0.4 * std::sin (t * 0.9);
            for (double f : pad)
                for (int h = 1; h < 12; ++h)
                    v += swell * 0.05 / h * std::sin (juce::MathConstants<double>::twoPi * f * h * t * (1.0 + 0.0008 * std::sin (t * 3.1)));
            v += 0.25 * std::sin (juce::MathConstants<double>::twoPi * 57.0 * t) * (0.5 + 0.5 * std::sin (t * 2.0));
            const double beat = std::fmod (t, 0.5);
            const bool snare = std::fmod (t, 1.0) >= 0.5;
            v += (snare ? 0.35 * (rng.nextFloat() * 2.0 - 1.0) : 0.7 * std::sin (juce::MathConstants<double>::twoPi * (50.0 + 90.0 * std::exp (-beat * 30.0)) * beat))
                 * std::exp (-beat * (snare ? 18.0 : 9.0));
            buf.setSample (0, i, (float) (v * 0.7));
            buf.setSample (1, i, (float) (v * 0.7 * (0.95 + 0.05 * std::sin (t))));
        }
        proc.processBlock (buf, midi);
        if (canvas != nullptr && b % 8 == 0)
            canvas->refresh();
    }
    if (canvas != nullptr)
        canvas->refresh();

    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::printf ("%s %s (%dx%d)\n", ok ? "écrit" : "ÉCHEC", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    editor.reset();
    return ok ? 0 : 1;
}
