#include <juce_gui_extra/juce_gui_extra.h>
#include <BinaryData.h>
#include "MainComponent.h"

#include "Platform.h"

/*  ONIRIQUE BROADCAST : application Windows autonome.

    - Une seule instance : relancer l'application ramène simplement la fenêtre.
    - Zone de notification : fermer la fenêtre laisse le micro virtuel actif (option), l'icône donne accès à
      MUTE / BYPASS / Quitter. "--minimized" (démarrage avec Windows) lance directement dans la zone de notification.
    - Réglages : %APPDATA%/Onirique/Broadcast.settings (périphériques, état complet des effets, taille de la fenêtre),
      sauvegardés régulièrement et à la fermeture.
*/
class OniriqueBroadcastApp : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "ONIRIQUE BROADCAST"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options opt;
        opt.applicationName = "Broadcast";
        opt.folderName = "Onirique";
        opt.filenameSuffix = ".settings";
        opt.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (opt);

        engine = std::make_unique<BroadcastEngine>();
        restoreState();
        router = std::make_unique<AudioRouter> (*engine);
        router->setConfig (loadConfig());

        window = std::make_unique<MainWindow> (*this);
        tray = std::make_unique<Tray> (*this);

        const bool startHidden = commandLine.contains ("--minimized") && keepInTray();
        if (startHidden) window->setVisible (false);
        else             showWindow();

        startTimer (10000);
    }

    void shutdown() override
    {
        stopTimer();
        saveState();
        tray = nullptr;
        window = nullptr;
        router = nullptr;
        engine = nullptr;
        settings = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override { showWindow(); }

    //==========================================================================
    bool keepInTray() const { return settings->getBoolValue ("keepInTray", true); }

    void showWindow()
    {
        if (window == nullptr) return;
        window->setVisible (true);
        window->setMinimised (false);
        window->toFront (true);
        applyOniriqueWindowStyle (window->getWindowHandle());
    }

    void closeWindow()
    {
        if (keepInTray())
        {
            window->setVisible (false);
            saveState();
            if (! settings->getBoolValue ("trayHintShown", false))
            {
                settings->setValue ("trayHintShown", true);
                tray->showInfoBubble ("Onirique Broadcast is still running",
                                      "Your virtual mic stays live. Use the tray icon to open the window or quit.");
            }
        }
        else
            quit();
    }

    void toggleParam (const char* id)
    {
        if (auto* p = engine->apvts.getParameter (id))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
    }

    bool paramOn (const char* id) const { return engine->apvts.getRawParameterValue (id)->load() > 0.5f; }

private:
    //==========================================================================
    // Barre de titre dessinée par l'application (dans la continuité du dégradé) : - [] X à la manière de Windows 11,
    // glisser = déplacer la fenêtre, double-clic = agrandir / restaurer.
    class TitleBar : public juce::Component, private juce::Timer
    {
    public:
        std::function<void()> onMinimise, onMaximise, onClose;
        std::function<void (const juce::MouseEvent&, bool start)> onDrag;
        std::function<bool()> isMaximised;

        TitleBar() { startTimerHz (60); }

        void paint (juce::Graphics& g) override
        {
            using namespace onirique;
            auto r = getLocalBounds().toFloat();
            g.fillAll (ui::night);
            g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.72f), 0.0f, 0.0f, ui::blood.withAlpha (0.55f), r.getWidth(), 0.0f, false));
            g.fillRect (r);
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRect (r);
            g.setColour (ui::line.withMultipliedAlpha (0.9f));
            g.fillRect (r.removeFromBottom (1.0f));

            for (int b = 0; b < 3; ++b)
            {
                const auto br = button (b);
                if (glow[b] > 0.001f)
                {
                    g.setColour (b == 2 ? juce::Colour (0xffC42B1C).withAlpha (glow[b]) : juce::Colours::white.withAlpha (0.08f * glow[b]));
                    g.fillRect (br);
                }
                const auto c = br.getCentre();
                const float s = br.getHeight() * 0.16f, w = std::max (1.0f, br.getHeight() / 32.0f);
                g.setColour (ui::mist.withAlpha (0.85f).interpolatedWith (juce::Colours::white, b == 2 ? glow[b] : 0.0f));
                if (b == 0)
                    g.fillRect (juce::Rectangle<float> (2.0f * s, w).withCentre (c));
                else if (b == 1)
                {
                    if (isMaximised && isMaximised())
                    {
                        g.drawRect (juce::Rectangle<float> (1.6f * s, 1.6f * s).withCentre (c.translated (-0.3f * s, 0.3f * s)), w);
                        juce::Path p;
                        p.startNewSubPath (c.x - 0.5f * s, c.y - 0.9f * s);
                        p.lineTo (c.x + 0.9f * s, c.y - 0.9f * s);
                        p.lineTo (c.x + 0.9f * s, c.y + 0.5f * s);
                        g.strokePath (p, juce::PathStrokeType (w));
                    }
                    else
                        g.drawRect (juce::Rectangle<float> (2.0f * s, 2.0f * s).withCentre (c), w);
                }
                else
                {
                    g.drawLine (c.x - s, c.y - s, c.x + s, c.y + s, w * 1.1f);
                    g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, w * 1.1f);
                }
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { hover = buttonAt (e.position); }
        void mouseExit (const juce::MouseEvent&) override   { hover = -1; }
        void mouseDown (const juce::MouseEvent& e) override
        {
            pressed = buttonAt (e.position);
            if (pressed < 0 && onDrag) onDrag (e, true);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (pressed < 0 && onDrag) onDrag (e, false);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const int b = buttonAt (e.position);
            if (b >= 0 && b == pressed)
            {
                if (b == 0 && onMinimise) onMinimise();
                if (b == 1 && onMaximise) onMaximise();
                if (b == 2 && onClose) onClose();
            }
            pressed = -1;
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (buttonAt (e.position) < 0 && onMaximise) onMaximise();
        }

    private:
        juce::Rectangle<float> button (int b) const
        {
            const float h = (float) getHeight(), w = h * 1.45f;
            return { (float) getWidth() - w * (float) (3 - b), 0.0f, w, h - 1.0f };
        }
        int buttonAt (juce::Point<float> p) const
        {
            for (int b = 0; b < 3; ++b) if (button (b).contains (p)) return b;
            return -1;
        }
        void timerCallback() override
        {
            bool changed = false;
            for (int b = 0; b < 3; ++b) changed |= onirique::ui::approach (glow[b], hover == b ? 1.0f : 0.0f, 0.3f);
            if (changed) repaint();
        }

        int hover = -1, pressed = -1;
        float glow[3] {};
    };

    // Contenu de la fenêtre : barre de titre + interface (même échelle : la barre fait 34 px pour 1640 px de large)
    struct Shell : public juce::Component
    {
        static constexpr float kBarH = 34.0f;
        explicit Shell (MainComponent* m) : main (m)
        {
            addAndMakeVisible (bar);
            addAndMakeVisible (main.get());
            setSize (main->getWidth(), main->getHeight() + barHeightFor (main->getWidth()));
        }
        static int barHeightFor (int w) { return juce::roundToInt (kBarH * (float) w / onirique::ui::kBaseW); }
        static float aspect() { return onirique::ui::kBaseW / (onirique::ui::kBaseH + kBarH); }
        void resized() override
        {
            // l'interface garde ses proportions : la plus grande taille qui tient, centrée ; les marges éventuelles
            // (plein écran sur un écran plus large) prolongent le fond de l'application
            const float u = std::min ((float) getWidth() / onirique::ui::kBaseW, (float) getHeight() / (onirique::ui::kBaseH + kBarH));
            const int bh = juce::roundToInt (kBarH * u);
            bar.setBounds (0, 0, getWidth(), bh);
            const int mw = juce::roundToInt (onirique::ui::kBaseW * u), mh = juce::roundToInt (onirique::ui::kBaseH * u);
            main->setBounds ((getWidth() - mw) / 2, bh + (getHeight() - bh - mh) / 2, mw, mh);
        }
        void paint (juce::Graphics& g) override
        {
            using namespace onirique;
            g.fillAll (ui::night);
            g.setGradientFill (juce::ColourGradient (ui::blood.withAlpha (0.45f), 0.0f, (float) main->getY(),
                                                     ui::night.withAlpha (0.0f), 0.0f, (float) main->getY() + (float) main->getHeight() * 0.62f, false));
            g.fillRect (getLocalBounds().withTop (main->getY()));
        }
        TitleBar bar;
        std::unique_ptr<MainComponent> main;
    };

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (OniriqueBroadcastApp& a)
            : DocumentWindow ("ONIRIQUE BROADCAST", onirique::ui::night, 0), app (a)
        {
            setUsingNativeTitleBar (false);
            setTitleBarHeight (0);
            setDropShadowEnabled (false);   // ombre et coins arrondis fournis par Windows (applyOniriqueWindowStyle)
            setLookAndFeel (&lnf);

            auto* content = new MainComponent (*app.engine, *app.router, *app.settings);
            auto* shell = new Shell (content);
            shell->bar.onMinimise = [this] { setMinimised (true); };
            shell->bar.onMaximise = [this] { toggleMaximise(); };
            shell->bar.onClose    = [this] { app.closeWindow(); };
            shell->bar.onDrag     = [this] (const juce::MouseEvent& e, bool start) { dragWindow (e, start); };
            shell->bar.isMaximised = [this] { return maximised; };
            content->onWindowDrag = [this] (const juce::MouseEvent& e, bool start) { dragWindow (e, start); };
            content->onToggleMaximise  = [this] { toggleMaximise(); };
            setContentOwned (shell, true);

            setResizable (true, true);   // coin de redimensionnement en bas à droite
            const auto hFor = [] (int w) { return juce::roundToInt ((float) w / Shell::aspect()); };
            setResizeLimits (1100, hFor (1100), 3840, hFor (3840));
            if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) Shell::aspect());

            // position mémorisée (la taille vient de l'interface)
            const auto pos = juce::StringArray::fromTokens (app.settings->getValue ("windowXY"), " ", "");
            if (pos.size() == 2) setTopLeftPosition (pos[0].getIntValue(), pos[1].getIntValue());
            else centreWithSize (getWidth(), getHeight());
            const auto area = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getBounds())->userArea;
            // fenêtre plus grande que l'écran (autre écran, ancienne version) : ramenée à 90 % de la zone utile
            if (getWidth() > area.getWidth() || getHeight() > area.getHeight())
            {
                const float k = 0.9f * std::min ((float) area.getWidth() / (float) getWidth(), (float) area.getHeight() / (float) getHeight());
                centreWithSize (juce::roundToInt ((float) getWidth() * k), juce::roundToInt ((float) getHeight() * k));
            }
            if (! area.intersects (getBounds().reduced (60))) centreWithSize (getWidth(), getHeight());

            const auto logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
            setIcon (logo);
            if (auto* peer = getPeer()) peer->setIcon (logo);
        }

        ~MainWindow() override { setLookAndFeel (nullptr); }

        void closeButtonPressed() override { app.closeWindow(); }
        juce::BorderSize<int> getBorderThickness() const override { return {}; }
        juce::BorderSize<int> getContentComponentBorder() const override { return {}; }

        // déplacement de la fenêtre depuis la barre de titre ou le fond de l'en-tête
        void dragWindow (const juce::MouseEvent& e, bool start)
        {
            if (maximised) return;   // plein écran : la fenêtre ne se déplace pas (double-clic pour restaurer)
            const auto rel = e.getEventRelativeTo (this);
            if (start) dragger.startDraggingComponent (this, rel);
            else       dragger.dragComponent (this, rel, nullptr);
        }

        // agrandir : la plus grande taille qui tient à l'écran sans déformer l'interface ; re-cliquer restaure
        void toggleMaximise()
        {
            auto* c = getConstrainer();
            if (maximised)
            {
                maximised = false;
                if (c != nullptr) c->setFixedAspectRatio ((double) Shell::aspect());
                setBounds (restoreBounds);
            }
            else
            {
                // plein écran : toute la zone de travail de l'écran (barre des tâches exceptée)
                restoreBounds = getBounds();
                maximised = true;
                if (c != nullptr) c->setFixedAspectRatio (0.0);
                setBounds (juce::Desktop::getInstance().getDisplays().getDisplayForRect (getBounds())->userArea);
            }
            repaint();
        }

        void moved() override   { DocumentWindow::moved();   remember(); }
        void resized() override { DocumentWindow::resized(); remember(); }

    private:
        void remember()
        {
            // taille et position "normales" uniquement : jamais celles du plein écran
            if (isVisible() && ! isMinimised() && ! maximised)
            {
                app.settings->setValue ("windowXY", juce::String (getX()) + " " + juce::String (getY()));
                app.settings->setValue ("uiWidth", getWidth());
            }
        }

        OniriqueBroadcastApp& app;
        onirique::ui::FontScope fontScope;   // polices de la charte, le temps que la fenêtre est ouverte
        OniriqueLookAndFeel lnf;
        juce::Rectangle<int> restoreBounds;
        juce::ComponentDragger dragger;
        bool maximised = false;
    };

    //==========================================================================
    class Tray : public juce::SystemTrayIconComponent
    {
    public:
        explicit Tray (OniriqueBroadcastApp& a) : app (a)
        {
            const auto logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
            setIconImage (logo, logo);
            setIconTooltip ("ONIRIQUE BROADCAST - virtual mic");
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu()) { app.showWindow(); return; }

            juce::PopupMenu m;
            m.setLookAndFeel (&lnf);
            m.addSectionHeader ("ONIRIQUE BROADCAST");
            m.addItem (1, "Open");
            m.addSeparator();
            m.addItem (2, "Mute virtual mic", true, app.paramOn (onirique::ids::mute));
            m.addItem (3, "Bypass effects", true, app.paramOn (onirique::ids::bypass));
            m.addItem (4, "Listen in headphones", true, app.paramOn (onirique::ids::monOn));
            m.addSeparator();
            m.addItem (9, "Quit");

            juce::Process::makeForegroundProcess();
            m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
            {
                switch (r)
                {
                    case 1: app.showWindow(); break;
                    case 2: app.toggleParam (onirique::ids::mute); break;
                    case 3: app.toggleParam (onirique::ids::bypass); break;
                    case 4: app.toggleParam (onirique::ids::monOn); break;
                    case 9: juce::JUCEApplication::quit(); break;
                    default: break;
                }
            });
        }

        void mouseDoubleClick (const juce::MouseEvent&) override { app.showWindow(); }

    private:
        OniriqueBroadcastApp& app;
        onirique::ui::FontScope fontScope;   // polices de la charte, le temps que la fenêtre est ouverte
        OniriqueLookAndFeel lnf;
    };

    //==========================================================================
    void timerCallback() override { saveState(); }

    void restoreState()
    {
        constexpr int kFactoryVersion = 2;   // à incrémenter quand les presets d'usine sont recalibrés
        if (auto xml = settings->getXmlValue ("state"))
        {
            if (xml->hasTagName (engine->apvts.state.getType()))
                engine->apvts.replaceState (juce::ValueTree::fromXml (*xml));
            engine->presets.takeSnapshot();

            // presets d'usine mis à jour : un preset d'usine resté tel quel est rechargé dans sa nouvelle version
            if (settings->getIntValue ("factoryVersion", 1) < kFactoryVersion && ! settings->getBoolValue ("presetModified", false))
            {
                const auto name = engine->presets.getCurrentName();
                for (const auto& e : engine->presets.list())
                    if (e.factory && e.name == name) engine->presets.load (e);
            }
        }
        else
            engine->presets.loadByName ("Streamer Clean");   // premier lancement : une base prête à l'emploi
        settings->setValue ("factoryVersion", kFactoryVersion);

        // jamais muet au démarrage : le micro virtuel doit fonctionner tout de suite
        if (auto* p = engine->apvts.getParameter (onirique::ids::mute)) p->setValueNotifyingHost (0.0f);
        for (auto* id : onirique::ids::mbSolo)
            if (auto* p = engine->apvts.getParameter (id)) p->setValueNotifyingHost (0.0f);
    }

    void saveState()
    {
        if (settings == nullptr || engine == nullptr) return;
        if (auto xml = engine->apvts.copyState().createXml())
            settings->setValue ("state", xml.get());
        settings->setValue ("presetModified", engine->presets.isModified());
        settings->saveIfNeeded();
    }

    AudioRouter::Config loadConfig()
    {
        AudioRouter::Config c;
        const bool firstRun = ! settings->containsKey ("output");
        c.input = settings->getValue ("input", router->getDefaultDeviceName (true));
        c.output = firstRun ? AudioRouter::findVirtualCable (router->getOutputNames()) : settings->getValue ("output");
        if (c.output.isEmpty())   // pas encore de micro virtuel (ex. VB-CABLE installé depuis, puis redémarrage) : choisi d'office
        {
            c.output = AudioRouter::findVirtualCable (router->getOutputNames());
            if (c.output.isNotEmpty()) { settings->setValue ("output", c.output); settings->saveIfNeeded(); }
        }
        c.monitor = settings->getValue ("monitor");
        if (firstRun)
        {
            // retour casque : la sortie Windows par défaut si ce n'est pas le câble virtuel (écoute coupée au départ)
            const auto def = router->getDefaultDeviceName (false);
            if (! AudioRouter::isVirtualCable (def)) c.monitor = def;
        }
        c.inputChannel = settings->getIntValue ("inputChannel", 2);
        c.mode = settings->getIntValue ("mode", AudioRouter::shared);
        c.bufferSize = settings->getIntValue ("bufferSize", 0);
        if (firstRun)
        {
            settings->setValue ("input", c.input);
            settings->setValue ("output", c.output);
            settings->setValue ("monitor", c.monitor);
            settings->saveIfNeeded();
        }
        return c;
    }

    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<BroadcastEngine> engine;
    std::unique_ptr<AudioRouter> router;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<Tray> tray;
};

START_JUCE_APPLICATION (OniriqueBroadcastApp)
