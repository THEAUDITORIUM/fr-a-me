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
        for (int i = 0; i < 2600; ++i)
        {
            const float x = page.getX() + rng.nextFloat() * page.getWidth();
            const float y = page.getY() + rng.nextFloat() * page.getHeight();
            const bool tinted = rng.nextFloat() < 0.35f;
            g.setColour ((tinted ? tint : neutral).withAlpha (0.05f + 0.07f * rng.nextFloat()));
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

    // Touffe d'herbes : n brins depuis une petite zone, tous penchés par le même vent.
    void grassTuft (juce::Graphics& g, juce::Random& rng, float x, float y, float spread, float height, int n, const juce::Colour* cols, int numCols)
    {
        for (int i = 0; i < n; ++i)
        {
            const float bx = x + (rng.nextFloat() - 0.5f) * spread;
            const float h = height * (0.45f + 0.55f * rng.nextFloat());
            g.setColour (cols[rng.nextInt (numCols)].withAlpha (0.35f + 0.35f * rng.nextFloat()));
            grassBlade (g, bx, y, h, h * (0.35f + 0.5f * rng.nextFloat()), 0.6f + 0.5f * rng.nextFloat());
        }
    }

    // Forme organique (lavis, pétale) : contour au rayon bruité.
    juce::Path blob (juce::Point<float> c, float rx, float ry, juce::Random& rng, float roughness, int points = 40)
    {
        juce::Path p;
        float phase1 = rng.nextFloat() * 6.28f, phase2 = rng.nextFloat() * 6.28f;
        for (int i = 0; i <= points; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / (float) points;
            const float k = 1.0f + roughness * (0.55f * std::sin (3.0f * a + phase1) + 0.35f * std::sin (5.0f * a + phase2)
                                               + 0.25f * (rng.nextFloat() - 0.5f));
            const juce::Point<float> q (c.x + std::cos (a) * rx * k, c.y + std::sin (a) * ry * k);
            if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        p.closeSubPath();
        return p;
    }
}

void drawGardenDecor (juce::Graphics& g, juce::Rectangle<float> page)
{
    // Prairie : grain de papier, ciel de dune au crépuscule (très léger),
    // collages d'herbe dans les marges, touffes couchées par le vent, brin au crayon.
    juce::Random rng (0x6a4d3e);
    paperGrain (g, page, rng, juce::Colour (0xff6c8f3a), juce::Colour (0xff8a8572));

    // Ciel : voile lavande → transparent, derrière le titre.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffc8c2dc).withAlpha (0.20f), 0.0f, 0.0f,
                                             juce::Colour (0xffe9c9cf).withAlpha (0.0f), 0.0f, 78.0f, false));
    g.fillRect (page.withHeight (78.0f));

    const juce::Colour grasses[] = { juce::Colour (0xff7f9f4a), juce::Colour (0xff5d7a3a), juce::Colour (0xffb7c98a),
                                     juce::Colour (0xff9db86a), juce::Colour (0xffc2b77a), juce::Colour (0xff6f8f6a) };
    int k = 0;
    for (auto b : collageBlocks)
    {
        const auto path = tornRect (b, rng);
        g.setColour (grasses[k % 4].withAlpha (0.26f));
        g.fillPath (path);
        // herbes couchées : longs brins penchés par le vent (comme les dunes)
        g.saveState();
        g.reduceClipRegion (path);
        for (int i = 0; i < 70; ++i)
        {
            const float x = b.getX() - 10.0f + rng.nextFloat() * (b.getWidth() + 10.0f);
            const float y = b.getY() + 8.0f + rng.nextFloat() * (b.getHeight() + 6.0f);
            const float h = 10.0f + rng.nextFloat() * 16.0f;
            g.setColour (grasses[(k + 1 + i) % 6].withAlpha (0.45f));
            grassBlade (g, x, y, h, h * (0.6f + 0.6f * rng.nextFloat()), 0.6f);
        }
        g.restoreState();
        ++k;
    }

    // Touffes de prairie : bas de page (coins) et bas des marges.
    for (float x = 6.0f; x < 120.0f; x += 9.0f)
        grassTuft (g, rng, x, page.getBottom() - 2.0f, 8.0f, 30.0f + 14.0f * rng.nextFloat(), 6, grasses, 6);
    for (float x = 884.0f; x < 980.0f; x += 9.0f)
        grassTuft (g, rng, x, page.getBottom() - 2.0f, 8.0f, 26.0f + 14.0f * rng.nextFloat(), 6, grasses, 6);
    grassTuft (g, rng, 34.0f, 420.0f, 18.0f, 26.0f, 9, grasses, 6);
    grassTuft (g, rng, 962.0f, 300.0f, 18.0f, 24.0f, 9, grasses, 6);
    grassTuft (g, rng, 960.0f, 600.0f, 16.0f, 22.0f, 8, grasses, 6);

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
    // deux épis de graminée à côté
    g.setColour (juce::Colour (0xff8a8a4a).withAlpha (0.7f));
    for (int e = 0; e < 2; ++e)
    {
        const float ex = 226.0f + 9.0f * (float) e, ey = 64.0f;
        juce::Path st; st.startNewSubPath (ex, ey); st.quadraticTo (ex + 2.0f, ey - 20.0f, ex + 7.0f, ey - 34.0f + 6.0f * (float) e);
        g.strokePath (st, juce::PathStrokeType (0.8f));
        for (int j = 0; j < 6; ++j)
        {
            const float t = 0.55f + 0.08f * (float) j;
            const float px = ex + 7.0f * t * t, py = ey - (34.0f - 6.0f * (float) e) * t;
            g.drawLine (px, py, px - 3.0f, py - 2.5f, 0.7f);
            g.drawLine (px, py, px + 2.5f, py - 3.0f, 0.7f);
        }
    }
}

void drawFloralDecor (juce::Graphics& g, juce::Rectangle<float> page)
{
    // Floral : lavis d'aquarelle rose, pétales veinés, éclaboussures d'encre rouille,
    // petite fleur au crayon (inspiré des planches florales de référence).
    juce::Random rng (0x7e7a15);
    paperGrain (g, page, rng, juce::Colour (0xffc76b86), juce::Colour (0xff9a8088));

    const juce::Colour roses[] = { juce::Colour (0xffe9a3b6), juce::Colour (0xffd77c98), juce::Colour (0xfff2c0cc),
                                   juce::Colour (0xffc9617f), juce::Colour (0xffe8b3a0) };

    // 1. Lavis : grandes taches translucides, bord plus foncé (pigment accumulé).
    struct Wash { float x, y, rx, ry; int col; };
    const Wash washes[] = { { 34.0f, 200.0f, 30.0f, 95.0f, 0 }, { 968.0f, 200.0f, 26.0f, 80.0f, 1 },
                            { 36.0f, 520.0f, 30.0f, 60.0f, 2 }, { 968.0f, 530.0f, 26.0f, 70.0f, 0 },
                            { 214.0f, 40.0f, 42.0f, 24.0f, 2 }, { 955.0f, 740.0f, 48.0f, 18.0f, 1 } };
    for (const auto& w : washes)
    {
        for (int layer = 0; layer < 3; ++layer)
        {
            const float sc = 1.0f - 0.18f * (float) layer;
            const auto p = blob ({ w.x + 4.0f * (float) layer, w.y - 3.0f * (float) layer }, w.rx * sc, w.ry * sc, rng, 0.22f);
            g.setColour (roses[(w.col + layer) % 5].withAlpha (0.14f));
            g.fillPath (p);
            if (layer == 0)
            {
                g.setColour (roses[3].withAlpha (0.28f));
                g.strokePath (p, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved));
            }
        }
    }

    // 2. Pétales veinés, posés dans les marges (aux emplacements des collages).
    int k = 0;
    for (auto b : collageBlocks)
    {
        const auto c = b.getCentre();
        const float len = juce::jmin (b.getHeight() * 0.42f, 48.0f), wid = b.getWidth() * 0.46f;
        const float ang = (rng.nextFloat() - 0.5f) * 0.9f;
        const auto tr = juce::AffineTransform::rotation (ang).translated (c.x, c.y);

        // contour de pétale : base étroite en bas, bord supérieur arrondi et un peu froissé
        juce::Path petal;
        petal.startNewSubPath (0.0f, len);
        petal.cubicTo (-wid * 1.2f, len * 0.4f, -wid * 1.1f, -len * 0.9f, -wid * 0.1f, -len);
        petal.quadraticTo (0.0f, -len * 0.86f, wid * 0.15f, -len * 1.02f);
        petal.cubicTo (wid * 1.15f, -len * 0.85f, wid * 1.2f, len * 0.35f, 0.0f, len);
        petal.closeSubPath();

        g.setColour (roses[k % 5].withAlpha (0.42f));
        g.fillPath (petal, tr);
        g.setColour (roses[3].withAlpha (0.35f));
        g.strokePath (petal, juce::PathStrokeType (0.8f), tr);

        // nervures : depuis la base, en éventail
        g.saveState();
        g.reduceClipRegion (petal, tr);
        g.setColour (roses[3].withAlpha (0.38f));
        for (int v = 0; v < 9; ++v)
        {
            const float t = ((float) v / 8.0f - 0.5f) * 1.6f;
            juce::Path vein;
            vein.startNewSubPath (0.0f, len * 0.95f);
            vein.quadraticTo (t * wid * 0.5f, 0.0f, t * wid * 1.1f, -len);
            g.strokePath (vein, juce::PathStrokeType (0.45f), tr);
        }
        // cœur crème à la base
        g.setColour (juce::Colour (0xfff3e3c4).withAlpha (0.6f));
        g.fillEllipse (juce::Rectangle<float> (-4.0f, len * 0.72f, 8.0f, 9.0f).transformedBy (tr));
        g.restoreState();
        ++k;
    }

    // 3. Éclaboussures d'encre rouille et petites taches sombres.
    const juce::Point<float> splats[] = { { 58.0f, 300.0f }, { 968.0f, 380.0f }, { 262.0f, 62.0f }, { 968.0f, 690.0f }, { 30.0f, 610.0f } };
    for (auto sp : splats)
    {
        for (int i = 0; i < 26; ++i)
        {
            const float r = 0.5f + 1.8f * rng.nextFloat() * rng.nextFloat();
            const float d = 26.0f * std::sqrt (rng.nextFloat());
            const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            g.setColour (juce::Colour (0xffa9673f).withAlpha (0.25f + 0.35f * rng.nextFloat()));
            g.fillEllipse (sp.x + std::cos (a) * d - r, sp.y + std::sin (a) * d - r, 2.0f * r, 2.0f * r);
        }
        g.setColour (juce::Colour (0xff6b2a2e).withAlpha (0.55f));
        g.fillEllipse (sp.x - 2.2f, sp.y - 2.2f, 4.4f, 4.4f);
    }

    // 4. Petite écriture à l'encre (traits fins), le long d'un lavis.
    g.setColour (juce::Colour (0xff4a2a2e).withAlpha (0.45f));
    for (int i = 0; i < 16; ++i)
    {
        const float x = 18.0f + (float) i * 3.2f, y = 268.0f + std::sin ((float) i) * 1.5f;
        g.drawLine (x, y, x + 1.2f, y - 3.0f - 2.0f * rng.nextFloat(), 0.5f);
    }

    // 5. Petite fleur dessinée au crayon, en haut à gauche (à la place du brin).
    const juce::Point<float> fc (206.0f, 36.0f);
    g.setColour (juce::Colour (0xffb64a6e).withAlpha (0.75f));
    juce::Path stem;
    stem.startNewSubPath (fc.x - 2.0f, 66.0f);
    stem.quadraticTo (fc.x - 6.0f, 52.0f, fc.x, fc.y + 6.0f);
    g.strokePath (stem, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int i = 0; i < 6; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 6.0f;
        juce::Path pp;
        pp.addEllipse (-3.2f, -8.5f, 6.4f, 9.0f);
        g.strokePath (pp, juce::PathStrokeType (0.9f), juce::AffineTransform::rotation (a).translated (fc.x, fc.y));
    }
    g.fillEllipse (fc.x - 1.8f, fc.y - 1.8f, 3.6f, 3.6f);
    juce::Path lf;                                           // une feuille sur la tige
    lf.startNewSubPath (0.0f, 0.0f);
    lf.quadraticTo (5.0f, -3.5f, 10.0f, 0.0f);
    lf.quadraticTo (5.0f, 3.5f, 0.0f, 0.0f);
    g.strokePath (lf, juce::PathStrokeType (0.8f), juce::AffineTransform::rotation (-0.6f).translated (fc.x - 4.0f, 56.0f));
}
