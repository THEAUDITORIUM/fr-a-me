/*
  ==============================================================================
    fr(a)me — PluginProcessor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Scale.h"

//==============================================================================
FrameAudioProcessor::FrameAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FRAME_STATE", createParameterLayout())
{
    mixParam  = apvts.getRawParameterValue (ParamIDs::mix);
    reverbParam = apvts.getRawParameterValue (ParamIDs::reverb);
    modeParam = apvts.getRawParameterValue (ParamIDs::mode);
    noteParam = apvts.getRawParameterValue (ParamIDs::note);
}

juce::AudioProcessorValueTreeState::ParameterLayout FrameAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // 1. MIX : 0–100 %, défaut 100 %.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::mix, 1 }, "mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
            .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("0123456789.").getFloatValue(); })));

    // 2. REVERB : 0–100 %, défaut 30 %. Réverbe ambient (FDN) avec nuage
    //    granulaire réinjecté, placée avant le moteur : queue dans la tonalité.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::reverb, 2 }, "reverb",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f,
        AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; })
            .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("0123456789.").getFloatValue(); })));

    // 3. MODE : major / minor.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::mode, 1 }, "mode", StringArray { "major", "minor" }, 0));

    // 4. NOTE : tonique parmi les 12 notes.
    StringArray notes;
    for (auto* n : frame::noteNames)
        notes.add (n);
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::note, 1 }, "note", notes, 0));

    return layout;
}

//==============================================================================
bool FrameAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;   // mono→mono, stéréo→stéréo
}

void FrameAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepared = false;
    if (! (sampleRate > 0.0)) sampleRate = 44100.0;    // hôte qui annonce 0 Hz : valeur sûre
    currentSampleRate = sampleRate;
    maxChunk = juce::jlimit (32, 8192, samplesPerBlock > 0 ? samplesPerBlock : 512);

    engine.prepare (sampleRate, 2);
    reverb.prepare (sampleRate, maxChunk);
    setLatencySamples (engine.getLatencySamples());   // compensation de latence par l'hôte

    dryBuffer.setSize (2, maxChunk, false, true, false);
    wetBuffer.setSize (2, maxChunk, false, true, false);
    dryDelay.setSize  (2, juce::jmax (1, engine.getLatencySamples()), false, true, false);
    dryDelay.clear();
    dryDelayWritePos = 0;

    mixSmoothed.reset (sampleRate, 0.05);              // 50 ms de lissage : pas de clic
    mixSmoothed.setCurrentAndTargetValue (mixParam->load() * 0.01f);

    engine.setScaleMask (getCurrentScaleMask());
    samplesPerWaveSlice = engine.getFftSize() / 8;     // l'onde défile à la même vitesse que le spectrogramme
    currentSlice = {};
    sliceCount = 0;
    engineNeedsReset = false;
    prepared = true;
}

void FrameAudioProcessor::releaseResources()
{
    prepared = false;
}

namespace
{
    /** Limiteur doux de sécurité : transparent sous −1 dBFS, puis courbe
        tanh qui plafonne la sortie à 0 dBFS (les renforts harmoniques ne
        peuvent pas faire saturer l'hôte). Une valeur invalide devient 0. */
    inline float safetyLimit (float y, bool& invalid) noexcept
    {
        if (! std::isfinite (y)) { invalid = true; return 0.0f; }
        constexpr float knee = 0.891f, room = 1.0f - knee;
        const float a = std::abs (y);
        if (a <= knee) return y;
        return std::copysign (knee + room * std::tanh ((a - knee) / room), y);
    }

    inline float finiteOrZero (float v) noexcept { return std::isfinite (v) ? v : 0.0f; }
}

uint16_t FrameAudioProcessor::getCurrentScaleMask() const noexcept
{
    const int note = juce::jlimit (0, 11, (int) std::lround (noteParam->load()));
    const auto mode = (std::lround (modeParam->load()) == 0) ? frame::Mode::major : frame::Mode::minor;
    return frame::scaleMask (note, mode);
}

//==============================================================================
void FrameAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    for (int c = numChannels; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    // Certains hôtes appellent processBlock avant prepareToPlay, ou avec un
    // bloc vide : on laisse alors passer le son sans y toucher.
    if (numChannels <= 0 || ! prepared || buffer.getNumSamples() <= 0)
        return;

    // Auto-réparation : si une valeur invalide est sortie au bloc précédent,
    // le moteur repart d'un état propre (sans allocation).
    if (engineNeedsReset)
    {
        engine.reset();
        reverb.reset();
        dryDelay.clear();
        reverbSmoothed = reverbParam->load() * 0.01f;
        engineNeedsReset = false;
    }

    engine.setScaleMask (getCurrentScaleMask());       // fondu interne si la gamme change
    mixSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, finiteOrZero (mixParam->load()) * 0.01f));

    // REVERB : lissage exponentiel (~60 ms), une fois par bloc. La
    // fréquence d'échantillonnage vient de prepareToPlay (jamais 0 ici).
    const double sr = currentSampleRate.load();
    const float blockSeconds = (float) ((double) buffer.getNumSamples() / (sr > 0.0 ? sr : 44100.0));
    const float k = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-blockSeconds / 0.06f));


    const float reverbTarget = juce::jlimit (0.0f, 1.0f, finiteOrZero (reverbParam->load()) * 0.01f);
    reverbSmoothed += (reverbTarget - reverbSmoothed) * k;
    if (! std::isfinite (reverbSmoothed)) reverbSmoothed = reverbTarget;


    // L'hôte peut envoyer un bloc plus grand qu'annoncé : on découpe.
    for (int start = 0; start < buffer.getNumSamples(); start += maxChunk)
        processChunk (buffer, start, juce::jmin (maxChunk, buffer.getNumSamples() - start));
}

void FrameAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    const int numChannels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);

    // Copie de l'entrée : dryBuffer garde le signal brut, wetBuffer sera traité.
    for (int c = 0; c < numChannels; ++c)
    {
        // Entrée assainie : une valeur invalide venant de l'hôte devient 0.
        float* d = dryBuffer.getWritePointer (c);
        const float* in = buffer.getReadPointer (c, startSample);
        for (int i = 0; i < numSamples; ++i)
            d[i] = finiteOrZero (in[i]);
        wetBuffer.copyFrom (c, 0, dryBuffer, c, 0, numSamples);
    }

    // Vue sur la partie utile de wetBuffer (pas d'allocation pour ≤ 32 canaux).
    juce::AudioBuffer<float> wetView (wetBuffer.getArrayOfWritePointers(), numChannels, 0, numSamples);
    reverb.process (wetView, numChannels, reverbSmoothed);   // 1. reverb ambient + nuage granulaire
    engine.process (wetView);                                // 2. recalage + harmoniques + peigne (queue comprise)

    const int delayLength = dryDelay.getNumSamples();
    bool invalid = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mixSmoothed.getNextValue();

        float inLo = 0.0f, inHi = 0.0f, outLo = 0.0f, outHi = 0.0f;

        for (int c = 0; c < numChannels; ++c)
        {
            const float x = dryBuffer.getSample (c, i);

            // Le signal sec est retardé de la même latence que le signal traité :
            // dry et wet restent parfaitement alignés quel que soit le MIX.
            float* line = dryDelay.getWritePointer (c);
            const float delayed = line[dryDelayWritePos];
            line[dryDelayWritePos] = x;

            const float y = safetyLimit (delayed + m * (wetBuffer.getSample (c, i) - delayed), invalid);
            buffer.setSample (c, startSample + i, y);

            inLo  = juce::jmin (inLo, x);  inHi  = juce::jmax (inHi, x);
            outLo = juce::jmin (outLo, y); outHi = juce::jmax (outHi, y);
        }

        if (++dryDelayWritePos >= delayLength)
            dryDelayWritePos = 0;

        // Tranche de forme d'onde pour l'interface.
        currentSlice.inMin  = juce::jmin (currentSlice.inMin, inLo);
        currentSlice.inMax  = juce::jmax (currentSlice.inMax, inHi);
        currentSlice.outMin = juce::jmin (currentSlice.outMin, outLo);
        currentSlice.outMax = juce::jmax (currentSlice.outMax, outHi);

        if (++sliceCount >= samplesPerWaveSlice)
        {
            waveQueue.push (currentSlice);   // sans verrou ; ignoré si l'UI est en retard
            currentSlice = {};
            sliceCount = 0;
        }
    }

    if (invalid)
        engineNeedsReset = true;     // le moteur sera réinitialisé au prochain bloc
}

void FrameAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // En bypass on garde la même latence (signal sec retardé) pour que l'hôte
    // n'entende pas de saut temporel, et on continue d'alimenter le moteur
    // pour une reprise sans résidu.
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    if (numChannels <= 0 || ! prepared || buffer.getNumSamples() <= 0)
        return;

    const int delayLength = dryDelay.getNumSamples();

    for (int start = 0; start < buffer.getNumSamples(); start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, buffer.getNumSamples() - start);
        for (int c = 0; c < numChannels; ++c)
        {
            float* w = wetBuffer.getWritePointer (c);
            const float* in = buffer.getReadPointer (c, start);
            for (int i = 0; i < n; ++i)
                w[i] = finiteOrZero (in[i]);
        }
        juce::AudioBuffer<float> wetView (wetBuffer.getArrayOfWritePointers(), numChannels, 0, n);
        engine.setScaleMask (getCurrentScaleMask());
        engine.process (wetView);

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                float* line = dryDelay.getWritePointer (c);
                const float x = finiteOrZero (buffer.getSample (c, start + i));
                buffer.setSample (c, start + i, line[dryDelayWritePos]);
                line[dryDelayWritePos] = x;
            }
            if (++dryDelayWritePos >= delayLength)
                dryDelayWritePos = 0;
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* FrameAudioProcessor::createEditor()
{
    return new FrameAudioProcessorEditor (*this);
}

void FrameAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Tous les paramètres sont sauvegardés avec le projet de l'hôte.
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FrameAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrameAudioProcessor();
}
