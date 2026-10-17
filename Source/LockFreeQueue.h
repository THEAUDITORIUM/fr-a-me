/*
  ==============================================================================
    fr(a)me — LockFreeQueue.h

    File FIFO "un producteur / un consommateur" sans verrou ni allocation,
    construite sur juce::AbstractFifo. Le thread audio pousse (push), le thread
    de l'interface lit (pop). Les éléments sont copiés par valeur dans un
    tableau pré-alloué : aucune allocation après la construction.
  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <array>

template <typename T, int Capacity>
class LockFreeQueue
{
public:
    LockFreeQueue() = default;

    /** Thread audio : ajoute un élément. Renvoie false si la file est pleine
        (l'élément est alors simplement abandonné — l'UI ne suit pas, ce n'est
        pas grave, l'audio ne doit jamais attendre). */
    bool push (const T& item) noexcept
    {
        const auto scope = fifo.write (1);

        if (scope.blockSize1 > 0) { storage[(size_t) scope.startIndex1] = item; return true; }
        if (scope.blockSize2 > 0) { storage[(size_t) scope.startIndex2] = item; return true; }
        return false;
    }

    /** Thread UI : retire l'élément le plus ancien. Renvoie false si vide. */
    bool pop (T& out) noexcept
    {
        const auto scope = fifo.read (1);

        if (scope.blockSize1 > 0) { out = storage[(size_t) scope.startIndex1]; return true; }
        if (scope.blockSize2 > 0) { out = storage[(size_t) scope.startIndex2]; return true; }
        return false;
    }

    /** Vide la file (à n'appeler que lorsque le producteur est arrêté). */
    void clear() noexcept { fifo.reset(); }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> storage {};

    JUCE_DECLARE_NON_COPYABLE (LockFreeQueue)
};
