// Tests hors ligne du cœur audio d'ONIRIQUE BROADCAST (sans périphérique) :
//  - pont entre horloges (DriftResampler) : dérive, fréquences différentes, gigue des pilotes, qualité du signal ;
//  - chaîne complète (BroadcastEngine) : stabilité, plafond du limiteur, NaN, gate, mute, bypass, réordonnancement.
#include <juce_dsp/juce_dsp.h>
#include "Engine.h"
#include "Audio/DriftResampler.h"
#include "Platform.h"

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

// Énergie hors de la fondamentale (bruit + distorsion + images) par rapport au signal, en dB.
double thdN (const std::vector<float>& x, double fs, double f0)
{
    constexpr int order = 15, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const size_t start = x.size() - (size_t) N;
    for (int i = 0; i < N; ++i)
    {
        // fenêtre Blackman-Harris 4 termes : fuite spectrale < -92 dB
        const double t = 2.0 * juce::MathConstants<double>::pi * i / (N - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        buf[(size_t) i] = (float) (x[start + (size_t) i] * w);
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const int k0 = (int) std::lround (f0 * N / fs);
    double sig = 0.0, rest = 0.0;
    for (int k = 2; k < N / 2; ++k)
    {
        const double e = (double) buf[(size_t) k] * buf[(size_t) k];
        if (std::abs (k - k0) <= 8) sig += e; else rest += e;
    }
    return 10.0 * std::log10 (rest / sig + 1.0e-30);
}

struct BridgeResult { int underruns; double maxErr, ppm, thd; };

// Simule un micro et une sortie sur deux horloges indépendantes pendant `seconds` secondes.
BridgeResult runBridge (double inRate, double outRate, int inBlock, int outBlock, double driftPpm, double jitterMs, double seconds)
{
    onirique::DriftResampler br;
    br.prepare (inRate, outRate, inBlock, outBlock);   // le logiciel ne connaît que la fréquence nominale
    const double realOut = outRate * (1.0 + driftPpm * 1.0e-6);

    juce::Random rnd (42);
    std::vector<float> in ((size_t) inBlock), out ((size_t) outBlock), captured;
    const double f0 = 997.0;
    double phase = 0.0, maxErr = 0.0;
    int underAfterSettle = 0;
    int64_t inCount = 1, outCount = 1;
    const double outOffset = 0.0037;   // phase quelconque entre les deux pilotes

    // horloges stables + gigue de réveil des threads (non cumulative), comme de vrais pilotes
    auto jitter = [&] { return (rnd.nextDouble() - 0.5) * 2.0 * jitterMs * 0.001; };
    double nextIn = (double) inCount * inBlock / inRate + jitter();
    double nextOut = outOffset + (double) outCount * outBlock / realOut + jitter();

    while (nextOut < seconds)
    {
        if (nextIn <= nextOut)
        {
            for (int i = 0; i < inBlock; ++i)
            {
                in[(size_t) i] = (float) (0.5 * std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * f0 / inRate;
            }
            br.push (in.data(), inBlock, nextIn);
            nextIn = (double) (++inCount) * inBlock / inRate + jitter();
        }
        else
        {
            const int before = br.getUnderruns();
            br.pull (out.data(), outBlock, nextOut);
            if (nextOut > 12.0)
            {
                underAfterSettle += br.getUnderruns() - before;
                if (nextOut > 30.0) maxErr = std::max (maxErr, std::abs (br.getFillError()));
            }
            if (nextOut > seconds - 2.0) captured.insert (captured.end(), out.begin(), out.end());
            nextOut = outOffset + (double) (++outCount) * outBlock / realOut + jitter();
        }
    }
    return { underAfterSettle, maxErr, br.getCorrectionPpm(), thdN (captured, outRate, f0) };
}

void testBridge()
{
    std::printf ("\nDriftResampler\n");
    {
        const auto r = runBridge (48000.0, 48000.0, 480, 480, 0.0, 0.3, 60.0);
        check (r.underruns == 0, "same clock, 480/480, jitter 0.3 ms: no dropouts (" + juce::String (r.underruns) + ")");
        check (r.thd < -90.0, "same clock: THD+N " + juce::String (r.thd, 1) + " dB (< -90)");
    }
    for (double ppm : { -400.0, -120.0, 150.0, 500.0 })
    {
        const auto r = runBridge (48000.0, 48000.0, 480, 480, ppm, 0.5, 240.0);
        check (r.underruns == 0 && std::abs (r.ppm + ppm) < 60.0,
               "drift " + juce::String (ppm, 0) + " ppm: dropouts " + juce::String (r.underruns) + ", correction "
               + juce::String (r.ppm, 0) + " ppm (expected ~" + juce::String (-ppm, 0) + "), max fill error "
               + juce::String (r.maxErr, 0) + " smp");
        check (r.thd < -85.0, "drift " + juce::String (ppm, 0) + " ppm: THD+N " + juce::String (r.thd, 1) + " dB (< -85)");
    }
    {
        const auto r = runBridge (48000.0, 44100.0, 480, 441, 80.0, 0.5, 120.0);
        check (r.underruns == 0, "48 kHz -> 44.1 kHz + 80 ppm: no dropouts (" + juce::String (r.underruns) + ")");
        check (r.thd < -80.0, "48 kHz -> 44.1 kHz: THD+N " + juce::String (r.thd, 1) + " dB (< -80)");
    }
    {
        const auto r = runBridge (44100.0, 48000.0, 441, 512, -60.0, 0.8, 120.0);
        check (r.underruns == 0, "44.1 kHz -> 48 kHz, blocks 441/512, jitter 0.8 ms: no dropouts (" + juce::String (r.underruns) + ")");
    }
    {
        const auto r = runBridge (48000.0, 48000.0, 128, 1024, 200.0, 0.5, 120.0);
        check (r.underruns == 0, "small mic blocks (128) / large output blocks (1024): no dropouts (" + juce::String (r.underruns) + ")");
    }
}

void setParam (BroadcastEngine& e, const char* id, float v)
{
    auto* p = e.apvts.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void testEngine()
{
    std::printf ("\nBroadcastEngine\n");
    BroadcastEngine e;
    e.presets.loadByName ("Radio Broadcast");
    const double fs = 48000.0;
    e.prepareEngine (fs, 480);
    std::printf ("  latency: %d samples (%.2f ms)\n", e.getProcessingLatencySamples(), 1000.0 * e.getProcessingLatencySamples() / fs);

    juce::Random rnd (7);
    std::vector<float> buf (480);
    double ph = 0.0;
    auto render = [&] (float amp, int blocks, float& peak, bool& bad, double& rms)
    {
        peak = 0.0f; bad = false;
        double acc = 0.0; int n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (auto& s : buf)
            {
                // voix synthétique : fondamentale 140 Hz + harmoniques + souffle
                s = amp * (float) (0.6 * std::sin (ph) + 0.25 * std::sin (3.0 * ph) + 0.12 * std::sin (7.0 * ph))
                    + amp * 0.05f * (rnd.nextFloat() * 2.0f - 1.0f);
                ph += 2.0 * juce::MathConstants<double>::pi * 140.0 / fs;
            }
            e.processVoice (buf.data(), (int) buf.size());
            for (float s : buf)
            {
                if (! std::isfinite (s)) bad = true;
                peak = std::max (peak, std::abs (s));
                if (b > blocks / 2) { acc += (double) s * s; ++n; }
            }
        }
        rms = std::sqrt (acc / std::max (1, n));
    };

    float peak; bool bad; double rms;
    render (1.5f, 400, peak, bad, rms);   // +3.5 dBFS d'entrée : le limiteur doit tenir
    const float ceiling = juce::Decibels::decibelsToGain (e.apvts.getRawParameterValue (onirique::ids::limCeiling)->load());
    check (! bad, "very loud input: no NaN / Inf");
    check (peak <= ceiling * 1.001f, "very loud input: output peak " + juce::String (juce::Decibels::gainToDecibels (peak), 2)
                                      + " dBFS <= ceiling " + juce::String (juce::Decibels::gainToDecibels (ceiling), 2) + " dBFS");

    render (0.1f, 400, peak, bad, rms);
    check (! bad && rms > 0.01, "normal voice level: signal passes (" + juce::String (juce::Decibels::gainToDecibels ((float) rms), 1) + " dBFS RMS)");

    // gate : bruit de pièce très faible -> atténué de RANGE dB
    setParam (e, onirique::ids::gateThr, -40.0f);
    setParam (e, onirique::ids::gateRange, 30.0f);
    setParam (e, onirique::ids::fetOn, 0.0f);
    setParam (e, onirique::ids::mbOn, 0.0f);
    setParam (e, onirique::ids::satOn, 0.0f);
    double rmsOpen, rmsClosed;
    render (0.003f, 300, peak, bad, rmsClosed);   // ~ -55 dBFS : sous le seuil
    setParam (e, onirique::ids::gateOn, 0.0f);
    render (0.003f, 300, peak, bad, rmsOpen);
    const double att = juce::Decibels::gainToDecibels ((float) (rmsClosed / rmsOpen));
    check (att < -25.0 && att > -35.0, "gate closed on room noise: " + juce::String (att, 1) + " dB (range 30 dB)");
    setParam (e, onirique::ids::gateOn, 1.0f);

    // mute -> silence
    setParam (e, onirique::ids::mute, 1.0f);
    render (0.3f, 100, peak, bad, rms);
    check (rms < 1.0e-6, "mute: clean silence");
    setParam (e, onirique::ids::mute, 0.0f);

    // réordonnancement + bypass en cours de route : rien d'instable
    float worst = 0.0f;
    for (int k = 0; k < 20; ++k)
    {
        setParam (e, onirique::ids::chainOrder, (float) ((k * 97) % 720));
        setParam (e, onirique::ids::bypass, (float) (k % 3 == 0));
        render (0.2f, 10, peak, bad, rms);
        worst = std::max (worst, peak);
        if (bad) break;
    }
    check (! bad && worst < 1.0f, "20 chain reorders + bypass toggles: stable, peak " + juce::String (worst, 3));

    // tous les modules actifs : charge CPU d'une seconde de son
    for (auto* id : { onirique::ids::gateOn, onirique::ids::eqOn, onirique::ids::fetOn, onirique::ids::mbOn, onirique::ids::deessOn, onirique::ids::satOn })
        setParam (e, id, 1.0f);
    setParam (e, onirique::ids::bypass, 0.0f);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    render (0.2f, 100, peak, bad, rms);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    check (! bad, "all modules on: no NaN");
    std::printf ("  CPU: 1 s of audio in %.1f ms (%.2f %% of one core)\n", ms, ms / 10.0);
}
} // namespace

// Niveau de sortie de chaque preset d'usine pour une voix parlée réaliste (syllabes modulées, ~ -20 dBFS RMS,
// crêtes ~ -8 dBFS). Cible broadcast : ~ -18 à -14 dBFS RMS, crêtes sous le plafond du limiteur.
void testPresetLevels()
{
    std::printf ("\nPreset output levels (speech-like input, -20 dBFS RMS)\n");
    const double fs = 48000.0;
    for (auto& entry : BroadcastEngine().presets.list())
    {
        BroadcastEngine e;
        e.presets.load (entry);
        e.prepareEngine (fs, 480);
        juce::Random rnd (3);
        std::vector<float> buf (480);
        double ph = 0.0, env = 0.0, accIn = 0.0, accOut = 0.0;
        float pkOut = 0.0f;
        int64_t n = 0;
        for (int b = 0; b < 1500; ++b)   // 15 s
        {
            for (auto& s : buf)
            {
                const double t = (double) n / fs;
                // syllabes ~4 Hz, phrases ~0.3 Hz, fondamentale qui glisse 110..190 Hz
                const double syl = std::pow (std::max (0.0, std::sin (2.0 * juce::MathConstants<double>::pi * 4.1 * t)), 0.6);
                const double phr = 0.55 + 0.45 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.31 * t);
                env += 0.01 * (syl * phr - env);
                const double f0 = 150.0 + 40.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.7 * t);
                ph += 2.0 * juce::MathConstants<double>::pi * f0 / fs;
                double v = 0.0;
                for (int h = 1; h <= 24; ++h) v += std::sin (h * ph) / (double) h * (h > 4 ? 0.5 : 1.0);
                s = (float) (0.16 * env * v + 0.002 * (rnd.nextFloat() * 2.0f - 1.0f));
                accIn += (double) s * s;
                ++n;
            }
            e.processVoice (buf.data(), (int) buf.size());
            if (b > 300)
                for (float s : buf) { accOut += (double) s * s; pkOut = std::max (pkOut, std::abs (s)); }
        }
        const double rmsIn = std::sqrt (accIn / (double) n), rmsOut = std::sqrt (accOut / (480.0 * 1199.0));
        std::printf ("  %-18s in %6.1f dBFS RMS  ->  out %6.1f dBFS RMS, peak %6.1f dBFS\n", entry.name.toRawUTF8(),
                     juce::Decibels::gainToDecibels ((float) rmsIn), juce::Decibels::gainToDecibels ((float) rmsOut),
                     juce::Decibels::gainToDecibels (pkOut));
    }
}

// Fonctions Windows en lecture seule : vérification de signature (installateur VB-CABLE) et format des câbles.
void testPlatform()
{
    std::printf ("\nWindows platform (read-only)\n");
    const juce::File vb ("C:\\Program Files\\VB\\CABLE\\VBCABLE_Setup_x64.exe");
    if (vb.existsAsFile())
        check (verifySignedBy (vb.getFullPathName().toWideCharPointer(), L"BUREL VINCENT"), "genuine VB-CABLE installer: signature accepted");
    check (! verifySignedBy (L"C:\\Windows\\System32\\notepad.exe", L"BUREL VINCENT"), "program signed by someone else (notepad): refused");
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    check (! verifySignedBy (self.getFullPathName().toWideCharPointer(), L"BUREL VINCENT"), "unsigned program: refused");
    const int out = getEndpointSampleRate (L"CABLE Input (VB-Audio Virtual Cable)", false);
    const int rec = getEndpointSampleRate (L"OB (VB-Audio Virtual Cable)", true);
    std::printf ("  CABLE Input: %d Hz, OB: %d Hz (0 = not present)\n", out, rec);
    check (getEndpointSampleRate (L"No such device", true) == 0, "unknown device: rate 0 (no false alarm)");
}

// Garde d'écho, chaîne complète : Discord joue (voix des autres), une fuite du casque arrive dans le micro (-32 dB,
// 30 ms de retard), l'utilisateur parle par moments PAR-DESSUS les autres. On compare garde activée / désactivée.
void testEchoGuard()
{
    std::printf ("\nEcho guard (Discord playback leaking into the mic)\n");
    struct Result { double echoOnly, doubleTalk, nearAlone; float coupling; };
    auto run = [] (bool guard, float leakGain = 0.025f)
    {
        const double fs = 48000.0;
        BroadcastEngine e;
        e.presets.loadByName ("Streamer Clean");
        setParam (e, onirique::ids::gateGuard, guard ? 1.0f : 0.0f);
        e.prepareEngine (fs, 480);

        juce::Random rnd (5);
        std::vector<float> mic (480), play (480);
        const int delay = (int) (0.030 * fs);
        std::vector<float> history ((size_t) delay + 480, 0.0f);
        double phF = 0.0, phN = 0.0, accEcho = 0.0, accDouble = 0.0, accNear = 0.0;
        int nEcho = 0, nDouble = 0, nNear = 0;
        for (int b = 0; b < 2000; ++b)   // 20 s
        {
            const double t0 = b * 480 / fs;
            // l'utilisateur parle : 12-13 s seul (Discord muet), 15-17 s par-dessus les autres
            const bool nearTalks = (t0 >= 12.0 && t0 < 13.0) || (t0 >= 15.0 && t0 < 17.0);
            const bool farTalks = ! (t0 >= 12.0 && t0 < 13.0);
            for (int i = 0; i < 480; ++i)
            {
                const double t = (b * 480 + i) / fs;
                const double sylF = std::max (0.0, std::sin (2.0 * juce::MathConstants<double>::pi * 3.3 * t));
                phF += 2.0 * juce::MathConstants<double>::pi * (120.0 + 30.0 * std::sin (t)) / fs;
                double f = 0.0;
                for (int h = 1; h <= 20; ++h) f += std::sin (h * phF) / h;
                play[(size_t) i] = farTalks ? (float) (0.25 * sylF * f) : 0.0f;   // ~ -18 dBFS RMS

                const double sylN = std::pow (std::max (0.0, std::sin (2.0 * juce::MathConstants<double>::pi * 4.2 * t + 1.0)), 0.5);
                phN += 2.0 * juce::MathConstants<double>::pi * (190.0 + 25.0 * std::sin (1.7 * t)) / fs;
                double nv = 0.0;
                for (int h = 1; h <= 20; ++h) nv += std::sin (h * phN) / h;
                const float near = nearTalks ? (float) (0.10 * sylN * nv) : 0.0f;   // voix ~ -26 dBFS RMS au micro

                // fuite : le son joué, retardé de 30 ms, atténué de 32 dB
                history.push_back (play[(size_t) i]);
                const float leak = history[history.size() - 1 - (size_t) delay] * leakGain;
                mic[(size_t) i] = near + leak + 0.0003f * (rnd.nextFloat() * 2.0f - 1.0f);
            }
            history.erase (history.begin(), history.begin() + 480);

            e.pushPlayback (play.data(), 480);
            e.processVoice (mic.data(), 480);
            double acc = 0.0;
            for (float s : mic) acc += (double) s * s;
            if (t0 >= 8.0 && t0 < 12.0)  { accEcho += acc;   nEcho += 480; }
            if (t0 >= 15.2 && t0 < 17.0) { accDouble += acc; nDouble += 480; }
            if (t0 >= 12.1 && t0 < 13.0) { accNear += acc;   nNear += 480; }
        }
        auto db = [] (double a, int n) { return 10.0 * std::log10 (a / std::max (1, n) + 1.0e-20); };
        return Result { db (accEcho, nEcho), db (accDouble, nDouble), db (accNear, nNear), e.getEchoGuardMeter().couplingDb.load() };
    };

    const auto off = run (false), on = run (true);
    std::printf ("  echo only (others talking):  guard off %.1f dBFS  /  guard on %.1f dBFS\n", off.echoOnly, on.echoOnly);
    std::printf ("  you talking over the others: guard off %.1f dBFS  /  guard on %.1f dBFS\n", off.doubleTalk, on.doubleTalk);
    std::printf ("  you alone:                   guard off %.1f dBFS  /  guard on %.1f dBFS\n", off.nearAlone, on.nearAlone);
    std::printf ("  learned coupling: %.1f dB (true leak -32 dB)\n", on.coupling);
    check (off.echoOnly - on.echoOnly > 20.0, "echo sent to Discord reduced by " + juce::String (off.echoOnly - on.echoOnly, 1) + " dB (> 20)");
    check (std::abs (off.doubleTalk - on.doubleTalk) < 1.5, "your voice over the others kept (" + juce::String (on.doubleTalk - off.doubleTalk, 2) + " dB)");
    check (std::abs (off.nearAlone - on.nearAlone) < 0.5, "your voice alone unchanged (" + juce::String (on.nearAlone - off.nearAlone, 2) + " dB)");

    // casque bien isolé : aucune fuite -> la garde reste en veille, rien ne change
    const auto isoOff = run (false, 0.0f), isoOn = run (true, 0.0f);
    check (isoOn.coupling < -100.0f && std::abs (isoOn.doubleTalk - isoOff.doubleTalk) < 0.05 && std::abs (isoOn.nearAlone - isoOff.nearAlone) < 0.05,
           "no leak (isolated headphones): guard stays idle, output identical");
}

// --capture <pid> <secondes> : capture le son d'un processus comme le partage d'écran de Discord, et l'analyse
int captureProcess (unsigned long pid, double seconds)
{
    struct Acc { std::atomic<double> energy { 0.0 }; std::atomic<long long> samples { 0 }; std::atomic<float> peak { 0.0f }; } acc;
    auto* lb = startProcessCapture (pid, [] (void* ctx, const float* x, int n)
    {
        auto& a = *static_cast<Acc*> (ctx);
        double e = 0.0;
        float pk = a.peak.load();
        for (int i = 0; i < n; ++i) { e += (double) x[i] * x[i]; pk = std::max (pk, std::abs (x[i])); }
        a.energy = a.energy.load() + e;
        a.samples = a.samples.load() + n;
        a.peak = pk;
    }, &acc, 48000);
    juce::Thread::sleep ((int) (seconds * 1000.0));
    const bool ok = isProcessLoopbackRunning (lb);
    stopProcessLoopback (lb);
    const double rms = acc.samples.load() > 0 ? std::sqrt (acc.energy.load() / (double) acc.samples.load()) : 0.0;
    std::printf ("capture of process %lu for %.1f s: capture running=%d, %lld samples, RMS %.1f dBFS, peak %.1f dBFS\n", pid, seconds, (int) ok,
                 acc.samples.load(), rms > 0.0 ? 20.0 * std::log10 (rms) : -999.0, acc.peak.load() > 0.0f ? 20.0 * std::log10 (acc.peak.load()) : -999.0);
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 4 && juce::String (argv[1]) == "--capture")
        return captureProcess ((unsigned long) juce::String (argv[2]).getLargeIntValue(), juce::String (argv[3]).getDoubleValue());
    testPlatform();
    testEchoGuard();
    testBridge();
    testEngine();
    testPresetLevels();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
