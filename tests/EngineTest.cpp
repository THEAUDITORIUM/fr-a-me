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

    // ---- 2. Sinus hors gamme → note de la gamme ----------------------------
    struct Case { double in; double expect; const char* name; uint16_t mask; };
    const Case cases[] = {
        { 455.0,  440.00, "455 Hz en la mineur -> la4 440 Hz",   aMinor },
        { 480.0,  493.88, "480 Hz en la mineur -> si4 493.9 Hz", aMinor },
        { 115.0,  110.00, "115 Hz en la mineur -> la2 110 Hz",   aMinor },
        { 285.0,  293.66, "285 Hz en do majeur -> re4 293.7 Hz", cMajor },
        { 2900.0, 2793.83,"2900 Hz en do majeur -> fa7 2793.8 Hz", cMajor },
        { 440.0,  440.00, "440 Hz (dans la gamme) -> inchangé",  aMinor },
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
        check (std::abs (cents) < 3.0 && spurious < dryResidue + 3.0, tc.name);
    }

    // ---- 3. Changement de gamme en cours de lecture : pas de clic -----------
    {
        std::printf ("3. changement de gamme sans clic\n");
        const int len = (int) sr * 3;
        juce::AudioBuffer<float> b (1, len);
        b.clear();
        sine (b, 300.0, 0.5f);
        SpectralEngine e; e.prepare (sr, 1);
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
        runEngine (engine, b, 480, aMinor);

        // Mesure : proportion d'énergie spectrale à ±20 cents d'une note de la gamme.
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
                // tolérance : ±20 cents, mais au moins ±4 Hz (largeur du lobe de mesure)
                const double noteHz = frame::midiToHz (frame::nearestScaleMidi (midi, aMinor));
                const double tolHz  = std::max (4.0, noteHz * (std::exp2 (20.0 / 1200.0) - 1.0));
                if (std::abs (hz - noteHz) < tolHz) inKey += p;
            }
            return 100.0 * inKey / total;
        };
        const double before = inKeyRatio (dry), after = inKeyRatio (b);
        std::printf ("     énergie dans la gamme : avant %.1f %%  après %.1f %%\n", before, after);
        check (after > 85.0 && after > before + 30.0, "le signal est recalé dans la tonalité");
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
        check (corr > 0.95, "les transitoires suivent l'original");
        if (outDir != juce::File())
        {
            writeWav (outDir.getChildFile ("drums_avant.wav"), dry);
            writeWav (outDir.getChildFile ("drums_apres_do_majeur.wav"), b);
        }
    }

    std::printf ("\n%s (%d échec%s)\n", failures == 0 ? "TOUS LES TESTS PASSENT" : "ÉCHECS", failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
