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
            BinaryData::PoppinsLight_ttf, BinaryData::PoppinsLight_ttfSize);
        return tf;
    }

    juce::Typeface::Ptr regularFace()
    {
        static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
            BinaryData::PoppinsRegular_ttf, BinaryData::PoppinsRegular_ttfSize);
        return tf;
    }
}

FrameLookAndFeel::FrameLookAndFeel()
{
    refreshColours();
    setDefaultSansSerifTypeface (lightFace());
}

void FrameLookAndFeel::refreshColours()
{
    setColour (juce::ResizableWindow::backgroundColourId, Palette::paper);
    setColour (juce::Slider::textBoxTextColourId, Palette::ink);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Palette::ink);
    setColour (juce::TooltipWindow::backgroundColourId, Palette::paper);
    setColour (juce::TooltipWindow::textColourId, Palette::ink);
    setColour (juce::TooltipWindow::outlineColourId, Palette::pencil);
}

juce::Font FrameLookAndFeel::mono (float height, bool regularWeight, float tracking)
{
    auto face = regularWeight ? regularFace() : lightFace();
    return juce::Font (juce::FontOptions (face).withHeight (height).withKerningFactor (tracking));
}

//==============================================================================
//  Relief
//==============================================================================
namespace Relief
{
    void raised (juce::Graphics& g, const juce::Path& p, float depth, bool pressed)
    {
        const auto b = p.getBounds();
        const int blur = juce::jmax (2, juce::roundToInt (depth * (pressed ? 1.0f : 2.2f)));
        const int off  = juce::jmax (1, juce::roundToInt (depth * (pressed ? 0.4f : 0.8f)));
        // ombre portée (bas droite) puis lumière (haut gauche)
        juce::DropShadow (Palette::shade.withAlpha (pressed ? 0.35f : 0.55f), blur, { off, off }).drawForPath (g, p);
        juce::DropShadow (Palette::light.withAlpha (0.95f), blur, { -off, -off }).drawForPath (g, p);
        // surface : très léger dégradé (plus claire vers la lumière)
        if (pressed)
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe4e7eb), b.getX(), b.getY(),
                                                     juce::Colour (0xffeef0f3), b.getRight(), b.getBottom(), false));
        else
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff3f4f6), b.getX(), b.getY(),
                                                     juce::Colour (0xffe3e6ea), b.getRight(), b.getBottom(), false));
        g.fillPath (p);
        g.setColour (Palette::light.withAlpha (0.55f));
        g.strokePath (p, juce::PathStrokeType (0.8f));
    }

    void inset (juce::Graphics& g, const juce::Path& p, float depth)
    {
        const auto b = p.getBounds();
        g.setColour (juce::Colour (0xffe4e7eb));
        g.fillPath (p);

        // ombre intérieure : le « reste du monde » projette son ombre dans le creux
        juce::Path outside;
        outside.addRectangle (b.expanded (depth * 6.0f + 8.0f));
        outside.addPath (p);
        outside.setUsingNonZeroWinding (false);

        const int blur = juce::jmax (2, juce::roundToInt (depth * 1.8f));
        const int off  = juce::jmax (1, juce::roundToInt (depth * 0.7f));
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (p);
        juce::DropShadow (Palette::shade.withAlpha (0.6f), blur, { off, off }).drawForPath (g, outside);
        juce::DropShadow (Palette::light.withAlpha (0.95f), blur, { -off, -off }).drawForPath (g, outside);
    }

    void raised (juce::Graphics& g, juce::Rectangle<float> r, float radius, float depth, bool pressed)
    {
        juce::Path p; p.addRoundedRectangle (r, radius); raised (g, p, depth, pressed);
    }
    void inset (juce::Graphics& g, juce::Rectangle<float> r, float radius, float depth)
    {
        juce::Path p; p.addRoundedRectangle (r, radius); inset (g, p, depth);
    }
    void raisedEllipse (juce::Graphics& g, juce::Rectangle<float> r, float depth, bool pressed)
    {
        juce::Path p; p.addEllipse (r); raised (g, p, depth, pressed);
    }
    void insetEllipse (juce::Graphics& g, juce::Rectangle<float> r, float depth)
    {
        juce::Path p; p.addEllipse (r); inset (g, p, depth);
    }
}

void drawPureBackground (juce::Graphics& g, juce::Rectangle<float> page)
{
    // Lumière au centre (un peu au-dessus), qui s'éteint doucement vers les bords.
    g.setColour (Palette::paper);
    g.fillRect (page);
    const auto c = juce::Point<float> (page.getCentreX(), page.getY() + page.getHeight() * 0.42f);
    juce::ColourGradient glow (juce::Colour (0xfff7f8f9), c.x, c.y,
                               juce::Colour (0xffd9dce1), c.x + page.getWidth() * 0.78f, c.y, true);
    glow.addColour (0.45, juce::Colour (0xffeef0f2));
    g.setGradientFill (glow);
    g.fillRect (page);

    // Grain fin, clair et sombre, comme un papier mat photographié.
    juce::Random rng (0x5eed);
    const int dots = (int) (page.getWidth() * page.getHeight() * 0.07f);
    for (int i = 0; i < dots; ++i)
    {
        const float x = page.getX() + rng.nextFloat() * page.getWidth();
        const float y = page.getY() + rng.nextFloat() * page.getHeight();
        const bool dark = rng.nextBool();
        g.setColour (dark ? juce::Colour (0xff59606b).withAlpha (0.018f + 0.022f * rng.nextFloat())
                          : juce::Colours::white.withAlpha (0.05f + 0.07f * rng.nextFloat()));
        g.fillRect (x, y, 1.0f, 1.0f);
    }
}

//==============================================================================
void FrameLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float pos, float startAngle, float endAngle, juce::Slider&)
{
    // Bouton sculpté : rainure creusée, arc fin de valeur dans la rainure,
    // disque en saillie au milieu, index en petite fossette.
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float depth = juce::jmax (2.0f, outer * 0.09f);

    // rainure (anneau en creux)
    const float grooveR = outer - depth * 0.5f;
    Relief::insetEllipse (g, juce::Rectangle<float> (grooveR * 2.0f, grooveR * 2.0f).withCentre (c), depth * 0.6f);

    // arc de valeur dans la rainure
    const float groove = juce::jmax (3.0f, outer * 0.12f);
    const float arcR = grooveR - groove * 0.5f - 1.0f;
    const float arcW = juce::jmax (1.5f, groove * 0.35f);
    {
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Palette::faint.darker (0.04f));
        g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (Palette::accentInk);
        g.strokePath (arc, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // disque en saillie
    const float knobR = grooveR - groove - 1.0f;
    Relief::raisedEllipse (g, juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (c), depth);

    // index : petite fossette
    const float dotR = juce::jmax (2.5f, knobR * 0.11f);
    const float ir = knobR - dotR * 2.4f;
    const juce::Point<float> tip (c.x + ir * std::sin (angle), c.y - ir * std::cos (angle));
    Relief::insetEllipse (g, juce::Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (tip), juce::jmax (1.0f, dotR * 0.5f));
    g.setColour (Palette::accentInk.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (dotR * 0.9f, dotR * 0.9f).withCentre (tip));
}

//==============================================================================
void drawMonoText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   float height, juce::Justification just, juce::Colour colour,
                   bool regularWeight, float tracking)
{
    g.setColour (colour);
    g.setFont (FrameLookAndFeel::mono (height * 0.92f, regularWeight, juce::jmax (0.16f, tracking)));
    g.drawText (text.toUpperCase(), area, just, false);
}
