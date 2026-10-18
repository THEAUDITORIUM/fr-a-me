/*
  ==============================================================================
    fr(a)me — Visualisers.h

    Les quatre bandes d'analyse, empilées comme sur une planche :
      • WaveBand        : forme d'onde (entrée en noir, sortie en jaune)
      • SpectrogramBand : spectrogramme défilant, encre et grain, les
                          fréquences de la gamme surlignées en jaune
      • PartialScore    : "partition graphique" noire, un rond par pic
                          détecté, jaune s'il a été recalé sur une note

    Toutes sont nourries par l'éditeur (thread UI) à partir des FIFO sans
    verrou du processeur. Elles ne touchent jamais au thread audio.
  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>
#include "PluginProcessor.h"

/** Axe fréquentiel logarithmique commun au spectrogramme et à la partition. */
namespace FreqAxis
{
    inline float toNorm (float hz) noexcept
    {
        const float lo = SpectralEngine::displayMinHz, hi = SpectralEngine::displayMaxHz;
        return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (hz, 1.0f) / lo) / std::log (hi / lo));
    }
    inline float fromNorm (float t) noexcept
    {
        const float lo = SpectralEngine::displayMinHz, hi = SpectralEngine::displayMaxHz;
        return lo * std::pow (hi / lo, t);
    }
}

//==============================================================================
class WaveBand final : public juce::Component
{
public:
    enum class Source { input, output };

    static constexpr int historySize = 860;      // une colonne par tranche

    explicit WaveBand (Source s) : source (s) { setInterceptsMouseClicks (false, false); }

    void push (const WaveSlice& slice) noexcept;
    void paint (juce::Graphics&) override;

private:
    Source source;
    std::array<float, historySize> lo {}, hi {};
    int writePos = 0;
};

//==============================================================================
class SpectrogramBand final : public juce::Component
{
public:
    static constexpr int columns = 430;           // deux tranches de forme d'onde par trame
    static constexpr int rows = SpectralFrameInfo::numSpectrumRows;

    SpectrogramBand();

    void push (const SpectralFrameInfo& frame);
    void setScaleMask (uint16_t mask);
    void refreshTheme();
    void paint (juce::Graphics&) override;

private:
    void renderColumn (int col);
    bool rowInScale (int row) const noexcept { return rowScale[(size_t) row]; }

    std::vector<float> levels;                    // columns × rows
    std::vector<float> grain;                     // texture fixe (grain d'encre)
    std::array<bool, rows> rowScale {};
    juce::Image image;
    int writeCol = 0;
    uint16_t mask = 0;
};

//==============================================================================
class PartialScore final : public juce::Component
{
public:
    static constexpr int columns = 215;           // une colonne pour deux trames : des points espacés
    static constexpr int peaksPerColumn = 14;

    PartialScore() { setInterceptsMouseClicks (false, false); }

    void push (const SpectralFrameInfo& frame) noexcept;
    void paint (juce::Graphics&) override;

private:
    struct Column
    {
        int count = 0;
        float y[peaksPerColumn] {};               // position normalisée 0 (grave) → 1 (aigu)
        float level[peaksPerColumn] {};
        bool snapped[peaksPerColumn] {};
    };

    std::array<Column, columns> history {};
    int writeCol = 0;
    int decimate = 0;
};
