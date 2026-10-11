#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include "../Engine.h"
#include "DriftResampler.h"

/*  Routage audio d'ONIRIQUE BROADCAST : trois flux WASAPI indépendants.

        MICRO (entrée)  --traitement-->  FIFO + rééchantillonneur  -->  MICRO VIRTUEL (sortie vers le câble virtuel)
                                     \->  FIFO + rééchantillonneur  -->  RETOUR CASQUE (facultatif)

    Le traitement tourne dans le thread du micro (latence minimale). Chaque sortie tire le son à sa propre
    horloge à travers un DriftResampler : les périphériques peuvent être différents, à des fréquences
    différentes, et leurs horloges dériver, sans clic ni latence qui s'accumule.

    Le "micro virtuel" est l'ENTRÉE d'un câble audio virtuel (VB-CABLE, VoiceMeeter, Virtual Audio Cable...) :
    tout ce qui y est joué ressort sur sa SORTIE, qui apparaît dans Windows comme un microphone. Discord, OBS,
    les jeux et toutes les applications choisissent ce micro-là.

    Robustesse : un chien de garde rouvre automatiquement un périphérique perdu (débranché, réveillé de veille,
    pris en exclusif par une autre application) dès qu'il redevient disponible.
*/
class AudioRouter : private juce::AudioIODeviceType::Listener,
                    private juce::Timer
{
public:
    enum Mode { shared = 0, lowLatency, exclusive };

    struct Config
    {
        juce::String input, output, monitor;   // noms de périphériques ("" = aucun)
        int inputChannel = 2;                  // 0 = canal 1, 1 = canal 2, 2 = 1 + 2 (mono)
        int mode = shared;
        int bufferSize = 0;                    // 0 = automatique (période du pilote)

        bool operator== (const Config& o) const
        {
            return input == o.input && output == o.output && monitor == o.monitor && inputChannel == o.inputChannel
                   && mode == o.mode && bufferSize == o.bufferSize;
        }
        bool operator!= (const Config& o) const { return ! (*this == o); }
    };

    struct Stream
    {
        bool active = false;          // ouvert et en cours de lecture
        juce::String name, error;
        double rate = 0.0;
        int block = 0, channels = 0;
    };

    struct Status
    {
        Stream input, output, monitor;
        double latencyMs = 0.0;       // micro -> micro virtuel (estimation)
        double driftPpm = 0.0;
        int dropouts = 0;
    };

    explicit AudioRouter (BroadcastEngine&);
    ~AudioRouter() override;

    void setConfig (const Config&, bool forceRestart = false);
    const Config& getConfig() const noexcept { return config; }

    juce::StringArray getInputNames() const;
    juce::StringArray getOutputNames() const;
    juce::String getDefaultDeviceName (bool input) const;   // périphérique par défaut de Windows
    Status getStatus() const;
    float getCpuLoad() const noexcept { return cpu.load (std::memory_order_relaxed); }
    bool isEchoReferenceRunning() const;   // capture de ce que joue le PC active (garde d'écho disponible)

    std::function<void()> onDeviceListChanged;   // thread de l'interface
    std::function<void()> onStatusChanged;       // thread de l'interface

    // ---- câbles virtuels
    static bool isVirtualCable (const juce::String& outputName);
    // nom du micro à choisir dans les applications (cherché parmi les vrais périphériques : reste juste après un renommage)
    juce::String recordingNameFor (const juce::String& outputName) const;
    void rescan();
    static juce::String findVirtualCable (const juce::StringArray& outputs);

private:
    struct InputCallback;
    struct OutputCallback;

    void audioDeviceListChanged() override;
    void timerCallback() override;

    juce::AudioIODeviceType* typeFor (int mode);
    void closeAll();
    void openAll();
    std::unique_ptr<juce::AudioIODevice> openDevice (juce::AudioIODeviceType&, const juce::String& name, bool isInput,
                                                     int bufferSize, juce::String& error);

    BroadcastEngine& engine;
    Config config;
    std::unique_ptr<juce::AudioIODeviceType> types[3];

    onirique::DriftResampler virtualBridge, monitorBridge;
    std::unique_ptr<InputCallback> inputCb;
    std::unique_ptr<OutputCallback> outputCb, monitorCb;
    std::unique_ptr<juce::AudioIODevice> inDev, outDev, monDev;
    Status status;
    std::atomic<float> cpu { 0.0f };
    int watchdogCooldown = 0;
    struct ProcessLoopback* playback = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioRouter)
};
