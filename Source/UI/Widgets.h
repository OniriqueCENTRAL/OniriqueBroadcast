#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"
#include "../Parameters.h"
#include "../PresetManager.h"

// Bouton rotatif (ou curseur, Layout::fader / horizontal) complet : nom + contrôle + valeur dans une pastille.
// L'arc lumineux suit la valeur en glissant ; il s'illumine au survol et pendant le réglage.
class KnobComponent : public juce::Component, public Animated
{
public:
    enum class Layout { textBelow, fader, horizontal };   // horizontal : NOM  ━━●━━  [valeur]

    KnobComponent (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title,
                   const juce::String& minLabel, const juce::String& maxLabel, Layout layout,
                   const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar = false);
    ~KnobComponent() override;

    void setMaxDiameter (float d) { maxD = d; resized(); }
    std::function<void (bool)> onFocusChange;
    // activité (ex. réduction en dB) : une LED braise s'allume à côté du nom quand elle dépasse 0.5
    std::function<float()> activity;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setFocus (bool hover, bool drag);

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String title;
    Layout layout;
    bool dragging = false, hovering = false;
    float shown = 0.0f, glow = 0.0f, act = 0.0f;
    juce::Rectangle<float> nameArea, valueArea;
    float unit = 1.0f, maxD = 1.0e6f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};

// Valeur réglable dans une pastille (glisser verticalement, molette, double-clic = défaut).
class DragValue : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    DragValue (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& tooltip);
    std::function<void (bool)> onFocusChange;
    void setPrefix (const juce::String& p) { prefix = p; repaint(); }

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float startNorm = 0.0f, glow = 0.0f;
    juce::String prefix;
    bool hovering = false, dragging = false;
};

// Bouton power d'un module : anneau braise lumineux quand le module est actif.
class PowerButton : public juce::ToggleButton, public Animated
{
public:
    PowerButton() { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    float on = 1.0f;
};

// Interrupteur : grand (MUTE), petit (BYPASS avec LED) ou mini (dans une bande de sélecteurs)
class PillToggle : public juce::ToggleButton, public Animated
{
public:
    enum class Style { big, small, mini };
    PillToggle (const juce::String& label, Style s) : juce::ToggleButton (label), style (s) { setClickingTogglesState (true); }

    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Style style;
    juce::String caption;
    float on = 0.0f;
};

// Sélecteur segmenté relié à un paramètre à choix (ou libre, sans paramètre) ; la pastille braise glisse vers le choix.
class SegmentSelector : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    SegmentSelector (juce::AudioProcessorValueTreeState&, const char* paramId,
                     const juce::StringArray& labels, const juce::StringArray& tooltips);
    SegmentSelector (const juce::StringArray& labels, const juce::StringArray& tooltips);   // sans paramètre

    std::function<void (int)> onChange;   // version sans paramètre
    void setSelected (int idx) { selected = juce::jlimit (0, labels.size() - 1, idx); repaint(); }
    int getSelected() const noexcept { return selected; }

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    void setTitle (const juce::String& t, const juce::String& tip = {}) { title = t; titleTip = tip; repaint(); }

private:
    int segmentAt (float x) const noexcept;
    juce::Rectangle<float> segmentArea() const noexcept;

    juce::StringArray labels, tips;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int selected = 0, hover = -1;
    float slide = -1.0f;
    juce::String title, titleTip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentSelector)
};

// VU-mètre à aiguille de réduction de gain (0 à droite, -20 dB à gauche), balistique VU (~300 ms).
class VuMeter : public juce::Component, public Animated
{
public:
    explicit VuMeter (std::function<float()> source) : read (std::move (source)) {}
    void setActive (bool a) { active = a; }
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    std::function<float()> read;
    float needle = 0.0f, shownDb = 0.0f;
    bool active = true;
};

// Mètre de niveau vertical mono : crête instantanée, retombée douce, maintien de crête 1.5 s,
// zone braise près de 0 dBFS, rouge au-delà ; crête maintenue affichée sous la barre (clic = remise à zéro).
class LevelMeter : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit LevelMeter (std::function<float()> consumePeak) : consume (std::move (consumePeak))
    {
        setTooltip ("Peak level (dBFS). The number is the highest recent peak: click to reset it.");
    }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override { maxDb = -100.0f; repaint(); }

private:
    static float norm (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 62.0f); }   // -60 .. +2 dBFS

    std::function<float()> consume;
    float levelDb = -100.0f, holdDb = -100.0f, maxDb = -100.0f;
    int holdFrames = 0, maxFrames = 0;
};

// Barre de niveau horizontale (carte ROUTING) : signal du micro, repère du seuil du gate (optionnel).
class SignalBar : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit SignalBar (std::function<float()> consumePeak) : consume (std::move (consumePeak)) {}
    std::function<float()> markerDb;   // repère (ex. seuil du gate), NaN = aucun
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    std::function<float()> consume;
    float levelDb = -100.0f, holdDb = -100.0f;
    int holdFrames = 0;
};

// Mètre de réduction de gain vertical : la barre descend depuis 0 dB, attaque immédiate, retour doux.
class GainReductionBar : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    GainReductionBar (std::function<float()> source, const juce::String& tip, float rangeDb = 18.0f)
        : read (std::move (source)), range (rangeDb) { setTooltip (tip); }
    void setActive (bool a) { active = a; }
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    std::function<float()> read;
    float gr = 0.0f, hold = 0.0f, range = 18.0f;
    int holdFrames = 0;
    bool active = true;
};

// Menu déroulant en pastille : étiquette courte + valeur + triangle. Le contenu du menu est fourni à l'ouverture.
class PillMenu : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit PillMenu (const juce::String& tag) : tagText (tag) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    // text : valeur (nom du périphérique) ; label : texte affiché à la place, si renseigné
    struct Item { juce::String text; bool enabled = true; bool header = false; juce::String badge; juce::String label; };
    std::function<std::vector<Item>()> items;          // liste à l'ouverture
    std::function<void (int index, const juce::String& text)> onSelect;

    void setValue (const juce::String& v, bool warn = false, const juce::String& shown = {})
    {
        if (v != value || warn != warning || shown != display) { value = v; warning = warn; display = shown; repaint(); }
    }
    const juce::String& getValue() const noexcept { return value; }
    void setStatus (juce::Colour c, float level) { statusColour = c; statusLevel = level; }   // LED à droite

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; }

private:
    juce::String tagText, value, display;
    bool hover = false, warning = false;
    float glow = 0.0f, led = 0.0f, statusLevel = 0.0f;
    juce::Colour statusColour { onirique::ui::live };
};

// Ordre de traitement (en-tête) : 6 pastilles GATE > EQ > 1176 > MULTI > DE-ESS > SAT à glisser à la souris.
class ProcessingOrderBar : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit ProcessingOrderBar (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    static constexpr int kN = onirique::kOrderModules;
    float chipW() const noexcept;
    float gapW() const noexcept { return (float) getHeight() * 0.75f; }
    float slotX (int slot) const noexcept { return (float) slot * (chipW() + gapW()); }
    int moduleAt (juce::Point<float>) const noexcept;
    std::array<int, kN> previewOrder() const noexcept;
    bool moduleOn (int module) const;

    juce::AudioProcessorValueTreeState& apvts;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::array<int, kN> order { 0, 1, 2, 3, 4, 5 };
    std::array<float, kN> chipX {}, lit {}, hoverGlow {};
    bool placed = false;
    int dragModule = -1, hoverModule = -1;
    float dragOffset = 0.0f, dragX = 0.0f;
    bool dragMoved = false;
};

// Barre de presets (en-tête) : précédent / nom (menu) / suivant + SAVE (champ de nom intégré).
class PresetBar : public juce::Component, public Animated
{
public:
    explicit PresetBar (onirique::PresetManager&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum Zone { none = -1, prev, name, next, save };
    Zone zoneAt (juce::Point<float>) const noexcept;
    void showMenu();
    void startSave();
    void endSave (bool commit);

    onirique::PresetManager& pm;
    juce::TextEditor editor;
    juce::Rectangle<float> zones[4];
    juce::String shownName;
    bool modified = false;
    Zone hover = none;
    float glow[4] {};
    int frame = 0;
};
