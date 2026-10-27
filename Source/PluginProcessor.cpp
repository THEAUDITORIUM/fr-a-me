#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "level", 1 }, "Level",
                                                            juce::NormalisableRange<float> (-60.0f, 6.0f, 0.01f, 2.2f), 0.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("dB")));
        return l;
    }

    constexpr double kMaxSeconds = 600.0;
}

SyncProcessor::SyncProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", makeLayout())
{
    formats.registerBasicFormats();
    levelParam = apvts.getRawParameterValue ("level");

    startTimerHz (4); // watch FL Studio's tempo on the message thread
}

SyncProcessor::~SyncProcessor()
{
    stopTimer();
    pool.removeAllJobs (true, 10000);
}

bool SyncProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SyncProcessor::prepareToPlay (double sampleRate, int)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
}

// ============================================================================
//  Audio thread
// ============================================================================
void SyncProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    buffer.clear();

    bool playing = false;
    double ppq = 0.0;
    bool hasPpq = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) if (*bpm > 1.0) hostBpm.store (*bpm);
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; hasPpq = true; }
        }
    }
    hostPlaying.store (playing);

    const bool previewing = preview.load() && ! playing;
    if (! playing && ! previewing)
    {
        playhead.store (-1.0);
        wasPreviewing = false;
        return;
    }

    juce::SpinLock::ScopedTryLockType lock (renderLock);
    if (! lock.isLocked()) return;

    const bool useB = listenB.load();
    auto buf = useB ? rendered : source;
    if (buf == nullptr || buf->getNumSamples() < 2) return;

    const int len = buf->getNumSamples();
    const double ratio = fileSampleRate / hostSampleRate;
    const double hBpm = hostBpm.load();
    const double bufBpm = useB ? renderedBpm.load() : sourceBpmAtomic.load();

    double idx = 0.0;
    if (playing && hasPpq)
    {
        // locked to FL's bar grid when the tempi match, otherwise free-running in time
        const double samples = std::abs (hBpm - bufBpm) < 0.01
                                   ? ppq * (60.0 / bufBpm) * fileSampleRate
                                   : ppq * (60.0 / hBpm) * fileSampleRate;
        idx = std::fmod (samples, (double) len);
        if (idx < 0.0) idx += len;
    }
    else
    {
        if (! wasPreviewing) previewPos = 0.0;
        idx = previewPos;
    }

    const float target = juce::Decibels::decibelsToGain (levelParam->load(), -59.9f);
    const int outCh = buffer.getNumChannels();
    const int inCh  = buf->getNumChannels();

    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        smoothedGain += (target - smoothedGain) * 0.002f;
        const int i0 = (int) idx;
        const int i1 = (i0 + 1) % len;
        const float fr = (float) (idx - i0);

        for (int ch = 0; ch < outCh; ++ch)
        {
            const float* d = buf->getReadPointer (juce::jmin (ch, inCh - 1));
            buffer.setSample (ch, s, (d[i0] + (d[i1] - d[i0]) * fr) * smoothedGain);
        }

        idx += ratio;
        if (idx >= len) idx -= len;
    }

    if (previewing) previewPos = idx;
    wasPreviewing = previewing;
    playhead.store (idx);
}

// ============================================================================
//  Sample management
// ============================================================================
bool SyncProcessor::loadFile (const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Format not supported"; return false; }
    if (reader->lengthInSamples < 64) { error = "Sample is empty"; return false; }
    if (reader->lengthInSamples > (juce::int64) (kMaxSeconds * reader->sampleRate)) { error = "Sample longer than 10 min"; return false; }

    const int chans = (int) juce::jlimit (1u, 2u, reader->numChannels);
    auto buf = std::make_shared<juce::AudioBuffer<float>> (chans, (int) reader->lengthInSamples);
    reader->read (buf.get(), 0, (int) reader->lengthInSamples, 0, true, chans > 1);

    {
        const juce::SpinLock::ScopedLockType sl (renderLock);
        source = buf;
        rendered = buf;
    }
    fileSampleRate = reader->sampleRate;
    sourceFile = file;

    onsetFn = warp::computeOnsetFunction (*source, fileSampleRate);
    settings.sourceBpm = warp::estimateTempo (onsetFn, source->getNumSamples());
    sourceBpmAtomic.store (settings.sourceBpm);
    renderedBpm.store (settings.sourceBpm);
    warped = false;
    redetect();
    renderVersion++;
    return true;
}

void SyncProcessor::clear()
{
    pool.removeAllJobs (true, 5000);
    {
        const juce::SpinLock::ScopedLockType sl (renderLock);
        source.reset();
        rendered.reset();
    }
    sourceFile = juce::File();
    onsets.clear();
    markers.clear();
    onsetFn = {};
    warped = false;
    preview = false;
    renderVersion++;
}

void SyncProcessor::redetect()
{
    if (source == nullptr) return;
    onsets = warp::pickOnsets (onsetFn, sensitivity);
    if (warped) warp();
    else
    {
        markers = warp::identityMarkers (onsets, source->getNumSamples());
        requestRender();
    }
}

double SyncProcessor::effectiveTargetBpm() const
{
    return hostSync ? hostBpm.load() : settings.targetBpm;
}

void SyncProcessor::warp()
{
    if (source == nullptr) return;
    settings.targetBpm = effectiveTargetBpm();
    markers = warp::computeWarp (onsets, source->getNumSamples(), fileSampleRate, settings);
    warped = true;
    lastAutoWarpHostBpm = hostBpm.load();
    requestRender();
}

void SyncProcessor::applyWarpSettingsChange()
{
    if (warped) warp();
}

void SyncProcessor::pollHostTempo()
{
    if (source == nullptr || ! hostSync || ! warped) return;
    const double h = hostBpm.load();
    if (h > 10.0 && h < 999.0 && std::abs (h - settings.targetBpm) > 0.005 && std::abs (h - lastAutoWarpHostBpm) > 0.005)
        warp();
}

// ---- marker editing ---------------------------------------------------------
void SyncProcessor::setMarkerDst (int index, double dst)
{
    if (index <= 0 || index >= (int) markers.size() - 1) return;
    const double gap = 0.004 * fileSampleRate;
    dst = juce::jlimit (markers[(size_t) index - 1].dst + gap, markers[(size_t) index + 1].dst - gap, dst);
    markers[(size_t) index].dst = dst;
    requestRender();
}

void SyncProcessor::addMarkerAtDst (double dst)
{
    if (markers.size() < 2) return;
    const double gap = 0.004 * fileSampleRate;
    const double src = warp::dstToSrc (markers, dst);
    for (size_t i = 0; i + 1 < markers.size(); ++i)
    {
        if (dst > markers[i].dst + gap && dst < markers[i + 1].dst - gap
            && src > markers[i].src + gap && src < markers[i + 1].src - gap)
        {
            markers.insert (markers.begin() + (long) i + 1, { src, dst });
            requestRender();
            return;
        }
    }
}

void SyncProcessor::removeMarker (int index)
{
    if (index <= 0 || index >= (int) markers.size() - 1) return;
    markers.erase (markers.begin() + index);
    requestRender();
}

// ---- settings ---------------------------------------------------------------
void SyncProcessor::setGridDiv (int div)       { settings.gridDiv = div; applyWarpSettingsChange(); }
void SyncProcessor::setAmount (float a)        { settings.amount = juce::jlimit (0.0f, 1.0f, a); applyWarpSettingsChange(); }
void SyncProcessor::setSourceBpm (double bpm)
{
    settings.sourceBpm = juce::jlimit (40.0, 300.0, bpm);
    sourceBpmAtomic.store (settings.sourceBpm);
    if (! warped) renderedBpm.store (settings.sourceBpm);
    applyWarpSettingsChange();
}
void SyncProcessor::setTargetBpm (double bpm)  { settings.targetBpm = juce::jlimit (40.0, 300.0, bpm); applyWarpSettingsChange(); }
void SyncProcessor::setHostSync (bool on)      { hostSync = on; applyWarpSettingsChange(); }
void SyncProcessor::setMode (warp::Mode m)     { mode = m; if (source != nullptr) requestRender(); }
void SyncProcessor::setSensitivity (float s)   { sensitivity = juce::jlimit (0.0f, 1.0f, s); redetect(); }

// ============================================================================
//  Rendering (background thread)
// ============================================================================
void SyncProcessor::requestRender()
{
    if (source == nullptr) return;

    const int gen = ++renderGeneration;
    auto src = source;
    auto mk  = markers;
    auto md  = mode;
    const double sr = fileSampleRate;
    const double bpm = warped ? settings.targetBpm : settings.sourceBpm;

    renderBusy = true;
    pool.addJob ([this, gen, src, mk, md, sr, bpm]
    {
        if (gen != renderGeneration.load()) return;

        bool identity = true;
        for (auto& m : mk) if (std::abs (m.src - m.dst) > 0.5) { identity = false; break; }

        std::shared_ptr<const juce::AudioBuffer<float>> result;
        if (identity) result = src;
        else
        {
            auto out = std::make_shared<juce::AudioBuffer<float>>();
            warp::render (*src, sr, mk, md, *out);
            result = out;
        }

        if (gen != renderGeneration.load()) return;
        {
            const juce::SpinLock::ScopedLockType sl (renderLock);
            rendered = result;
            renderedBpm.store (bpm);
        }
        renderVersion++;
        renderBusy = false;
    });
}

std::shared_ptr<const juce::AudioBuffer<float>> SyncProcessor::getRendered() const
{
    const juce::SpinLock::ScopedLockType sl (renderLock);
    return rendered;
}

// ============================================================================
//  Export
// ============================================================================
bool SyncProcessor::exportTo (const juce::File& file, juce::String& error)
{
    auto buf = getRendered();
    if (buf == nullptr) { error = "Nothing to export"; return false; }

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) { error = "Cannot write file"; return false; }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), fileSampleRate,
                                                                          (unsigned int) buf->getNumChannels(), 24, {}, 0));
    if (writer == nullptr) { error = "Cannot create WAV"; return false; }
    stream.release();

    if (! writer->writeFromAudioSampleBuffer (*buf, 0, buf->getNumSamples())) { error = "Write failed"; return false; }
    return true;
}

juce::File SyncProcessor::renderTempFileForDrag()
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SYNC");
    dir.createDirectory();
    const double bpm = warped ? settings.targetBpm : settings.sourceBpm;
    auto name = (sourceFile.existsAsFile() ? sourceFile.getFileNameWithoutExtension() : juce::String ("sample"))
                + "_SYNC_" + juce::String (bpm, std::abs (bpm - std::round (bpm)) < 1.0e-6 ? 0 : 2) + "bpm.wav";
    auto f = dir.getChildFile (name);
    juce::String err;
    return exportTo (f, err) ? f : juce::File();
}

// ============================================================================
//  State (saved inside the FL Studio project)
// ============================================================================
void SyncProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("SYNC");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("file", sourceFile.getFullPathName());
    xml.setAttribute ("srcBpm", settings.sourceBpm);
    xml.setAttribute ("tgtBpm", settings.targetBpm);
    xml.setAttribute ("grid", settings.gridDiv);
    xml.setAttribute ("amount", (double) settings.amount);
    xml.setAttribute ("mode", mode == warp::Mode::Beats ? 0 : 1);
    xml.setAttribute ("sens", (double) sensitivity);
    xml.setAttribute ("hostSync", hostSync);
    xml.setAttribute ("warped", warped);
    xml.setAttribute ("listenB", listenB.load());

    auto* mk = xml.createNewChildElement ("MARKERS");
    for (auto& m : markers)
    {
        auto* e = mk->createNewChildElement ("M");
        e->setAttribute ("s", m.src);
        e->setAttribute ("d", m.dst);
    }

    if (auto params = apvts.copyState().createXml())
        xml.addChildElement (params.release());

    copyXmlToBinary (xml, dest);
}

void SyncProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("SYNC")) return;

    if (auto* p = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*p));

    settings.gridDiv = xml->getIntAttribute ("grid", 16);
    settings.amount  = (float) xml->getDoubleAttribute ("amount", 1.0);
    settings.targetBpm = xml->getDoubleAttribute ("tgtBpm", 120.0);
    mode = xml->getIntAttribute ("mode", 0) == 0 ? warp::Mode::Beats : warp::Mode::Flow;
    sensitivity = (float) xml->getDoubleAttribute ("sens", 0.55);
    hostSync = xml->getBoolAttribute ("hostSync", true);
    listenB = xml->getBoolAttribute ("listenB", true);

    const juce::File f (xml->getStringAttribute ("file"));
    juce::String err;
    if (f.existsAsFile() && loadFile (f, err))
    {
        settings.sourceBpm = xml->getDoubleAttribute ("srcBpm", settings.sourceBpm);
        sourceBpmAtomic.store (settings.sourceBpm);

        std::vector<warp::Marker> saved;
        if (auto* mk = xml->getChildByName ("MARKERS"))
            for (auto* e : mk->getChildIterator())
                saved.push_back ({ e->getDoubleAttribute ("s"), e->getDoubleAttribute ("d") });

        warped = xml->getBoolAttribute ("warped", false);
        if (saved.size() >= 2) markers = saved;
        lastAutoWarpHostBpm = settings.targetBpm;
        renderedBpm.store (warped ? settings.targetBpm : settings.sourceBpm);
        requestRender();
    }
}

juce::AudioProcessorEditor* SyncProcessor::createEditor() { return new SyncEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SyncProcessor(); }
