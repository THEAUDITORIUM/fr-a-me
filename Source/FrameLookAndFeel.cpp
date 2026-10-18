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
        case Theme::floral:     // fleur : 5 pétales au trait + cœur
        {
            const float pr = s * 0.14f, dist = s * 0.17f;
            for (int i = 0; i < 5; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 5.0f - juce::MathConstants<float>::halfPi;
                juce::Path petal;
                petal.addEllipse (-pr * 0.75f, -pr * 1.15f, pr * 1.5f, pr * 2.3f);
                g.strokePath (petal, juce::PathStrokeType (lw * 0.9f),
                              juce::AffineTransform::rotation (a + juce::MathConstants<float>::halfPi)
                                  .translated (c.x + std::cos (a) * dist, c.y + std::sin (a) * dist));
            }
            g.fillEllipse (c.x - s * 0.06f, c.y - s * 0.06f, s * 0.12f, s * 0.12f);
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

namespace
{
    // Papier déchiré : rectangle aux bords irréguliers.
    juce::Path tornRect (juce::Rectangle<float> r, juce::Random& rng)
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
    }

    // Emplacements des papiers collés dans les marges (communs aux deux thèmes).
    const juce::Rectangle<float> collageBlocks[] = {
        { 14.0f, 120.0f, 46.0f, 150.0f }, { 22.0f, 300.0f, 34.0f, 90.0f }, { 940.0f, 140.0f, 42.0f, 120.0f },
        { 936.0f, 470.0f, 50.0f, 100.0f }, { 18.0f, 470.0f, 40.0f, 70.0f } };

    void paperGrain (juce::Graphics& g, juce::Rectangle<float> page, juce::Random& rng, juce::Colour tint, juce::Colour neutral)
    {
        for (int i = 0; i < 1200; ++i)                       // grain léger : le fond reste blanc
        {
            const float x = page.getX() + rng.nextFloat() * page.getWidth();
            const float y = page.getY() + rng.nextFloat() * page.getHeight();
            const bool tinted = rng.nextFloat() < 0.35f;
            g.setColour ((tinted ? tint : neutral).withAlpha (0.03f + 0.04f * rng.nextFloat()));
            g.fillRect (x, y, 1.0f + rng.nextFloat(), 1.0f);
        }
    }
}

namespace
{
    // Brin d'herbe couché par le vent : courbe partant du pied (x, y), penchée vers la droite.
    void grassBlade (juce::Graphics& g, float x, float y, float height, float lean, float width)
    {
        juce::Path p;
        p.startNewSubPath (x, y);
        p.quadraticTo (x + lean * 0.25f, y - height * 0.6f, x + lean, y - height);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// Décors volontairement sobres : fond blanc dominant, trois touches de couleur
// dans les marges, un petit croquis au crayon en haut à gauche.
namespace { constexpr int decorBlocks[] = { 0, 2, 4 }; }   // haut gauche, haut droite, bas gauche

void drawGardenDecor (juce::Graphics& g, juce::Rectangle<float> page)
{
    juce::Random rng (0x6a4d3e);
    paperGrain (g, page, rng, juce::Colour (0xff6c8f3a), juce::Colour (0xff8a8572));

    const juce::Colour greens[] = { juce::Colour (0xff9db86a), juce::Colour (0xff7f9f4a), juce::Colour (0xffc3d4a0) };
    int k = 0;
    for (int bi : decorBlocks)
    {
        const auto b = collageBlocks[bi];
        const auto path = tornRect (b, rng);
        g.setColour (greens[k % 3].withAlpha (0.18f));
        g.fillPath (path);
        // quelques herbes couchées par le vent
        g.saveState();
        g.reduceClipRegion (path);
        for (int i = 0; i < 22; ++i)
        {
            const float x = b.getX() - 6.0f + rng.nextFloat() * (b.getWidth() + 6.0f);
            const float y = b.getY() + 10.0f + rng.nextFloat() * b.getHeight();
            const float h = 12.0f + rng.nextFloat() * 14.0f;
            g.setColour (greens[(k + 1 + i) % 3].darker (0.2f).withAlpha (0.35f));
            grassBlade (g, x, y, h, h * (0.6f + 0.5f * rng.nextFloat()), 0.6f);
        }
        // quelques fleurs des champs roses, très discrètes
        for (int f = 0; f < 3; ++f)
        {
            const float fx = b.getX() + 6.0f + rng.nextFloat() * (b.getWidth() - 12.0f);
            const float fy = b.getY() + 10.0f + rng.nextFloat() * (b.getHeight() - 20.0f);
            g.setColour (juce::Colour (0xffe8a3b6).withAlpha (0.55f));
            g.fillEllipse (fx - 1.6f, fy - 1.6f, 3.2f, 3.2f);
        }
        g.restoreState();
        ++k;
    }

    // Brin dessiné au crayon, en haut à gauche (un bouton rose au bout).
    juce::Path stem;
    stem.startNewSubPath (196.0f, 64.0f);
    stem.cubicTo (200.0f, 48.0f, 206.0f, 34.0f, 218.0f, 22.0f);
    g.setColour (juce::Colour (0xff6c8f3a).withAlpha (0.7f));
    g.strokePath (stem, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    auto leaf = [&g] (float x, float y, float angle, float len)
    {
        juce::Path l;
        l.startNewSubPath (0.0f, 0.0f);
        l.quadraticTo (len * 0.5f, -len * 0.35f, len, 0.0f);
        l.quadraticTo (len * 0.5f, len * 0.35f, 0.0f, 0.0f);
        g.strokePath (l, juce::PathStrokeType (0.85f), juce::AffineTransform::rotation (angle).translated (x, y));
    };
    leaf (199.0f, 50.0f, -2.6f, 11.0f);
    leaf (203.0f, 40.0f, -0.5f, 10.0f);
    leaf (209.0f, 31.0f, -2.4f, 9.0f);
    leaf (215.0f, 25.0f, -0.8f, 8.0f);
    g.setColour (juce::Colour (0xffe8a3b6).withAlpha (0.8f));
    g.fillEllipse (216.0f, 18.5f, 4.5f, 4.5f);
}

void drawFloralDecor (juce::Graphics& g, juce::Rectangle<float> page)
{
    juce::Random rng (0x7e7a15);
    paperGrain (g, page, rng, juce::Colour (0xffc9a3ad), juce::Colour (0xff8a8572));   // grain neutre : fond blanc

    const juce::Colour roses[] = { juce::Colour (0xffeeb0c0), juce::Colour (0xfff4c9d3), juce::Colour (0xffe59bb0) };
    const juce::Colour veinInk (0xffcf6a88);
    int k = 0;
    for (int bi : decorBlocks)
    {
        // Un pétale par emplacement : forme douce, quelques nervures fines.
        const auto b = collageBlocks[bi];
        const auto c = b.getCentre();
        const float len = juce::jmin (b.getHeight() * 0.40f, 44.0f), wid = b.getWidth() * 0.44f;
        const auto tr = juce::AffineTransform::rotation ((rng.nextFloat() - 0.5f) * 0.7f).translated (c.x, c.y);

        juce::Path petal;
        petal.startNewSubPath (0.0f, len);
        petal.cubicTo (-wid * 1.2f, len * 0.4f, -wid * 1.1f, -len * 0.9f, -wid * 0.1f, -len);
        petal.quadraticTo (0.0f, -len * 0.88f, wid * 0.15f, -len * 1.02f);
        petal.cubicTo (wid * 1.15f, -len * 0.85f, wid * 1.2f, len * 0.35f, 0.0f, len);
        petal.closeSubPath();

        g.setColour (roses[k % 3].withAlpha (0.38f));
        g.fillPath (petal, tr);

        g.saveState();
        g.reduceClipRegion (petal, tr);
        g.setColour (veinInk.withAlpha (0.22f));
        for (int v = 0; v < 5; ++v)
        {
            const float t = ((float) v / 4.0f - 0.5f) * 1.4f;
            juce::Path vein;
            vein.startNewSubPath (0.0f, len * 0.95f);
            vein.quadraticTo (t * wid * 0.5f, 0.0f, t * wid * 1.1f, -len);
            g.strokePath (vein, juce::PathStrokeType (0.45f), tr);
        }
        g.restoreState();
        ++k;
    }

    // Petite fleur dessinée au crayon, en haut à gauche.
    const juce::Point<float> fc (206.0f, 36.0f);
    g.setColour (juce::Colour (0xff8fae6a).withAlpha (0.8f));                 // tige et feuille vert tendre
    juce::Path stem;
    stem.startNewSubPath (fc.x - 2.0f, 66.0f);
    stem.quadraticTo (fc.x - 6.0f, 52.0f, fc.x, fc.y + 6.0f);
    g.strokePath (stem, juce::PathStrokeType (0.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path lf;
    lf.startNewSubPath (0.0f, 0.0f);
    lf.quadraticTo (5.0f, -3.5f, 10.0f, 0.0f);
    lf.quadraticTo (5.0f, 3.5f, 0.0f, 0.0f);
    g.strokePath (lf, juce::PathStrokeType (0.8f), juce::AffineTransform::rotation (-0.6f).translated (fc.x - 4.0f, 56.0f));
    g.setColour (juce::Colour (0xffd0708e).withAlpha (0.75f));
    for (int i = 0; i < 5; ++i)
    {
        juce::Path pp;
        pp.addEllipse (-3.2f, -8.5f, 6.4f, 9.0f);
        g.strokePath (pp, juce::PathStrokeType (0.85f),
                      juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * (float) i / 5.0f).translated (fc.x, fc.y));
    }
    g.fillEllipse (fc.x - 1.6f, fc.y - 1.6f, 3.2f, 3.2f);
}
