#include "MainComponent.h"
#include "Platform.h"
#include <BinaryData.h>

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\OniriqueBroadcast";
}

//==============================================================================
void ActionPill::tick()
{
    bool changed = ui::approach (glow, hover ? 1.0f : 0.0f, 0.2f);
    if (flashAmt > 0.0f) { flashAmt = std::max (0.0f, flashAmt - 0.03f); changed = true; }
    if (changed) repaint();
}

void ActionPill::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 26.0f;
    const float lit = juce::jmax (glow, flashAmt);
    juce::Path p;
    p.addRoundedRectangle (r, h * 0.5f);
    ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.6f * lit + (strong ? 0.3f : 0.0f));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillPath (p);
    if (strong)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.55f + 0.25f * lit), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.6f), r.getRight(), r.getBottom(), false));
        g.fillPath (p);
    }
    g.setColour (ui::ember.withAlpha (0.15f * lit + 0.35f * flashAmt));
    g.fillPath (p);
    g.setColour (ui::line.interpolatedWith (ui::ember, juce::jmax (lit, strong ? 0.8f : 0.0f)));
    g.strokePath (p, juce::PathStrokeType (std::max (1.0f, u)));
    g.setColour ((strong ? ui::bone : ui::haze).interpolatedWith (ui::bone, lit));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.46f, r.getWidth() - 14.0f * u, text, true, 0.10f));
    g.drawText (text, r, juce::Justification::centred, false);
}

//==============================================================================
bool MainComponent::isAutostartEnabled()
{
    return juce::WindowsRegistry::valueExists (kRunKey);
}

void MainComponent::setAutostart (bool on)
{
    if (on)
        juce::WindowsRegistry::setValue (kRunKey, "\"" + juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName()
                                                  + "\" --minimized");
    else
        juce::WindowsRegistry::deleteValue (kRunKey);
}

//==============================================================================
MainComponent::MainComponent (BroadcastEngine& e, AudioRouter& r, juce::PropertiesFile& s)
    : engine (e), router (r), settings (s)
{
    tooltipWindow.setLookAndFeel (&lnf);
    setLookAndFeel (&lnf);
    setOpaque (true);

    // ---- boutons, curseurs
    for (auto* k : { &inKnob, &monVolKnob, &gateThrKnob, &gateRangeKnob, &gateAtkKnob, &gateHoldKnob, &gateRelKnob,
                     &fetInKnob, &fetOutKnob, &fetAtkKnob, &fetRelKnob, &fetMixKnob, &mbAttackKnob, &mbReleaseKnob,
                     &plosiveFader, &deessFreqKnob, &deessAmtKnob, &driveKnob, &outKnob, &ceilKnob })
    {
        addAndMakeVisible (k);
        animated.push_back (k);
    }

    static const char* bandNames[] { "LOW", "LO-MID", "HI-MID", "HIGH" };
    static const char* bandTips[] { "LOW band threshold: below the first crossover (boom, plosives).",
                                    "LO-MID band threshold: body and boxiness.",
                                    "HI-MID band threshold: presence and harshness.",
                                    "HIGH band threshold: air, breaths and sibilance." };
    for (int b = 0; b < 4; ++b)
    {
        mbThr[(size_t) b] = std::make_unique<KnobComponent> (engine.apvts, ids::mbThr[b], bandNames[b], "-60", "0", Layout::textBelow,
                                                              juce::String (bandTips[b]) + " Double-click to reset.", lnf);
        mbGain[(size_t) b] = std::make_unique<DragValue> (engine.apvts, ids::mbGain[b],
                                                           juce::String (bandNames[b]) + " band gain after compression: drag up/down. Double-click to reset.");
        mbGain[(size_t) b]->setPrefix ("GAIN ");
        mbListen[(size_t) b] = std::make_unique<PillToggle> ("LISTEN", PillToggle::Style::mini);
        mbListen[(size_t) b]->setTooltip (juce::String ("Listen to the ") + bandNames[b] + " band only.");
        mbListenAtt[(size_t) b] = std::make_unique<ButtonAttachment> (engine.apvts, ids::mbSolo[b], *mbListen[(size_t) b]);
        for (auto* c : std::initializer_list<juce::Component*> { mbThr[(size_t) b].get(), mbGain[(size_t) b].get(), mbListen[(size_t) b].get() })
            addAndMakeVisible (c);
        animated.push_back (mbThr[(size_t) b].get());
        animated.push_back (mbGain[(size_t) b].get());
        animated.push_back (mbListen[(size_t) b].get());
    }

    for (auto* c : std::initializer_list<juce::Component*> { &orderBar, &presetBar, &muteButton, &bypassButton,
                                                             &inputMenu, &outputMenu, &monitorMenu, &bufferMenu, &channelSel, &modeSel,
                                                             &inputBar, &monitorToggle, &copyButton, &cableButton, &soundButton, &renameButton, &fixButton,
                                                             &trayToggle, &autostartToggle,
                                                             &analyzer, &peqChip, &gateDisplay, &gateHystV, &guardToggle, &fetRatio, &vu1176,
                                                             &mbDisplay, &mbPresets, &deessGr, &satType, &limToggle, &limGr, &outMeter })
        addAndMakeVisible (c);
    for (auto* a : std::initializer_list<Animated*> { &orderBar, &presetBar, &muteButton, &bypassButton,
                                                      &inputMenu, &outputMenu, &monitorMenu, &bufferMenu, &channelSel, &modeSel,
                                                      &inputBar, &monitorToggle, &copyButton, &cableButton, &soundButton, &renameButton, &fixButton,
                                                      &trayToggle, &autostartToggle,
                                                      &analyzer, &peqChip, &gateDisplay, &gateHystV, &guardToggle, &fetRatio, &vu1176,
                                                      &mbDisplay, &mbPresets, &deessGr, &satType, &limToggle, &limGr, &outMeter })
        animated.push_back (a);

    // ---- boutons power : un par module
    struct PowerDef { PowerButton& b; const char* id; const char* name; };
    for (auto& d : { PowerDef { eqPower, ids::eqOn, "parametric EQ" }, PowerDef { gatePower, ids::gateOn, "noise gate" },
                     PowerDef { fetPower, ids::fetOn, "1176 compressor" }, PowerDef { mbPower, ids::mbOn, "multiband compressor" },
                     PowerDef { deessPower, ids::deessOn, "de-esser / plosive suppressor" }, PowerDef { satPower, ids::satOn, "saturation" } })
    {
        addAndMakeVisible (d.b);
        animated.push_back (&d.b);
        d.b.setTooltip (juce::String ("Turn the ") + d.name + " on/off (click-free).");
        powerAtts.push_back (std::make_unique<ButtonAttachment> (engine.apvts, d.id, d.b));
    }

    muteButton.setTooltip ("Mute the virtual microphone: the apps receive clean silence (smooth fade, no click).");
    muteButton.setCaption ("MUTED");
    bypassButton.setTooltip ("Bypass all the effects: the virtual mic receives your raw microphone.");
    muteAtt = std::make_unique<ButtonAttachment> (engine.apvts, ids::mute, muteButton);
    bypassAtt = std::make_unique<ButtonAttachment> (engine.apvts, ids::bypass, bypassButton);
    monitorToggle.setTooltip ("Hear the processed voice in your headphones (what the apps receive). Note: like any sound played "
                              "by the PC, this monitor can be picked up by a Discord screen share with sound - the virtual mic itself never is.");
    monitorAtt = std::make_unique<ButtonAttachment> (engine.apvts, ids::monOn, monitorToggle);
    limToggle.setTooltip ("Output limiter: guarantees the virtual mic never clips.");
    limAtt = std::make_unique<ButtonAttachment> (engine.apvts, ids::limOn, limToggle);
    plosiveFader.activity = [this] { return engine.getPlosiveReductionDb(); };
    gateHystV.setPrefix ("HYST ");
    guardAtt = std::make_unique<ButtonAttachment> (engine.apvts, ids::gateGuard, guardToggle);
    inputBar.setTooltip ("Microphone level (after GAIN). The red mark is the noise gate threshold.");
    inputBar.markerDb = [this]
    {
        return engine.apvts.getRawParameterValue (ids::gateOn)->load() > 0.5f ? engine.apvts.getRawParameterValue (ids::gateThr)->load()
                                                                               : std::numeric_limits<float>::quiet_NaN();
    };

    // ---- EQ paramétrique : réglages précis de la bande sélectionnée
    for (int b = 0; b < 6; ++b)
    {
        const juce::String n = "Band " + juce::String (b + 1);
        peqFreqV[(size_t) b] = std::make_unique<DragValue> (engine.apvts, ids::peqFreq[b], n + " frequency: drag up/down (Shift = fine), double-click = reset.");
        peqGainV[(size_t) b] = std::make_unique<DragValue> (engine.apvts, ids::peqGain[b], n + " gain (bell / shelves): drag up/down, double-click = 0 dB.");
        peqQV[(size_t) b]    = std::make_unique<DragValue> (engine.apvts, ids::peqQ[b],    n + " Q (width / resonance): drag up/down or mouse wheel on the dot.");
        peqFreqV[(size_t) b]->setPrefix ("FREQ ");
        peqGainV[(size_t) b]->setPrefix ("GAIN ");
        peqQV[(size_t) b]->setPrefix ("Q ");
        for (auto* v : { peqFreqV[(size_t) b].get(), peqGainV[(size_t) b].get(), peqQV[(size_t) b].get() })
        {
            addChildComponent (v);
            animated.push_back (v);
        }
    }
    analyzer.onSelect = [this] (int b) { selectPeqBand (b); };
    selectPeqBand (analyzer.getSelected());

    // ---- ROUTING : périphériques
    inputMenu.setTooltip ("Your physical microphone.");
    outputMenu.setTooltip ("Where the processed voice is sent: choose the INPUT of a virtual audio cable (e.g. CABLE Input). "
                           "Its OUTPUT then appears in Windows as a microphone for Discord, OBS and games.");
    monitorMenu.setTooltip ("Headphones / speakers to hear yourself (optional).");
    bufferMenu.setTooltip ("Driver buffer size. AUTO = the driver's period. Smaller = lower latency but more CPU.");

    inputMenu.items   = [this] { return deviceItems (true, false, false); };
    outputMenu.items  = [this] { return deviceItems (false, true, true); };
    monitorMenu.items = [this] { return deviceItems (false, true, false); };
    bufferMenu.items  = []
    {
        std::vector<PillMenu::Item> v { { "BUFFER SIZE", true, true } };
        for (auto* t : { "AUTO", "64 smp", "128 smp", "256 smp", "480 smp", "512 smp", "1024 smp" }) v.push_back ({ t });
        return v;
    };
    inputMenu.onSelect   = [this] (int, const juce::String& t) { auto c = router.getConfig(); c.input = t; applyConfig (c); };
    outputMenu.onSelect  = [this] (int, const juce::String& t) { auto c = router.getConfig(); c.output = t == "None" ? juce::String() : t; applyConfig (c); };
    monitorMenu.onSelect = [this] (int, const juce::String& t) { auto c = router.getConfig(); c.monitor = t == "None" ? juce::String() : t; applyConfig (c); };
    bufferMenu.onSelect  = [this] (int, const juce::String& t) { auto c = router.getConfig(); c.bufferSize = t == "AUTO" ? 0 : t.getIntValue(); applyConfig (c); };
    channelSel.onChange  = [this] (int i) { auto c = router.getConfig(); c.inputChannel = i; applyConfig (c); };
    modeSel.onChange     = [this] (int i) { auto c = router.getConfig(); c.mode = i; applyConfig (c); };

    copyButton.setTooltip ("Copy the microphone name to select in your apps.");
    copyButton.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (recordingName);
        copyButton.setText ("COPIED");
        copyButton.flash();
        juce::Component::SafePointer<ActionPill> safe (&copyButton);
        juce::Timer::callAfterDelay (1500, [safe] { if (safe != nullptr) safe->setText ("COPY"); });
    };
    cableButton.setTooltip ("Downloads and installs VB-CABLE, the free virtual audio cable driver from VB-Audio, "
                            "which creates the virtual microphone. Windows asks for administrator permission.");
    cableButton.onClick = [this] { if (! installer.isBusy()) askInstallCable(); };
    fixButton.setTooltip ("Sets the virtual cable to 48 kHz (the rate of Discord, OBS and games): no resampling by Windows, "
                          "so Discord's echo cancellation and noise suppression work cleanly on your voice.");
    fixButton.onClick = [this] { fixFormat(); };
    soundButton.setTooltip ("Opens the Windows recording devices: you can rename 'CABLE Output' (e.g. 'Onirique Mic') "
                            "or set it as the default microphone.");
    soundButton.onClick = [] { juce::File ("C:\\Windows\\System32\\control.exe").startAsProcess ("mmsys.cpl,,1"); };
    renameButton.setTooltip ("Rename the virtual microphone as it appears in Discord, OBS and games (e.g. OB).");
    renameButton.onClick = [this] { askRename(); };

    trayToggle.setTooltip ("When you close the window, Onirique Broadcast keeps running in the notification area "
                           "(the virtual mic stays live). Quit from the tray icon.");
    trayToggle.setToggleState (settings.getBoolValue ("keepInTray", true), juce::dontSendNotification);
    trayToggle.onClick = [this]
    {
        settings.setValue ("keepInTray", trayToggle.getToggleState());
        settings.saveIfNeeded();
        if (onTrayPreferenceChanged) onTrayPreferenceChanged (trayToggle.getToggleState());
    };
    autostartToggle.setTooltip ("Launch Onirique Broadcast (minimized in the tray) when you log into Windows.");
    autostartToggle.setToggleState (isAutostartEnabled(), juce::dontSendNotification);
    autostartToggle.onClick = [this] { setAutostart (autostartToggle.getToggleState()); };

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    router.onStatusChanged = [this] { refreshStatus(); };
    router.onDeviceListChanged = [this] { refreshStatus(); };

    const int w = juce::jlimit (1100, 2600, settings.getIntValue ("uiWidth", 1480));
    setSize (w, juce::roundToInt ((float) w * ui::kBaseH / ui::kBaseW));
    veils.draw = [this] (juce::Graphics& g)
    {
        const float u = scale();
        for (const auto& sct : sections)
        {
            if (sct.power == nullptr || sct.veil < 0.002f) continue;
            g.setColour (ui::deep.withAlpha (0.72f * sct.veil));
            g.fillRoundedRectangle (sct.bounds.reduced (1.0f).withTrimmedTop (36.0f * u), 14.0f * u);
        }
    };
    addAndMakeVisible (veils);
    refreshStatus();
    startTimerHz (60);
}

MainComponent::~MainComponent()
{
    stopTimer();
    router.onStatusChanged = nullptr;
    router.onDeviceListChanged = nullptr;
    tooltipWindow.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
std::vector<PillMenu::Item> MainComponent::deviceItems (bool inputs, bool allowNone, bool markCables)
{
    std::vector<PillMenu::Item> v;
    v.push_back ({ inputs ? "RECORDING DEVICES" : (markCables ? "VIRTUAL MICROPHONES" : "PLAYBACK DEVICES"), true, true });
    if (allowNone) v.push_back ({ "None" });
    const auto names = inputs ? router.getInputNames() : router.getOutputNames();
    if (markCables)
    {
        // câbles virtuels d'abord : ce sont eux qui deviennent un micro pour les autres applications
        // affichés sous le nom du MICRO qu'ils créent (ex. "OB  (via CABLE Input)") : c'est ce nom-là qu'on choisit dans Discord / OBS
        for (const auto& n : names)
            if (AudioRouter::isVirtualCable (n))
            {
                const auto mic = router.recordingNameFor (n).upToFirstOccurrenceOf (" (", false, false);
                v.push_back ({ n, true, false, "- VIRTUAL MIC", mic + "  (via " + n.upToFirstOccurrenceOf (" (", false, false) + ")" });
            }
        bool sep = false;
        for (const auto& n : names)
            if (! AudioRouter::isVirtualCable (n))
            {
                if (! sep) { v.push_back ({ "OTHER OUTPUTS (NOT A MICROPHONE)", true, true }); sep = true; }
                v.push_back ({ n });
            }
    }
    else
        for (const auto& n : names) v.push_back ({ n });
    if (names.isEmpty()) v.push_back ({ "No device found", false });
    return v;
}

void MainComponent::applyConfig (AudioRouter::Config c)
{
    router.setConfig (c);
    saveConfig();
    refreshStatus();
}

void MainComponent::saveConfig()
{
    const auto& c = router.getConfig();
    settings.setValue ("input", c.input);
    settings.setValue ("output", c.output);
    settings.setValue ("monitor", c.monitor);
    settings.setValue ("inputChannel", c.inputChannel);
    settings.setValue ("mode", c.mode);
    settings.setValue ("bufferSize", c.bufferSize);
    settings.saveIfNeeded();
}

void MainComponent::askInstallCable()
{
    auto* aw = new juce::AlertWindow ("CREATE THE VIRTUAL MICROPHONE",
        "Onirique Broadcast will download VB-CABLE (free virtual audio cable driver by VB-Audio, about 1.3 MB) from "
        "vb-audio.com, check its digital signature, then install it.\n\n"
        "Windows will ask for administrator permission and may ask you to confirm the driver installation. "
        "A restart may be needed afterwards.\n\nVB-CABLE is donationware: you can support its author at vb-audio.com.",
        juce::MessageBoxIconType::NoIcon, this);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("INSTALL", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int r)
    {
        if (safe != nullptr && r == 1) safe->installer.start();
    }), true);
}

void MainComponent::installFinished()
{
    cableButton.setText ("INSTALL VB-CABLE (FREE)");
    if (installer.getState() == CableInstaller::State::failed)
    {
        const auto msg = installer.getMessage();
        installer.reset();
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("VB-CABLE").withMessage (msg + "\n\nYou can also install it manually from vb-audio.com.")
                                          .withButton ("OPEN VB-AUDIO.COM").withButton ("CLOSE").withAssociatedComponent (this),
                                      [] (int r) { if (r == 1) juce::URL (CableInstaller::kWebsite).launchInDefaultBrowser(); });
        return;
    }

    // installé : le nouveau câble est choisi comme micro virtuel et réglé en 48 kHz (sinon après le redémarrage,
    // au prochain lancement)
    router.rescan();
    const auto cable = AudioRouter::findVirtualCable (router.getOutputNames());
    if (cable.isNotEmpty())
    {
        auto c = router.getConfig();
        c.output = cable;
        applyConfig (c);
        fixFormat();
    }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon)
                                      .withTitle ("VIRTUAL MICROPHONE INSTALLED")
                                      .withMessage (cable.isNotEmpty()
                                                    ? "Your virtual mic is ready: choose \"" + router.recordingNameFor (cable) + "\" in Discord, OBS and games.\n\n"
                                                      "Windows recommends a restart to finish the driver installation."
                                                    : juce::String ("Restart Windows to finish the installation: the virtual mic will be detected "
                                                                    "and selected automatically at the next launch."))
                                      .withButton ("RESTART NOW").withButton ("LATER").withAssociatedComponent (this),
                                  [] (int r)
                                  {
                                      if (r == 1)
                                          juce::File ("C:\\Windows\\System32\\shutdown.exe").startAsProcess ("/r /t 5 /c \"Onirique Broadcast: finishing the virtual mic installation\"");
                                  });
    installer.reset();
}

void MainComponent::checkFormat()
{
    const auto& c = router.getConfig();
    formatOk = true;
    if (! hasCable || recordingName.isEmpty()) return;
    // 0 = inconnu : on ne signale rien
    const int out = getEndpointSampleRate (c.output.toWideCharPointer(), false);
    const int rec = getEndpointSampleRate (recordingName.toWideCharPointer(), true);
    formatOk = (out == 0 || out == 48000) && (rec == 0 || rec == 48000);
}

void MainComponent::fixFormat()
{
    auto c = router.getConfig();
    const auto rec = router.recordingNameFor (c.output);
    const bool okOut = setEndpointFormat48k (c.output.toWideCharPointer(), false);
    const bool okRec = rec.isNotEmpty() && setEndpointFormat48k (rec.toWideCharPointer(), true);
    router.setConfig (c, true);   // le flux vers le câble est rouvert dans le nouveau format
    formatCheckCountdown = 0;
    refreshStatus();
    if (! (okOut && okRec) || ! formatOk)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "48 KHZ",
            "Windows refused the change. In the Sound panel that opens, set \"" + c.output + "\" (Playback tab) and \"" + rec
            + "\" (Recording tab) to 48000 Hz in Properties > Advanced > Default Format.", "OK", this);
        juce::File ("C:\\Windows\\System32\\control.exe").startAsProcess ("mmsys.cpl");
    }
}

void MainComponent::askRename()
{
    auto* aw = new juce::AlertWindow ("RENAME THE VIRTUAL MIC",
                                      "New name shown in Discord, OBS and games\n(currently \"" + recordingName + "\"):",
                                      juce::MessageBoxIconType::NoIcon, this);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("name", "OB");
    aw->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MainComponent> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int r)
    {
        const auto name = aw->getTextEditorContents ("name").trim();
        if (safe != nullptr && r == 1 && name.isNotEmpty()) safe->doRename (name);
    }), true);
}

void MainComponent::doRename (const juce::String& name)
{
    const bool ok = renameRecordingDevice (recordingName.toWideCharPointer(), name.toWideCharPointer());
    router.rescan();
    refreshStatus();
    if (! ok)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "RENAME",
            "Windows refused the change. In the Sound panel that opens: Recording tab > \"" + recordingName
            + "\" > Properties, then type the new name.", "OK", this);
        juce::File ("C:\\Windows\\System32\\control.exe").startAsProcess ("mmsys.cpl,,1");
    }
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    // fond de l'en-tête (hors boutons) : on déplace la fenêtre
    draggingWindow = e.eventComponent == this && e.position.y < 92.0f * scale();
    if (draggingWindow && onWindowDrag) onWindowDrag (e, true);
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingWindow && onWindowDrag) onWindowDrag (e, false);
}

void MainComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.eventComponent == this && e.position.y < 92.0f * scale() && onToggleMaximise) onToggleMaximise();
}

void MainComponent::refreshStatus()
{
    status = router.getStatus();
    const auto& c = router.getConfig();

    auto show = [] (PillMenu& m, const juce::String& name, const AudioRouter::Stream& st)
    {
        const bool err = name.isNotEmpty() && ! st.active;
        m.setValue (name, err);
        m.setStatus (err ? ui::ember : ui::live, name.isEmpty() ? 0.0f : 1.0f);
    };
    show (inputMenu, c.input, status.input);
    show (outputMenu, c.output, status.output);
    if (AudioRouter::isVirtualCable (c.output))   // le micro virtuel est présenté sous son nom de micro (ex. OB)
        outputMenu.setValue (c.output, c.output.isNotEmpty() && ! status.output.active, router.recordingNameFor (c.output));
    show (monitorMenu, c.monitor, status.monitor);
    bufferMenu.setValue (c.bufferSize <= 0 ? juce::String ("AUTO") : juce::String (c.bufferSize) + " smp");
    bufferMenu.setStatus (ui::live, 0.0f);
    channelSel.setSelected (c.inputChannel);
    modeSel.setSelected (c.mode);

    hasCable = AudioRouter::isVirtualCable (c.output);
    recordingName = hasCable ? router.recordingNameFor (c.output) : juce::String();
    const bool anyCable = AudioRouter::findVirtualCable (router.getOutputNames()).isNotEmpty();
    if (--formatCheckCountdown <= 0) { checkFormat(); formatCheckCountdown = 5; }   // ~toutes les 5 s (COM)
    guardToggle.setTooltip (router.isEchoReferenceRunning()
        ? "Echo guard: Discord, games and music leak from your headphones / speakers into the mic. The guard listens to "
          "what the PC plays, learns how much leaks, and keeps the gate closed (-30 dB) while only that leak is there. "
          "Discord's echo cancellation then has nothing to fight and leaves your voice alone: keep it ON in Discord."
        : "Echo guard unavailable: needs Windows 10 version 2004 or later.");
    copyButton.setVisible (hasCable && status.output.active);
    cableButton.setVisible (! anyCable);
    soundButton.setVisible (anyCable);
    // même emplacement : SET 48 KHZ tant que le format n'est pas bon, sinon RENAME
    fixButton.setVisible (hasCable && recordingName.isNotEmpty() && ! formatOk);
    renameButton.setVisible (hasCable && recordingName.isNotEmpty() && formatOk);
    repaint (routingBounds.getSmallestIntegerContainer());
    repaint (onAirRect.getSmallestIntegerContainer().expanded (20));
}

void MainComponent::selectPeqBand (int b)
{
    peqChip.setBand (b);
    for (int i = 0; i < 6; ++i)
        for (auto* v : { peqFreqV[(size_t) i].get(), peqGainV[(size_t) i].get(), peqQV[(size_t) i].get() })
            if (v != nullptr) v->setVisible (i == b);
}

//==============================================================================
void MainComponent::timerCallback()
{
    clock += 1.0 / 60.0;
    ++frame;

    for (auto* a : animated) a->tick();

    // installation de VB-CABLE : progression sur le bouton, puis suite à donner
    {
        using S = CableInstaller::State;
        const auto st = installer.getState();
        if (st == S::downloading)     cableButton.setText ("DOWNLOADING  " + juce::String (juce::roundToInt (installer.getProgress() * 100.0f)) + " %");
        else if (st == S::verifying)  cableButton.setText ("CHECKING SIGNATURE...");
        else if (st == S::installing) cableButton.setText ("INSTALLING - ACCEPT WINDOWS PROMPT");
        if (st != lastInstallState)
        {
            lastInstallState = st;
            if (st == S::done || st == S::failed) installFinished();
        }
    }

    for (auto& s : sections)
        if (s.power != nullptr && ui::approach (s.veil, s.power->getToggleState() ? 0.0f : 1.0f, 0.14f))
            veils.repaint (s.bounds.getSmallestIntegerContainer());

    const bool bypassed = engine.apvts.getRawParameterValue (ids::bypass)->load() > 0.5f;
    vu1176.setActive (fetPower.getToggleState() && ! bypassed);
    deessGr.setActive (deessPower.getToggleState() && ! bypassed);
    limGr.setActive (limToggle.getToggleState() && ! bypassed);

    // témoin OVER : sortie au-dessus de -0.1 dBFS, maintenu ~1.5 s
    if (engine.consumeOverPeak() > 0.989f) overHold = 90;
    else if (overHold > 0) --overHold;
    if (ui::approach (overGlow, overHold > 0 ? 1.0f : 0.0f, overHold > 0 ? 0.5f : 0.08f))
        repaint (overRect.getSmallestIntegerContainer().expanded (8));

    // ON AIR : micro virtuel actif et non coupé
    const bool muted = engine.apvts.getRawParameterValue (ids::mute)->load() > 0.5f;
    ui::approach (onAir, status.output.active && status.input.active ? (muted ? 0.35f : 1.0f) : 0.0f, 0.1f);

    // en-tête vivant : lueur du logo, signal qui parcourt la ligne, étoiles, ON AIR qui respire
    repaint (logoRect.expanded (logoRect.getHeight() * 0.4f).getSmallestIntegerContainer());
    repaint (signalRect.getSmallestIntegerContainer());
    repaint (onAirRect.getSmallestIntegerContainer().expanded (14));
    if ((frame & 1) == 0)
        for (const auto& st : stars)
            repaint (juce::Rectangle<float> (st.second * 2.4f, st.second * 2.4f).withCentre (st.first).getSmallestIntegerContainer());
    if ((frame % 15) == 0)
        repaint (engineInfoRect.getSmallestIntegerContainer().expanded (4));
    if ((frame & 1) == 0 && status.output.active && hasCable)
        repaint (vmStatusRect.withHeight (24.0f * scale()).getSmallestIntegerContainer());   // LED "ONLINE" qui pulse
}

//==============================================================================
void MainComponent::resized()
{
    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();
    const float m = 20.0f * u, gap = 14.0f * u, top = 96.0f * u;
    auto place = [] (juce::Component& c, juce::Rectangle<float> r) { c.setBounds (r.toNearestInt()); };
    auto inner = [u] (juce::Rectangle<float> r) { return r.withTrimmedTop (40.0f * u).withTrimmedBottom (12.0f * u).reduced (14.0f * u, 0.0f); };

    // ---- en-tête
    logoRect = { 20.0f * u, 12.0f * u, 60.0f * u * 985.0f / 895.0f, 60.0f * u };
    signalRect = { 0.0f, 80.0f * u, W, 12.0f * u };
    place (bypassButton, juce::Rectangle<float> (122.0f * u, 40.0f * u).withRightX (W - m).withY (20.0f * u));
    place (muteButton, juce::Rectangle<float> (150.0f * u, 44.0f * u).withRightX ((float) bypassButton.getX() - 10.0f * u).withY (18.0f * u));
    place (presetBar, juce::Rectangle<float> (370.0f * u, 40.0f * u).withRightX ((float) muteButton.getX() - 14.0f * u).withY (20.0f * u));
    onAirRect = juce::Rectangle<float> (150.0f * u, 40.0f * u).withRightX ((float) presetBar.getX() - 14.0f * u).withY (20.0f * u);
    place (orderBar, { logoRect.getRight() + 16.0f * u, 54.0f * u, 470.0f * u, 19.0f * u });

    if (logo.isValid())
    {
        const int lh = juce::jlimit (16, 600, juce::roundToInt (logoRect.getHeight() * 2.0f));
        logoScaled = logo.rescaled (juce::roundToInt ((float) lh * 985.0f / 895.0f), lh, juce::Graphics::highResamplingQuality);
        const int pad = lh / 4;
        juce::Image src (juce::Image::ARGB, logoScaled.getWidth() + 2 * pad, logoScaled.getHeight() + 2 * pad, true);
        { juce::Graphics lg (src); lg.drawImageAt (logoScaled, pad, pad); }
        logoGlow = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        juce::ImageConvolutionKernel k (15);
        k.createGaussianBlur ((float) lh / 22.0f);
        k.applyToImage (logoGlow, src, src.getBounds());
    }

    // fin du titre + sous-titre (les étoiles commencent après)
    {
        const auto f1 = ui::font (ui::Face::display, 29.0f * u, true, 0.10f), f2 = ui::font (ui::Face::display, 29.0f * u, false, 0.10f);
        const auto f3 = ui::font (ui::Face::mono, 10.0f * u, false, 0.12f);
        headerTextRight = logoRect.getRight() + 14.0f * u + ui::textWidth (f1, "ONIRIQUE") + 2.0f + 10.0f * u
                          + ui::textWidth (f2, "BROADCAST") + 2.0f + 22.0f * u + ui::textWidth (f3, "REAL-TIME VOICE CHAIN + VIRTUAL MIC");
    }
    stars.clear();
    juce::Random rnd (7);
    for (int i = 0; i < 10; ++i)
    {
        const float x0 = std::max ((float) orderBar.getRight(), headerTextRight) + 24.0f * u, x1 = onAirRect.getX() - 16.0f * u;
        const float x = x0 + (x1 - x0) * rnd.nextFloat();
        const float y = (10.0f + 62.0f * rnd.nextFloat()) * u;
        stars.push_back ({ { x, y }, (2.5f + 4.5f * rnd.nextFloat() * rnd.nextFloat()) * u });
    }

    // ---- colonne ROUTING
    routingBounds = { m, top, 318.0f * u, H - m - top };
    {
        auto rr = routingBounds.reduced (16.0f * u, 0.0f).withTrimmedTop (44.0f * u).withTrimmedBottom (14.0f * u);
        const float rowH = 34.0f * u, segH = 26.0f * u, titleH = 20.0f * u, sp = 8.0f * u, secGap = 16.0f * u;

        secTitles[0] = rr.removeFromTop (titleH);
        rr.removeFromTop (4.0f * u);
        place (inputMenu, rr.removeFromTop (rowH));
        rr.removeFromTop (sp);
        place (channelSel, rr.removeFromTop (segH));
        rr.removeFromTop (sp + 2.0f * u);
        place (inputBar, rr.removeFromTop (12.0f * u));
        rr.removeFromTop (sp);
        place (inKnob, rr.removeFromTop (30.0f * u));
        rr.removeFromTop (secGap);

        secTitles[1] = rr.removeFromTop (titleH);
        rr.removeFromTop (4.0f * u);
        place (outputMenu, rr.removeFromTop (rowH));
        rr.removeFromTop (sp);
        vmStatusRect = rr.removeFromTop (118.0f * u);
        {
            auto btn = vmStatusRect.reduced (12.0f * u, 10.0f * u).removeFromBottom (26.0f * u);
            const float bw = (btn.getWidth() - 12.0f * u) / 3.0f;
            place (renameButton, btn.withWidth (bw));
            place (fixButton, btn.removeFromLeft (bw));
            btn.removeFromLeft (6.0f * u);
            place (soundButton, btn.removeFromLeft (bw));
            btn.removeFromLeft (6.0f * u);
            place (copyButton, btn);
            place (cableButton, vmStatusRect.reduced (12.0f * u, 10.0f * u).removeFromBottom (26.0f * u));
        }
        rr.removeFromTop (secGap);

        secTitles[2] = rr.removeFromTop (titleH);
        rr.removeFromTop (4.0f * u);
        place (monitorMenu, rr.removeFromTop (rowH));
        rr.removeFromTop (sp);
        place (monitorToggle, rr.removeFromTop (26.0f * u).withSizeKeepingCentre (std::min (rr.getWidth(), 150.0f * u), 26.0f * u)
                                .withX (rr.getX()));
        rr.removeFromTop (sp);
        place (monVolKnob, rr.removeFromTop (30.0f * u));
        rr.removeFromTop (secGap);

        secTitles[3] = rr.removeFromTop (titleH);
        rr.removeFromTop (4.0f * u);
        place (modeSel, rr.removeFromTop (segH));
        rr.removeFromTop (sp);
        place (bufferMenu, rr.removeFromTop (30.0f * u));
        rr.removeFromTop (sp);
        engineInfoRect = rr.removeFromTop (64.0f * u);

        auto bottom = rr.removeFromBottom (2.0f * 26.0f * u + 6.0f * u);
        place (trayToggle, bottom.removeFromTop (26.0f * u));
        bottom.removeFromTop (6.0f * u);
        place (autostartToggle, bottom.removeFromTop (26.0f * u));
    }

    // ---- cartes des modules
    const float x0 = m + routingBounds.getWidth() + gap, rw = W - m - x0;
    const float row1H = 300.0f * u, row2H = 290.0f * u;
    const float y1 = top, y2 = y1 + row1H + gap, y3 = y2 + row2H + gap, row3H = H - m - y3;
    const float eqW = 806.0f * u, fetW = 470.0f * u, deW = 420.0f * u, satW = 290.0f * u;

    const std::vector<Section> old = sections;
    sections.clear();
    sections.push_back ({ "ANALYZER / PARAMETRIC EQ", { x0, y1, eqW, row1H }, &eqPower });
    sections.push_back ({ "NOISE GATE", { x0 + eqW + gap, y1, rw - eqW - gap, row1H }, &gatePower });
    sections.push_back ({ "1176  FET COMPRESSOR", { x0, y2, fetW, row2H }, &fetPower });
    sections.push_back ({ "MULTIBAND", { x0 + fetW + gap, y2, rw - fetW - gap, row2H }, &mbPower });
    sections.push_back ({ "DE-ESSER / PLOSIVES", { x0, y3, deW, row3H }, &deessPower });
    sections.push_back ({ "SATURATION", { x0 + deW + gap, y3, satW, row3H }, &satPower });
    sections.push_back ({ "OUTPUT", { x0 + deW + satW + 2.0f * gap, y3, rw - deW - satW - 2.0f * gap, row3H }, nullptr });
    for (auto& s : sections)
    {
        for (const auto& o : old) if (o.title == s.title) s.veil = o.veil;
        if (s.power == nullptr) continue;
        if (old.empty()) s.veil = s.power->getToggleState() ? 0.0f : 1.0f;
        place (*s.power, juce::Rectangle<float> (32.0f * u, 32.0f * u).withCentre ({ s.bounds.getRight() - 24.0f * u, s.bounds.getY() + 20.0f * u }));
    }
    auto titleStrip = [u] (const Section& s, float w)   // zone à droite du titre, avant le bouton power
    {
        return juce::Rectangle<float> (w, 24.0f * u).withRightX (s.bounds.getRight() - (s.power != nullptr ? 48.0f : 14.0f) * u)
                                                    .withY (s.bounds.getY() + 8.0f * u);
    };

    // ---- EQ / ANALYSEUR
    {
        const auto& s = sections[0];
        place (analyzer, inner (s.bounds));
        auto strip = titleStrip (s, 440.0f * u);
        place (peqChip, strip.removeFromLeft (150.0f * u));
        strip.removeFromLeft (6.0f * u);
        const auto fR = strip.removeFromLeft (110.0f * u);
        strip.removeFromLeft (6.0f * u);
        const auto gR = strip.removeFromLeft (96.0f * u);
        strip.removeFromLeft (6.0f * u);
        for (int i = 0; i < 6; ++i)
        {
            place (*peqFreqV[(size_t) i], fR);
            place (*peqGainV[(size_t) i], gR);
            place (*peqQV[(size_t) i], strip);
        }
    }

    // ---- NOISE GATE
    {
        const auto& s = sections[1];
        const auto hystR = titleStrip (s, 118.0f * u);
        place (gateHystV, hystR);
        place (guardToggle, hystR.withWidth (130.0f * u).withRightX (hystR.getX() - 8.0f * u));
        auto gi = inner (s.bounds);
        place (gateDisplay, gi.removeFromTop (gi.getHeight() * 0.5f));
        gi.removeFromTop (6.0f * u);
        KnobComponent* k[] { &gateThrKnob, &gateRangeKnob, &gateAtkKnob, &gateHoldKnob, &gateRelKnob };
        const float cw = gi.getWidth() / 5.0f;
        for (int i = 0; i < 5; ++i)
            place (*k[i], { gi.getX() + cw * (float) i, gi.getY(), cw, gi.getHeight() });
    }

    // ---- 1176 : VU + MIX en haut, 4 boutons en bas
    {
        const auto& s = sections[2];
        place (fetRatio, titleStrip (s, 200.0f * u));
        auto fi = inner (s.bounds);
        auto topRow = fi.removeFromTop (100.0f * u);
        place (vu1176, topRow.removeFromLeft (170.0f * u));
        topRow.removeFromLeft (16.0f * u);
        place (fetMixKnob, topRow.withSizeKeepingCentre (topRow.getWidth(), 30.0f * u));
        fi.removeFromTop (6.0f * u);
        KnobComponent* k[] { &fetInKnob, &fetOutKnob, &fetAtkKnob, &fetRelKnob };
        const float cw = fi.getWidth() / 4.0f;
        for (int i = 0; i < 4; ++i)
            place (*k[i], { fi.getX() + cw * (float) i, fi.getY(), cw, fi.getHeight() });
    }

    // ---- MULTIBAND : écran + attaque / relâchement à gauche, 4 colonnes de bande à droite
    {
        const auto& s = sections[3];
        place (mbPresets, titleStrip (s, std::min (440.0f * u, s.bounds.getWidth() - 200.0f * u)));
        auto mi = inner (s.bounds);
        auto left = mi.removeFromLeft (mi.getWidth() * 0.49f);
        mi.removeFromLeft (14.0f * u);
        place (mbReleaseKnob, left.removeFromBottom (28.0f * u));
        left.removeFromBottom (6.0f * u);
        place (mbAttackKnob, left.removeFromBottom (28.0f * u));
        left.removeFromBottom (8.0f * u);
        place (mbDisplay, left);

        const float cw = mi.getWidth() / 4.0f;
        const auto listenRow = mi.removeFromBottom (24.0f * u);
        mi.removeFromBottom (5.0f * u);
        const auto gainRow = mi.removeFromBottom (22.0f * u);
        mi.removeFromBottom (4.0f * u);
        for (int b = 0; b < 4; ++b)
        {
            const float cx = mi.getX() + cw * ((float) b + 0.5f);
            mbThr[(size_t) b]->setMaxDiameter (78.0f * u);
            place (*mbThr[(size_t) b], juce::Rectangle<float> (cw, mi.getHeight()).withCentre ({ cx, mi.getCentreY() }));
            place (*mbGain[(size_t) b], juce::Rectangle<float> (cw - 8.0f * u, gainRow.getHeight()).withCentre ({ cx, gainRow.getCentreY() }));
            place (*mbListen[(size_t) b], juce::Rectangle<float> (std::min (cw - 8.0f * u, 92.0f * u), listenRow.getHeight())
                                              .withCentre ({ cx, listenRow.getCentreY() }));
        }
    }

    // ---- DE-ESSER / PLOSIVES
    {
        auto di = inner (sections[4].bounds);
        place (plosiveFader, di.removeFromTop (30.0f * u));
        di.removeFromTop (6.0f * u);
        place (deessGr, di.removeFromRight (46.0f * u));
        di.removeFromRight (6.0f * u);
        place (deessFreqKnob, di.removeFromLeft (di.getWidth() * 0.5f));
        place (deessAmtKnob, di);
    }

    // ---- SATURATION
    {
        auto si = inner (sections[5].bounds);
        place (satType, si.removeFromTop (26.0f * u));
        si.removeFromTop (6.0f * u);
        place (driveKnob, si);
    }

    // ---- OUTPUT
    {
        const auto& s = sections[6];
        place (limToggle, titleStrip (s, 108.0f * u));
        overRect = juce::Rectangle<float> (70.0f * u, 18.0f * u).withRightX ((float) limToggle.getX() - 12.0f * u)
                                                                .withCentre ({ (float) limToggle.getX() - 12.0f * u - 35.0f * u, (float) limToggle.getBounds().getCentreY() });
        auto oi = inner (s.bounds);
        place (outMeter, oi.removeFromRight (46.0f * u));
        oi.removeFromRight (8.0f * u);
        place (limGr, oi.removeFromRight (46.0f * u));
        oi.removeFromRight (8.0f * u);
        place (outKnob, oi.removeFromLeft (oi.getWidth() * 0.5f));
        place (ceilKnob, oi);
    }

    veils.setBounds (getLocalBounds());
}

//==============================================================================
void MainComponent::paintStar (juce::Graphics& g, juce::Point<float> c, float r, float alpha)
{
    juce::Path s;
    const float k = 0.16f * r;
    s.startNewSubPath (c.x, c.y - r);
    s.lineTo (c.x + k, c.y - k); s.lineTo (c.x + r, c.y); s.lineTo (c.x + k, c.y + k);
    s.lineTo (c.x, c.y + r);     s.lineTo (c.x - k, c.y + k); s.lineTo (c.x - r, c.y); s.lineTo (c.x - k, c.y - k);
    s.closeSubPath();
    g.setColour (ui::ember.withAlpha (0.25f * alpha));
    g.fillEllipse (juce::Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c));
    g.setColour (ui::mist.withAlpha (alpha));
    g.fillPath (s);
}

void MainComponent::paintHeader (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth();
    const float t = (float) clock;

    for (size_t i = 0; i < stars.size(); ++i)
    {
        const float a = 0.15f + 0.75f * (0.5f + 0.5f * std::sin (t * (0.9f + 0.37f * (float) (i % 5)) + (float) i * 1.7f));
        paintStar (g, stars[i].first, stars[i].second, a);
    }

    // logo : lueur braise qui respire + sigil net
    if (logoScaled.isValid())
    {
        const float breathe = 0.5f + 0.5f * std::sin (t * 2.0f * kPi / 5.0f);
        const float pad = logoRect.getHeight() / 4.0f;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setColour (ui::ember.withAlpha (0.55f + 0.35f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setColour (juce::Colours::white.withAlpha (0.08f + 0.10f * breathe));
        g.drawImage (logoGlow, logoRect.expanded (pad), juce::RectanglePlacement::stretchToFit, true);
        g.setOpacity (1.0f);
        g.drawImage (logoScaled, logoRect, juce::RectanglePlacement::stretchToFit);
    }

    // titre
    {
        const float x = logoRect.getRight() + 14.0f * u;
        auto f1 = ui::font (ui::Face::display, 29.0f * u, true, 0.10f);
        auto f2 = ui::font (ui::Face::display, 29.0f * u, false, 0.10f);
        const float w1 = ui::textWidth (f1, "ONIRIQUE") + 2.0f;
        const float w2 = ui::textWidth (f2, "BROADCAST") + 2.0f;
        const float y = 12.0f * u, hh = 36.0f * u;
        g.setFont (f1);
        g.setColour (ui::mist);
        g.drawText ("ONIRIQUE", juce::Rectangle<float> (x, y, w1, hh), juce::Justification::centredLeft, false);
        g.setColour (ui::ember);
        g.drawText ("BROADCAST", juce::Rectangle<float> (x + w1 + 10.0f * u, y, w2 + 30.0f * u, hh), juce::Justification::centredLeft, false);
        g.setColour (ui::haze.withAlpha (0.7f));
        g.setFont (ui::font (ui::Face::mono, 10.0f * u, false, 0.12f));
        g.drawText ("REAL-TIME VOICE CHAIN + VIRTUAL MIC", juce::Rectangle<float> (x + w1 + w2 + 22.0f * u, y + 4.0f * u, 300.0f * u, hh),
                    juce::Justification::centredLeft, false);
    }

    // ON AIR : enseigne lumineuse qui respire quand le micro virtuel émet
    {
        const auto r = onAirRect.reduced (1.0f);
        const float breathe = 0.75f + 0.25f * std::sin (t * 2.4f);
        const float lit = onAir * breathe;
        const bool muted = engine.apvts.getRawParameterValue (ids::mute)->load() > 0.5f;
        juce::Path p;
        p.addRoundedRectangle (r, 9.0f * u);
        ui::strokeGlow (g, p, ui::ember, 2.0f * u, 1.1f * lit);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillPath (p);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.55f * lit), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.6f * lit), r.getRight(), r.getBottom(), false));
        g.fillPath (p);
        g.setColour (ui::line.interpolatedWith (ui::ember, onAir));
        g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.4f * u)));

        const juce::String txt = onAir < 0.05f ? "OFFLINE" : (muted ? "MUTED" : "ON AIR");
        const float d = 9.0f * u;
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + 18.0f * u, r.getCentreY() });
        g.setColour (ui::bone.withAlpha (0.5f * lit));
        g.fillEllipse (led.expanded (d * 0.6f));
        g.setColour (ui::haze.withAlpha (0.4f).interpolatedWith (ui::bone, lit));
        g.fillEllipse (led);
        g.setColour (ui::haze.interpolatedWith (ui::bone, onAir));
        g.setFont (ui::fitFont (ui::Face::display, r.getHeight() * 0.46f, r.getWidth() - 44.0f * u, txt, true, 0.18f));
        g.drawText (txt, r.withTrimmedLeft (30.0f * u), juce::Justification::centred, false);
    }

    // ligne de signal : filet qui s'estompe aux bords + impulsion lumineuse qui la parcourt
    {
        const float ly = 84.0f * u;
        juce::ColourGradient cg (juce::Colours::transparentBlack, 20.0f * u, ly, juce::Colours::transparentBlack, W - 20.0f * u, ly, false);
        cg.addColour (0.08, ui::line);
        cg.addColour (0.92, ui::line);
        g.setGradientFill (cg);
        g.fillRect (juce::Rectangle<float> (20.0f * u, ly, W - 40.0f * u, std::max (1.0f, u)));

        const float len = 130.0f * u;
        const float span = W - 40.0f * u + len;
        const float px = 20.0f * u - len + std::fmod (t * 0.24f, 1.0f) * span;
        const auto streak = juce::Rectangle<float> (px, ly - 1.5f * u, len, 3.5f * u);
        juce::ColourGradient sg (ui::bone.withAlpha (0.0f), streak.getX(), ly, ui::ember, streak.getRight(), ly, false);
        sg.addColour (0.6, ui::bone);
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<float> (20.0f * u, signalRect.getY(), W - 40.0f * u, signalRect.getHeight()).toNearestInt());
        g.setColour (ui::ember.withAlpha (0.25f));
        g.fillRoundedRectangle (streak.expanded (0.0f, 2.5f * u).withTrimmedLeft (len * 0.5f), 3.0f * u);
        g.setGradientFill (sg);
        g.fillRoundedRectangle (streak, streak.getHeight() * 0.5f);
    }
}

void MainComponent::paintRouting (juce::Graphics& g)
{
    const float u = scale();
    const float es = u;
    static const char* titles[] { "MICROPHONE", "VIRTUAL MICROPHONE", "HEADPHONES", "AUDIO ENGINE" };

    for (int i = 0; i < 4; ++i)
    {
        const auto r = secTitles[i];
        const auto f = ui::font (ui::Face::mono, std::max (10.5f * u, 10.0f), true, 0.16f);
        const float tw = ui::textWidth (f, titles[i]);
        g.setColour (ui::haze);
        g.setFont (f);
        g.drawText (titles[i], r, juce::Justification::centredLeft, false);
        g.setColour (ui::line);
        g.fillRect (juce::Rectangle<float> (r.getX() + tw + 10.0f * u, r.getCentreY(), r.getWidth() - tw - 10.0f * u, 1.0f));
    }

    // ---- statut du micro virtuel
    {
        const auto r = vmStatusRect;
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (r, 10.0f * u);
        const bool online = status.output.active && hasCable;
        g.setColour (online ? ui::live.withAlpha (0.35f) : ui::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f * u, 1.0f);

        auto in = r.reduced (12.0f * u, 9.0f * u);
        in.removeFromBottom (32.0f * u);   // boutons
        const auto config = router.getConfig();
        const bool anyCable = cableButton.isVisible() == false;

        juce::String head, line1, line2;
        juce::Colour headCol = ui::ember;
        if (! anyCable)
        {
            head = "NO VIRTUAL MIC YET";
            line1 = "One click installs the free VB-CABLE driver";
            line2 = "that creates it (admin permission asked).";
        }
        else if (config.output.isEmpty())
        {
            head = "NOT CONNECTED";
            line1 = "Choose a virtual cable input above";
            line2 = "(e.g. CABLE Input) to create the mic.";
        }
        else if (! status.output.active)
        {
            head = "UNAVAILABLE";
            line1 = status.output.error.isNotEmpty() ? status.output.error : juce::String ("The device could not be opened.");
            line2 = "Retrying automatically...";
        }
        else if (! hasCable)
        {
            head = "NOT A VIRTUAL CABLE";
            line1 = "Apps will not see this output as a mic.";
            line2 = "Pick a virtual cable for Discord / OBS.";
        }
        else
        {
            head = formatOk ? "ONLINE" : "ONLINE - NOT 48 KHZ";
            headCol = formatOk ? ui::live : ui::ember.interpolatedWith (ui::bone, 0.3f);
            line1 = ! formatOk ? "Click SET 48 KHZ for clean Discord audio:"
                  : ! status.output.exclusive ? "Shared mode: screen shares may echo you."
                  : "In Discord, OBS, games... choose the mic:";
            line2 = recordingName;
        }

        const float d = 8.0f * u;
        const float pulse = online ? 0.7f + 0.3f * std::sin ((float) clock * 3.0f) : 1.0f;
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ in.getX() + d * 0.5f, in.getY() + 9.0f * u });
        g.setColour (headCol.withAlpha (0.35f * pulse));
        g.fillEllipse (led.expanded (d * 0.8f));
        g.setColour (headCol.withAlpha (pulse));
        g.fillEllipse (led);
        g.setColour (headCol.interpolatedWith (ui::bone, 0.35f));
        g.setFont (ui::font (ui::Face::mono, std::max (11.5f * u, 10.5f), true, 0.14f));
        g.drawText (head, juce::Rectangle<float> (led.getRight() + 8.0f * u, in.getY(), in.getWidth(), 18.0f * u), juce::Justification::centredLeft, false);

        g.setColour (ui::haze);
        g.setFont (ui::fitFont (ui::Face::body, std::max (12.0f * u, 11.0f), in.getWidth(), line1));
        g.drawText (line1, juce::Rectangle<float> (in.getX(), in.getY() + 22.0f * u, in.getWidth(), 18.0f * u), juce::Justification::centredLeft, true);
        g.setColour (online ? ui::bone : ui::haze);
        g.setFont (ui::fitFont (online ? ui::Face::body : ui::Face::body, std::max ((online ? 13.5f : 12.0f) * u, 11.0f), in.getWidth(), line2, online));
        g.drawText (line2, juce::Rectangle<float> (in.getX(), in.getY() + 40.0f * u, in.getWidth(), 20.0f * u), juce::Justification::centredLeft, true);
        juce::ignoreUnused (es);
    }

    // ---- moteur audio : fréquences, tampons, latence, CPU
    {
        auto r = engineInfoRect;
        const auto f = ui::font (ui::Face::mono, std::max (10.5f * u, 10.0f));
        const float lh = r.getHeight() / 4.0f;
        auto rowText = [&] (int row, const juce::String& key, const juce::String& val, juce::Colour c)
        {
            const auto lr = juce::Rectangle<float> (r.getX(), r.getY() + lh * (float) row, r.getWidth(), lh);
            g.setFont (f);
            g.setColour (ui::haze.withAlpha (0.8f));
            g.drawText (key, lr, juce::Justification::centredLeft, false);
            g.setColour (c);
            g.drawText (val, lr, juce::Justification::centredRight, false);
        };
        auto stream = [] (const AudioRouter::Stream& s)
        {
            if (s.name.isEmpty()) return juce::String ("-");
            if (! s.active) return juce::String ("offline");
            return juce::String (s.rate / 1000.0, 1) + " kHz  /  " + juce::String (s.block) + " smp" + (s.exclusive ? "  EXCL" : "");
        };
        const auto st = router.getStatus();
        rowText (0, "MIC", stream (st.input), st.input.active ? ui::mist : ui::ember);
        rowText (1, "VIRTUAL MIC", stream (st.output), st.output.active ? ui::mist : ui::ember);
        rowText (2, "LATENCY  /  DROPOUTS", st.latencyMs > 0.0 ? "~" + juce::String (juce::roundToInt (st.latencyMs)) + " ms  /  "
                                                                 + juce::String (st.dropouts)
                                                               : juce::String ("-"), st.dropouts > 0 ? ui::ember.interpolatedWith (ui::bone, 0.4f) : ui::mist);
        rowText (3, "CPU  /  CLOCK DRIFT", juce::String (router.getCpuLoad() * 100.0f, 1) + " %  /  "
                                           + (st.driftPpm > 0.0 ? "+" : "") + juce::String (juce::roundToInt (st.driftPpm)) + " ppm", ui::mist);
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    const float u = scale();
    const float W = (float) getWidth(), H = (float) getHeight();

    // ---- fond (DA PRO SERIES) : noir profond, lueurs rouges très discrètes, grain fin
    g.fillAll (ui::night);
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.55f), W * 0.22f, H * 0.45f, ui::night.withAlpha (0.0f), W * 0.22f + W * 0.6f, H * 0.45f, true));
    g.fillRect (getLocalBounds());
    g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.22f), W * 0.92f, H * 1.05f, ui::night.withAlpha (0.0f), W * 0.92f - W * 0.45f, H * 1.05f, true));
    g.fillRect (getLocalBounds());
    {
        juce::Random rnd (21);
        for (int i = 0; i < 140; ++i)
        {
            const float x = rnd.nextFloat() * W, y = rnd.nextFloat() * H, s = (0.5f + 1.2f * rnd.nextFloat() * rnd.nextFloat()) * u;
            g.setColour (ui::mist.withAlpha (0.03f + 0.07f * rnd.nextFloat()));
            g.fillEllipse (x, y, s, s);
        }
    }

    {
        juce::Graphics::ScopedSaveState ss (g);
        paintHeader (g);
    }

    // ---- cartes : verre sombre, filet fin, ombre portée, titre + point braise
    auto drawCard = [&] (juce::Rectangle<float> r, const juce::String& title, bool on, bool hasPower)
    {
        // verre sombre (DA PRO SERIES) : charbon, reflet rose pâle très léger en haut, filet fin
        const float rad = 12.0f * u;
        juce::Path card;
        card.addRoundedRectangle (r, rad);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), juce::roundToInt (28.0f * u), { 0, juce::roundToInt (12.0f * u) })
            .drawForPath (g, card);
        g.setColour (ui::deep.withAlpha (0.94f));
        g.fillPath (card);
        g.setGradientFill (juce::ColourGradient (ui::lilac.withAlpha (0.035f), 0.0f, r.getY(), ui::lilac.withAlpha (0.0f), 0.0f, r.getY() + r.getHeight() * 0.7f, false));
        g.fillPath (card);
        g.setColour (ui::line);
        g.strokePath (card, juce::PathStrokeType (std::max (1.0f, 1.0f * u)));

        const float dot = 6.0f * u;
        const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + 20.0f * u, r.getY() + 20.0f * u });
        g.setColour (ui::ember.withAlpha (on ? 0.35f : 0.0f));
        g.fillEllipse (dotR.expanded (dot * 0.7f));
        g.setColour (on ? ui::ember : ui::haze.withAlpha (0.5f));
        g.fillEllipse (dotR);

        const float maxW = r.getWidth() - (hasPower ? 90.0f : 60.0f) * u;
        g.setColour (ui::mist);
        const auto f = ui::fitFont (ui::Face::display, 12.5f * u, maxW, title, true, 0.06f);
        g.setFont (f);
        g.drawText (title, juce::Rectangle<float> (r.getX() + 32.0f * u, r.getY() + 9.0f * u, maxW, 22.0f * u),
                    juce::Justification::centredLeft, false);
    };

    drawCard (routingBounds, "AUDIO ROUTING", status.input.active && status.output.active, false);
    paintRouting (g);
    for (const auto& s : sections)
        drawCard (s.bounds, s.title, s.power == nullptr || s.power->getToggleState(), s.power != nullptr);

    // ---- témoin OVER (carte OUTPUT)
    {
        const float d = 8.0f * u;
        const auto dot = juce::Rectangle<float> (d, d).withCentre ({ overRect.getX() + d * 0.5f, overRect.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.45f * overGlow));
        g.fillEllipse (dot.expanded (d * 0.9f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, overGlow));
        g.fillEllipse (dot);
        g.setColour (ui::haze.interpolatedWith (ui::bone, overGlow));
        g.setFont (ui::fitFont (ui::Face::mono, 11.0f * u, overRect.getWidth() - d - 8.0f * u, "OVER", true, 0.1f));
        g.drawText ("OVER", overRect.withTrimmedLeft (d + 8.0f * u), juce::Justification::centredLeft, false);
    }
}
