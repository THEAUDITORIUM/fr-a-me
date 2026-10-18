/*
  ==============================================================================
    fr(a)me — RetuneVocoder.cpp
  ==============================================================================
*/

#include "RetuneVocoder.h"
#include "Scale.h"
#include <cmath>

namespace
{
    // Calibrage (comparaison avec les exemples de référence) :
    // RETUNE 2.1 — vocodeur robotique façon Vocodex.
   #ifndef RT_GAIN
    #define RT_GAIN 0.36f
   #endif
   #ifndef RT_TILT
    #define RT_TILT -5.0f
   #endif
   #ifndef RT_REL
    #define RT_REL 0.008f
   #endif
   #ifndef RT_BW
    #define RT_BW 2.2
   #endif
   #ifndef RT_EXP
    #define RT_EXP 1.35f
   #endif
   #ifndef RT_KEEP
    #define RT_KEEP 0.35f
   #endif
   #ifndef RT_BUZZ
    #define RT_BUZZ 0.45f
   #endif
   #ifndef RT_BUZZ3
    #define RT_BUZZ3 0.22f
   #endif
   #ifndef RT_SPREAD
    #define RT_SPREAD 0.55f
   #endif
    constexpr float outputGain = RT_GAIN;   // niveau de la nappe vocodée (avant compensation)
    constexpr float highTiltDb = RT_TILT;   // aigus en retrait au-dessus de 2 kHz
    constexpr float keepOriginal = RT_KEEP; // part du son d'origine gardée (variations, glissés, textures)
    constexpr float buzz = RT_BUZZ;         // timbre robotique : octave ajoutée à chaque porteuse
    constexpr float buzz3 = RT_BUZZ3;       // et douzième (×3) : grain plus « machine »
    constexpr float stereoSpread = RT_SPREAD; // bandes alternées gauche / droite (largeur Vocodex)
   #ifndef RT_ANGLE
    #define RT_ANGLE 1.05
   #endif
    const double stereoCos = std::cos (RT_ANGLE), stereoSin = std::sin (RT_ANGLE);   // décalage de phase du canal droit
}

void RetuneVocoder::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    envAttack    = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
    envRelease   = 1.0f - std::exp (-1.0f / (RT_REL * (float) sampleRate));
    amountCoef   = 1.0f - std::exp (-1.0f / (0.030f * (float) sampleRate));
    crossfadeStep = 1.0f / (0.06f * (float) sampleRate);
    expansion = RT_EXP;
    levelCoef = 1.0f - std::exp (-1.0f / (0.3f  * (float) sampleRate));
    levelUp   = 1.0f - std::exp (-1.0f / (0.05f * (float) sampleRate));
    fx.prepare (sampleRate);

    const uint16_t m = mask;
    hasBank = false;
    mask = 0;
    setScale (m);
    reset();
}

void RetuneVocoder::reset() noexcept
{
    for (auto& bank : banks)
        for (int i = 0; i < bank.count; ++i)
        {
            auto& b = bank.bands[(size_t) i];
            b.bp1.clear(); b.bp2.clear();
            b.prev[0] = b.prev[1] = 0.0;
            b.env[0] = b.env[1] = 0.0f;
        }
    crossfade = 1.0f;
    fx.reset();
    eIn = eVoc = 0.0f;
    levelGain = 1.0f;
}

void RetuneVocoder::setScale (uint16_t newMask) noexcept
{
    if (hasBank && newMask == mask)
        return;
    mask = newMask;

    if (! hasBank)
    {
        buildBank (banks[current], mask);
        banks[1 - current].count = 0;
        hasBank = true;
        crossfade = 1.0f;
        return;
    }

    // Nouvelle banque construite dans l'emplacement libre, puis fondu croisé.
    current = 1 - current;
    buildBank (banks[current], mask);
    crossfade = 0.0f;
}

void RetuneVocoder::buildBank (Bank& bank, uint16_t m) noexcept
{
    bank.count = 0;
    if ((m & 0x0FFF) == 0)
        return;

    // Notes de la gamme dans la plage (avec une note de garde de chaque côté pour les bornes).
    int notes[maxBands + 24];
    int n = 0;
    for (int midi = lowNote - 12; midi <= highNote + 12; ++midi)
        if (frame::maskContains (m, midi))
            notes[n++] = midi;

    const double nyq = sampleRate * 0.45;

    for (int i = 1; i + 1 < n; ++i)
    {
        const int note = notes[i];
        if (note < lowNote || note > highNote)
            continue;
        const double hz = frame::midiToHz (note);
        if (hz > nyq)
            break;

        // Bornes au milieu (en demi-tons) des notes de la gamme voisines.
        const double lo = frame::midiToHz (0.5 * (notes[i - 1] + note));
        const double hi = frame::midiToHz (0.5 * (notes[i + 1] + note));
        const double bwOct = std::log2 (hi / lo);

        auto& b = bank.bands[(size_t) bank.count++];
        b.note = note;
        b.hz = hz;

        // Passe-bande RBJ (gain 1 au centre), deux en cascade : la largeur de
        // chaque étage est élargie pour que la cascade couvre [lo, hi] à −3 dB.
        const double stageBw = bwOct * RT_BW;
        const double w0 = juce::MathConstants<double>::twoPi * hz / sampleRate;
        const double alpha = std::sin (w0) * std::sinh (std::log (2.0) / 2.0 * stageBw * w0 / std::sin (w0));
        const double a0 = 1.0 + alpha;
        for (auto* f : { &b.bp1, &b.bp2 })
        {
            f->b0 = alpha / a0;  f->b1 = 0.0;  f->b2 = -alpha / a0;
            f->a1 = -2.0 * std::cos (w0) / a0;  f->a2 = (1.0 - alpha) / a0;
            f->clear();
        }
        b.cosW = std::cos (w0);
        b.sinW = juce::jmax (1.0e-6, std::sin (w0));
        b.prev[0] = b.prev[1] = 0.0;
        b.env[0] = b.env[1] = 0.0f;
        b.phaseInc = juce::MathConstants<double>::twoPi * hz / sampleRate;
        b.phase = std::fmod ((double) note * 2.39996322972865332, juce::MathConstants<double>::twoPi);   // phases étalées (pas d'impulsion commune)
        b.sinNow = std::sin (b.phase);
        b.cosNow = std::cos (b.phase);
        b.sin2Now = std::sin (2.0 * b.phase);
        b.cos2Now = std::cos (2.0 * b.phase);
        b.sin3Now = std::sin (3.0 * b.phase);
        b.cos3Now = std::cos (3.0 * b.phase);

        // Inclinaison douce dans l'aigu (au-dessus de 2 kHz).
        const double octAbove2k = juce::jmax (0.0, std::log2 (hz / 2000.0));
        b.gain = outputGain * (float) std::pow (10.0, highTiltDb * juce::jmin (2.0, octAbove2k) / 2.0 / 20.0);

        // Bandes alternées gauche / droite (le grave reste au centre).
        const float side = (bank.count % 2 == 0 ? 1.0f : -1.0f) * stereoSpread * (float) juce::jlimit (0.0, 1.0, std::log2 (hz / 150.0));
        b.panL = std::sqrt (0.5f * (1.0f - side)) * 1.41421356f;
        b.panR = std::sqrt (0.5f * (1.0f + side)) * 1.41421356f;
        b.buzzOk = hz * 2.0 < nyq;
        b.buzz3Ok = hz * 3.0 < nyq;
    }
}

void RetuneVocoder::runBank (Bank& bank, const double* in, int nch, double* out) noexcept
{
    // 1. Analyse : enveloppe de chaque bande (amplitude du signal analytique).
    for (int i = 0; i < bank.count; ++i)
    {
        auto& b = bank.bands[(size_t) i];
        for (int c = 0; c < nch; ++c)
        {
            const double y = b.bp2.tick (b.bp1.tick (in[c], c), c);
            const double q = (b.prev[c] - y * b.cosW) / b.sinW;
            b.prev[c] = y;
            const float a = (float) std::sqrt (y * y + q * q);
            float& e = b.env[c];
            e += (a - e) * (a > e ? envAttack : envRelease);
        }
    }

    // 2. Sélectivité : chaque bande est comparée à ses voisines (±3 bandes,
    //    ≈ ±demi-octave). Les notes réellement jouées ressortent, le « fond »
    //    entre elles s'efface : seules les notes de la gamme les plus proches
    //    de ce que joue le son sonnent (expansion, exposant RT_EXP).
    for (int c = 0; c < nch; ++c)
    {
        for (int i = 0; i < bank.count; ++i)
        {
            const int i0 = juce::jmax (0, i - 3), i1 = juce::jmin (bank.count - 1, i + 3);
            float sum = 0.0f;
            for (int j = i0; j <= i1; ++j) sum += bank.bands[(size_t) j].env[c];
            const float local = sum / (float) (i1 - i0 + 1) + 1.0e-9f;
            const float e = bank.bands[(size_t) i].env[c];
            const float ratio = e / local;
            const float r = juce::jmin (ratio, 8.0f);
            bank.shaped[(size_t) i] = std::abs (expansion - 2.0f) < 1.0e-6f ? e * r              // cas courant : rapide
                                                                         : e * std::pow (r, expansion - 1.0f);
        }

        // 3. Synthèse : sur chaque note de la gamme, une porteuse « robotique »
        //    (fondamentale + octave, en phase) exactement accordée ; bandes
        //    réparties gauche / droite.
        double acc = 0.0;
        for (int i = 0; i < bank.count; ++i)
        {
            const auto& b = bank.bands[(size_t) i];
            // Canal droit : même note, phase décalée de ~60° → gauche et droite
            // partiellement décorrélés (largeur façon Vocodex, sans désaccord,
            // et sans perte en mono).
            const double f1 = b.sinNow + (b.buzzOk ? (double) buzz * b.sin2Now : 0.0)
                                       + (b.buzz3Ok ? (double) buzz3 * b.sin3Now : 0.0);
            const double carrier = (c == 0 || nch < 2)
                                 ? f1
                                 : stereoCos * f1 + stereoSin * (b.cosNow + (b.buzzOk ? (double) buzz * b.cos2Now : 0.0)
                                                                         + (b.buzz3Ok ? (double) buzz3 * b.cos3Now : 0.0));
            const float pan = nch > 1 ? (c == 0 ? b.panL : b.panR) : 1.0f;
            acc += (double) (bank.shaped[(size_t) i] * b.gain * pan) * carrier;
        }
        out[c] += acc;
    }

    for (int i = 0; i < bank.count; ++i)
    {
        auto& b = bank.bands[(size_t) i];
        b.phase += b.phaseInc;
        if (b.phase > juce::MathConstants<double>::twoPi)
            b.phase -= juce::MathConstants<double>::twoPi;
        b.sinNow = std::sin (b.phase);
        b.cosNow = std::cos (b.phase);
        b.sin2Now = 2.0 * b.sinNow * b.cosNow;                 // sin(2φ)
        b.cos2Now = 1.0 - 2.0 * b.sinNow * b.sinNow;           // cos(2φ)
        b.sin3Now = b.sinNow * (3.0 - 4.0 * b.sinNow * b.sinNow);   // sin(3φ)
        b.cos3Now = b.cosNow * (4.0 * b.cosNow * b.cosNow - 3.0);   // cos(3φ)
    }
}

void RetuneVocoder::process (juce::AudioBuffer<float>& buffer, int numChannels, float amount) noexcept
{
    const int len = buffer.getNumSamples();
    const int nch = juce::jmin (numChannels, buffer.getNumChannels(), 2);
    if (len <= 0 || nch <= 0)
        return;

    amount = juce::jlimit (0.0f, 1.0f, std::isfinite (amount) ? amount : 0.0f);
    float* data[2] = { buffer.getWritePointer (0), nch > 1 ? buffer.getWritePointer (1) : nullptr };

    // Rien à faire (retune coupé et fondu terminé) : on ne touche pas au son.
    if (amount <= 0.0f && amountSmoothed < 1.0e-4f)
    {
        if (amountSmoothed > 0.0f)
            fx.reset();                       // les modulations repartiront d'un état propre
        amountSmoothed = 0.0f;
        return;
    }

    for (int i = 0; i < len; ++i)
    {
        amountSmoothed += (amount - amountSmoothed) * amountCoef;

        double in[2] = { 0.0, 0.0 };
        for (int c = 0; c < nch; ++c)
            in[c] = std::isfinite (data[c][i]) ? (double) data[c][i] : 0.0;

        double wetNew[2] = { 0.0, 0.0 }, wetOld[2] = { 0.0, 0.0 };
        runBank (banks[current], in, nch, wetNew);
        if (crossfade < 1.0f)
        {
            runBank (banks[1 - current], in, nch, wetOld);
            crossfade = juce::jmin (1.0f, crossfade + crossfadeStep);
        }

        const double xf = (double) crossfade;
        double voc[2] = { 0.0, 0.0 };
        float inE = 0.0f, vocE = 0.0f;
        for (int c = 0; c < nch; ++c)
        {
            // Nappe vocodée + une part du son d'origine : aucune partie du
            // sample n'est perdue (glissés, textures, attaques restent là).
            voc[c] = wetOld[c] * (1.0 - xf) + wetNew[c] * xf + (double) keepOriginal * in[c];
        }

        // Modulations du retune : phaser → flanger → chorus (0 latence).
        fx.processFrame (voc[0], voc[1], nch < 2);

        for (int c = 0; c < nch; ++c)
        {
            inE  += (float) (in[c] * in[c]);
            vocE += (float) (voc[c] * voc[c]);
        }

        // Volume : le son « retune » garde le niveau du son d'origine
        // (remonte en ~50 ms, redescend en ~300 ms).
        eIn  += (inE  - eIn)  * levelCoef;
        eVoc += (vocE - eVoc) * levelCoef;
        const float target = eVoc > 1.0e-12f ? juce::jlimit (0.25f, 4.0f, std::sqrt (eIn / eVoc)) : levelGain;
        levelGain += (target - levelGain) * (target > levelGain ? levelUp : levelCoef);
        if (! std::isfinite (levelGain)) levelGain = 1.0f;

        for (int c = 0; c < nch; ++c)
        {
            const double y = in[c] + (double) amountSmoothed * (voc[c] * (double) levelGain - in[c]);
            data[c][i] = std::isfinite (y) ? (float) y : 0.0f;
        }
    }

    // Sécurité : état invalide → vidé.
    for (auto& bank : banks)
        for (int i = 0; i < bank.count; ++i)
        {
            auto& b = bank.bands[(size_t) i];
            for (int c = 0; c < 2; ++c)
                if (! std::isfinite (b.env[c]) || ! std::isfinite (b.bp2.z1[c]))
                {
                    b.bp1.clear(); b.bp2.clear(); b.env[0] = b.env[1] = 0.0f; b.prev[0] = b.prev[1] = 0.0;
                }
        }
}
