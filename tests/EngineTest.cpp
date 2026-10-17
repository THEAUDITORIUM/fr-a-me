/*
  ==============================================================================
    fr(a)me — banc de test hors-ligne du moteur spectral.

    Compilation : cmake -B build -DFRAME_BUILD_TESTS=ON && cmake --build build
    Lancement   : ./build/FrameEngineTest_artefacts/<config>/frame_engine_test [dossier_wav]

    Vérifie : reconstruction exacte sans recalage (latence correcte), recalage
    d'un sinus hors gamme sur la bonne note, pureté du résultat, absence de
    clic lors d'un changement de gamme, indépendance vis-à-vis de la taille de
    bloc. Écrit aussi quelques fichiers WAV avant/après pour l'écoute.
  ==============================================================================
*/

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/SpectralEngine.h"
#include "../Source/Scale.h"

#include <cstdio>
#include <random>

namespace
{
    constexpr double sr = 48000.0;
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
        if (! ok) ++failures;
    }

    /** Traite tout le buffer, par blocs de taille donnée (ou variable si 0). */
    void runEngine (SpectralEngine& e, juce::AudioBuffer<float>& buf, int blockSize, uint16_t mask)
    {
        std::mt19937 rng (7);
        int pos = 0;
        while (pos < buf.getNumSamples())
        {
            int n = blockSize > 0 ? blockSize : 1 + (int) (rng() % 700);
            n = juce::jmin (n, buf.getNumSamples() - pos);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), buf.getNumChannels(), pos, n);
            e.setScaleMask (mask);
            e.process (view);
            pos += n;
        }
    }

    /** Fréquence dominante (FFT 32768 + interpolation parabolique) et rapport
        d'énergie hors du lobe principal (en dB). */
    std::pair<double, double> dominantFrequency (const float* x, int len)
    {
        constexpr int order = 15, N = 1 << order;
        juce::dsp::FFT f (order);
        std::vector<float> w ((size_t) N * 2, 0.0f);
        for (int n = 0; n < juce::jmin (N, len); ++n)
            w[(size_t) n] = x[n] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N));
        f.performFrequencyOnlyForwardTransform (w.data(), true);

        int best = 1;
        for (int k = 1; k < N / 2; ++k) if (w[(size_t) k] > w[(size_t) best]) best = k;
        const double a = std::log (w[(size_t) best - 1] + 1e-12), b = std::log (w[(size_t) best] + 1e-12),
                     c = std::log (w[(size_t) best + 1] + 1e-12);
        const double offset = 0.5 * (a - c) / (a - 2 * b + c);

        double inLobe = 0, total = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double p = (double) w[(size_t) k] * w[(size_t) k];
            total += p;
            if (std::abs (k - best) <= 4) inLobe += p;
        }
        return { (best + offset) * sr / N, 10.0 * std::log10 ((total - inLobe) / inLobe + 1e-15) };
    }

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf)
    {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
        if (os == nullptr) return;
        auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                     .withNumChannels (buf.getNumChannels())
                                                     .withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (os, opts))
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    void sine (juce::AudioBuffer<float>& b, double hz, float amp, int start = 0, int end = -1)
    {
        if (end < 0) end = b.getNumSamples();
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int n = start; n < end; ++n)
                b.addSample (c, n, amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * n / sr));
    }
}

int main (int argc, char** argv)
{
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File();
    if (outDir != juce::File()) outDir.createDirectory();

    const uint16_t aMinor = frame::scaleMask (9, frame::Mode::minor);
    const uint16_t cMajor = frame::scaleMask (0, frame::Mode::major);

    SpectralEngine engine;
    engine.prepare (sr, 2);
    engine.setColour (0.0f);               // tests 1 à 6 : recalage seul (mesurable), sans peigne ni queue
    const int latency = engine.getLatencySamples();
    std::printf ("fft %d, latence %d échantillons (%.1f ms)\n", engine.getFftSize(), latency, 1000.0 * latency / sr);

    // ---- 1. Identité (recalage à 0) : reconstruction exacte -----------------
    {
        std::printf ("1. reconstruction sans recalage (bruit + sinus, stéréo)\n");
        const int len = (int) sr * 2;
        juce::AudioBuffer<float> in (2, len);
        std::mt19937 rng (1);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        for (int c = 0; c < 2; ++c) for (int n = 0; n < len; ++n) in.setSample (c, n, nd (rng));
        sine (in, 333.3, 0.3f);
        auto out = in;
        engine.reset();
        engine.setCorrectionAmount (0.0f);
        runEngine (engine, out, 0, aMinor);

        double err = 0, ref = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = latency + engine.getFftSize(); n < len; ++n)
            {
                const double d = out.getSample (c, n) - in.getSample (c, n - latency);
                err += d * d; ref += (double) in.getSample (c, n - latency) * in.getSample (c, n - latency);
            }
        const double snr = 10 * std::log10 (ref / (err + 1e-20));
        std::printf ("     erreur de reconstruction : %.1f dB sous le signal\n", snr);
        check (snr > 90.0, "reconstruction parfaite, latence déclarée exacte");
        engine.setCorrectionAmount (1.0f);
    }

    // ---- 2. Recalage « façon Chroma » -----------------------------------------
    //  dans la gamme (au demi-ton près) → intact ; sinon → note de la gamme
    //  voisine, en gardant la moitié de l'écart en cents.
    struct Case { double in; double expect; const char* name; uint16_t mask; };
    const Case cases[] = {
        { 466.16, 440.00, "la#4 en la mineur -> la4",                         aMinor },
        { 116.54, 110.00, "la#2 en la mineur -> la2",                         aMinor },
        { 455.0,  frame::midiToHz (frame::chromaTargetMidi (frame::hzToMidi (455.0), aMinor)),
                          "455 Hz (la#-42c) -> la-21c (434.8 Hz)",            aMinor },
        { 285.0,  frame::midiToHz (frame::chromaTargetMidi (frame::hzToMidi (285.0), cMajor)),
                          "285 Hz (do#-52c) en do majeur -> do+24c",          cMajor },
        { 480.0,  480.00, "480 Hz (si-49c, dans la gamme) -> intact",         aMinor },
        { 440.0,  440.00, "440 Hz (dans la gamme) -> intact",                 aMinor },
    };
    std::printf ("2. recalage de sinus\n");
    for (const auto& tc : cases)
    {
        const int len = (int) sr * 2;
        juce::AudioBuffer<float> b (2, len);
        b.clear();
        sine (b, tc.in, 0.5f);
        const double dryResidue = dominantFrequency (b.getReadPointer (0) + len / 2, len / 2).second;
        engine.reset();
        runEngine (engine, b, 512, tc.mask);
        const auto [hz, spurious] = dominantFrequency (b.getReadPointer (0) + len / 2, len / 2);
        const double cents = 1200.0 * std::log2 (hz / tc.expect);
        float rms = b.getRMSLevel (0, len / 2, len / 2);
        std::printf ("     %-42s mesuré %.2f Hz (%+.1f cents), résidu %.1f dB (entrée %.1f), niveau %.2f dB\n",
                     tc.name, hz, cents, spurious, dryResidue, juce::Decibels::gainToDecibels (rms / (0.5f / std::sqrt (2.0f))));
        check (std::abs (cents) < 3.0 && spurious < juce::jmax (dryResidue, -48.0) + 3.0, tc.name);
    }

    // ---- 3. Changement de gamme en cours de lecture : pas de clic -----------
    {
        std::printf ("3. changement de gamme sans clic\n");
        const int len = (int) sr * 3;
        juce::AudioBuffer<float> b (1, len);
        b.clear();
        sine (b, 300.0, 0.5f);
        SpectralEngine e; e.prepare (sr, 1); e.setColour (0.0f);
        float maxJump = 0.0f, steadyJump = 0.0f;
        const int block = 256;
        for (int pos = 0; pos < len; pos += block)
        {
            juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 1, pos, juce::jmin (block, len - pos));
            const bool second = pos > len / 2;
            e.setScaleMask (second ? frame::scaleMask (3, frame::Mode::major)   // ré# majeur : 300 → 311 Hz
                                   : frame::scaleMask (2, frame::Mode::major)); // ré majeur  : 300 → 293.7 Hz
            e.process (view);
        }
        for (int n = (int) sr; n < len - 1; ++n)
        {
            const float j = std::abs (b.getSample (0, n + 1) - b.getSample (0, n));
            if (n < len / 2 - 4096) steadyJump = juce::jmax (steadyJump, j);
            else maxJump = juce::jmax (maxJump, j);
        }
        std::printf ("     pente max stable %.4f / pendant le changement %.4f\n", steadyJump, maxJump);
        check (maxJump < steadyJump * 1.25f, "pas de discontinuité lors du changement");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("test_changement_gamme.wav"), b);
    }

    // ---- 4. Indépendance de la taille de bloc --------------------------------
    {
        std::printf ("4. indépendance de la taille de bloc\n");
        const int len = (int) sr;
        juce::AudioBuffer<float> a (2, len);
        a.clear(); sine (a, 371.0, 0.3f); sine (a, 1234.0, 0.2f);
        auto b = a;
        engine.reset(); runEngine (engine, a, 64, cMajor);
        engine.reset(); runEngine (engine, b, 0, cMajor);
        float diff = 0;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < len; ++n)
            diff = juce::jmax (diff, std::abs (a.getSample (c, n) - b.getSample (c, n)));
        check (diff < 1.0e-6f, "même résultat avec des blocs de 64 ou aléatoires");
    }

    // ---- 5. Signal riche : accord désaccordé en scie → partiels dans la gamme -
    {
        std::printf ("5. accord de scies désaccordées (pad) en la mineur\n");
        const int len = (int) sr * 4;
        juce::AudioBuffer<float> b (2, len);
        b.clear();
        const double notes[] = { 220.0 * 1.03, 261.63 * 0.985, 329.63 * 1.02, 155.0 };
        for (int c = 0; c < 2; ++c)
            for (double f0 : notes)
            {
                const double f = f0 * (c == 0 ? 0.998 : 1.002);
                for (int h = 1; h * f < 8000.0; ++h)
                    for (int n = 0; n < len; ++n)
                        b.addSample (c, n, (float) (0.08 / h * std::sin (juce::MathConstants<double>::twoPi * h * f * n / sr)));
            }
        auto dry = b;
        engine.reset();
        engine.setColour (1.0f);           // réglage par défaut du plugin
        runEngine (engine, b, 480, aMinor);
        engine.setColour (0.0f);

        // Mesure (critère Chroma) : proportion d'énergie dont le demi-ton le
        // plus proche appartient à la gamme.
        auto inKeyRatio = [&] (const juce::AudioBuffer<float>& buf)
        {
            constexpr int order = 15, N = 1 << order;
            juce::dsp::FFT f (order);
            std::vector<float> w ((size_t) N * 2, 0.0f);
            const float* x = buf.getReadPointer (0) + len / 2;
            for (int n = 0; n < N; ++n)
                w[(size_t) n] = x[n] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N));
            f.performFrequencyOnlyForwardTransform (w.data(), true);
            double inKey = 0, total = 0;
            for (int k = 1; k < N / 2; ++k)
            {
                const double hz = k * sr / N;
                if (hz < 60 || hz > 8000) continue;
                const double p = (double) w[(size_t) k] * w[(size_t) k];
                const double midi = frame::hzToMidi (hz);
                total += p;
                if (frame::maskContains (aMinor, (int) std::floor (midi + 0.5))) inKey += p;
            }
            return 100.0 * inKey / total;
        };
        const double before = inKeyRatio (dry), after = inKeyRatio (b);
        std::printf ("     énergie dans la gamme : avant %.1f %%  après %.1f %%\n", before, after);
        check (after > 90.0 && after > before + 20.0, "le signal est recalé dans la tonalité");
        if (outDir != juce::File())
        {
            writeWav (outDir.getChildFile ("pad_avant.wav"), dry);
            writeWav (outDir.getChildFile ("pad_apres_la_mineur.wav"), b);
        }
    }

    // ---- 6. Percussions : l'enveloppe temporelle (attaques) est conservée ----
    {
        std::printf ("6. percussions (kick + caisse claire synthétiques) : attaques conservées\n");
        const int len = (int) sr * 3;
        juce::AudioBuffer<float> b (2, len);
        b.clear();
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> ud (-1.0f, 1.0f);
        const int beat = (int) (sr * 0.25);
        for (int start = 0; start + beat <= len; start += beat)
        {
            const bool snare = (start / beat) % 2 == 1;
            double ph = 0;
            for (int n = 0; n < beat; ++n)
            {
                const float env = std::exp (-(float) n / (float) (sr * (snare ? 0.06 : 0.12)));
                float v;
                if (snare) v = 0.5f * env * (ud (rng) * 0.7f + 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 190.0 * n / sr));
                else { const double f = 50.0 + 120.0 * std::exp (-n / (sr * 0.03)); ph += juce::MathConstants<double>::twoPi * f / sr; v = 0.8f * env * (float) std::sin (ph); }
                b.setSample (0, start + n, v); b.setSample (1, start + n, v);
            }
        }
        auto dry = b;
        engine.reset();
        runEngine (engine, b, 256, cMajor);

        // Enveloppes RMS sur 2 ms, sortie réalignée de la latence.
        const int win = (int) (sr * 0.002);
        std::vector<double> e1, e2;
        for (int n = latency + win; n + win < len; n += win)
        {
            double a = 0, c = 0;
            for (int i = 0; i < win; ++i)
            {
                a += (double) dry.getSample (0, n - latency + i) * dry.getSample (0, n - latency + i);
                c += (double) b.getSample (0, n + i) * b.getSample (0, n + i);
            }
            e1.push_back (std::sqrt (a / win)); e2.push_back (std::sqrt (c / win));
        }
        double m1 = 0, m2 = 0; for (size_t i = 0; i < e1.size(); ++i) { m1 += e1[i]; m2 += e2[i]; }
        m1 /= (double) e1.size(); m2 /= (double) e2.size();
        double sxy = 0, sxx = 0, syy = 0;
        for (size_t i = 0; i < e1.size(); ++i) { sxy += (e1[i]-m1)*(e2[i]-m2); sxx += (e1[i]-m1)*(e1[i]-m1); syy += (e2[i]-m2)*(e2[i]-m2); }
        const double corr = sxy / std::sqrt (sxx * syy);
        std::printf ("     corrélation des enveloppes (2 ms) entrée/sortie : %.3f\n", corr);
        check (corr > 0.93, "les transitoires suivent l'original");
        if (outDir != juce::File())
        {
            writeWav (outDir.getChildFile ("drums_avant.wav"), dry);
            writeWav (outDir.getChildFile ("drums_apres_do_majeur.wav"), b);
        }
    }

    // ---- 7. Résonance harmonique : accord dans la gamme + queue ambiante -----
    {
        std::printf ("7. résonance harmonique (la4 tenu 1 s puis silence, la mineur)\n");
        const int len = (int) sr * 5, stop = (int) sr;
        juce::AudioBuffer<float> b (2, len);
        b.clear();
        sine (b, 440.0, 0.4f, 0, stop);
        for (int n = stop - 480; n < stop; ++n)               // fin du sinus en fondu (pas de clic)
            for (int c = 0; c < 2; ++c) b.setSample (c, n, b.getSample (c, n) * (float) (stop - n) / 480.0f);

        SpectralEngine e; e.prepare (sr, 2);
        e.setColour (0.0f); e.setResonanceAmount (1.0f);   // queue seule, mesurable
        runEngine (e, b, 512, aMinor);

        bool finite = true; float peak = 0.0f;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < len; ++n)
        { const float v = b.getSample (c, n); finite &= std::isfinite (v); peak = juce::jmax (peak, std::abs (v)); }

        // Niveau des notes de l'accord pendant la note tenue (fenêtre 0.5–1 s, réalignée)
        auto bandLevel = [&] (int start, double hz)
        {
            constexpr int order = 14, N = 1 << order;
            juce::dsp::FFT f (order);
            std::vector<float> w ((size_t) N * 2, 0.0f);
            for (int n = 0; n < N; ++n)
                w[(size_t) n] = b.getSample (0, start + n) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N));
            f.performFrequencyOnlyForwardTransform (w.data(), true);
            const int k = (int) std::lround (hz * N / sr);
            float m = 0; for (int j = k - 3; j <= k + 3; ++j) m = juce::jmax (m, w[(size_t) j]);
            return juce::Decibels::gainToDecibels (m / (N * 0.25f), -150.0f);
        };
        const int hold = latency + (int) (0.6 * sr);
        const float a4 = bandLevel (hold, 440.0), a5 = bandLevel (hold, 880.0),
                    e5 = bandLevel (hold, 659.26), c5 = bandLevel (hold, 523.25), bb = bandLevel (hold, 466.16);
        std::printf ("     pendant : la4 %.1f dB  la5 %.1f  mi5 (quinte) %.1f  do5 (tierce) %.1f  | la#4 (hors gamme) %.1f\n", a4, a5, e5, c5, bb);

        auto rms = [&] (double t0) { return juce::Decibels::gainToDecibels (b.getRMSLevel (0, latency + (int) (t0 * sr), (int) (0.2 * sr)), -150.0f); };
        const float r05 = rms (1.5), r25 = rms (3.0), r45 = rms (4.6);
        std::printf ("     queue après l'arrêt : +0.5 s %.1f dB, +2 s %.1f dB, +3.6 s %.1f dB ; crête %.2f\n", r05, r25, r45, peak);

        check (finite && peak < 1.0f, "pas de saturation ni de valeur invalide");
        check (a5 > -45.0f && e5 > -45.0f && c5 > -50.0f && bb < c5 - 25.0f, "octave, quinte et tierce de la gamme renforcées, rien hors gamme");
        check (r05 > -40.0f && r25 < r05 - 6.0f && r45 < r25, "queue ambiante qui s'éteint progressivement");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("resonance_la_mineur.wav"), b);
    }

    // ---- 8. Peigne COLOR : notes de la gamme renforcées, niveau conservé -------
    {
        std::printf ("8. peigne COLOR 100 %% sur bruit blanc (la mineur)\n");
        const int len = (int) sr * 4;
        juce::AudioBuffer<float> b (2, len);
        std::mt19937 rng (5);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        for (int c = 0; c < 2; ++c) for (int n = 0; n < len; ++n) b.setSample (c, n, nd (rng));
        const auto dry = b;

        SpectralEngine e; e.prepare (sr, 2); e.setColour (1.0f);
        runEngine (e, b, 512, aMinor);

        constexpr int order = 15, N = 1 << order;
        juce::dsp::FFT f (order);
        auto avgSpectrum = [&] (const juce::AudioBuffer<float>& buf, int offset)
        {
            std::vector<double> acc ((size_t) N / 2, 0.0);
            std::vector<float> w ((size_t) N * 2);
            for (int start = offset + (int) sr; start + N < offset + len - (int) (0.2 * sr); start += N / 2)
            {
                std::fill (w.begin(), w.end(), 0.0f);
                for (int n = 0; n < N; ++n)
                    w[(size_t) n] = buf.getSample (0, start + n) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N));
                f.performFrequencyOnlyForwardTransform (w.data(), true);
                for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += (double) w[(size_t) k] * w[(size_t) k];
            }
            return acc;
        };
        // l'entrée est comparée à la sortie réalignée de la latence
        juce::AudioBuffer<float> dryAligned (1, len); dryAligned.clear();
        for (int n = latency; n < len; ++n) dryAligned.setSample (0, n, dry.getSample (0, n - latency));
        const auto pin = avgSpectrum (dryAligned, 0), pout = avgSpectrum (b, 0);

        double onNote = 0, onNoteIn = 0, offNote = 0, offNoteIn = 0, totIn = 0, totOut = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = k * sr / N;
            totIn += pin[(size_t) k]; totOut += pout[(size_t) k];
            if (hz < 800 || hz > 4000) continue;
            const double midi = frame::hzToMidi (hz);
            const double dist = std::abs (midi - frame::nearestScaleMidi (midi, aMinor));
            if (dist < 0.08)      { onNote  += pout[(size_t) k]; onNoteIn  += pin[(size_t) k]; }
            else if (dist > 0.35) { offNote += pout[(size_t) k]; offNoteIn += pin[(size_t) k]; }
        }
        const double gOn  = 10 * std::log10 (onNote / onNoteIn);
        const double gOff = 10 * std::log10 (offNote / offNoteIn);
        const double gTot = 10 * std::log10 (totOut / totIn);
        std::printf ("     sur les notes %+.1f dB, entre les notes %+.1f dB (contraste %.1f dB), niveau global %+.1f dB\n",
                     gOn, gOff, gOn - gOff, gTot);
        check (gOn - gOff > 10.0, "contraste notes / hors-notes marqué (comme Chroma)");
        check (std::abs (gTot) < 2.0, "niveau global conservé");
        if (outDir != juce::File()) { writeWav (outDir.getChildFile ("bruit_avant.wav"), dry); writeWav (outDir.getChildFile ("bruit_color100.wav"), b); }
    }

    // ---- 9. REVERB harmonique + particules ------------------------------------
    {
        std::printf ("9. reverb 70 %% sur une salve de bruit de 0.3 s (la mineur)\n");
        const int len = (int) sr * 6, burst = (int) (0.3 * sr);
        juce::AudioBuffer<float> b (2, len);
        b.clear();
        std::mt19937 rng (9);
        std::normal_distribution<float> nd (0.0f, 0.2f);
        for (int n = 0; n < burst; ++n) { const float v = nd (rng); b.setSample (0, n, v); b.setSample (1, n, v); }

        SpectralEngine e; e.prepare (sr, 2); e.setColour (1.0f); e.setReverb (0.7f);
        runEngine (e, b, 512, aMinor);

        bool finite = true; float peak = 0;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < len; ++n)
        { const float v = b.getSample (c, n); finite &= std::isfinite (v); peak = juce::jmax (peak, std::abs (v)); }

        auto rmsAt = [&] (double t) { return juce::Decibels::gainToDecibels (b.getRMSLevel (0, latency + (int) (t * sr), (int) (0.25 * sr)), -150.0f); };
        const float t1 = rmsAt (1.0), t3 = rmsAt (3.0), t5 = rmsAt (5.0);

        // corrélation gauche/droite de la queue (entrée mono parfaite)
        double sxy = 0, sxx = 0, syy = 0;
        for (int n = latency + (int) sr; n < latency + (int) (3 * sr); ++n)
        { const double l = b.getSample (0, n), r = b.getSample (1, n); sxy += l * r; sxx += l * l; syy += r * r; }
        const double lr = sxy / std::sqrt (sxx * syy + 1e-20);

        // part harmonique de la queue (1 s → 3 s)
        constexpr int order = 15, N = 1 << order;
        juce::dsp::FFT f (order);
        std::vector<float> w ((size_t) N * 2, 0.0f);
        for (int n = 0; n < N; ++n)
            w[(size_t) n] = b.getSample (0, latency + (int) sr + n) * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) N));
        f.performFrequencyOnlyForwardTransform (w.data(), true);
        double inKey = 0, total = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = k * sr / N;
            if (hz < 200 || hz > 6000) continue;
            const double p = (double) w[(size_t) k] * w[(size_t) k], midi = frame::hzToMidi (hz);
            total += p;
            if (std::abs (midi - frame::nearestScaleMidi (midi, aMinor)) < 0.25) inKey += p;
        }
        const double harm = 100.0 * inKey / (total + 1e-20);

        std::printf ("     queue : +1 s %.1f dB, +3 s %.1f dB, +5 s %.1f dB ; harmonique %.0f %% ; corrélation G/D %.2f ; crête %.2f\n",
                     t1, t3, t5, harm, lr, peak);
        check (finite && peak < 1.5f, "reverb : pas de valeur invalide ni d'emballement");
        check (t1 > -50.0f && t3 < t1 && t5 < t3, "reverb : queue présente qui s'éteint");
        check (harm > 60.0, "reverb : queue accordée sur la gamme (bruit → notes)");
        check (lr < 0.8, "reverb : queue large (gauche et droite décorrélées)");
        if (outDir != juce::File()) writeWav (outDir.getChildFile ("reverb_particules_la_mineur.wav"), b);
    }

    // ---- 10. Robustesse : COLOR et REVERB qui bougent à chaque bloc -----------
    {
        std::printf ("10. automation brutale de COLOR / REVERB / gamme (20 s)\n");
        SpectralEngine e; e.prepare (sr, 2);
        juce::AudioBuffer<float> b (2, 256);
        std::mt19937 rng (2); std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        bool finite = true;
        for (int blk = 0; blk < (int) (20 * sr / 256); ++blk)
        {
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 256; ++n) b.setSample (c, n, 0.4f * u (rng));
            e.setColour (2.0f * (float) ((blk * 7919) % 101) / 100.0f);
            e.setReverb ((float) ((blk * 104729) % 101) / 100.0f);
            e.setScaleMask (frame::scaleMask ((blk / 50) % 12, (blk / 120) % 2 ? frame::Mode::minor : frame::Mode::major));
            e.process (b);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < 256; ++n) finite &= std::isfinite (b.getSample (c, n));
        }
        check (finite, "aucune valeur invalide sous automation brutale");
    }

    std::printf ("\n%s (%d échec%s)\n", failures == 0 ? "TOUS LES TESTS PASSENT" : "ÉCHECS", failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
