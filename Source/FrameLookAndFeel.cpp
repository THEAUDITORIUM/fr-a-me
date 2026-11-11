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
    // Pas de rebord : la forme naît de la matière. Ombres très larges et
    // légères, surface de la même teinte que le fond (à peine plus claire vers
    // la lumière), aucun trait de contour.
    void raised (juce::Graphics& g, const juce::Path& p, float depth, bool pressed)
    {
        const auto b = p.getBounds();
        const float d = pressed ? depth * 0.45f : depth;
        const int blurFar  = juce::jmax (3, juce::roundToInt (d * 4.0f));
        const int blurNear = juce::jmax (2, juce::roundToInt (d * 1.6f));
        const int off = juce::jmax (1, juce::roundToInt (d * 1.1f));
        // ombre lointaine très douce + ombre proche (bas droite), lumière (haut gauche)
        juce::DropShadow (Palette::shade.withAlpha (0.30f), blurFar,  { off, off + 1 }).drawForPath (g, p);
        juce::DropShadow (Palette::shade.withAlpha (0.22f), blurNear, { off / 2, off / 2 }).drawForPath (g, p);
        juce::DropShadow (Palette::light.withAlpha (1.0f),  blurFar,  { -off, -off }).drawForPath (g, p);
        // surface : légère convexité (claire en haut à gauche)
        g.setGradientFill (juce::ColourGradient (juce::Colour (pressed ? 0xffe8eaed : 0xfff4f5f7), b.getX(), b.getY(),
                                                 juce::Colour (pressed ? 0xffeef0f2 : 0xffe5e8ec), b.getRight(), b.getBottom(), false));
        g.fillPath (p);
        // reflet diffus au sommet de la forme (pas de contour)
        if (! pressed)
        {
            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.55f), b.getX() + b.getWidth() * 0.3f, b.getY() + b.getHeight() * 0.25f,
                                        juce::Colours::white.withAlpha (0.0f), b.getX() + b.getWidth() * 0.3f + b.getWidth() * 0.55f,
                                        b.getY() + b.getHeight() * 0.25f, true);
            g.setGradientFill (sheen);
            g.fillPath (p);
        }
    }

    void inset (juce::Graphics& g, const juce::Path& p, float depth)
    {
        const auto b = p.getBounds();
        juce::Path outside;
        outside.addRectangle (b.expanded (depth * 8.0f + 10.0f));
        outside.addPath (p);
        outside.setUsingNonZeroWinding (false);

        const int blur = juce::jmax (3, juce::roundToInt (depth * 3.0f));
        const int off  = juce::jmax (1, juce::roundToInt (depth * 0.9f));
        // lèvre : la matière se soulève très légèrement autour du creux (lumière
        // en bas à droite, comme sur une empreinte pressée), sans bord net
        juce::DropShadow (Palette::light.withAlpha (0.55f), juce::jmax (3, juce::roundToInt (depth * 2.4f)), { off / 2, off / 2 }).drawForPath (g, p);
        // fond du creux (plus sombre côté lumière : le bord le cache), ombre intérieure
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe1e4e8), b.getX(), b.getY(),
                                                 juce::Colour (0xffeceef1), b.getRight(), b.getBottom(), false));
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (p);
            g.fillPath (p);
            juce::DropShadow (Palette::shade.withAlpha (0.55f), blur, { off, off }).drawForPath (g, outside);
            juce::DropShadow (Palette::light.withAlpha (0.9f), blur, { -off, -off }).drawForPath (g, outside);
        }
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
    juce::ColourGradient glow (juce::Colour (0xfff8f9fa), c.x, c.y,
                               juce::Colour (0xffd5d9de), c.x + page.getWidth() * 0.8f, c.y, true);
    glow.addColour (0.5, juce::Colour (0xffebedf0));
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
