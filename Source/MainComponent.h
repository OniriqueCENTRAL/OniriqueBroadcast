#pragma once

#include "UI/Displays.h"
#include "Audio/AudioRouter.h"
#include "Audio/CableInstaller.h"
#include "UI/OniriqueBar.h"

// Bouton d'action en pastille (COPY, GET VB-CABLE...) : lueur au survol, clic = onClick.
class ActionPill : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit ActionPill (const juce::String& t, bool accent = false) : text (t), strong (accent)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    std::function<void()> onClick;
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void flash() { flashAmt = 1.0f; }

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; }
    void mouseUp (const juce::MouseEvent& e) override  { if (onClick && getLocalBounds().contains (e.getPosition())) onClick(); }

private:
    juce::String text;
    bool strong = false, hover = false;
    float glow = 0.0f, flashAmt = 0.0f;
};

class MainComponent : public onirique::ui::ScaledRoot,
                      private juce::Timer
{
public:
    MainComponent (BroadcastEngine&, AudioRouter&, juce::PropertiesFile&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (bool)> onTrayPreferenceChanged;
    std::function<void (const juce::MouseEvent&, bool start)> onWindowDrag;   // en-tête vide : déplacer la fenêtre
    std::function<void()> onToggleMaximise;                                   // double-clic : agrandir / restaurer

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // réglages de l'application (non sonores)
    static bool isAutostartEnabled();
    static void setAutostart (bool);

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using Layout = KnobComponent::Layout;

    struct Section
    {
        juce::String title;
        juce::Rectangle<float> bounds;
        PowerButton* power = nullptr;
        float veil = 0.0f;
    };

    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / onirique::ui::kBaseW; }
    void paintHeader (juce::Graphics&);
    float titleWidth() const;
    void paintTitle (juce::Graphics&, juce::Rectangle<float> area) const;   // le titre, dans la barre du haut (en points)
    void paintRouting (juce::Graphics&);
    void paintStar (juce::Graphics&, juce::Point<float> c, float r, float alpha);
    void selectPeqBand (int band);
    void refreshStatus();
    void applyConfig (AudioRouter::Config);
    void askRename();
    void askInstallCable();
    void installFinished();
    void fixFormat();
    void checkFormat();
    void doRename (const juce::String&);
    void saveConfig();
    std::vector<PillMenu::Item> deviceItems (bool inputs, bool allowNone, bool markCables);

    BroadcastEngine& engine;
    AudioRouter& router;
    juce::PropertiesFile& settings;
    onirique::ui::FontScope fontScope;   // polices de la charte, le temps que la fenêtre est ouverte
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };

    // ---- en-tête
    ProcessingOrderBar orderBar { engine.apvts };
    PillToggle muteButton { "MUTE", PillToggle::Style::big };
    std::unique_ptr<ButtonAttachment> muteAtt;
    // la barre du haut des plugins ONIRIQUE : le titre, les presets, IN (bypass), A/B, copier / coller
    onirique::nuit::Bar bar { engine.presets, engine.apvts, "BROADCAST", {}, (int) onirique::ui::kBaseW, (int) onirique::ui::kBaseW };

    // ---- ROUTING
    PillMenu inputMenu { "" }, outputMenu { "" }, monitorMenu { "" }, bufferMenu { "BUFFER" };
    SegmentSelector channelSel { { "IN 1", "IN 2", "1 + 2" },
                                 { "Use input channel 1 only (mic plugged into input 1 of an interface).",
                                   "Use input channel 2 only.",
                                   "Mix channels 1 and 2 (USB microphones, stereo devices)." } };
    SegmentSelector modeSel { { "SHARED", "LOW LAT", "EXCLUSIVE" },
                              { "Windows shared mode: works with every device, other apps can still use them (~10 ms buffers).",
                                "Shared low-latency mode (Windows 10+): smaller buffers when the driver supports it.",
                                "Exclusive mode: lowest latency, but the mic and the virtual cable are reserved for Onirique." } };
    SignalBar inputBar { [this] { return engine.consumeInputPeak(); } };
    KnobComponent inKnob    { engine.apvts, onirique::ids::input, "GAIN", "", "", Layout::horizontal,
                              "Microphone gain before the effects. Double-click = 0 dB.", lnf, true };
    PillToggle monitorToggle { "LISTEN", PillToggle::Style::mini };
    std::unique_ptr<ButtonAttachment> monitorAtt;
    KnobComponent monVolKnob { engine.apvts, onirique::ids::monVol, "VOLUME", "", "", Layout::horizontal,
                               "Headphone monitor volume (does not change what the virtual mic sends).", lnf };
    ActionPill copyButton { "COPY" }, cableButton { "INSTALL VB-CABLE (FREE)", true }, soundButton { "SOUND" }, renameButton { "RENAME" },
               fixButton { "SET 48 KHZ", true };
    CableInstaller installer;
    CableInstaller::State lastInstallState = CableInstaller::State::idle;
    PillToggle trayToggle { "KEEP RUNNING IN TRAY", PillToggle::Style::mini }, autostartToggle { "START WITH WINDOWS", PillToggle::Style::mini };

    // ---- EQ / ANALYSEUR
    SpectrumAnalyzer analyzer { engine };
    PeqBandChip peqChip { engine.apvts };
    std::array<std::unique_ptr<DragValue>, 6> peqFreqV, peqGainV, peqQV;

    // ---- NOISE GATE
    GateDisplay gateDisplay { engine };
    KnobComponent gateThrKnob  { engine.apvts, onirique::ids::gateThr,     "THRESHOLD", "-80", "0",  Layout::textBelow,
                                 "Level above which the gate opens. Drag the red line in the display or use LEARN. Double-click = reset.", lnf };
    KnobComponent gateRangeKnob { engine.apvts, onirique::ids::gateRange,  "RANGE",     "0",   "80", Layout::textBelow,
                                  "How much the background is lowered when you are not talking. ~20 dB stays natural, 80 dB = full mute.", lnf };
    KnobComponent gateAtkKnob  { engine.apvts, onirique::ids::gateAttack,  "ATTACK",    "0.1", "50", Layout::textBelow,
                                 "Opening speed. With the 1.5 ms look-ahead even fast settings never click.", lnf };
    KnobComponent gateHoldKnob { engine.apvts, onirique::ids::gateHold,    "HOLD",      "0",   "500", Layout::textBelow,
                                 "Time the gate stays open after the voice drops: keeps word endings and short pauses.", lnf };
    KnobComponent gateRelKnob  { engine.apvts, onirique::ids::gateRelease, "RELEASE",   "10",  "2k", Layout::textBelow,
                                 "Closing speed: slow = natural fade, fast = tight.", lnf };
    PillToggle guardToggle { "ECHO GUARD", PillToggle::Style::mini };
    std::unique_ptr<ButtonAttachment> guardAtt;
    DragValue gateHystV { engine.apvts, onirique::ids::gateHyst,
                          "Hysteresis: the gate closes this many dB below the threshold (no chattering). Drag up/down." };

    // ---- 1176
    SegmentSelector fetRatio { engine.apvts, onirique::ids::fetRatio, { "4", "8", "12", "20", "ALL" },
                               { "Ratio 4:1 - the vocal classic.", "Ratio 8:1.", "Ratio 12:1.", "Ratio 20:1 - limiting.",
                                 "All buttons in: extreme ratio, over-compression and aggressive distortion." } };
    VuMeter vu1176 { [this] { return engine.getFetReductionDb(); } };
    KnobComponent fetInKnob  { engine.apvts, onirique::ids::fetIn,      "INPUT",   "-12", "+36", Layout::textBelow,
                               "Drives the voice into the 1176 (fixed threshold, like the hardware): more input = more compression.", lnf };
    KnobComponent fetOutKnob { engine.apvts, onirique::ids::fetOut,     "OUTPUT",  "-24", "+24", Layout::textBelow,
                               "1176 output level.", lnf, true };
    KnobComponent fetAtkKnob { engine.apvts, onirique::ids::fetAttack,  "ATTACK",  "1",   "7",   Layout::textBelow,
                               "Attack, 1 (800 us) to 7 (20 us, fastest) - higher = faster, like the hardware.", lnf };
    KnobComponent fetRelKnob { engine.apvts, onirique::ids::fetRelease, "RELEASE", "1",   "7",   Layout::textBelow,
                               "Release, 1 (1.1 s) to 7 (50 ms, fastest).", lnf };
    KnobComponent fetMixKnob { engine.apvts, onirique::ids::fetMix,     "MIX", "", "", Layout::horizontal,
                               "Parallel compression: blend of compressed and dry voice (100 % = fully compressed).", lnf };

    // ---- MULTIBAND
    MultibandDisplay mbDisplay { engine };
    MbPresetBox mbPresets { engine.apvts };
    std::array<std::unique_ptr<KnobComponent>, 4> mbThr;
    std::array<std::unique_ptr<DragValue>, 4> mbGain;
    std::array<std::unique_ptr<PillToggle>, 4> mbListen;
    std::array<std::unique_ptr<ButtonAttachment>, 4> mbListenAtt;
    KnobComponent mbAttackKnob  { engine.apvts, onirique::ids::mbAttack,  "ATTACK",  "", "", Layout::horizontal,
                                  "Attack of all bands (lows a bit slower, highs a bit faster, automatically).", lnf };
    KnobComponent mbReleaseKnob { engine.apvts, onirique::ids::mbRelease, "RELEASE", "", "", Layout::horizontal,
                                  "Release of all bands, optical: slows down after long compressed phrases.", lnf };

    // ---- DE-ESSER / PLOSIVES
    KnobComponent plosiveFader { engine.apvts, onirique::ids::plosive, "PLOSIVES", "", "", Layout::horizontal,
                                 "Plosive suppressor: catches P / B pops and pulls the lows down only while they happen. "
                                 "The LED lights up when a plosive is caught.", lnf };
    KnobComponent deessFreqKnob { engine.apvts, onirique::ids::deessFreq, "FREQUENCY", "3k", "10k", Layout::textBelow,
                                  "De-esser frequency, 3 to 10 kHz.", lnf };
    KnobComponent deessAmtKnob  { engine.apvts, onirique::ids::deessAmt,  "INTENSITY", "0",  "100", Layout::textBelow,
                                  "How much the sibilance (S, SH, CH) is reduced.", lnf };
    GainReductionBar deessGr { [this] { return engine.getDeEssReductionDb(); },
                               "De-esser gain reduction (dB): how much the sibilance is being pulled down right now." };

    // ---- SATURATION
    SegmentSelector satType { engine.apvts, onirique::ids::satType, { "TUBE", "ANALOG", "TAPE" },
                              { "Tube: asymmetric tube stage, even harmonics, transients kept crisp.",
                                "Analog: class A + transformers, 1073-style. Thick lows, forward mids, rounded top.",
                                "Tape: softened highs and peaks, slow tape compression, head bump." } };
    KnobComponent driveKnob { engine.apvts, onirique::ids::drive, "DRIVE", "0", "10", Layout::textBelow,
                              "Saturation amount. The level stays constant: only the harmonics change.", lnf };

    // ---- OUTPUT
    KnobComponent outKnob  { engine.apvts, onirique::ids::output,     "OUTPUT",  "-24", "+24", Layout::textBelow,
                             "Level sent to the virtual mic. Double-click = 0 dB.", lnf, true };
    KnobComponent ceilKnob { engine.apvts, onirique::ids::limCeiling, "CEILING", "-12", "0",   Layout::textBelow,
                             "Limiter ceiling: the virtual mic never goes above this level (no clipping in Discord / OBS).", lnf };
    PillToggle limToggle { "LIMITER", PillToggle::Style::mini };
    std::unique_ptr<ButtonAttachment> limAtt;
    GainReductionBar limGr { [this] { return engine.getLimiterReductionDb(); },
                             "Limiter gain reduction (dB).", 12.0f };
    LevelMeter outMeter { [this] { return engine.consumeOutputPeak(); } };

    PowerButton eqPower, gatePower, fetPower, mbPower, deessPower, satPower;
    std::vector<std::unique_ptr<ButtonAttachment>> powerAtts;

    std::vector<Animated*> animated;
    std::vector<Section> sections;
    juce::Rectangle<float> routingBounds, vmStatusRect, engineInfoRect, onAirRect, overRect, signalRect;
    juce::Rectangle<float> secTitles[4];
    juce::Image logo, logoGlow;   // le logo du titre (dans la barre) et sa lueur, qui respire
    std::vector<std::pair<juce::Point<float>, float>> stars;
    double clock = 0.0;
    int frame = 0, overHold = 0;
    float overGlow = 0.0f, onAir = 0.0f, signalGlow = 0.0f;

    AudioRouter::Status status;
    juce::String recordingName;
    bool hasCable = false, draggingWindow = false;
    bool formatOk = true;          // micro virtuel en 48 kHz des deux côtés (sinon Windows rééchantillonne avant Discord)
    int formatCheckCountdown = 0;

    struct Overlay : juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        Overlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
    } veils;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
