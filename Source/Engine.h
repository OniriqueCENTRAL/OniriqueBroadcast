#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Parameters.h"
#include "StockPresets.h"
#include "DSP/NoiseGate.h"
#include "DSP/EchoGuard.h"
#include "DSP/ParametricEQ.h"
#include "DSP/Comp1176.h"
#include "DSP/MultibandOpto.h"
#include "DSP/DeEsser.h"
#include "DSP/PlosiveSuppressor.h"
#include "DSP/SaturationLab.h"
#include "DSP/Limiter.h"
#include "DSP/SpectrumTap.h"

/*  Moteur de traitement d'ONIRIQUE BROADCAST : la voix (mono) traverse
        INPUT -> [GATE, EQ, 1176, MULTIBAND, DE-ESSER / PLOSIVES, SATURATION dans l'ordre choisi] -> OUTPUT -> LIMITER
    puis part vers le micro virtuel et le retour casque.

    - Traitement en MONO à la fréquence du micro, blocs découpés en morceaux de taille connue (pas d'allocation).
    - Chaque module a un interrupteur sans clic : fondu de 20 ms entre le signal sec et le signal traité. Le GATE
      et le LIMITER gardent leur anticipation même coupés (latence constante).
    - Changement d'ordre des modules : creux de 6 ms (fondu sortant / entrant), jamais de clic.
    - BYPASS : fondu de 20 ms vers le micro brut. MUTE : fondu de 15 ms vers le silence (le micro virtuel reste
      ouvert, les applications reçoivent du silence propre).
    - Sécurité NaN / Inf en sortie.

    L'objet hérite d'AudioProcessor uniquement pour profiter de l'AudioProcessorValueTreeState (paramètres,
    gestes, sauvegarde) : ce n'est pas un plugin.
*/
class BroadcastEngine : public juce::AudioProcessor
{
public:
    BroadcastEngine();

    // ---- thread audio (micro)
    void prepareEngine (double sampleRate, int maxBlockSize);
    void processVoice (float* voice, int numSamples) noexcept;   // traite en place
    bool isPrepared() const noexcept { return prepared.load(); }

    // ---- thread de capture de ce que joue le PC (référence de la garde d'écho), mono 48 kHz
    void pushPlayback (const float* mono, int numSamples) noexcept;
    void releaseEngine() { prepared = false; }

    // ---- mesures (thread de l'interface)
    float consumeInputPeak() noexcept  { return inPeak.exchange (0.0f); }
    float consumeOutputPeak() noexcept { return outPeak.exchange (0.0f); }
    float consumeOverPeak() noexcept   { return overPeak.exchange (0.0f); }
    const onirique::NoiseGate::Meter& getGateMeter() const noexcept       { return gate.getMeter(); }
    const onirique::EchoGuard::Meter& getEchoGuardMeter() const noexcept  { return echoGuard.getMeter(); }
    const onirique::MultibandOpto::Meter& getMultibandMeter() const noexcept { return multiband.getMeter(); }
    float getFetReductionDb() const noexcept      { return fetOnNow.load() ? fet.getGainReductionDb() : 0.0f; }
    float getDeEssReductionDb() const noexcept    { return deesser.getReductionDb(); }
    float getPlosiveReductionDb() const noexcept  { return plosives.getReductionDb(); }
    float getLimiterReductionDb() const noexcept  { return limiter.getReductionDb(); }
    int getProcessingLatencySamples() const noexcept;
    double getEngineSampleRate() const noexcept   { return engineRate.load(); }

    juce::AudioProcessorValueTreeState apvts;
    /** Les presets d'usine et ceux de l'utilisateur, la comparaison A/B : ce que montre la barre du haut. */
    onirique::PresetBank presets { apvts, "Broadcast", onirique::broadcast::stockPresets(), onirique::broadcast::tagNames(),
                                   onirique::broadcast::notStored() };
    onirique::SpectrumTap inputTap, outputTap;

    // ---- AudioProcessor (non utilisé comme plugin)
    const juce::String getName() const override { return "ONIRIQUE BROADCAST"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    void readParams (bool snap) noexcept;
    void processChunk (float* x, int n) noexcept;
    void runModule (int module, float* x, int n) noexcept;
    float param (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed); }

    using Smooth = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    onirique::EchoGuard echoGuard;   // en tête de chaîne, avant tout module
    onirique::NoiseGate gate;
    onirique::ParametricEQ eq;
    onirique::FetCompressor fet;
    onirique::MultibandOpto multiband;
    onirique::DeEsser deesser;
    onirique::PlosiveSuppressor plosives;
    onirique::SaturationLab saturation;
    onirique::Limiter limiter;

    std::array<Smooth, onirique::kOrderModules> power;   // 0 = module coupé, 1 = actif (fondu)
    Smooth inGain, outGain, bypassMix, muteGain, fetMix;
    Smooth orderGain;
    std::array<int, onirique::kOrderModules> order { 0, 1, 2, 3, 4, 5 };
    int orderIndex = 0, pendingOrder = 0;

    std::vector<float> dry, raw, wet;

    // son joué par le PC : FIFO sans verrou (capture -> micro), puis enveloppe par trames de 5 ms (240 éch. à 48 kHz)
    void updatePlaybackLevel() noexcept;
    static constexpr int kPlaybackFifo = 1 << 15, kPbFrame = 240;
    juce::AbstractFifo playbackFifo { kPlaybackFifo };
    std::vector<float> playbackBuf = std::vector<float> ((size_t) kPlaybackFifo, 0.0f), playbackScratch = std::vector<float> (4096, 0.0f);
    int pbFrameCount = 0;
    bool pbDiscard = true;   // au démarrage : la référence accumulée avant la mise en route est jetée
    double pbAcc = 0.0;
    int maxBlock = 512;
    std::atomic<bool> prepared { false }, fetOnNow { false };
    std::atomic<double> engineRate { 48000.0 };
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, overPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BroadcastEngine)
};
