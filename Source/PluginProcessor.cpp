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

    // 2. MODE : major / minor.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::mode, 1 }, "mode", StringArray { "major", "minor" }, 0));

    // 3. NOTE : tonique parmi les 12 notes.
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
    currentSampleRate = sampleRate;
    maxChunk = juce::jmax (32, samplesPerBlock);

    engine.prepare (sampleRate, 2);
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
    if (numChannels <= 0)
        return;

    engine.setScaleMask (getCurrentScaleMask());       // fondu interne si la gamme change
    mixSmoothed.setTargetValue (mixParam->load() * 0.01f);

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
        dryBuffer.copyFrom (c, 0, buffer, c, startSample, numSamples);
        wetBuffer.copyFrom (c, 0, buffer, c, startSample, numSamples);
    }

    // Vue sur la partie utile de wetBuffer (pas d'allocation pour ≤ 32 canaux).
    juce::AudioBuffer<float> wetView (wetBuffer.getArrayOfWritePointers(), numChannels, 0, numSamples);
    engine.process (wetView);

    const int delayLength = dryDelay.getNumSamples();

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

            const float y = delayed + m * (wetBuffer.getSample (c, i) - delayed);
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
}

void FrameAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // En bypass on garde la même latence (signal sec retardé) pour que l'hôte
    // n'entende pas de saut temporel, et on continue d'alimenter le moteur
    // pour une reprise sans résidu.
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    if (numChannels <= 0)
        return;

    const int delayLength = dryDelay.getNumSamples();

    for (int start = 0; start < buffer.getNumSamples(); start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, buffer.getNumSamples() - start);
        for (int c = 0; c < numChannels; ++c)
            wetBuffer.copyFrom (c, 0, buffer, c, start, n);
        juce::AudioBuffer<float> wetView (wetBuffer.getArrayOfWritePointers(), numChannels, 0, n);
        engine.setScaleMask (getCurrentScaleMask());
        engine.process (wetView);

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numChannels; ++c)
            {
                float* line = dryDelay.getWritePointer (c);
                const float x = buffer.getSample (c, start + i);
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
