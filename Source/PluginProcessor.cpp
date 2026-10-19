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
    harmoParam = apvts.getRawParameterValue (ParamIDs::harmo);
    gateOnParam = apvts.getRawParameterValue (ParamIDs::gateOn);
    gateLengthParam = apvts.getRawParameterValue (ParamIDs::gateLength);
    for (int i = 0; i < TranceGate::numSteps; ++i)
        gateStepParam[i] = apvts.getRawParameterValue (ParamIDs::gateStep[i]);
    reverbParam = apvts.getRawParameterValue (ParamIDs::reverb);
    gainParam   = apvts.getRawParameterValue (ParamIDs::gain);
    modeParam = apvts.getRawParameterValue (ParamIDs::mode);
    noteParam = apvts.getRawParameterValue (ParamIDs::note);
    eqLowParam  = apvts.getRawParameterValue (ParamIDs::eqLow);
    eqHighParam = apvts.getRawParameterValue (ParamIDs::eqHigh);
    for (int b = 0; b < 3; ++b)
    {
        eqBellHzParam[b] = apvts.getRawParameterValue (ParamIDs::eqBellHz[b]);
        eqBellDbParam[b] = apvts.getRawParameterValue (ParamIDs::eqBellDb[b]);
        eqBellQParam[b]  = apvts.getRawParameterValue (ParamIDs::eqBellQ[b]);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout FrameAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // 1. HARMO (ancien MIX) : 0–100 %, défaut 100 %. Dose uniquement le
    //    traitement harmonique (peigne + harmoniques) : à 0 %, reverb, gate,
    //    EQ et gain continuent de fonctionner.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::harmo, 1 }, "harmo",
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

    // 3. GAIN de sortie : −24 … +12 dB, défaut 0 dB.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::gain, 1 }, "gain",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const String& s) { return s.retainCharacters ("-0123456789.").getFloatValue(); })));

    // 4. GATE : coupe le son sur les temps (16 pas), éteint par défaut, 15 motifs.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::gateOn, 1 }, "gate", false));
    // Pas du gate : vélocité 0 … 1 de chaque pas (0 = fermé), éditables ;
    // valeurs de départ = motif « bloom ».
    {
        const auto& start = TranceGate::preset (0);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::gateLength, 1 }, "gate length",
            NormalisableRange<float> (0.05f, 1.0f, 0.01f), start.length));
        for (int i = 0; i < TranceGate::numSteps; ++i)
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { ParamIDs::gateStep[i], 1 }, "gate step " + String (i + 1),
                NormalisableRange<float> (0.0f, 1.0f, 0.01f), start.steps[(size_t) i],
                AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
    }

    // 5. MODE : major / minor.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::mode, 1 }, "mode", StringArray { "major", "minor" }, 0));

    // 6. NOTE : tonique parmi les 12 notes.
    StringArray notes;
    for (auto* n : frame::noteNames)
        notes.add (n);
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::note, 1 }, "note", notes, 0));

    // 7. EQ façon Pro-Q (partie spectrum) : coupe basse, 3 cloches, coupe haute.
    //    Coupes en butée (20 Hz / 20 kHz) et cloches à 0 dB = aucun effet.
    auto logRange = [] (float lo, float hi)
    {
        return NormalisableRange<float> (lo, hi,
            [] (float a, float b, float t) { return a * std::pow (b / a, t); },
            [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
            [] (float a, float b, float v) { return jlimit (a, b, v); });
    };
    auto hzText = AudioParameterFloatAttributes()
        .withLabel ("Hz")
        .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; })
        .withValueFromStringFunction ([] (const String& s) { const float v = s.retainCharacters ("0123456789.").getFloatValue(); return s.containsIgnoreCase ("k") ? v * 1000.0f : v; });
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::eqLow, 2 },  "eq low cut",  logRange (ProEq::minHz, ProEq::maxHz), ProEq::minHz, hzText));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::eqHigh, 2 }, "eq high cut", logRange (ProEq::minHz, ProEq::maxHz), ProEq::maxHz, hzText));
    for (int b = 0; b < ProEq::numBells; ++b)
    {
        const String n = "eq bell " + String (b + 1);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::eqBellHz[b], 1 }, n + " freq",
            logRange (ProEq::minHz, ProEq::maxHz), ProEq::defaultBellHz[b], hzText));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::eqBellDb[b], 1 }, n + " gain",
            NormalisableRange<float> (-ProEq::maxGainDb, ProEq::maxGainDb, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::eqBellQ[b], 1 }, n + " q",
            logRange (ProEq::minQ, ProEq::maxQ), 1.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
    }

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

    snapper.prepare (sampleRate);                      // recalage des partiels : 0 latence
    snapper.setScale (getCurrentScaleMask());
    snapper.reset();
    harmonizer.prepare (sampleRate);                   // traitement harmonique : 0 latence
    harmonizer.setScale (getCurrentScaleMask());
    harmonizer.reset();
    engine.prepare (sampleRate, 2);                    // analyse spectrale pour l'affichage uniquement
    engine.setColour (1.0f);
    reverb.prepare (sampleRate, maxChunk);
    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam->load()));
    setLatencySamples (0);                             // AUCUN retard : l'effet sonne avec le son

    dryBuffer.setSize (2, maxChunk, false, true, false);
    wetBuffer.setSize (2, maxChunk, false, true, false);
    analysisBuffer.setSize (2, maxChunk, false, true, false);

    harmoSmoothed.reset (sampleRate, 0.05);            // 50 ms de lissage : pas de clic
    harmoSmoothed.setCurrentAndTargetValue (harmoParam->load() * 0.01f);
    preHarmoBuffer.setSize (2, maxChunk, false, true, false);
    gate.prepare (sampleRate);
    freePpq = 0.0;
    keyDetector.prepare (sampleRate);                  // SCAN : analyse de l'entrée (seulement pendant un scan)
    holdIn  = 1.0f - std::exp (-1.0f / (0.03f * (float) sampleRate));   // vers le son d'origine : 30 ms
    holdOut = 1.0f - std::exp (-1.0f / (0.15f * (float) sampleRate));   // retour de l'effet : 150 ms
    holdSmoothed = (scanHold.load() || ! keyReady.load()) ? 1.0f : 0.0f;
    proEq.prepare (sampleRate);
    outBuffer.setSize (2, maxChunk, false, true, false);

    engine.setScaleMask (getCurrentScaleMask());
    samplesPerWaveSlice = engine.getFftSize() / 8;     // l'onde défile à la même vitesse que le spectrogramme
    currentSlice = {};
    sliceCount = 0;
    engineNeedsReset = false;

    // Garde-fou et vu-mètres : constantes de temps.
    guardCoef      = 1.0f - std::exp (-1.0f / (0.15f * (float) sampleRate));   // énergie sur ~150 ms
    restoreAttack  = 1.0f - std::exp (-32.0f / (0.03f * (float) sampleRate)); // réinjection en ~30 ms
    restoreRelease = 1.0f - std::exp (-32.0f / (0.6f  * (float) sampleRate)); // retrait en ~600 ms
    meterCoef      = 1.0f - std::exp (-1.0f / (0.3f  * (float) sampleRate));
    guardDry = guardWet = 0.0f;
    directRestore = 0.0f;
    meterIn = meterOut = 0.0f;
    limiterGain = 1.0f;
    limiterRelease = 1.0f - std::exp (-1.0f / (0.12f * (float) sampleRate));
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
        constexpr float knee = 0.97f, room = 1.0f - knee;
        const float a = std::abs (y);
        if (a <= knee) return y;
        return std::copysign (knee + room * std::tanh ((a - knee) / room), y);
    }

    inline float finiteOrZero (float v) noexcept { return std::isfinite (v) ? v : 0.0f; }
}

ProEq::Settings FrameAudioProcessor::getEqSettings() const noexcept
{
    ProEq::Settings s;
    s.lowHz = eqLowParam->load();
    s.highHz = eqHighParam->load();
    for (int b = 0; b < ProEq::numBells; ++b)
    {
        s.bellHz[(size_t) b] = eqBellHzParam[b]->load();
        s.bellDb[(size_t) b] = eqBellDbParam[b]->load();
        s.bellQ [(size_t) b] = eqBellQParam[b]->load();
    }
    return s;
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
        harmonizer.reset();
        snapper.reset();
        gate.reset();
        proEq.reset();
        reverb.reset();
        guardDry = guardWet = 0.0f;
        directRestore = 0.0f;
        reverbSmoothed = reverbParam->load() * 0.01f;
        engineNeedsReset = false;
    }

    // SCAN : le son d'entrée (avant tout traitement) est analysé pendant un scan.
    keyDetector.push (buffer.getReadPointer (0), numChannels > 1 ? buffer.getReadPointer (1) : nullptr, buffer.getNumSamples());

    engine.setScaleMask (getCurrentScaleMask());       // (affichage)
    harmonizer.setScale (getCurrentScaleMask());       // gains glissés si la gamme change
    snapper.setScale (getCurrentScaleMask());          // fondu croisé si la gamme change
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 12.0f, finiteOrZero (gainParam->load()))));
    harmoSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, finiteOrZero (harmoParam->load()) * 0.01f));

    // REVERB : lissage exponentiel (~60 ms), une fois par bloc. La
    // fréquence d'échantillonnage vient de prepareToPlay (jamais 0 ici).
    const double sr = currentSampleRate.load();
    const float blockSeconds = (float) ((double) buffer.getNumSamples() / (sr > 0.0 ? sr : 44100.0));
    const float k = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-blockSeconds / 0.06f));


    const float reverbTarget = juce::jlimit (0.0f, 1.0f, finiteOrZero (reverbParam->load()) * 0.01f);
    reverbSmoothed += (reverbTarget - reverbSmoothed) * k;
    if (! std::isfinite (reverbSmoothed)) reverbSmoothed = reverbTarget;


    // GATE : position et tempo du DAW. Transport arrêté (ou pas d'info) : le
    // gate tourne seul, au tempo du DAW (120 par défaut).
    {
        double bpm = 120.0, ppq = freePpq;
        bool playing = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) if (*b > 1.0 && *b < 1000.0) bpm = *b;
                playing = pos->getIsPlaying();
                if (playing)
                    if (auto q = pos->getPpqPosition()) ppq = *q;
            }
        ppqPerSample = bpm / 60.0 / (sr > 0.0 ? sr : 44100.0);
        blockPpq = std::isfinite (ppq) ? ppq : 0.0;
        freePpq = playing ? 0.0 : std::fmod (blockPpq + ppqPerSample * buffer.getNumSamples(), 4096.0);
    }

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
    reverb.process (wetView, numChannels, reverbSmoothed);   // 1. reverb ambient + particules (0 latence)
    for (int c = 0; c < numChannels; ++c)                    // (copie pour le dosage HARMO)
        preHarmoBuffer.copyFrom (c, 0, wetBuffer, c, 0, numSamples);
    snapper.process (wetView, numChannels, 1.0f);            // 3a. partiels tenus recalés sur la gamme
    harmonizer.process (wetView, numChannels, 1.0f);         // 3. peigne + harmoniques justes (0 latence)

    // HARMO : dosage du traitement harmonique seul (100 % = son de la 2.0.0).
    for (int i = 0; i < numSamples; ++i)
    {
        const float h = harmoSmoothed.getNextValue();
        for (int c = 0; c < numChannels; ++c)
        {
            const float pre = preHarmoBuffer.getSample (c, i);
            wetBuffer.setSample (c, i, pre + h * (wetBuffer.getSample (c, i) - pre));
        }
    }

   #ifdef FRAME_TEST_SIMULATE_WET_LOSS
    wetView.applyGain (0.03f);                               // test du garde-fou uniquement (jamais dans le plugin)
   #endif

    // Analyse spectrale de la sortie traitée, pour l'affichage seulement
    // (copie : le son n'est jamais retardé par l'analyse).
    for (int c = 0; c < numChannels; ++c)
        analysisBuffer.copyFrom (c, 0, wetBuffer, c, 0, numSamples);
    {
        juce::AudioBuffer<float> analysisView (analysisBuffer.getArrayOfWritePointers(), numChannels, 0, numSamples);
        engine.process (analysisView);
    }

    bool invalid = false;

    // 5a. Garde-fou + gain de sortie → outBuffer.
    for (int i = 0; i < numSamples; ++i)
    {
        const float og = outputGain.getNextValue();
        for (int c = 0; c < numChannels; ++c)
        {
            // Zéro latence : le son d'origine et le son traité sont alignés tels quels.
            const float delayed = dryBuffer.getSample (c, i);

            // Garde-fou « tout le son passe » : si le chemin traité a perdu une
            // grande partie du son d'origine, le son direct est réinjecté.
            const float rawWet = wetBuffer.getSample (c, i);
            const float wet = rawWet + directRestore * delayed;

            // La perte se mesure AVANT correction (sinon le garde-fou se freinerait lui-même).
            guardDry += (delayed * delayed - guardDry) * guardCoef;
            guardWet += (rawWet * rawWet - guardWet) * guardCoef;

            outBuffer.setSample (c, i, wet * og);   // chaque module est dosé par son propre bouton
        }

        // Mise à jour du garde-fou, une fois par paquet de 32 échantillons.
        if ((++guardCounter & 31) == 0)
        {
            float target = 0.0f;
            if (guardDry > 1.0e-7f)                               // le son d'origine joue (> −70 dBFS)
            {
                const float ratio = std::sqrt (guardWet / guardDry);
                // traité à plus de −10 dB sous l'original : on réinjecte progressivement
                // (pleine réinjection vers −20 dB ; la coupure observée dans FL était à −27 dB)
                target = juce::jlimit (0.0f, 1.0f, (0.32f - ratio) / 0.22f);
            }
            const float k = target > directRestore ? restoreAttack : restoreRelease;
            directRestore += (target - directRestore) * k;
            if (! std::isfinite (directRestore) || directRestore < 1.0e-4f) directRestore = target > 0.0f ? directRestore : 0.0f;
            if (! std::isfinite (guardDry) || ! std::isfinite (guardWet)) { guardDry = guardWet = 0.0f; directRestore = 0.0f; }
        }
    }

    // 5b. EQ façon Pro-Q : après le garde-fou, qui ne « répare » donc pas
    //     une coupure voulue.
    proEq.process (outBuffer.getArrayOfWritePointers(), numChannels, numSamples, getEqSettings());

    const bool gateOnNow = gateOnParam->load() > 0.5f;
    std::array<float, TranceGate::numSteps> gateSteps {};
    for (int s = 0; s < TranceGate::numSteps; ++s)
        gateSteps[(size_t) s] = finiteOrZero (gateStepParam[s]->load());
    const float gateLen = finiteOrZero (gateLengthParam->load());

    // 5c. Limiteur de sortie, GATE, vu-mètres, tranches de forme d'onde.
    for (int i = 0; i < numSamples; ++i)
    {
        float inLo = 0.0f, inHi = 0.0f, outLo = 0.0f, outHi = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            const float x = dryBuffer.getSample (c, i);
            inLo = juce::jmin (inLo, x);  inHi = juce::jmax (inHi, x);
        }

        // Limiteur lié en stéréo : attaque immédiate, relâchement ~120 ms,
        // plafond −0.5 dBFS. Le gain varie en douceur : pas d'écrêtage, donc
        // pas de grésillement. Le tanh de sécurité ne sert qu'aux crêtes extrêmes.
        float peak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            peak = juce::jmax (peak, std::abs (outBuffer.getSample (c, i)));
        constexpr float ceiling = 0.944f;                     // −0.5 dBFS
        const float needed = peak > ceiling ? ceiling / peak : 1.0f;
        limiterGain = needed < limiterGain ? needed : limiterGain + (1.0f - limiterGain) * limiterRelease;
        if (! std::isfinite (limiterGain)) limiterGain = 1.0f;

        // GATE : coupe sur les temps (sur le son traité ; 0 latence).
        const float gg = gate.next (blockPpq + ppqPerSample * (double) (startSample + i), gateOnNow, gateSteps, gateLen);

        // SCAN : pendant l'écoute, et tant qu'aucun scan n'est terminé, on
        // n'entend que le son d'origine (brut).
        const float holdTarget = (scanHold.load (std::memory_order_relaxed)
                                  || ! keyReady.load (std::memory_order_relaxed)) ? 1.0f : 0.0f;
        holdSmoothed += (holdTarget - holdSmoothed) * (holdTarget > holdSmoothed ? holdIn : holdOut);
        if (holdSmoothed < 1.0e-5f) holdSmoothed = 0.0f;
        if (holdSmoothed > 0.99999f) holdSmoothed = 1.0f;

        for (int c = 0; c < numChannels; ++c)
        {
            float y = safetyLimit (outBuffer.getSample (c, i) * limiterGain, invalid) * gg;
            if (holdSmoothed >= 1.0f)
                y = dryBuffer.getSample (c, i);                     // son d'origine, à l'identique
            else if (holdSmoothed > 0.0f)
                y += holdSmoothed * (dryBuffer.getSample (c, i) - y);
            buffer.setSample (c, startSample + i, y);
            outLo = juce::jmin (outLo, y); outHi = juce::jmax (outHi, y);
        }

        meterIn  += (inHi - inLo  - meterIn)  * meterCoef;
        meterOut += (outHi - outLo - meterOut) * meterCoef;

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

    inputLevelDb.store  (juce::Decibels::gainToDecibels (0.5f * meterIn,  -100.0f));
    outputLevelDb.store (juce::Decibels::gainToDecibels (0.5f * meterOut, -100.0f));
    restoreActive.store (directRestore > 0.05f);
    gateStep.store (gateOnNow ? gate.getStep() : -1);
}

void FrameAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Zéro latence : en bypass, le son passe tel quel (rien à compenser).
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (c, i)))
                buffer.setSample (c, i, 0.0f);
}

//==============================================================================
bool FrameAudioProcessor::isIntroDone() const
{
    return (bool) apvts.state.getProperty ("introDone", false);
}

void FrameAudioProcessor::setIntroDone (bool d)
{
    apvts.state.setProperty ("introDone", d, nullptr);
}

void FrameAudioProcessor::loadGatePreset (int index)
{
    index = juce::jlimit (0, TranceGate::numPresets - 1, index);
    const auto& p = TranceGate::preset (index);
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    for (int i = 0; i < TranceGate::numSteps; ++i)
        set (ParamIDs::gateStep[i], p.steps[(size_t) i]);
    set (ParamIDs::gateLength, p.length);
    apvts.state.setProperty ("gatePreset", index, nullptr);
    apvts.state.setProperty ("gateEdited", false, nullptr);
}

void FrameAudioProcessor::setKeyReady (bool r)
{
    keyReady.store (r);
    apvts.state.setProperty ("keyReady", r, nullptr);
}

void FrameAudioProcessor::startScan()
{
    scanHold.store (true);
    keyDetector.start();
}

void FrameAudioProcessor::cancelScan()
{
    keyDetector.cancel();
    scanHold.store (false);
}

void FrameAudioProcessor::applyDetectedKey (int tonic, int mode)
{
    // L'effet revient (fondu de 150 ms) une fois la tonalité réglée.
    struct Release { std::atomic<bool>& h; ~Release() { h.store (false); } } release { scanHold };
    // Fil interface : tonique et mode réglés comme si on cliquait (geste complet,
    // l'hôte enregistre le changement et l'automation reste cohérente).
    if (tonic < 0 || tonic > 11) return;
    setKeyReady (true);                     // premier scan terminé : l'effet s'active
    if (auto* p = apvts.getParameter (ParamIDs::note))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) tonic));
        p->endChangeGesture();
    }
    if (auto* p = apvts.getParameter (ParamIDs::mode))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (mode == 1 ? 1 : 0)));
        p->endChangeGesture();
    }
}

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
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            // 3.5 : le scan n'est plus imposé au lancement — l'effet est toujours actif
            // (pendant un scan, la sortie reste le son d'origine, comme avant).
            keyReady.store (true);
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FrameAudioProcessor();
}
