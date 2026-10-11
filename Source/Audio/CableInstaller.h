#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

/*  Installation assistée de VB-CABLE (pilote de câble audio virtuel gratuit de VB-Audio) quand aucun câble
    virtuel n'est présent sur le PC. Tourne dans son propre thread, l'interface lit l'état.

        1. téléchargement du pack officiel depuis download.vb-audio.com ;
        2. extraction ;
        3. vérification : le programme d'installation doit porter une signature Authenticode valide de l'éditeur
           de VB-Audio ("BUREL VINCENT") - sinon rien n'est exécuté ;
        4. installation silencieuse (-i -h) avec les droits administrateur : Windows demande l'autorisation à
           l'utilisateur (et peut demander de confirmer l'installation du pilote).
*/
class CableInstaller : private juce::Thread
{
public:
    enum class State { idle, downloading, verifying, installing, done, failed };

    CableInstaller() : juce::Thread ("VB-CABLE install") {}
    ~CableInstaller() override { stopThread (3000); }

    void start()
    {
        if (isThreadRunning()) return;
        state = State::downloading;
        progress = 0.0f;
        startThread();
    }

    State getState() const noexcept { return state.load(); }
    float getProgress() const noexcept { return progress.load(); }
    bool isBusy() const noexcept { const auto s = state.load(); return s == State::downloading || s == State::verifying || s == State::installing; }
    juce::String getMessage() const { const juce::ScopedLock sl (lock); return message; }
    void reset() { if (! isThreadRunning()) state = State::idle; }

    static constexpr const char* kPackUrl = "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip";
    static constexpr const char* kWebsite = "https://vb-audio.com/Cable/";

private:
    void run() override;
    void fail (const juce::String& m) { { const juce::ScopedLock sl (lock); message = m; } state = State::failed; }

    std::atomic<State> state { State::idle };
    std::atomic<float> progress { 0.0f };
    juce::CriticalSection lock;
    juce::String message;
};
