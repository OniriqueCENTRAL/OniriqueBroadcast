#include "Engine.h"

using namespace onirique;

namespace
{
constexpr int kChunk = 512;   // taille de traitement interne : les blocs du pilote sont découpés en morceaux de ce format

void storeMax (std::atomic<float>& a, float v) noexcept
{
    float cur = a.load (std::memory_order_relaxed);
    while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
}
} // namespace

BroadcastEngine::BroadcastEngine()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "OniriqueBroadcast", createLayout())
{
    dry.assign ((size_t) kChunk, 0.0f);
    raw.assign ((size_t) kChunk, 0.0f);
    wet.assign ((size_t) kChunk, 0.0f);
}

void BroadcastEngine::prepareEngine (double sampleRate, int)
{
    prepared = false;
    maxBlock = kChunk;
    setRateAndBufferSizeDetails (sampleRate, maxBlock);

    echoGuard.prepare (sampleRate);
    gate.prepare (sampleRate);
    eq.prepare (sampleRate);
    fet.prepare (sampleRate);
    multiband.prepare (sampleRate);
    deesser.prepare (sampleRate, true);              // sans anticipation : latence nulle
    plosives.prepare (sampleRate);
    saturation.prepare (sampleRate, maxBlock, 2);    // x4, IIR polyphase : quelques échantillons de latence
    limiter.prepare (sampleRate);

    for (auto& p : power) p.reset (sampleRate, 0.02);
    for (auto* s : { &inGain, &outGain, &bypassMix, &fetMix }) s->reset (sampleRate, 0.02);
    muteGain.reset (sampleRate, 0.015);
    orderGain.reset (sampleRate, 0.003);

    readParams (true);
    pbDiscard = true;
    for (auto& p : power) p.setCurrentAndTargetValue (p.getTargetValue());
    for (auto* s : { &inGain, &outGain, &bypassMix, &fetMix, &muteGain }) s->setCurrentAndTargetValue (s->getTargetValue());
    orderIndex = pendingOrder = juce::roundToInt (param (ids::chainOrder));
    order = decodeOrder (orderIndex);
    orderGain.setCurrentAndTargetValue (1.0f);

    engineRate = sampleRate;
    prepared = true;
}

int BroadcastEngine::getProcessingLatencySamples() const noexcept
{
    return echoGuard.getLatencySamples() + gate.getLatencySamples() + limiter.getLatencySamples() + saturation.getLatencySamples();
}

void BroadcastEngine::readParams (bool snap) noexcept
{
    auto on = [this] (const char* id) { return param (id) > 0.5f; };
    auto target = [snap] (Smooth& s, float v) { if (snap) s.setCurrentAndTargetValue (v); else s.setTargetValue (v); };

    target (inGain, dbToGain (param (ids::input)));
    target (outGain, dbToGain (param (ids::output)));
    target (bypassMix, on (ids::bypass) ? 1.0f : 0.0f);
    target (muteGain, on (ids::mute) ? 0.0f : 1.0f);
    target (fetMix, param (ids::fetMix) * 0.01f);

    power[modGate].setTargetValue (1.0f);   // le gate gère lui-même son interrupteur (latence constante)
    power[modEq].setTargetValue (on (ids::eqOn) ? 1.0f : 0.0f);
    power[modFet].setTargetValue (on (ids::fetOn) ? 1.0f : 0.0f);
    power[modMulti].setTargetValue (on (ids::mbOn) ? 1.0f : 0.0f);
    power[modDeess].setTargetValue (on (ids::deessOn) ? 1.0f : 0.0f);
    power[modSat].setTargetValue (on (ids::satOn) ? 1.0f : 0.0f);
    fetOnNow = on (ids::fetOn);

    gate.setParams (on (ids::gateOn), param (ids::gateThr), param (ids::gateRange), param (ids::gateAttack),
                    param (ids::gateHold), param (ids::gateRelease), param (ids::gateHyst));

    std::array<ParametricEQ::Band, ParametricEQ::kBands> bands;
    for (int b = 0; b < ParametricEQ::kBands; ++b)
    {
        auto& bd = bands[(size_t) b];
        bd.on = on (ids::peqOn[b]);
        bd.type = juce::roundToInt (param (ids::peqType[b]));
        bd.freq = param (ids::peqFreq[b]);
        bd.gainDb = param (ids::peqGain[b]);
        bd.q = param (ids::peqQ[b]);
    }
    eq.setBands (bands, snap);

    fet.setParams (param (ids::fetIn), param (ids::fetOut), param (ids::fetAttack), param (ids::fetRelease),
                   juce::roundToInt (param (ids::fetRatio)), snap);

    MultibandOpto::Params mp;
    mp.x1 = param (ids::mbX[0]); mp.x2 = param (ids::mbX[1]); mp.x3 = param (ids::mbX[2]);
    std::array<bool, 4> solo {};
    for (int b = 0; b < 4; ++b)
    {
        mp.thr[(size_t) b] = param (ids::mbThr[b]);
        mp.gain[(size_t) b] = param (ids::mbGain[b]);
        solo[(size_t) b] = on (ids::mbSolo[b]);
    }
    mp.attackMs = param (ids::mbAttack);
    mp.releaseMs = param (ids::mbRelease);
    multiband.setParams (mp, snap);
    multiband.setSolo (solo, snap);

    deesser.setParams (param (ids::deessFreq), param (ids::deessAmt) * 0.01f, 0.0f, snap);
    plosives.setAmount (param (ids::plosive) * 0.01f, snap);

    saturation.setDrive (param (ids::drive), snap);
    saturation.setMode (juce::roundToInt (param (ids::satType)), snap);

    limiter.setParams (on (ids::limOn), param (ids::limCeiling), snap);
}

void BroadcastEngine::pushPlayback (const float* mono, int numSamples) noexcept
{
    int s1, n1, s2, n2;
    playbackFifo.prepareToWrite (numSamples, s1, n1, s2, n2);   // FIFO pleine (micro arrêté) : le surplus est ignoré
    std::copy (mono, mono + n1, playbackBuf.data() + s1);
    std::copy (mono + n1, mono + n1 + n2, playbackBuf.data() + s2);
    playbackFifo.finishedWrite (n1 + n2);
}

void BroadcastEngine::updatePlaybackLevel() noexcept
{
    for (;;)
    {
        int s1, n1, s2, n2;
        playbackFifo.prepareToRead ((int) playbackScratch.size(), s1, n1, s2, n2);
        if (n1 + n2 == 0) break;
        std::copy (playbackBuf.data() + s1, playbackBuf.data() + s1 + n1, playbackScratch.data());
        std::copy (playbackBuf.data() + s2, playbackBuf.data() + s2 + n2, playbackScratch.data() + n1);
        playbackFifo.finishedRead (n1 + n2);
        if (pbDiscard) continue;   // ce qui s'est accumulé avant la mise en route n'est plus d'actualité

        for (int i = 0; i < n1 + n2; ++i)
        {
            const double v = playbackScratch[(size_t) i];
            pbAcc += v * v;
            if (++pbFrameCount >= kPbFrame)
            {
                // trame de 5 ms : RMS ramené à la crête d'un sinus
                const double ms = pbAcc / kPbFrame;
                echoGuard.pushPlaybackFrame (ms > 1.0e-12 ? (float) (10.0 * std::log10 (2.0 * ms)) : -120.0f);
                pbAcc = 0.0;
                pbFrameCount = 0;
            }
        }
    }
    pbDiscard = false;
}

void BroadcastEngine::processVoice (float* voice, int numSamples) noexcept
{
    if (! prepared.load (std::memory_order_acquire))
    {
        std::fill (voice, voice + numSamples, 0.0f);
        return;
    }
    for (int pos = 0; pos < numSamples; pos += kChunk)
        processChunk (voice + pos, std::min (kChunk, numSamples - pos));
}

void BroadcastEngine::runModule (int module, float* x, int n) noexcept
{
    float* ch[1] { x };

    if (module == modGate)
    {
        gate.process (ch, 1, n);
        return;
    }

    auto& pw = power[(size_t) module];
    const bool fading = pw.isSmoothing();
    if (! fading && pw.getTargetValue() < 0.5f) return;             // coupé : rien à calculer

    const bool usesMix = module == modFet && (fetMix.isSmoothing() || fetMix.getTargetValue() < 0.999f);
    if (! fading && ! usesMix)
    {
        switch (module)
        {
            case modEq:    eq.process (ch, 1, n); break;
            case modFet:   fet.process (ch, 1, n); break;
            case modMulti: multiband.process (ch, 1, n); break;
            case modDeess: plosives.process (ch, 1, n); deesser.process (ch, 1, n); break;
            case modSat:   saturation.process (ch, 1, n); break;
            default: break;
        }
        return;
    }

    // fondu (interrupteur) et / ou mélange parallèle (MIX du 1176) : dry + k (wet - dry)
    if (fading && pw.getCurrentValue() < 1.0e-4f)
    {
        // le module se rallume : on repart d'un état propre (pas de réduction de gain restée en mémoire)
        if (module == modFet) fet.reset();
        if (module == modMulti) multiband.reset();
    }
    std::copy (x, x + n, dry.data());
    switch (module)
    {
        case modEq:    eq.process (ch, 1, n); break;
        case modFet:   fet.process (ch, 1, n); break;
        case modMulti: multiband.process (ch, 1, n); break;
        case modDeess: plosives.process (ch, 1, n); deesser.process (ch, 1, n); break;
        case modSat:   saturation.process (ch, 1, n); break;
        default: break;
    }
    for (int i = 0; i < n; ++i)
    {
        float k = pw.getNextValue();
        if (module == modFet) k *= fetMix.getNextValue();
        x[i] = dry[(size_t) i] + k * (x[i] - dry[(size_t) i]);
    }
}

void BroadcastEngine::processChunk (float* x, int n) noexcept
{
    readParams (false);

    // ---- entrée : micro brut gardé pour le BYPASS, puis gain d'entrée
    float pkIn = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        raw[(size_t) i] = x[i];
        x[i] *= inGain.getNextValue();
        pkIn = std::max (pkIn, std::abs (x[i]));
    }
    storeMax (inPeak, pkIn);
    inputTap.push (x, nullptr, n);

    // ---- garde d'écho : retient la fuite du son joué AVANT tout traitement (quel que soit l'ordre des modules)
    updatePlaybackLevel();
    echoGuard.process (x, n, param (ids::gateGuard) > 0.5f);

    // ---- changement d'ordre demandé : fondu sortant, bascule au silence, fondu entrant
    const int wanted = juce::roundToInt (param (ids::chainOrder));
    if (wanted != orderIndex)
    {
        pendingOrder = wanted;
        orderGain.setTargetValue (0.0f);
    }

    for (int m : order) runModule (m, x, n);

    // ---- sortie, limiteur, creux de réordonnancement, bypass, mute
    for (int i = 0; i < n; ++i) x[i] *= outGain.getNextValue();
    {
        float* ch[1] { x };
        limiter.process (ch, 1, n);
    }

    float pkOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float og = orderGain.getNextValue();
        if (og <= 0.0f && orderGain.getTargetValue() <= 0.0f && pendingOrder != orderIndex)
        {
            orderIndex = pendingOrder;
            order = decodeOrder (orderIndex);
            orderGain.setTargetValue (1.0f);
        }
        const float b = bypassMix.getNextValue();
        float y = x[i] * og;
        y = y + b * (raw[(size_t) i] - y);
        y = sanitize (y * muteGain.getNextValue(), 4.0f);
        x[i] = y;
        pkOut = std::max (pkOut, std::abs (y));
    }
    storeMax (outPeak, pkOut);
    storeMax (overPeak, pkOut);
    outputTap.push (x, nullptr, n);
}
