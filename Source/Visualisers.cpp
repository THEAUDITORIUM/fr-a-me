/*
  ==============================================================================
    fr(a)me — Visualisers.cpp
  ==============================================================================
*/

#include "Visualisers.h"
#include "FrameLookAndFeel.h"
#include "Scale.h"

//==============================================================================
//  WaveBand
//==============================================================================
void WaveBand::push (const WaveSlice& s) noexcept
{
    if (source == Source::input) { lo[(size_t) writePos] = s.inMin;  hi[(size_t) writePos] = s.inMax; }
    else                         { lo[(size_t) writePos] = s.outMin; hi[(size_t) writePos] = s.outMax; }
    writePos = (writePos + 1) % historySize;
}

void WaveBand::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float mid = b.getCentreY();
    const float half = b.getHeight() * 0.5f;
    const float colW = b.getWidth() / (float) historySize;

    // axe central très fin
    g.setColour (Palette::faint);
    g.drawHorizontalLine ((int) mid, b.getX(), b.getRight());

    // Légère compression (exposant 0.7) : les passages calmes restent lisibles,
    // comme sur les planches de référence.
    auto shape = [] (float v) { return std::copysign (std::pow (juce::jmin (1.0f, std::abs (v)), 0.7f), v); };

    g.setColour (source == Source::input ? Palette::ink : Palette::accent);

    juce::RectangleList<float> bars;
    bars.ensureStorageAllocated (historySize);
    for (int i = 0; i < historySize; ++i)
    {
        const int idx = (writePos + i) % historySize;   // le plus ancien à gauche
        const float top = mid - shape (hi[(size_t) idx]) * half;
        const float bot = mid - shape (lo[(size_t) idx]) * half;
        bars.addWithoutMerging ({ b.getX() + (float) i * colW, top, juce::jmax (colW, 0.8f), juce::jmax (0.6f, bot - top) });
    }
    g.fillRectList (bars);
}

//==============================================================================
//  SpectrogramBand
//==============================================================================
SpectrogramBand::SpectrogramBand()
    : levels ((size_t) (columns * rows), 0.0f),
      grain  ((size_t) (columns * rows), 1.0f),
      image  (juce::Image::RGB, columns, rows, true)
{
    setInterceptsMouseClicks (false, false);

    // Grain fixe : chaque pixel a son propre "poids d'encre", ce qui donne le
    // rendu granuleux d'une impression plutôt qu'un dégradé lisse.
    juce::Random rng (0x5eed);
    for (auto& v : grain)
        v = 0.72f + 0.5f * rng.nextFloat();

    image.clear (image.getBounds(), Palette::paper);
}

void SpectrogramBand::setScaleMask (uint16_t newMask)
{
    if (newMask == mask)
        return;
    mask = newMask;

    // Une ligne est "dans la gamme" si sa fréquence centrale est à moins de
    // ±30 cents d'une note de la gamme.
    for (int r = 0; r < rows; ++r)
    {
        const float hz = FreqAxis::fromNorm (((float) r + 0.5f) / (float) rows);
        const double midi = frame::hzToMidi (hz);
        rowScale[(size_t) r] = std::abs (midi - frame::nearestScaleMidi (midi, mask)) < 0.30;
    }

    for (int c = 0; c < columns; ++c)   // re-colorie l'historique
        renderColumn (c);
    repaint();
}

void SpectrogramBand::push (const SpectralFrameInfo& frame)
{
    for (int r = 0; r < rows; ++r)
        levels[(size_t) (writeCol * rows + r)] = frame.spectrum[r];
    renderColumn (writeCol);
    writeCol = (writeCol + 1) % columns;
}

void SpectrogramBand::renderColumn (int col)
{
    static const juce::Colour scaleInk (0xffc8b52c);          // encre jaune des fréquences de la gamme
    static const juce::Colour scalePaper = Palette::paper.interpolatedWith (Palette::accent, 0.10f);

    for (int r = 0; r < rows; ++r)
    {
        const size_t i = (size_t) (col * rows + r);
        const float v = juce::jlimit (0.0f, 1.0f, std::pow (levels[i], 1.6f) * 1.15f * grain[i]);
        const bool inScale = rowScale[(size_t) r];

        const auto base = inScale ? scalePaper : Palette::paper;
        const auto inkC = inScale ? scaleInk : Palette::ink;
        image.setPixelAt (col, rows - 1 - r, base.interpolatedWith (inkC, v));   // aigu en haut
    }
}

void SpectrogramBand::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);   // pixels nets : grain d'impression

    // Deux morceaux : du plus ancien (writeCol) à la fin, puis du début à writeCol.
    const float colW = b.getWidth() / (float) columns;
    const int firstPart = columns - writeCol;

    g.drawImage (image, b.getX(), b.getY(), (float) firstPart * colW, b.getHeight(),
                 writeCol, 0, firstPart, rows);
    if (writeCol > 0)
        g.drawImage (image, b.getX() + (float) firstPart * colW, b.getY(), (float) writeCol * colW, b.getHeight(),
                     0, 0, writeCol, rows);

    // Cadre fin
    g.setColour (Palette::pencil);
    g.drawRect (b, 0.6f);
}

//==============================================================================
//  PartialScore
//==============================================================================
namespace { constexpr float minLevel = 0.38f; }    // seuil de lisibilité des partiels

void PartialScore::push (const SpectralFrameInfo& frame) noexcept
{
    if ((decimate++ & 1) != 0)
        return;

    auto& col = history[(size_t) writeCol];
    col.count = 0;

    for (int i = 0; i < frame.numPeaks && col.count < peaksPerColumn; ++i)
    {
        if (frame.peakLevel[i] < minLevel)
            continue;                                   // on ne garde que les partiels lisibles
        col.y[col.count]       = FreqAxis::toNorm (frame.peakFreq[i]);
        col.level[col.count]   = frame.peakLevel[i];
        col.snapped[col.count] = frame.peakSnapped[i];
        ++col.count;
    }
    writeCol = (writeCol + 1) % columns;
}

void PartialScore::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Palette::scoreBlack);
    g.fillRect (b);

    const float colW = b.getWidth() / (float) columns;
    const float padY = 6.0f;
    const float h = b.getHeight() - 2.0f * padY;

    for (int i = 0; i < columns; ++i)
    {
        const auto& col = history[(size_t) ((writeCol + i) % columns)];
        const float x = b.getX() + ((float) i + 0.5f) * colW;

        for (int p = 0; p < col.count; ++p)
        {
            const float lvl = (col.level[p] - minLevel) / (1.0f - minLevel);   // 0..1
            const float y = b.getBottom() - padY - col.y[p] * h;
            const float r = 0.8f + lvl * lvl * 3.6f;                    // taille ∝ amplitude

            g.setColour (col.snapped[p] ? Palette::accent : juce::Colours::white);

            if (lvl > 0.35f)                                          // ronds pleins
                g.fillEllipse (x - r, y - r, 2.0f * r, 2.0f * r);
            else if (lvl > 0.15f)                                     // anneaux
                g.drawEllipse (x - r, y - r, 2.0f * r, 2.0f * r, 0.6f);
            else                                                      // petits carrés
                g.fillRect (x - 0.6f, y - 0.6f, 1.2f, 1.2f);
        }
    }
}
