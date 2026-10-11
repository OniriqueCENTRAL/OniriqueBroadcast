#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Widgets.h"
#include "../Engine.h"
#include "../MultibandPresets.h"

// Analyseur en temps réel : spectre d'entrée, spectre de sortie et courbe EXACTE de l'EQ paramétrique
// (mêmes filtres que l'audio). Les 6 bandes sont des points numérotés à glisser.
class SpectrumAnalyzer : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit SpectrumAnalyzer (BroadcastEngine&);

    std::function<void (int)> onSelect;
    int getSelected() const noexcept { return selected; }
    void select (int band);

    void paint (juce::Graphics&) override;
    void tick() override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static void showTypeMenu (juce::Component& target, juce::AudioProcessorValueTreeState&, int band);

private:
    static constexpr int kOrder = 12, kN = 1 << kOrder, kPts = 256, kPeq = onirique::ParametricEQ::kBands;
    static constexpr float kEqRange = 18.0f;

    void analyse (onirique::SpectrumTap&, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec);
    float hzAt (int i) const noexcept { return 20.0f * std::pow (1000.0f, (float) i / (float) (kPts - 1)); }
    float param (const char* id) const { return engine.apvts.getRawParameterValue (id)->load(); }
    juce::Rectangle<float> plotArea() const noexcept;
    float xOf (float hz) const noexcept;
    float hzOf (float x) const noexcept;
    float yEq (float db) const noexcept;
    float dbOf (float y) const noexcept;
    onirique::ParametricEQ::Band band (int b) const;
    juce::Point<float> nodePos (int b) const;
    int nodeAt (juce::Point<float>) const;
    void setParam (const char* id, float v, bool gesture = true);

    BroadcastEngine& engine;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, histIn, histOut, scratch, fftBuf;
    int writeIn = 0, writeOut = 0, frame = 0;
    float windowGain = 1.0f;
    std::array<float, kPts> specIn {}, specOut {}, curve {}, selCurve {};
    float eqAmt = 1.0f;
    int selected = 0, hoverNode = -1, dragNode = -1;
    float nodeGlow[kPeq] {};
    int qFlashBand = -1, qFlashFrames = 0;
};

// Barre de réglage de la bande sélectionnée de l'EQ paramétrique : n° + LED (on/off) + type (menu).
class PeqBandChip : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit PeqBandChip (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        setTooltip ("Selected EQ band: click the LED to switch it on/off, click the name to change its type.");
    }
    void setBand (int b) { band = b; repaint(); }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::AudioProcessorValueTreeState& apvts;
    int band = 0;
    float lit = 0.0f;
    juce::String shownType;
};

// Écran du MULTIBAND : 4 bandes avec niveau d'entrée, seuil (glisser verticalement dans la bande),
// réduction de gain (rideau braise depuis le haut), fréquences de coupure (glisser les séparations).
class MultibandDisplay : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit MultibandDisplay (BroadcastEngine&);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

private:
    static constexpr int kB = 4;
    juce::Rectangle<float> plotArea() const noexcept;
    float xOf (float hz) const noexcept;
    float hzOf (float x) const noexcept;
    float yOf (float db) const noexcept;
    float dbOf (float y) const noexcept;
    float bandLeft (int b) const noexcept;
    float bandRight (int b) const noexcept;
    int crossoverAt (float x) const noexcept;
    int bandAt (float x) const noexcept;
    static float value (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }
    static void setValue (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }

    BroadcastEngine& engine;
    juce::RangedAudioParameter *xP[3] {}, *thrP[kB] {}, *gainP[kB] {};
    float x[3] { 200.0f, 1500.0f, 6000.0f }, thr[kB] {}, gain[kB] {}, gr[kB] {}, lvl[kB] { -100, -100, -100, -100 };
    float hoverGlow[kB] {}, xGlow[3] {};
    bool active = false;
    int dragX = -1, dragBand = -1, hoverX = -1, hoverBand = -1;
};

// Sélecteur des presets internes du MULTIBAND.
class MbPresetBox : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit MbPresetBox (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        setTooltip ("Multiband presets: starting points for voice (only the multiband settings change).");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String shown;
    bool hover = false;
    float glow = 0.0f;
};

// Écran du NOISE GATE : historique défilant du niveau (5 s), état ouvert / fermé, courbe de gain,
// seuil (glisser verticalement), hystérésis, bruit de fond mesuré et bouton LEARN.
class GateDisplay : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit GateDisplay (BroadcastEngine&);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    static constexpr int kHist = 300;
    static constexpr float kMinDb = -80.0f;
    juce::Rectangle<float> plotArea() const noexcept;
    juce::Rectangle<float> learnArea() const noexcept;
    float yOf (float db) const noexcept;
    float dbOf (float y) const noexcept;
    float param (const char* id) const { return engine.apvts.getRawParameterValue (id)->load(); }

    BroadcastEngine& engine;
    juce::RangedAudioParameter* thrP = nullptr;
    std::array<float, kHist> level {}, gainHist {};
    int head = 0;
    float thrShown = -48.0f, hystShown = 5.0f, floorShown = -90.0f, openLed = 0.0f, active = 0.0f;
    bool echoBlocked = false;   // la garde retient la fuite du son joué
    float thrGlow = 0.0f, learnGlow = 0.0f;
    bool dragging = false, hoverThr = false, hoverLearn = false;
    int learnFrames = 0;             // > 0 : écoute du bruit de fond en cours
    float learnMin = 0.0f;
    juce::String learnMessage;
    int messageFrames = 0;
};
