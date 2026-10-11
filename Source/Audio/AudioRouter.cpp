#include "AudioRouter.h"
#include "../Platform.h"

//==============================================================================
// Thread du micro : conversion en mono, traitement, envoi vers les deux ponts.
struct AudioRouter::InputCallback final : juce::AudioIODeviceCallback
{
    explicit InputCallback (AudioRouter& r) : router (r) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        rate = d->getCurrentSampleRate();
        mono.assign (4096, 0.0f);
        router.engine.prepareEngine (rate, d->getCurrentBufferSizeSamples());
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

        const auto t0 = juce::Time::getHighResolutionTicks();
        const float* a = numIn > 0 ? in[0] : nullptr;
        const float* b = numIn > 1 ? in[1] : nullptr;
        const int sel = channel.load (std::memory_order_relaxed);
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

        for (int pos = 0; pos < n;)
        {
            const int k = std::min ((int) mono.size(), n - pos);
            float* m = mono.data();
            if (sel == 0 || b == nullptr)      { if (a != nullptr) std::copy (a + pos, a + pos + k, m); else std::fill (m, m + k, 0.0f); }
            else if (sel == 1 || a == nullptr) std::copy (b + pos, b + pos + k, m);
            else for (int i = 0; i < k; ++i)   m[i] = 0.5f * (a[pos + i] + b[pos + i]);

            router.engine.processVoice (m, k);
            router.virtualBridge.push (m, k, now);
            router.monitorBridge.push (m, k, now);
            pos += k;
        }

        // charge processeur : temps de calcul / durée du bloc (lissée)
        const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
        const float load = (float) (secs * rate / std::max (1, n));
        const float prev = router.cpu.load (std::memory_order_relaxed);
        router.cpu.store (prev + 0.05f * (load - prev), std::memory_order_relaxed);
    }

    AudioRouter& router;
    std::vector<float> mono;
    double rate = 48000.0;
    std::atomic<int> channel { 2 };
};

//==============================================================================
// Thread d'une sortie (micro virtuel ou retour casque) : tire le son du pont à sa propre cadence.
struct AudioRouter::OutputCallback final : juce::AudioIODeviceCallback
{
    OutputCallback (onirique::DriftResampler& b, BroadcastEngine& e, bool monitor) : bridge (b), engine (e), isMonitor (monitor) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        tmp.assign (4096, 0.0f);
        gainCoef = (float) (1.0 - std::exp (-1.0 / (0.02 * d->getCurrentSampleRate())));
        gain = 0.0f;
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        float target = 1.0f;
        if (isMonitor)
        {
            const bool on = engine.apvts.getRawParameterValue (onirique::ids::monOn)->load (std::memory_order_relaxed) > 0.5f;
            target = on ? onirique::dbToGain (engine.apvts.getRawParameterValue (onirique::ids::monVol)->load (std::memory_order_relaxed)) : 0.0f;
        }
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

        for (int pos = 0; pos < n;)
        {
            const int k = std::min ((int) tmp.size(), n - pos);
            bridge.pull (tmp.data(), k, now);
            for (int i = 0; i < k; ++i)
            {
                gain += gainCoef * (target - gain);
                tmp[(size_t) i] *= gain;
            }
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr) std::copy (tmp.data(), tmp.data() + k, out[c] + pos);
            pos += k;
        }
    }

    onirique::DriftResampler& bridge;
    BroadcastEngine& engine;
    const bool isMonitor;
    std::vector<float> tmp;
    float gain = 0.0f, gainCoef = 0.01f;
};

//==============================================================================
AudioRouter::AudioRouter (BroadcastEngine& e) : engine (e)
{
    inputCb = std::make_unique<InputCallback> (*this);
    outputCb = std::make_unique<OutputCallback> (virtualBridge, engine, false);
    monitorCb = std::make_unique<OutputCallback> (monitorBridge, engine, true);
    typeFor (shared);
    // référence de la garde d'écho : tout ce que joue le PC sauf nous (Windows 10 2004+ ; sinon la garde reste inactive)
    playback = startProcessLoopback ([] (void* ctx, const float* mono, int n) { static_cast<BroadcastEngine*> (ctx)->pushPlayback (mono, n); },
                                     &engine, 48000);
    startTimer (1000);
}

AudioRouter::~AudioRouter()
{
    stopTimer();
    stopProcessLoopback (playback);
    playback = nullptr;
    closeAll();
    if (types[shared] != nullptr) types[shared]->removeListener (this);
}

juce::AudioIODeviceType* AudioRouter::typeFor (int mode)
{
    mode = juce::jlimit (0, 2, mode);
    if (types[mode] == nullptr)
    {
        const auto m = mode == lowLatency ? juce::WASAPIDeviceMode::sharedLowLatency
                     : mode == exclusive  ? juce::WASAPIDeviceMode::exclusive
                                          : juce::WASAPIDeviceMode::shared;
        types[mode].reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (m));
        if (types[mode] != nullptr)
        {
            types[mode]->scanForDevices();
            if (mode == shared) types[mode]->addListener (this);
        }
    }
    if (types[mode] == nullptr && mode != shared) return typeFor (shared);   // mode indisponible sur cette version de Windows
    return types[mode].get();
}

juce::StringArray AudioRouter::getInputNames() const
{
    return types[shared] != nullptr ? types[shared]->getDeviceNames (true) : juce::StringArray();
}

juce::StringArray AudioRouter::getOutputNames() const
{
    return types[shared] != nullptr ? types[shared]->getDeviceNames (false) : juce::StringArray();
}

juce::String AudioRouter::getDefaultDeviceName (bool input) const
{
    if (types[shared] == nullptr) return {};
    const auto names = types[shared]->getDeviceNames (input);
    if (names.isEmpty()) return {};
    const int idx = types[shared]->getDefaultDeviceIndex (input);
    return juce::isPositiveAndBelow (idx, names.size()) ? names[idx] : names[0];
}

//==============================================================================
std::unique_ptr<juce::AudioIODevice> AudioRouter::openDevice (juce::AudioIODeviceType& type, const juce::String& name,
                                                              bool isInput, int bufferSize, juce::String& error)
{
    error.clear();
    if (name.isEmpty()) return {};

    type.scanForDevices();
    if (! type.getDeviceNames (isInput).contains (name))
    {
        error = "Disconnected - waiting for the device";
        return {};
    }

    std::unique_ptr<juce::AudioIODevice> d (isInput ? type.createDevice ({}, name) : type.createDevice (name, {}));
    if (d == nullptr)
    {
        error = "Unable to access the device";
        return {};
    }

    juce::BigInteger ins, outs;
    if (isInput) ins.setRange (0, juce::jlimit (1, 2, d->getInputChannelNames().size()), true);
    else         outs.setRange (0, juce::jlimit (1, 2, d->getOutputChannelNames().size()), true);

    const auto rates = d->getAvailableSampleRates();
    const double rate = rates.contains (48000.0) ? 48000.0 : 0.0;   // 48 kHz : la fréquence de Discord / OBS
    error = d->open (ins, outs, rate, bufferSize);
    if (error.isNotEmpty())
    {
        if (error.containsIgnoreCase ("couldn't open") || error.containsIgnoreCase ("couldn't start"))
            error = "In use by another application (exclusive mode?) or unsupported format";
        return {};
    }
    return d;
}

void AudioRouter::closeAll()
{
    // le producteur (micro) d'abord, puis les consommateurs
    if (inDev != nullptr)  { inDev->stop();  inDev->close();  inDev.reset(); }
    if (outDev != nullptr) { outDev->stop(); outDev->close(); outDev.reset(); }
    if (monDev != nullptr) { monDev->stop(); monDev->close(); monDev.reset(); }
    engine.releaseEngine();
}

void AudioRouter::openAll()
{
    status = {};
    auto* t = typeFor (config.mode);
    auto* sharedType = typeFor (shared);
    if (t == nullptr || sharedType == nullptr)
    {
        status.input.error = status.output.error = "Windows audio (WASAPI) unavailable";
        return;
    }

    inDev = openDevice (*t, config.input, true, config.bufferSize, status.input.error);
    outDev = openDevice (*t, config.output, false, config.bufferSize, status.output.error);
    if (config.monitor.isNotEmpty() && config.monitor == config.output && config.mode == exclusive)
        status.monitor.error = "Already used by the virtual mic (exclusive mode)";
    else
        monDev = openDevice (*sharedType, config.monitor, false, 0, status.monitor.error);

    const double inRate = inDev != nullptr ? inDev->getCurrentSampleRate() : 48000.0;
    const int inBlock = inDev != nullptr ? inDev->getCurrentBufferSizeSamples() : 480;
    if (outDev != nullptr)
        virtualBridge.prepare (inRate, outDev->getCurrentSampleRate(), inBlock, outDev->getCurrentBufferSizeSamples());
    if (monDev != nullptr)
        monitorBridge.prepare (inRate, monDev->getCurrentSampleRate(), inBlock, monDev->getCurrentBufferSizeSamples());

    inputCb->channel = config.inputChannel;

    // les sorties d'abord (elles jouent du silence en attendant), puis le micro
    if (outDev != nullptr) outDev->start (outputCb.get());
    if (monDev != nullptr) monDev->start (monitorCb.get());
    if (inDev != nullptr)  inDev->start (inputCb.get());
}

void AudioRouter::setConfig (const Config& c, bool forceRestart)
{
    const bool onlyChannel = ! forceRestart && c.input == config.input && c.output == config.output && c.monitor == config.monitor
                             && c.mode == config.mode && c.bufferSize == config.bufferSize && inDev != nullptr;
    config = c;
    if (onlyChannel)
    {
        inputCb->channel = config.inputChannel;   // simple choix de canal : sans rien rouvrir
        return;
    }
    closeAll();
    openAll();
    watchdogCooldown = 3;
    timerCallback();
}

//==============================================================================
AudioRouter::Status AudioRouter::getStatus() const
{
    auto s = status;
    auto fill = [] (Stream& st, juce::AudioIODevice* d, const juce::String& name)
    {
        st.name = name;
        st.active = d != nullptr && d->isPlaying();
        if (d != nullptr)
        {
            st.rate = d->getCurrentSampleRate();
            st.block = d->getCurrentBufferSizeSamples();
            st.channels = d->getActiveInputChannels().countNumberOfSetBits() + d->getActiveOutputChannels().countNumberOfSetBits();
        }
    };
    fill (s.input, inDev.get(), config.input);
    fill (s.output, outDev.get(), config.output);
    fill (s.monitor, monDev.get(), config.monitor);

    if (inDev != nullptr && outDev != nullptr)
    {
        const double inRate = inDev->getCurrentSampleRate(), outRate = outDev->getCurrentSampleRate();
        s.latencyMs = 1000.0 * inDev->getInputLatencyInSamples() / inRate
                    + 1000.0 * engine.getProcessingLatencySamples() / inRate
                    + virtualBridge.getTargetLatencyMs()
                    + 1000.0 * outDev->getOutputLatencyInSamples() / outRate;
        s.driftPpm = virtualBridge.getCorrectionPpm();
        s.dropouts = virtualBridge.getUnderruns();
    }
    return s;
}

void AudioRouter::audioDeviceListChanged()
{
    if (onDeviceListChanged) onDeviceListChanged();
    watchdogCooldown = 0;   // un périphérique est peut-être revenu : on vérifie tout de suite
    timerCallback();
}

void AudioRouter::timerCallback()
{
    // chien de garde : un périphérique choisi qui ne joue plus (débranché, veille, pris par une autre
    // application) est rouvert dès qu'il est de nouveau présent
    auto lost = [] (const juce::String& name, const std::unique_ptr<juce::AudioIODevice>& d)
    {
        return name.isNotEmpty() && (d == nullptr || ! d->isPlaying());
    };
    const auto ins = getInputNames(), outs = getOutputNames();

    if (watchdogCooldown > 0) --watchdogCooldown;
    else
    {
        const bool inLost = lost (config.input, inDev) && ins.contains (config.input);
        const bool outLost = lost (config.output, outDev) && outs.contains (config.output);
        const bool monLost = lost (config.monitor, monDev) && outs.contains (config.monitor)
                             && ! (config.monitor == config.output && config.mode == exclusive);

        if (inLost)
        {
            closeAll();
            openAll();
            watchdogCooldown = 3;
        }
        else if (outLost || monLost)
        {
            // sortie seule : le micro et le traitement continuent, seul le consommateur est rouvert
            const double inRate = inDev != nullptr ? inDev->getCurrentSampleRate() : 48000.0;
            const int inBlock = inDev != nullptr ? inDev->getCurrentBufferSizeSamples() : 480;
            if (outLost)
            {
                if (outDev != nullptr) { outDev->stop(); outDev->close(); outDev.reset(); }
                outDev = openDevice (*typeFor (config.mode), config.output, false, config.bufferSize, status.output.error);
                if (outDev != nullptr)
                {
                    virtualBridge.prepare (inRate, outDev->getCurrentSampleRate(), inBlock, outDev->getCurrentBufferSizeSamples());
                    outDev->start (outputCb.get());
                }
            }
            if (monLost)
            {
                if (monDev != nullptr) { monDev->stop(); monDev->close(); monDev.reset(); }
                monDev = openDevice (*typeFor (shared), config.monitor, false, 0, status.monitor.error);
                if (monDev != nullptr)
                {
                    monitorBridge.prepare (inRate, monDev->getCurrentSampleRate(), inBlock, monDev->getCurrentBufferSizeSamples());
                    monDev->start (monitorCb.get());
                }
            }
            watchdogCooldown = 3;
        }
        // périphérique absent : message clair tant qu'il n'est pas revenu
        if (lost (config.input, inDev) && ! ins.contains (config.input))       status.input.error = "Disconnected - waiting for the device";
        if (lost (config.output, outDev) && ! outs.contains (config.output))   status.output.error = "Disconnected - waiting for the device";
        if (lost (config.monitor, monDev) && ! outs.contains (config.monitor)) status.monitor.error = "Disconnected - waiting for the device";
    }

    if (onStatusChanged) onStatusChanged();
}

//==============================================================================
bool AudioRouter::isVirtualCable (const juce::String& n)
{
    static const char* keys[] { "CABLE", "VB-Audio", "VoiceMeeter", "Virtual Audio Cable", "Virtual Cable", "Hi-Fi Cable",
                                "VAIO", "Voicemod", "Virtual Speakers", "Virtual Mic" };
    for (auto* k : keys)
        if (n.containsIgnoreCase (k)) return true;
    return false;
}

void AudioRouter::rescan()
{
    if (types[shared] != nullptr) types[shared]->scanForDevices();
}

juce::String AudioRouter::recordingNameFor (const juce::String& n) const
{
    // l'entrée et la sortie d'un câble virtuel partagent le nom du pilote entre parenthèses :
    // "CABLE Input (VB-Audio Virtual Cable)" <-> "CABLE Output (VB-Audio Virtual Cable)" (ou "OB (VB-Audio Virtual Cable)"
    // une fois renommé)
    const auto suffix = n.fromLastOccurrenceOf ("(", true, false);
    if (suffix.length() > 2)
    {
        juce::StringArray same;
        for (const auto& in : getInputNames())
            if (in.endsWith (suffix)) same.add (in);
        if (same.size() == 1) return same[0];
        for (const auto& in : same)
            if (in.containsIgnoreCase ("Output")) return in;
    }
    // VB-CABLE : "CABLE Input (VB-Audio Virtual Cable)" -> "CABLE Output (VB-Audio Virtual Cable)"
    if (n.containsIgnoreCase ("CABLE In 16ch")) return n.replace ("CABLE In 16ch", "CABLE Output", true);
    if (n.containsIgnoreCase ("Input"))         return n.replace ("Input", "Output", true);
    // Virtual Audio Cable (Line 1...) : la sortie d'enregistrement porte le même nom
    return n;
}

juce::String AudioRouter::findVirtualCable (const juce::StringArray& outs)
{
    for (const char* pref : { "CABLE Input", "VoiceMeeter Input", "Virtual Audio Cable", "Hi-Fi Cable Input", "CABLE" })
        for (const auto& o : outs)
            if (o.containsIgnoreCase (pref)) return o;
    for (const auto& o : outs)
        if (isVirtualCable (o)) return o;
    return {};
}

bool AudioRouter::isEchoReferenceRunning() const
{
    return isProcessLoopbackRunning (playback);
}
