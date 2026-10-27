#include "PluginEditor.h"
#include "BinaryData.h"

// ============================================================================
//  Black & white palette
// ============================================================================
namespace col
{
    const juce::Colour bg        { 0xff0b0b0b };
    const juce::Colour panel     { 0xff161616 };
    const juce::Colour header    { 0xff1f1f1f };
    const juce::Colour boxFill   { 0xff222222 };
    const juce::Colour boxHover  { 0xff2d2d2d };
    const juce::Colour border    { 0xff323232 };
    const juce::Colour text      { 0xffeaeaea };
    const juce::Colour dim       { 0xff8c8c8c };
    const juce::Colour faint     { 0xff4c4c4c };
    const juce::Colour waveBg    { 0xff101010 };
    const juce::Colour ruler     { 0xff1a1a1a };
    const juce::Colour strip     { 0xff141414 };
    const juce::Colour beatLine  { 0xff1a1a1a };
    const juce::Colour barLine   { 0xff2c2c2c };
    const juce::Colour wave      { 0xffdedede };
    const juce::Colour white     { 0xffffffff };
    const juce::Colour black     { 0xff000000 };
}

static constexpr int kW = 920, kH = 240;
static const int kGridDivs[] = { 4, 8, 16, 32 };
static constexpr float kSens[] = { 0.25f, 0.55f, 0.85f };
static const char* kSensNames[] = { "Soft", "Medium", "Hard" };

static juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// ============================================================================
SyncEditor::SyncEditor (SyncProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    medium  = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf,  BinaryData::InterMedium_ttfSize);
    setSize (kW, kH);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

SyncEditor::~SyncEditor() { stopTimer(); }

// ---- drawing helpers ---------------------------------------------------------
juce::Font SyncEditor::font (float size, bool med) const
{
    auto tf = med ? medium : regular;
    if (tf != nullptr) return juce::Font (juce::FontOptions (tf).withHeight (size));
    return juce::Font (juce::FontOptions (size));
}

void SyncEditor::text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, float size,
                       juce::Colour c, bool med, juce::Justification j) const
{
    g.setColour (c);
    g.setFont (font (size, med));
    g.drawText (s, r, j, true);
}

void SyncEditor::box (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& s, Hit h,
                      bool on, bool enabled, juce::Justification j) const
{
    const auto rf = r.toFloat();
    const bool hot = enabled && (hover == h || pressed == h);
    g.setColour (on ? col::white : (hot ? col::boxHover : col::boxFill));
    g.fillRoundedRectangle (rf, 2.0f);
    g.setColour (on ? col::white : col::border);
    g.drawRoundedRectangle (rf.reduced (0.5f), 2.0f, 1.0f);
    const auto tc = ! enabled ? col::faint : (on ? col::black : col::text);
    text (g, s, rf.reduced (6.0f, 0.0f), 11.5f, tc, on, j);
}

void SyncEditor::label (juce::Graphics& g, juce::Rectangle<int> b, const juce::String& s) const
{
    text (g, s, juce::Rectangle<float> ((float) b.getX(), (float) b.getY() - 14.0f, (float) b.getWidth(), 13.0f),
          10.5f, col::dim);
}

// ============================================================================
//  Layout — modelled on Ableton's clip view
// ============================================================================
void SyncEditor::resized()
{
    rTitle = { 0, 0, kW, 30 };
    rPanel = { 8, 34, 310, kH - 42 };
    rName  = { 18, 44, 290, 20 };

    const int ax = 18, bx = 116, cx = 214, cw = 94, w = 90;

    // column A — file
    rImport = { ax, 74,  w, 20 };
    rExport = { ax, 98,  w, 20 };
    rA      = { ax, 122, w / 2, 20 };
    rB      = { ax + w / 2, 122, w - w / 2, 20 };
    rDrag   = { ax, 150, w, 70 };

    // column B — warp
    rWarp   = { bx, 74,  w, 20 };
    rSource = { bx, 114, w, 20 };
    rHalf   = { bx, 138, w / 2 - 2, 18 };
    rDouble = { bx + w / 2 + 2, 138, w / 2 - 2, 18 };
    rMode   = { bx, 162, w, 20 };
    rTarget = { bx, 200, w, 20 };
    rHost   = { bx + w - 30, 186, 30, 13 };     // small "FL" reset link next to the Target label

    // column C — grid
    rGrid    = { cx, 88,  cw, 20 };
    rAmount  = { cx, 126, cw, 20 };
    rDetect  = { cx, 164, cw, 20 };
    rPreview = { cx, 200, cw, 20 };

    // right side
    rRuler = { 326, 34, kW - 326 - 8, 18 };
    rStrip = { 326, rRuler.getBottom(), rRuler.getWidth(), 16 };
    rWave  = { 326, rStrip.getBottom(), rRuler.getWidth(), rPanel.getBottom() - rStrip.getBottom() };
}

// ============================================================================
//  Geometry
// ============================================================================
double SyncEditor::totalDst() const
{
    const auto& m = proc.getMarkers();
    if (m.size() >= 2) return juce::jmax (1.0, m.back().dst);
    if (auto r = proc.getRendered()) return (double) juce::jmax (1, r->getNumSamples());
    return 1.0;
}

float SyncEditor::xForDst (double dst) const
{
    return (float) rWave.getX() + (float) (dst / totalDst()) * (float) rWave.getWidth();
}

double SyncEditor::dstForX (float x) const
{
    return juce::jlimit (0.0, totalDst(), (double) (x - (float) rWave.getX()) / (double) rWave.getWidth() * totalDst());
}

double SyncEditor::samplesPerBeatOut() const
{
    const auto& s = proc.getSettings();
    const double bpm = proc.isWarped() ? s.targetBpm : s.sourceBpm;
    return 60.0 / juce::jmax (1.0, bpm) * proc.getFileSampleRate();
}

void SyncEditor::rebuildPeaks()
{
    pkMin.clear(); pkMax.clear();
    auto r = proc.getRendered();
    if (r == nullptr || r->getNumSamples() == 0) return;

    const int cols = rWave.getWidth();
    const double total = totalDst();
    peaksTotal = total;
    pkMin.assign ((size_t) cols, 0.0f);
    pkMax.assign ((size_t) cols, 0.0f);

    float mx = 1.0e-6f;
    for (int c = 0; c < cols; ++c)
    {
        const int a = juce::jmin (r->getNumSamples() - 1, (int) (total * c / cols));
        const int b = juce::jmax (a + 1, juce::jmin (r->getNumSamples(), (int) (total * (c + 1) / cols)));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < r->getNumChannels(); ++ch)
        {
            auto* d = r->getReadPointer (ch);
            for (int i = a; i < b; ++i) { lo = juce::jmin (lo, d[i]); hi = juce::jmax (hi, d[i]); }
        }
        pkMin[(size_t) c] = lo; pkMax[(size_t) c] = hi;
        mx = juce::jmax (mx, juce::jmax (hi, -lo));
    }
    for (size_t c = 0; c < pkMin.size(); ++c) { pkMin[c] /= mx; pkMax[c] /= mx; }
}

// ============================================================================
//  Paint
// ============================================================================
void SyncEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    const bool has = proc.hasSample();
    const auto& st = proc.getSettings();

    // ---- title --------------------------------------------------------------------
    {
        g.setFont (font (13.0f, true).withExtraKerningFactor (0.35f));
        g.setColour (col::text);
        g.drawText ("SYNC", rTitle, juce::Justification::centred, false);
    }

    // ---- left panel ------------------------------------------------------------
    g.setColour (col::panel);
    g.fillRect (rPanel);

    // file name (draggable to FL when a sample is loaded)
    {
        const auto rf = rName.toFloat();
        g.setColour (hover == Hit::Name && has ? col::boxHover : col::boxFill);
        g.fillRoundedRectangle (rf, 2.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (rf.reduced (0.5f), 2.0f, 1.0f);
        const juce::String n = has ? proc.getFile().getFileName() : utf8 ("No sample  \xe2\x80\x94  drop a file");
        text (g, n, rf.reduced (7.0f, 0.0f).withTrimmedRight (has ? 70.0f : 0.0f), 11.5f, has ? col::text : col::dim);
        if (has)
            text (g, juce::String (proc.getFileSampleRate() / 1000.0, 1) + " kHz", rf.reduced (7.0f, 0.0f), 10.5f, col::dim,
                  false, juce::Justification::centredRight);
    }

    // column A
    box (g, rImport, "Import", Hit::Import);
    box (g, rExport, "Export", Hit::Export, false, has);
    box (g, rA, "A", Hit::A, ! proc.listenB.load(), has);
    box (g, rB, "B", Hit::B,   proc.listenB.load(), has);

    {   // drag-out tile
        const auto rf = rDrag.toFloat();
        const bool hot = has && (hover == Hit::Drag || pressed == Hit::Drag);
        g.setColour (hot ? col::boxHover : col::boxFill);
        g.fillRoundedRectangle (rf, 2.0f);
        g.setColour (hot ? col::text : col::border);
        const float dl[] = { 3.0f, 3.0f };
        juce::Path outline; outline.addRoundedRectangle (rf.reduced (0.5f), 2.0f);
        juce::Path dashed; juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dl, 2);
        g.fillPath (dashed);

        const auto c = has ? col::text : col::faint;
        g.setColour (c);
        const float gx = rf.getCentreX(), gy = rf.getY() + 20.0f;
        juce::Path arrow;   // arrow pointing out of the plugin
        arrow.startNewSubPath (gx - 7.0f, gy + 7.0f);
        arrow.lineTo (gx + 7.0f, gy - 7.0f);
        arrow.startNewSubPath (gx, gy - 7.0f);
        arrow.lineTo (gx + 7.0f, gy - 7.0f);
        arrow.lineTo (gx + 7.0f, gy);
        g.strokePath (arrow, juce::PathStrokeType (1.4f));
        text (g, "Drag to FL", juce::Rectangle<float> (rf.getX(), rf.getY() + 34.0f, rf.getWidth(), 14.0f), 11.5f, c, true, juce::Justification::centred);
        text (g, "playlist", juce::Rectangle<float> (rf.getX(), rf.getY() + 48.0f, rf.getWidth(), 12.0f), 10.5f, has ? col::dim : col::faint, false, juce::Justification::centred);
    }

    // column B
    box (g, rWarp, "Warp", Hit::Warp, proc.isWarped(), has);
    label (g, rSource, "Seg. BPM");
    box (g, rSource, has ? juce::String (st.sourceBpm, 2) : utf8 ("\xe2\x80\x94"), Hit::Source, false, has);
    box (g, rHalf, ":2", Hit::Half, false, has);
    box (g, rDouble, "*2", Hit::Double, false, has);
    box (g, rMode, (proc.getMode() == warp::Mode::Beats ? "Beats" : "Flow") + utf8 ("  \xe2\x96\xbe"), Hit::Mode);
    label (g, rTarget, "Target");
    text (g, proc.isHostSync() ? "FL" : utf8 ("\xe2\x86\xba FL"), rHost.toFloat(), 10.5f,
          proc.isHostSync() ? col::faint : (hover == Hit::Host ? col::white : col::dim), proc.isHostSync() ? false : true,
          juce::Justification::centredRight);
    {
        const double t = proc.isHostSync() ? proc.getHostBpm() : st.targetBpm;
        box (g, rTarget, juce::String (t, 2), Hit::Target);
    }

    // column C
    label (g, rGrid, "Quantise");
    box (g, rGrid, "1/" + juce::String (st.gridDiv), Hit::Grid);
    label (g, rAmount, "Amount");
    {
        const auto rf = rAmount.toFloat();
        box (g, rAmount, {}, Hit::Amount);
        g.setColour (col::faint.withAlpha (0.6f));
        g.fillRoundedRectangle (rf.reduced (1.0f).withWidth ((rf.getWidth() - 2.0f) * st.amount), 1.5f);
        text (g, juce::String (juce::roundToInt (st.amount * 100.0f)) + " %", rf, 11.5f, col::text, false, juce::Justification::centred);
    }
    label (g, rDetect, "Detect");
    {
        const float s = proc.getSensitivity();
        const int sel = s < 0.4f ? 0 : (s < 0.7f ? 1 : 2);
        box (g, rDetect, kSensNames[sel], Hit::Detect);
    }
    box (g, rPreview, utf8 ("\xe2\x96\xb6  Preview"), Hit::Preview, proc.preview.load(), has);

    // ---- right: ruler / marker strip / waveform --------------------------------
    g.setColour (col::ruler);   g.fillRect (rRuler);
    g.setColour (col::strip);   g.fillRect (rStrip);
    g.setColour (col::waveBg);  g.fillRect (rWave);

    const double total = totalDst();
    const double spb = samplesPerBeatOut();

    if (has && spb > 0.0 && total / spb < 600.0)
    {
        int beat = 0;
        const bool showBeats = rWave.getWidth() / (total / spb) > 6.0;
        for (double d = 0.0; d < total - 1.0; d += spb, ++beat)
        {
            const float x = std::round (xForDst (d));
            const bool bar = beat % 4 == 0;
            if (! bar && ! showBeats) continue;
            g.setColour (bar ? col::barLine : col::beatLine);
            g.fillRect (x, (float) rWave.getY(), 1.0f, (float) rWave.getHeight());
            g.setColour (bar ? col::dim : col::faint);
            g.fillRect (x, (float) rRuler.getBottom() - (bar ? 9.0f : 4.0f), 1.0f, bar ? 9.0f : 4.0f);
            if (bar)
                text (g, juce::String (beat / 4 + 1), juce::Rectangle<float> (x + 3.0f, (float) rRuler.getY(), 30.0f, 14.0f), 10.5f, col::dim);
        }
    }

    const float cy = (float) rWave.getCentreY();
    const float hh = rWave.getHeight() * 0.5f - 12.0f;

    if (! has)
    {
        g.setColour (col::faint);
        g.fillRect ((float) rWave.getX(), cy, (float) rWave.getWidth(), 1.0f);
        text (g, dragHover ? "Release to load" : "Drop a sample here", rWave.toFloat().withTrimmedBottom (40.0f), 13.0f,
              dragHover ? col::text : col::dim, false, juce::Justification::centred);
        if (juce::Time::getMillisecondCounterHiRes() < statusUntil)
            text (g, status.replace ("\n", "  "), rWave.toFloat().reduced (8.0f, 6.0f), 11.0f, col::dim, false,
                  juce::Justification::bottomLeft);
    }
    else
    {
        // waveform
        g.setColour (col::wave);
        for (size_t c = 0; c < pkMax.size(); ++c)
        {
            const float x = (float) rWave.getX() + (float) c;
            const float top = cy - pkMax[c] * hh, bot = cy - pkMin[c] * hh;
            g.fillRect (x, top, 1.0f, juce::jmax (1.0f, bot - top));
        }

        // warp markers: flag in the strip + hairline through the wave
        const auto& m = proc.getMarkers();
        for (int i = 1; i + 1 < (int) m.size(); ++i)
        {
            const float x = std::round (xForDst (m[(size_t) i].dst)) + 0.5f;
            const bool active = (i == hoverMarker && (hover == Hit::Marker)) || i == dragMarker;

            g.setColour (active ? col::white.withAlpha (0.8f) : col::white.withAlpha (0.12f));
            g.fillRect (x - 0.5f, (float) rWave.getY(), 1.0f, (float) rWave.getHeight());

            juce::Path flag;
            const float t = (float) rStrip.getY() + 2.0f;
            flag.startNewSubPath (x - 4.5f, t);
            flag.lineTo (x + 4.5f, t);
            flag.lineTo (x + 4.5f, t + 7.0f);
            flag.lineTo (x, t + 12.0f);
            flag.lineTo (x - 4.5f, t + 7.0f);
            flag.closeSubPath();
            g.setColour (active ? col::white : juce::Colour (0xffa8a8a8));
            g.fillPath (flag);
            g.setColour (col::black);
            g.strokePath (flag, juce::PathStrokeType (0.8f));
        }

        // playhead
        const double ph = proc.getPlayheadDst();
        if (ph >= 0.0)
        {
            const double d = proc.listenB.load() ? ph : warp::srcToDst (m, ph);
            const float x = xForDst (d);
            g.setColour (col::white);
            g.fillRect (x - 0.5f, (float) rStrip.getY(), 1.5f, (float) (rWave.getBottom() - rStrip.getY()));
        }

        if (dragHover)
        {
            g.setColour (col::white.withAlpha (0.05f));
            g.fillRect (rWave);
        }

        // transient messages (export, load errors) — bottom-left, fades away
        if (juce::Time::getMillisecondCounterHiRes() < statusUntil)
            text (g, status.replace ("\n", "  "), rWave.toFloat().reduced (8.0f, 6.0f), 11.0f, col::dim, false,
                  juce::Justification::bottomLeft);

        // corner readout, like Ableton's zoom/grid indicator
        text (g, "1/" + juce::String (st.gridDiv), rWave.toFloat().reduced (8.0f, 6.0f), 13.0f, col::faint, true,
              juce::Justification::bottomRight);
    }
}

// ============================================================================
//  Help text (bottom of the left panel)
// ============================================================================
juce::String SyncEditor::helpFor (Hit h) const
{
    switch (h)
    {
        case Hit::Name:    return proc.hasSample() ? "Drag the warped sample into FL" : "Drop a sample anywhere";
        case Hit::Import:  return "Load a sample (wav, aif, flac, mp3, ogg)\nor drop it anywhere on the plugin";
        case Hit::Export:  return "Save the warped sample as a 24-bit WAV";
        case Hit::Drag:    return "Click and drag into the FL playlist\nor the channel rack";
        case Hit::A:       return "Listen to the original";
        case Hit::B:       return "Listen to the warped result";
        case Hit::Warp:    return "Lock the sample to the tempo and the grid";
        case Hit::Source:  return "Original tempo (detected)\nDrag up/down, double-click to type";
        case Hit::Half:    return "Halve the original tempo";
        case Hit::Double:  return "Double the original tempo";
        case Hit::Mode:    return "Beats: slices, no artefacts (drums)\nFlow: smooth stretch (melodies, vocals)";
        case Hit::Target:  return proc.isHostSync() ? "Follows FL Studio's tempo\nTurn off 'Follow FL tempo' to set it"
                                                    : "Tempo to warp to\nDrag up/down, double-click to type";
        case Hit::Grid:    return "Grid the hits snap to\nClick: next  " + utf8 ("\xc2\xb7") + "  right-click: previous";
        case Hit::Amount:  return "How hard hits are pulled to the grid\nDrag up/down, double-click = 100 %";
        case Hit::Detect:  return "Transient detection sensitivity\nClick to change";
        case Hit::Host:    return "Warp to FL's tempo (re-warps\nautomatically when the tempo changes)";
        case Hit::Preview: return "Loop the result while FL is stopped";
        case Hit::Strip:
        case Hit::Wave:    return "Double-click: add a warp marker";
        case Hit::Marker:  return "Drag to move (Shift = off grid)\nDouble-click to delete";
        case Hit::None:    break;
    }
    return {};
}

void SyncEditor::setStatus (const juce::String& s, double seconds)
{
    status = s;
    statusUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    repaint();
}

// ============================================================================
//  Hit testing
// ============================================================================
SyncEditor::Hit SyncEditor::hitTest (juce::Point<int> p, int& markerIndex) const
{
    markerIndex = -1;
    const bool has = proc.hasSample();

    if (has && (rStrip.contains (p) || rWave.contains (p)))
    {
        const auto& m = proc.getMarkers();
        float best = rStrip.contains (p) ? 6.0f : 4.0f;
        for (int i = 1; i + 1 < (int) m.size(); ++i)
        {
            const float d = std::abs (xForDst (m[(size_t) i].dst) - (float) p.x);
            if (d < best) { best = d; markerIndex = i; }
        }
        if (markerIndex >= 0) return Hit::Marker;
        return rStrip.contains (p) ? Hit::Strip : Hit::Wave;
    }

    const std::pair<juce::Rectangle<int>, Hit> boxes[] = {
        { rName, Hit::Name }, { rImport, Hit::Import }, { rExport, Hit::Export }, { rDrag, Hit::Drag },
        { rA, Hit::A }, { rB, Hit::B }, { rWarp, Hit::Warp }, { rSource, Hit::Source },
        { rHalf, Hit::Half }, { rDouble, Hit::Double }, { rMode, Hit::Mode }, { rTarget, Hit::Target },
        { rGrid, Hit::Grid }, { rAmount, Hit::Amount }, { rDetect, Hit::Detect },
        { rPreview, Hit::Preview }, { rHost, Hit::Host } };

    for (auto& [r, h] : boxes)
    {
        if (! r.contains (p)) continue;
        const bool needsSample = h == Hit::Name || h == Hit::Export || h == Hit::Drag || h == Hit::A || h == Hit::B
                              || h == Hit::Warp || h == Hit::Source || h == Hit::Half || h == Hit::Double || h == Hit::Preview;
        if (needsSample && ! has) return Hit::None;
        if (h == Hit::Host && proc.isHostSync()) return Hit::None;
        return h;
    }
    return Hit::None;
}

// ============================================================================
//  Mouse
// ============================================================================
void SyncEditor::mouseMove (const juce::MouseEvent& e)
{
    int mi = -1;
    const Hit h = hitTest (e.getPosition(), mi);
    if (h == hover && mi == hoverMarker) return;

    hover = h; hoverMarker = mi;
    switch (h)
    {
        case Hit::Marker:  setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Hit::Source: case Hit::Amount:
                           setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
        case Hit::Target:  setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
        case Hit::Drag: case Hit::Name:
                           setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case Hit::None: case Hit::Wave: case Hit::Strip:
                           setMouseCursor (juce::MouseCursor::NormalCursor); break;
        default:           setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
    }
    repaint();
}

void SyncEditor::mouseExit (const juce::MouseEvent&)
{
    hover = Hit::None; hoverMarker = -1;
    repaint();
}

void SyncEditor::mouseDown (const juce::MouseEvent& e)
{
    if (textEdit != nullptr) { textEdit.reset(); textEditTarget = Hit::None; }

    int mi = -1;
    pressed = hitTest (e.getPosition(), mi);
    downPos = e.getPosition();
    externalDragStarted = false;
    const auto& st = proc.getSettings();

    switch (pressed)
    {
        case Hit::Marker:  dragMarker = mi; break;
        case Hit::Amount:  dragStartValue = st.amount; break;
        case Hit::Source:  dragStartValue = st.sourceBpm; break;
        case Hit::Target:  dragStartValue = proc.isHostSync() ? proc.getHostBpm() : st.targetBpm; break;

        case Hit::Import:  openImport(); break;
        case Hit::A:       proc.listenB = false; break;
        case Hit::B:       proc.listenB = true;  break;
        case Hit::Warp:
            proc.warp();
            setStatus ("Warped to " + juce::String (proc.getSettings().targetBpm, 2) + " bpm  " + utf8 ("\xc2\xb7") + "  1/" + juce::String (st.gridDiv), 2.0);
            break;
        case Hit::Half:    proc.setSourceBpm (st.sourceBpm * 0.5); break;
        case Hit::Double:  proc.setSourceBpm (st.sourceBpm * 2.0); break;
        case Hit::Mode:    proc.setMode (proc.getMode() == warp::Mode::Beats ? warp::Mode::Flow : warp::Mode::Beats); break;
        case Hit::Grid:
        {
            int idx = 0;
            for (int i = 0; i < 4; ++i) if (kGridDivs[i] == st.gridDiv) idx = i;
            idx = (idx + (e.mods.isPopupMenu() || e.mods.isShiftDown() ? 3 : 1)) % 4;
            proc.setGridDiv (kGridDivs[idx]);
            break;
        }
        case Hit::Detect:
        {
            const float s = proc.getSensitivity();
            const int sel = s < 0.4f ? 0 : (s < 0.7f ? 1 : 2);
            proc.setSensitivity (kSens[(sel + (e.mods.isPopupMenu() ? 2 : 1)) % 3]);
            break;
        }
        case Hit::Host:    proc.setHostSync (true); break;
        case Hit::Preview: proc.preview = ! proc.preview.load(); break;

        case Hit::Export: case Hit::Drag: case Hit::Name:   // resolved on drag / mouse-up
        case Hit::Strip: case Hit::Wave: case Hit::None:
            break;
    }
    repaint();
}

void SyncEditor::startExternalDrag()
{
    if (externalDragStarted || ! proc.hasSample()) return;
    externalDragStarted = true;
    auto f = proc.renderTempFileForDrag();
    if (! f.existsAsFile()) { setStatus ("Could not prepare the file"); return; }
    setStatus ("Drop it in the FL playlist", 2.0);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void SyncEditor::mouseDrag (const juce::MouseEvent& e)
{
    const int dy = downPos.y - e.getPosition().y;
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;

    switch (pressed)
    {
        case Hit::Marker:
        {
            if (dragMarker < 0) break;
            double dst = dstForX ((float) e.getPosition().x);
            if (proc.isWarped() && ! e.mods.isShiftDown())
            {
                const double grid = samplesPerBeatOut() * 4.0 / proc.getSettings().gridDiv;
                dst = std::round (dst / grid) * grid;
            }
            proc.setMarkerDst (dragMarker, dst);
            break;
        }
        case Hit::Amount: proc.setAmount ((float) dragStartValue + dy * 0.01f * fine); break;
        case Hit::Source: proc.setSourceBpm (dragStartValue + dy * 0.1 * fine); break;
        case Hit::Target:
            if (dy != 0)
            {
                if (proc.isHostSync()) proc.setHostSync (false);
                proc.setTargetBpm (dragStartValue + dy * 0.1 * fine);
            }
            break;
        case Hit::Drag: case Hit::Name: case Hit::Export:
            if (e.getDistanceFromDragStart() > 4) startExternalDrag();
            break;
        default: break;
    }
    repaint();
}

void SyncEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! externalDragStarted && e.getDistanceFromDragStart() < 5)
    {
        if (pressed == Hit::Export) openExport();
        if (pressed == Hit::Drag)   setStatus ("Hold the click and drag into the FL playlist", 2.5);
    }

    pressed = Hit::None;
    dragMarker = -1;
    hover = Hit::None;
    mouseMove (e);
    repaint();
}

void SyncEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    int mi = -1;
    switch (hitTest (e.getPosition(), mi))
    {
        case Hit::Marker: proc.removeMarker (mi); break;
        case Hit::Strip:
        case Hit::Wave:   proc.addMarkerAtDst (dstForX ((float) e.getPosition().x)); break;
        case Hit::Source: beginTextEdit (Hit::Source); break;
        case Hit::Target: beginTextEdit (Hit::Target); break;
        case Hit::Amount: proc.setAmount (1.0f); break;
        default: break;
    }
    repaint();
}

// ============================================================================
//  Inline BPM entry
// ============================================================================
void SyncEditor::beginTextEdit (Hit which)
{
    textEditTarget = which;
    textEdit = std::make_unique<juce::TextEditor>();
    auto& t = *textEdit;
    t.setBounds (which == Hit::Source ? rSource : rTarget);
    t.setFont (font (11.5f));
    t.setColour (juce::TextEditor::backgroundColourId, col::white);
    t.setColour (juce::TextEditor::textColourId, col::black);
    t.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    t.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    t.setColour (juce::TextEditor::highlightColourId, juce::Colour (0xffbbbbbb));
    t.setColour (juce::CaretComponent::caretColourId, col::black);
    t.setInputRestrictions (7, "0123456789.,");
    t.setJustification (juce::Justification::centred);
    const auto& st = proc.getSettings();
    t.setText (juce::String (which == Hit::Source ? st.sourceBpm
                                                  : (proc.isHostSync() ? proc.getHostBpm() : st.targetBpm), 2), false);
    t.selectAll();

    auto close = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SyncEditor> (this)]
        {
            if (safe != nullptr) { safe->textEdit.reset(); safe->textEditTarget = Hit::None; safe->repaint(); }
        });
    };
    t.onReturnKey = [this, close]
    {
        const double v = textEdit->getText().replaceCharacter (',', '.').getDoubleValue();
        if (v >= 40.0 && v <= 300.0)
        {
            if (textEditTarget == Hit::Source) proc.setSourceBpm (v);
            else { proc.setHostSync (false); proc.setTargetBpm (v); }
        }
        close();
    };
    t.onEscapeKey = close;
    t.onFocusLost = close;

    addAndMakeVisible (t);
    t.grabKeyboardFocus();
}

// ============================================================================
//  Files
// ============================================================================
static bool isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg");
}

bool SyncEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (externalDragStarted) return false; // don't re-import our own drag
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}

void SyncEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    for (auto& f : files)
        if (isAudioFile (f)) { loadFile (juce::File (f)); break; }
    repaint();
}

void SyncEditor::loadFile (const juce::File& f)
{
    juce::String err;
    if (proc.loadFile (f, err))
    {
        peaksVersion = -1;
        setStatus (f.getFileName() + "\n" + juce::String (proc.getSettings().sourceBpm, 2) + " bpm detected", 3.0);
    }
    else setStatus ("Could not load\n" + err, 4.0);
}

void SyncEditor::openImport()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SyncEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f.existsAsFile()) safe->loadFile (f);
    });
}

void SyncEditor::openExport()
{
    if (! proc.hasSample()) return;
    const auto& st = proc.getSettings();
    const double bpm = proc.isWarped() ? st.targetBpm : st.sourceBpm;
    const auto name = proc.getFile().getFileNameWithoutExtension() + "_SYNC_"
                      + juce::String (bpm, std::abs (bpm - std::round (bpm)) < 1.0e-6 ? 0 : 2) + "bpm.wav";
    const auto start = proc.getFile().getParentDirectory().getChildFile (name);

    chooser = std::make_unique<juce::FileChooser> ("Export warped sample", start, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<SyncEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension ("wav");
        juce::String err;
        if (safe->proc.exportTo (f, err)) safe->setStatus ("Exported\n" + f.getFileName(), 3.5);
        else                              safe->setStatus ("Export failed\n" + err, 4.0);
    });
}

// ============================================================================
//  Timer
// ============================================================================
void SyncEditor::timerCallback()
{
    bool dirty = false;

    if (proc.getRenderVersion() != peaksVersion || (proc.hasSample() && std::abs (totalDst() - peaksTotal) > 1.0))
    {
        peaksVersion = proc.getRenderVersion();
        rebuildPeaks();
        dirty = true;
    }

    const double ph = proc.getPlayheadDst();
    if (std::abs (ph - lastPlayhead) > 1.0e-9) { lastPlayhead = ph; dirty = true; }
    if (std::abs (proc.getHostBpm() - lastHostBpm) > 1.0e-6) { lastHostBpm = proc.getHostBpm(); dirty = true; }
    if (statusUntil > 0.0 && juce::Time::getMillisecondCounterHiRes() > statusUntil) { statusUntil = 0.0; dirty = true; }
    if (proc.isRendering()) dirty = true;

    if (dirty) repaint();
}
