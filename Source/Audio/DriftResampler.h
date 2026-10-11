#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>
#include <vector>

namespace onirique
{
/*  Pont entre deux périphériques audio qui n'ont PAS la même horloge (le micro USB, le câble virtuel,
    le casque de retour...). Une seule FIFO producteur -> consommateur, sans verrou ni allocation.

    - Le thread du micro pousse les échantillons traités (push).
    - Le thread de la sortie les tire à sa propre cadence (pull), à travers un rééchantillonneur sinc
      fenêtré (32 points, 512 phases interpolées) dont le rapport est corrigé en continu.
    - Régulation : le remplissage moyen de la FIFO est comparé à la cible ; l'écart corrige le rapport
      de quelques centaines de ppm au plus (inaudible). Les deux horloges peuvent dériver indéfiniment,
      la latence reste constante.
    - Fréquences différentes (ex. micro 48 kHz, sortie 44.1 kHz) : le rapport de base s'en charge, la
      coupure du filtre descend sous la nouvelle Nyquist (pas de repliement).
    - Sous-alimentation (sortie démarrée avant le micro, micro débranché...) : fondu vers le silence,
      puis ré-amorçage automatique dès que la FIFO est de nouveau pleine à la cible.
    - Trop plein (sortie arrêtée un moment) : la lecture saute directement à la cible, la latence ne
      s'accumule jamais.
*/
class DriftResampler
{
public:
    static constexpr int kSize = 1 << 15, kMask = kSize - 1;
    static constexpr int kTaps = 32, kHalf = kTaps / 2, kPhases = 512;

    DriftResampler() : ring ((size_t) kSize, 0.0f) {}

    // à appeler quand le CONSOMMATEUR ne tourne pas ; le producteur peut continuer à pousser (il ne touche qu'à
    // l'anneau et au compteur d'écriture, qui ne sont jamais réalloués ni remis à zéro)
    // à appeler quand le CONSOMMATEUR ne tourne pas ; le producteur peut continuer à pousser (il ne touche qu'à
    // l'anneau et aux compteurs d'écriture, qui ne sont jamais réalloués ni remis à zéro)
    void prepare (double inputRate, double outputRate, int inputBlock, int outputBlock)
    {
        inRate = inputRate;
        outRate = outputRate;
        baseStep = inRate / outRate;

        // cible (remplissage "continu", voir pull) : au pire moment, juste avant l'arrivée d'un bloc du micro, il
        // doit rester un bloc de sortie + la demi-fenêtre du filtre + une marge pour la gigue des pilotes (adaptative)
        baseTarget = (double) inputBlock + (double) outputBlock * baseStep + kHalf;
        marginSec = 0.002;
        target = baseTarget + marginSec * inRate;
        maxFill = (baseTarget + kMaxMargin * inRate) * 2.0 + 256.0;

        buildKernel (0.5 * std::min (1.0, outRate / inRate) * 0.94);
        readInt = written.load (std::memory_order_acquire);   // on repart de "maintenant" : rien d'ancien n'est rejoué
        frac = 0.0;
        primed = false;
        fillAvg = target;
        integ = 0.0;
        corr = 0.0;
        lockTime = windowTime = 0.0;
        minHeadroom = 1.0e9;
        fade = 0.0f;
        underruns.store (0);
    }

    // ---- thread producteur (nowSec : horloge monotone, en secondes)
    void push (const float* x, int n, double nowSec) noexcept
    {
        auto w = written.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            ring[(size_t) ((w + i) & kMask)] = x[i];
        lastBlock.store (n, std::memory_order_relaxed);
        pushTime.store (nowSec, std::memory_order_relaxed);
        written.store (w + n, std::memory_order_release);
    }

    // ---- thread consommateur
    void pull (float* y, int n, double nowSec) noexcept
    {
        const auto w = written.load (std::memory_order_acquire);

        // Remplissage CONTINU : les échantillons déjà dans la FIFO + ceux que le micro a captés depuis son dernier
        // bloc (ils arriveront avec le prochain). Contrairement au remplissage brut, il ne dépend pas de la phase
        // entre les deux pilotes : c'est lui qu'on régule.
        const double since = juce::jlimit (0.0, (double) lastBlock.load (std::memory_order_relaxed),
                                           (nowSec - pushTime.load (std::memory_order_relaxed)) * inRate);
        const double readPos = (double) readInt + frac;
        double fill = (double) w + since - readPos;

        if (fill > maxFill || (double) w < readPos - kHalf)
        {
            // la sortie a pris du retard, ou le producteur a redémarré : on se recale directement sur la cible
            readInt = (std::int64_t) std::floor ((double) w + since - target);
            frac = 0.0;
            fill = target;
            fillAvg = target;
        }

        if (! primed)
        {
            // amorçage : on attend la cible, sans jamais rejouer ce qui est déjà sorti
            if (fill >= target)
            {
                // on démarre EXACTEMENT sur la cible : l'excédent (jusqu'à un bloc de sortie) est sauté
                const auto skip = (std::int64_t) std::floor (fill - target);
                readInt += skip;
                fill -= (double) skip;
                primed = true;
                fillAvg = fill;
            }
            else
            {
                std::fill (y, y + n, 0.0f);
                return;
            }
        }

        // ---- régulation PI sur l'écart de remplissage exprimé en secondes, amortissement critique :
        //      e'' + 2w e' + w^2 e = 0. La boucle démarre rapide (w = 0.5 rad/s : verrouillage en quelques secondes)
        //      puis ralentit progressivement jusqu'à 0.04 rad/s : la correction devient quasi immobile, aucune
        //      modulation audible du signal. Le remplissage est moyenné sur une durée qui suit la lenteur de la boucle.
        const double dt = (double) n / outRate;
        lockTime += dt;
        const double w0 = std::max (0.04, 0.5 * std::exp (-lockTime / 15.0));
        fillAvg += std::min (1.0, dt * w0 / 0.12) * (fill - fillAvg);
        const double err = (fillAvg - target) / inRate;
        const double p = 2.0 * w0 * err;
        // anti-emballement : l'intégrateur ne charge pas quand la correction est déjà en butée dans le même sens
        const double nextInteg = juce::jlimit (-0.004, 0.004, integ + w0 * w0 * err * dt);
        if (std::abs (p + nextInteg) < 0.005 || std::abs (nextInteg) < std::abs (integ)) integ = nextInteg;
        corr = juce::jlimit (-0.005, 0.005, p + integ);
        const double step = baseStep * (1.0 + corr);

        // ---- marge adaptative : on mesure la réserve réelle au moment de chaque lecture ; si elle reste large
        //      pendant 10 s, la marge (donc la latence) diminue un peu ; chaque sous-alimentation l'augmente
        minHeadroom = std::min (minHeadroom, (double) (w - readInt) - ((double) n * step + kHalf + 1));
        windowTime += dt;
        if (windowTime >= 10.0)
        {
            if (minHeadroom > 0.002 * inRate && marginSec > kMinMargin)
                setMargin (marginSec - 0.0005);
            windowTime = 0.0;
            minHeadroom = 1.0e9;
        }

        int i = 0;
        for (; i < n; ++i)
        {
            if (readInt + kHalf + 1 > w) break;   // plus assez d'échantillons arrivés : sous-alimentation

            y[i] = interpolate (readInt, frac) * fade;
            if (fade < 1.0f) fade = std::min (1.0f, fade + 0.002f);   // entrée en fondu après un ré-amorçage

            frac += step;
            const double adv = std::floor (frac);
            readInt += (std::int64_t) adv;
            frac -= adv;
        }

        if (i < n)
        {
            // sous-alimentation : fondu rapide du dernier échantillon vers 0, puis ré-amorçage
            float last = i > 0 ? y[i - 1] : 0.0f;
            for (; i < n; ++i) { last *= 0.95f; y[i] = last; }
            primed = false;
            fade = 0.0f;
            underruns.fetch_add (1, std::memory_order_relaxed);
            setMargin (marginSec + 0.002);   // les pilotes sont plus irréguliers que prévu : plus de réserve
        }
    }

    // ---- mesures (n'importe quel thread, indicatives)
    double getTargetLatencyMs() const noexcept { return 1000.0 * target / inRate; }
    double getMarginMs() const noexcept        { return 1000.0 * marginSec; }
    double getCorrectionPpm() const noexcept   { return corr * 1.0e6; }
    double getFillError() const noexcept       { return fillAvg - target; }
    int getUnderruns() const noexcept          { return underruns.load (std::memory_order_relaxed); }

private:
    float interpolate (std::int64_t base, double f) const noexcept
    {
        // phase fractionnaire -> deux tables voisines interpolées
        const double ph = f * kPhases;
        const int p0 = std::min (kPhases - 1, (int) ph);
        const float t = (float) (ph - p0);
        const float* k0 = &kernel[(size_t) p0 * kTaps];
        const float* k1 = k0 + kTaps;

        float acc = 0.0f;
        const std::int64_t start = base - kHalf + 1;
        for (int k = 0; k < kTaps; ++k)
        {
            const float s = ring[(size_t) ((start + k) & kMask)];
            acc += s * (k0[k] + t * (k1[k] - k0[k]));
        }
        return acc;
    }

    // sinc fenêtré (Kaiser beta 8.5), une table par phase, gain DC normalisé à 1
    void buildKernel (double cutoff)
    {
        kernel.assign ((size_t) (kPhases + 1) * kTaps, 0.0f);
        const double beta = 8.5, i0b = bessel0 (beta);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double f = (double) p / kPhases;
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                const double x = (double) (k - kHalf + 1) - f;        // distance au point de lecture
                const double s = std::abs (x) < 1.0e-9 ? 2.0 * cutoff
                                                       : std::sin (2.0 * juce::MathConstants<double>::pi * cutoff * x) / (juce::MathConstants<double>::pi * x);
                const double r = x / (kHalf + 0.5);
                const double w = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                kernel[(size_t) (p * kTaps + k)] = (float) (s * w);
                sum += s * w;
            }
            for (int k = 0; k < kTaps; ++k) kernel[(size_t) (p * kTaps + k)] = (float) (kernel[(size_t) (p * kTaps + k)] / sum);
        }
    }

    static double bessel0 (double x) noexcept
    {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 32; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
        return s;
    }

    std::vector<float> ring, kernel;
    std::atomic<std::int64_t> written { 0 };
    std::atomic<int> underruns { 0 }, lastBlock { 0 };
    std::atomic<double> pushTime { 0.0 };

    double inRate = 48000.0, outRate = 48000.0, baseStep = 1.0;
    static constexpr double kMinMargin = 0.0015, kMaxMargin = 0.03;
    void setMargin (double m) noexcept { marginSec = juce::jlimit (kMinMargin, kMaxMargin, m); target = baseTarget + marginSec * inRate; }

    double target = 1024.0, baseTarget = 1000.0, maxFill = 4096.0, marginSec = 0.002;
    double lockTime = 0.0, windowTime = 0.0, minHeadroom = 1.0e9;

    // état du consommateur
    std::int64_t readInt = 0;
    double frac = 0.0, fillAvg = 0.0, integ = 0.0, corr = 0.0;
    bool primed = false;
    float fade = 0.0f;
};
} // namespace onirique
