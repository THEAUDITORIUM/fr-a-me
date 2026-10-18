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
    setColour (juce::TooltipWindow::outlineColourId, Palette::ink);
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

//==============================================================================
void drawThemeIcon (juce::Graphics& g, Theme t, juce::Rectangle<float> r, bool selected)
{
    const auto c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight());

    if (selected)
    {
        g.setColour (Palette::accent);
        g.fillEllipse (r.withSizeKeepingCentre (s, s));
    }
    g.setColour (selected ? Palette::onAccent : Palette::graphite);
    const float lw = 1.0f;

    switch (t)
    {
        case Theme::light:      // soleil : disque + 8 rayons
        {
            const float rad = s * 0.18f;
            g.drawEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, lw);
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                g.drawLine (c.x + std::cos (a) * s * 0.27f, c.y + std::sin (a) * s * 0.27f,
                            c.x + std::cos (a) * s * 0.38f, c.y + std::sin (a) * s * 0.38f, lw);
            }
            break;
        }
        case Theme::dark:       // lune : croissant
        {
            juce::Path moon;
            const float rad = s * 0.30f;
            moon.addEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
            juce::Path cut;
            cut.addEllipse (c.x - rad * 0.45f, c.y - rad * 1.15f, rad * 2.0f, rad * 2.0f);
            moon.setUsingNonZeroWinding (false);
            moon.addPath (cut);
            g.saveState();
            g.reduceClipRegion (moon);                  // croissant = disque privé du second disque
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
            g.restoreState();
            break;
        }
        case Theme::garden:     // petit arbre : tronc + feuillage au trait
        {
            g.drawLine (c.x, c.y + s * 0.36f, c.x, c.y - s * 0.02f, lw);
            g.drawLine (c.x, c.y + s * 0.12f, c.x - s * 0.12f, c.y + s * 0.02f, lw * 0.8f);
            g.drawLine (c.x, c.y + s * 0.06f, c.x + s * 0.13f, c.y - s * 0.04f, lw * 0.8f);
            g.drawEllipse (c.x - s * 0.24f, c.y - s * 0.38f, s * 0.48f, s * 0.40f, lw);
            g.drawEllipse (c.x - s * 0.08f, c.y - s * 0.26f, s * 0.16f, s * 0.14f, lw * 0.7f);
            break;
        }
    }
}

void drawGardenDecor (juce::Graphics& g, juce::Rectangle<float> page)
{
    // Grain de papier (déterministe : même rendu à chaque image).
    juce::Random rng (0x6a4d3e);
    for (int i = 0; i < 2600; ++i)
    {
        const float x = page.getX() + rng.nextFloat() * page.getWidth();
        const float y = page.getY() + rng.nextFloat() * page.getHeight();
        const bool green = rng.nextFloat() < 0.35f;
        g.setColour ((green ? juce::Colour (0xff6c8f3a) : juce::Colour (0xff8a8572)).withAlpha (0.05f + 0.07f * rng.nextFloat()));
        g.fillRect (x, y, 1.0f + rng.nextFloat(), 1.0f);
    }

    // Aplats de collage végétal (papiers déchirés, verts mousse), discrets,
    // dans les marges de la planche.
    auto tornRect = [&rng] (juce::Rectangle<float> r)
    {
        juce::Path p;
        const int steps = 18;
        auto jitter = [&rng] { return (rng.nextFloat() - 0.5f) * 2.2f; };
        p.startNewSubPath (r.getX(), r.getY());
        for (int i = 1; i <= steps; ++i) p.lineTo (r.getX() + r.getWidth() * (float) i / steps, r.getY() + jitter());
        for (int i = 1; i <= steps; ++i) p.lineTo (r.getRight() + jitter(), r.getY() + r.getHeight() * (float) i / steps);
        for (int i = 1; i <= steps; ++i) p.lineTo (r.getRight() - r.getWidth() * (float) i / steps, r.getBottom() + jitter());
        for (int i = 1; i <= steps; ++i) p.lineTo (r.getX() + jitter(), r.getBottom() - r.getHeight() * (float) i / steps);
        p.closeSubPath();
        return p;
    };
    const juce::Colour mosses[] = { juce::Colour (0xff7f9f4a), juce::Colour (0xff5d7a3a), juce::Colour (0xffb7c98a), juce::Colour (0xff9db86a) };
    const juce::Rectangle<float> blocks[] = {
        { 14.0f, 120.0f, 46.0f, 150.0f }, { 22.0f, 300.0f, 34.0f, 90.0f }, { 940.0f, 140.0f, 42.0f, 120.0f },
        { 936.0f, 470.0f, 50.0f, 100.0f }, { 18.0f, 470.0f, 40.0f, 70.0f } };
    int k = 0;
    for (auto b : blocks)
    {
        const auto path = tornRect (b);
        g.setColour (mosses[k % 4].withAlpha (0.28f));
        g.fillPath (path);
        // texture d'herbe : petits traits obliques
        g.saveState();
        g.reduceClipRegion (path);
        for (int i = 0; i < 90; ++i)
        {
            const float x = b.getX() + rng.nextFloat() * b.getWidth();
            const float y = b.getY() + rng.nextFloat() * b.getHeight();
            g.setColour (mosses[(k + 1 + i) % 4].withAlpha (0.35f));
            g.drawLine (x, y, x + (rng.nextFloat() - 0.3f) * 4.0f, y - 3.0f - rng.nextFloat() * 4.0f, 0.7f);
        }
        g.restoreState();
        ++k;
    }

    // Brin dessiné au crayon, en haut à gauche (comme les croquis des planches).
    juce::Path stem;
    stem.startNewSubPath (196.0f, 64.0f);
    stem.cubicTo (200.0f, 48.0f, 206.0f, 34.0f, 218.0f, 22.0f);
    g.setColour (juce::Colour (0xff5d7f34).withAlpha (0.75f));
    g.strokePath (stem, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto leaf = [&g] (float x, float y, float angle, float len)
    {
        juce::Path l;
        l.startNewSubPath (0.0f, 0.0f);
        l.quadraticTo (len * 0.5f, -len * 0.35f, len, 0.0f);
        l.quadraticTo (len * 0.5f, len * 0.35f, 0.0f, 0.0f);
        g.strokePath (l, juce::PathStrokeType (0.9f), juce::AffineTransform::rotation (angle).translated (x, y));
    };
    leaf (199.0f, 50.0f, -2.6f, 11.0f);
    leaf (203.0f, 40.0f, -0.5f, 10.0f);
    leaf (209.0f, 31.0f, -2.4f, 9.0f);
    leaf (215.0f, 25.0f, -0.8f, 8.0f);
}
