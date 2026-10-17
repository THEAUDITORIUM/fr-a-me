/*
  ==============================================================================
    fr(a)me — Scale.h

    Utilitaires de gamme : construction d'un masque de 12 classes de hauteur
    (bit 0 = C, bit 1 = C#, ... bit 11 = B) à partir d'une tonique et d'un mode,
    puis recherche de la note de la gamme la plus proche d'une fréquence.
    Accordage tempéré, A4 = 440 Hz (note MIDI 69).
  ==============================================================================
*/

#pragma once

#include <cmath>
#include <cstdint>

namespace frame
{
    enum class Mode { major = 0, minor = 1 };

    inline constexpr const char* noteNames[12] = { "c", "c#", "d", "d#", "e", "f",
                                                   "f#", "g", "g#", "a", "a#", "b" };

    /** Intervalles (en demi-tons depuis la tonique). Mineur = mineur naturel. */
    inline uint16_t intervalMask (Mode mode) noexcept
    {
        // major : 0 2 4 5 7 9 11      minor : 0 2 3 5 7 8 10
        constexpr int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
        constexpr int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int* steps = (mode == Mode::major) ? majorSteps : minorSteps;

        uint16_t m = 0;
        for (int i = 0; i < 7; ++i)
            m = (uint16_t) (m | (1u << steps[i]));
        return m;
    }

    /** Masque des classes de hauteur de la gamme tonique + mode. */
    inline uint16_t scaleMask (int tonic, Mode mode) noexcept
    {
        const uint16_t iv = intervalMask (mode);
        uint16_t m = 0;
        for (int i = 0; i < 12; ++i)
            if (iv & (1u << i))
                m = (uint16_t) (m | (1u << ((tonic + i) % 12)));
        return m;
    }

    inline bool maskContains (uint16_t mask, int pitchClass) noexcept
    {
        return (mask >> (((pitchClass % 12) + 12) % 12)) & 1u;
    }

    inline double hzToMidi (double hz) noexcept   { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
    inline double midiToHz (double midi) noexcept { return 440.0 * std::exp2 ((midi - 69.0) / 12.0); }

    /** Renvoie la hauteur MIDI (entière) de la note de la gamme la plus proche
        de la hauteur MIDI fractionnaire donnée. Masque vide => hauteur inchangée. */
    inline double nearestScaleMidi (double midi, uint16_t mask) noexcept
    {
        if ((mask & 0x0FFF) == 0)
            return midi;

        const int centre = (int) std::floor (midi + 0.5);
        double best = midi;
        double bestDist = 1.0e9;

        // Au plus 6 demi-tons de part et d'autre : toute gamme non vide y a une note.
        for (int d = 0; d <= 6; ++d)
        {
            for (int sign = -1; sign <= 1; sign += 2)
            {
                const int m = centre + sign * d;
                if (maskContains (mask, m))
                {
                    const double dist = std::abs ((double) m - midi);
                    if (dist < bestDist) { bestDist = dist; best = (double) m; }
                }
                if (d == 0) break;
            }
            if (bestDist < (double) d - 0.5) break; // plus rien de plus proche possible
        }
        return best;
    }
}
