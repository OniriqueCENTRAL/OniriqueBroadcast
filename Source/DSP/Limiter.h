#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include "Common.h"

namespace onirique
{
/*  LIMITER de sortie : le micro virtuel ne sature jamais (Discord, OBS et les jeux reçoivent un signal
    propre même quand on crie dans le micro).

    - Anticipation de 1 ms : l'audio est retardé, le détecteur voit la crête à venir. Le gain requis
      (plafond / crête) est maintenu sur toute la fenêtre d'anticipation, puis lissé : la réduction est
      complète au moment exact où la crête sort, sans écrêtage.
    - Attaque en rampe sur la fenêtre d'anticipation, relâchement 120 ms (programme-dépendant : plus lent
      après une forte réduction, pas de pompage sur une voix soutenue).
    - Sécurité finale : écrêtage doux au plafond (n'agit que si une crête échappait encore au limiteur).
    - Désactivé : même latence, gain revenu en douceur à 0 dB, aucun saut.
*/
class Limiter
{
public:
    static constexpr double kLookaheadSec = 0.001;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        look = std::max (1, (int) std::lround (kLookaheadSec * fs));
        for (auto& d : delay) d.prepare (look);
        req.assign ((size_t) look + 1, 1.0f);
        reqPos = 0;
        atk = (float) (1.0 - std::exp (-1.0 / (0.25 * look)));
        rel = coef (120.0);
        relSlow = coef (400.0);
        ceilS.reset (fs, 0.05);
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delay) d.reset();
        std::fill (req.begin(), req.end(), 1.0f);
        g = 1.0f;
        depth = 0.0f;
        grMeter.store (0.0f, std::memory_order_relaxed);
    }

    int getLatencySamples() const noexcept { return look; }

    void setParams (bool enabled, float ceilingDb, bool snap = false) noexcept
    {
        on = enabled;
        const float c = dbToGain (juce::jlimit (-24.0f, 0.0f, ceilingDb));
        if (snap) ceilS.setCurrentAndTargetValue (c); else ceilS.setTargetValue (c);
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        float peakGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float ceil = ceilS.getNextValue();
            float pk = 0.0f;
            for (int c = 0; c < numCh; ++c) pk = std::max (pk, std::abs (ch[c][i]));

            // gain requis pour cette crête, maintenu sur la fenêtre d'anticipation (minimum glissant)
            req[(size_t) reqPos] = (on && pk > ceil) ? ceil / pk : 1.0f;
            if (++reqPos >= (int) req.size()) reqPos = 0;
            float target = 1.0f;
            for (float r : req) target = std::min (target, r);

            // rampe d'attaque calée sur l'anticipation, relâchement plus lent après une réduction profonde
            depth += 0.0005f * ((1.0f - g) - depth);
            const float r = depth > 0.15f ? relSlow : rel;
            g += (target < g ? atk : r) * (target - g);
            g = std::min (g, 1.0f);
            peakGr = std::max (peakGr, -gainToDb (std::max (g, 1.0e-4f)));

            for (int c = 0; c < numCh; ++c)
            {
                float y = delay[c].process (ch[c][i]) * g;
                if (on)
                {
                    // filet de sécurité : écrêtage doux uniquement au-delà du plafond
                    const float a = std::abs (y);
                    if (a > ceil * 0.98f)
                    {
                        const float knee = ceil * 0.98f, room = ceil - knee;
                        const float over = (a - knee) / room;
                        y = std::copysign (knee + room * over / (1.0f + over), y);
                    }
                }
                ch[c][i] = y;
            }
        }
        grMeter.store (peakGr, std::memory_order_relaxed);
    }

    float getReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }

private:
    float coef (double timeMs) const noexcept { return (float) (1.0 - std::exp (-1.0 / (timeMs * 0.001 * fs))); }

    double fs = 48000.0;
    int look = 48, reqPos = 0;
    bool on = true;
    DelayLine delay[2];
    std::vector<float> req;
    float atk = 0.1f, rel = 0.001f, relSlow = 0.0005f, g = 1.0f, depth = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> ceilS { 1.0f };
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
