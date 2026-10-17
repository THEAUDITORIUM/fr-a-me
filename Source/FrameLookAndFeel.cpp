/*
  ==============================================================================
    fr(a)me — FrameLookAndFeel.cpp
  ==============================================================================
*/

#include "FrameLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    juce::Typeface::Ptr lightFace()
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::IBMPlexMonoLight_ttf, BinaryData::IBMPlexMonoLight_ttfSize);
        return tf;
    }

    juce::Typeface::Ptr regularFace()
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
        return tf;
    }
}

FrameLookAndFeel::FrameLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::paper);
    setColour (juce::Slider::textBoxTextColourId, Palette::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Palette::ink);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::paper);
    setColour (juce::TooltipWindow::textColourId, Palette::ink);
    setColour (juce::TooltipWindow::outlineColourId, Palette::ink);
    setDefaultSansSerifTypeface (lightFace());
}

juce::Font FrameLookAndFeel::mono (float height, bool regularWeight, float tracking)
{
    auto face = regularWeight ? regularFace() : lightFace();
    return juce::Font (juce::FontOptions (face).withHeight (height).withKerningFactor (tracking));
}

void FrameLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float pos, float startAngle, float endAngle, juce::Slider&)
{
    // Potentiomètre minimaliste : cercle au trait fin, graduations d'instrument,
    // arc et index jaunes.
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 6.0f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Graduations : 21 traits, un plus long tous les 5 (0, 25, 50, 75, 100 %).
    for (int i = 0; i <= 20; ++i)
    {
        const float a = startAngle + (float) i / 20.0f * (endAngle - startAngle);
        const bool major = (i % 5) == 0;
        const float r0 = radius + 2.5f, r1 = radius + (major ? 7.0f : 4.5f);
        g.setColour (major ? Palette::ink : Palette::pencil);
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a),
                    c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), major ? 0.9f : 0.6f);
    }

    // Arc de valeur (jaune, sous le trait noir).
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, angle, true);
    g.setColour (Palette::accent);
    g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    // Cercle au trait fin.
    g.setColour (Palette::ink);
    g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Index : trait noir du centre vers le bord + pastille jaune.
    const juce::Point<float> tip (c.x + (radius - 1.0f) * std::sin (angle), c.y - (radius - 1.0f) * std::cos (angle));
    g.drawLine (c.x, c.y, tip.x, tip.y, 1.0f);
    g.setColour (Palette::accentInk);
    g.fillEllipse (tip.x - 3.0f, tip.y - 3.0f, 6.0f, 6.0f);
    g.setColour (Palette::ink);
    g.fillEllipse (c.x - 1.5f, c.y - 1.5f, 3.0f, 3.0f);
}

//==============================================================================
void drawMonoText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   float height, juce::Justification just, juce::Colour colour,
                   bool regularWeight, float tracking)
{
    g.setColour (colour);
    g.setFont (FrameLookAndFeel::mono (height, regularWeight, tracking));
    g.drawText (text.toLowerCase(), area, just, false);
}

void drawCallout (juce::Graphics& g, const juce::String& label, juce::Point<float> labelPos,
                  juce::Point<float> target, bool labelOnLeft)
{
    const auto font = FrameLookAndFeel::mono (10.0f);
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, label.toLowerCase(), 0.0f, 0.0f);
    const float textW = ga.getBoundingBox (0, -1, true).getWidth();

    // étiquette
    const float textX = labelOnLeft ? labelPos.x : labelPos.x - textW;
    drawMonoText (g, label, { textX, labelPos.y - 7.0f, textW + 4.0f, 14.0f }, 10.0f,
                  juce::Justification::centredLeft, Palette::ink);

    // trait horizontal puis vertical
    const float lineStart = labelOnLeft ? textX + textW + 8.0f : textX - 8.0f;
    g.setColour (Palette::ink);
    g.drawLine (lineStart, labelPos.y, target.x, labelPos.y, 0.7f);
    g.drawLine (target.x, labelPos.y, target.x, target.y, 0.7f);
}
