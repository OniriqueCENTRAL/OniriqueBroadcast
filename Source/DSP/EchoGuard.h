#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  ECHO GUARD : rend le micro virtuel compatible avec la suppression d'écho de Discord.

    Le problème : le son que joue le PC (la voix des autres sur Discord, le jeu) fuit du casque ou des haut-parleurs
    dans le micro. La chaîne (gate, 1176, multibande) traite cette fuite comme de la voix : elle peut ouvrir le gate
    puis être remontée de ~20 dB par la compression. Discord reçoit alors un écho fort et DÉFORMÉ (non linéaire,
    impossible à soustraire) : sa suppression d'écho apprend qu'elle doit couper fort... et coupe aussi la voix de
    l'utilisateur dès que les autres parlent. D'où le réflexe de désactiver la suppression d'écho.

    La solution, en tête de chaîne (avant toute compression, quel que soit l'ordre des modules) :
      1. Référence : ce que joue le PC (tout sauf Onirique Broadcast), en trames de 5 ms (pushPlaybackFrame).
      2. Alignement : le retard réel son joué -> micro est mesuré par corrélation des enveloppes (de -100 à +200 ms),
         recalculé chaque seconde sur les 3 dernières secondes.
      3. Couplage : niveau de la fuite = niveau joué (aligné) + couplage ; le couplage est le 25e centile de l'écart
         micro / son joué pendant que le PC joue (les moments où l'utilisateur se tait dominent : la fuite seule).
      4. N'agit que si une fuite est RÉELLEMENT détectée (corrélation >= 0.4). Casque bien isolé : rien ne change.
      5. Le micro ne contient que la fuite (niveau < fuite prévue + 8 dB) : -30 dB après un maintien de 150 ms.
         Dès que la voix dépasse la fuite prévue, ouverture en 1 ms (anticipation de 1.5 ms : aucune syllabe mangée).
    Discord ne reçoit presque plus d'écho : sa suppression d'écho n'a rien à combattre et laisse la voix intacte.
*/
class EchoGuard
{
public:
    static constexpr int kHist = 640;                 // trames de 5 ms : 3.2 s d'historique
    static constexpr int kMinLag = -20, kMaxLag = 40; // retard cherché : -100 ms .. +200 ms
    static constexpr float kMarginDb = 8.0f, kDepthDb = 30.0f;

    struct Meter
    {
        std::atomic<float> couplingDb { -120.0f }, leakDb { -120.0f }, correlation { 0.0f }, lagMs { 0.0f };
        std::atomic<bool> active { false }, blocking { false };
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        frameLen = std::max (1, (int) std::lround (0.005 * fs));
        look = std::max (1, (int) std::lround (0.0015 * fs));
        delay.prepare (look);
        hp.setCoeffs (BiquadCoeffs::highpass (fs, 90.0, 0.707));
        rmsC = coef (3.0);
        peakRel = coef (40.0);
        openC = coef (1.0);
        closeC = coef (40.0);
        linC = coef (1.0);
        holdLen = (int) (0.15 * fs);
        reset();
    }

    void reset() noexcept
    {
        delay.reset();
        hp.reset();
        ms = peak = 0.0f;
        acc = 0.0;
        accN = 0;
        mic.fill (-120.0f);
        ref.fill (-120.0f);
        micCount = refCount = 0;
        sinceEstimate = 0;
        lag = 0;
        coupling = -120.0f;
        active = false;
        weak = 0;
        leakNow = -120.0f;
        gDb = 0.0f;
        gLin = 1.0f;
        hold = 0;
    }

    int getLatencySamples() const noexcept { return look; }

    // ---- référence : une trame de 5 ms du son joué (dB, crête équivalente). Une référence trop en avance sur le
    //      micro (rafale au démarrage) est ignorée : les deux historiques restent calés à quelques trames près.
    void pushPlaybackFrame (float db) noexcept
    {
        if (refCount >= micCount + 30) return;
        ref[(size_t) (refCount % kHist)] = db;
        ++refCount;
    }

    void process (float* x, int n, bool enabled) noexcept
    {
        bool blockingNow = false;
        for (int i = 0; i < n; ++i)
        {
            const float s = hp.process (x[i], 0);
            ms += rmsC * (s * s - ms);
            peak = std::abs (s) > peak ? std::abs (s) : peak + peakRel * (std::abs (s) - peak);
            const float det = std::max (std::sqrt (2.0f * ms), 0.5f * peak);
            const float lvl = det > 1.0e-6f ? gainToDb (det) : -120.0f;

            acc += (double) s * s;
            if (++accN >= frameLen)
            {
                micFrame (accN > 0 ? (float) (10.0 * std::log10 (2.0 * acc / accN + 1.0e-12)) : -120.0f);
                acc = 0.0;
                accN = 0;
            }

            // le micro ne contient que la fuite du son joué : on la retient
            const bool echoOnly = enabled && active && leakNow > -90.0f && lvl < leakNow + kMarginDb;
            if (! echoOnly) hold = holdLen;
            else if (hold > 0) --hold;
            const bool closed = echoOnly && hold == 0;
            blockingNow |= closed;

            const float target = closed ? -kDepthDb : 0.0f;
            gDb += (target > gDb ? openC : closeC) * (target - gDb);
            gLin += linC * (dbToGain (gDb) - gLin);
            x[i] = delay.process (x[i]) * gLin;
        }
        meter.blocking.store (blockingNow, std::memory_order_relaxed);
        meter.leakDb.store (active ? leakNow : -120.0f, std::memory_order_relaxed);
    }

    const Meter& getMeter() const noexcept { return meter; }

private:
    float coef (double timeMs) const noexcept { return (float) (1.0 - std::exp (-1.0 / (timeMs * 0.001 * fs))); }

    float refAt (long long r) const noexcept
    {
        if (r < 0 || r >= refCount || r < refCount - kHist) return -120.0f;
        return ref[(size_t) (r % kHist)];
    }

    void micFrame (float db) noexcept
    {
        // pas de référence (rien n'est joué : Windows n'envoie alors aucun paquet) : on comble par du silence
        while (refCount < micCount) { ref[(size_t) (refCount % kHist)] = -120.0f; ++refCount; }

        mic[(size_t) (micCount % kHist)] = db;
        ++micCount;
        if (++sinceEstimate >= 200) { sinceEstimate = 0; estimate(); }

        // fuite prévue pour la trame en cours : son joué aligné (max de 5 trames autour du retard mesuré) + couplage
        float r = -120.0f;
        const long long k = micCount - 1;
        for (int d = -2; d <= 2; ++d)
            r = std::max (r, refAt (std::min (k - lag + d, refCount - 1)));
        // maintenue et relâchée à 30 dB/s : entre deux syllabes des autres, la garde reste en place (sinon elle se
        // rouvrirait à chaque syllabe) ; quand ils se taisent, la voix reprend vite la main
        const float predicted = active ? r + coupling : -120.0f;
        leakNow = std::max (predicted, leakNow - 0.15f);
    }

    // retard (corrélation des enveloppes en dB) puis couplage (25e centile), sur les 3 dernières secondes
    void estimate() noexcept
    {
        const long long n = std::min<long long> (600, micCount);
        if (n < 200) return;
        const long long k0 = micCount - n;

        float bestCorr = -1.0f;
        int bestLag = 0;
        for (int L = kMinLag; L <= kMaxLag; ++L)
        {
            double sm = 0, sr = 0, smm = 0, srr = 0, smr = 0;
            int cnt = 0, refOn = 0;
            for (long long k = k0; k < micCount; ++k)
            {
                const long long r = k - L;
                if (r < 0 || r >= refCount || r < refCount - kHist) continue;
                const double m = std::max (-80.0f, mic[(size_t) (k % kHist)]);
                const double v = std::max (-80.0f, ref[(size_t) (r % kHist)]);
                sm += m; sr += v; smm += m * m; srr += v * v; smr += m * v;
                ++cnt;
                if (v > -50.0) ++refOn;
            }
            if (cnt < 150 || refOn < cnt / 5) continue;   // le PC n'a presque rien joué : rien à apprendre
            const double cov = smr / cnt - (sm / cnt) * (sr / cnt);
            const double vm = smm / cnt - (sm / cnt) * (sm / cnt), vr = srr / cnt - (sr / cnt) * (sr / cnt);
            if (vm <= 1.0e-6 || vr <= 1.0e-6) continue;
            const float c = (float) (cov / std::sqrt (vm * vr));
            if (c > bestCorr) { bestCorr = c; bestLag = L; }
        }
        meter.correlation.store (std::max (0.0f, bestCorr), std::memory_order_relaxed);

        if (bestCorr < 0.4f)
        {
            // pas de fuite mesurable (ou rien n'est joué) : après 3 s sans preuve, la garde se met en veille
            if (++weak >= 3) { active = false; coupling = -120.0f; }
            publish();
            return;
        }

        int cnt = 0;
        for (long long k = k0; k < micCount && cnt < kHist; ++k)
        {
            const float v = refAt (k - bestLag);
            if (v > -50.0f) diffs[(size_t) cnt++] = mic[(size_t) (k % kHist)] - v;
        }
        if (cnt < 40) { publish(); return; }
        auto* mid = diffs.data() + cnt / 4;
        std::nth_element (diffs.data(), mid, diffs.data() + cnt);
        const float c = std::min (-10.0f, *mid);   // jamais plus fort que -10 dB : la voix garde toujours la main

        coupling = active ? 0.5f * (coupling + c) : c;
        lag = bestLag;
        active = true;
        weak = 0;
        publish();
    }

    void publish() noexcept
    {
        meter.active.store (active, std::memory_order_relaxed);
        meter.couplingDb.store (active ? coupling : -120.0f, std::memory_order_relaxed);
        meter.lagMs.store ((float) lag * 5.0f, std::memory_order_relaxed);
    }

    double fs = 48000.0;
    int frameLen = 240, look = 72, holdLen = 7200, hold = 0;
    DelayLine delay;
    Biquad hp;
    float rmsC = 0, peakRel = 0, openC = 0, closeC = 0, linC = 0;
    float ms = 0, peak = 0, gDb = 0, gLin = 1.0f;
    double acc = 0.0;
    int accN = 0;

    std::array<float, kHist> mic {}, ref {}, diffs {};
    long long micCount = 0, refCount = 0;
    int sinceEstimate = 0, lag = 0, weak = 0;
    float coupling = -120.0f, leakNow = -120.0f;
    bool active = false;
    Meter meter;
};
} // namespace onirique
