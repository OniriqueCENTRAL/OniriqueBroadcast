#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace onirique
{
namespace ids
{
    inline constexpr const char* input      = "input";
    inline constexpr const char* output     = "output";
    inline constexpr const char* bypass     = "bypass";
    inline constexpr const char* mute       = "mute";
    inline constexpr const char* chainOrder = "chainOrder";

    // NOISE GATE
    inline constexpr const char* gateOn      = "gateOn";
    inline constexpr const char* gateThr     = "gateThreshold";
    inline constexpr const char* gateRange   = "gateRange";
    inline constexpr const char* gateAttack  = "gateAttack";
    inline constexpr const char* gateHold    = "gateHold";
    inline constexpr const char* gateRelease = "gateRelease";
    inline constexpr const char* gateHyst    = "gateHysteresis";
    inline constexpr const char* gateGuard   = "gateEchoGuard";

    // EQ paramétrique 6 bandes
    inline constexpr const char* eqOn       = "eqOn";
    inline constexpr const char* peqOn[6]   { "peq1On", "peq2On", "peq3On", "peq4On", "peq5On", "peq6On" };
    inline constexpr const char* peqType[6] { "peq1Type", "peq2Type", "peq3Type", "peq4Type", "peq5Type", "peq6Type" };
    inline constexpr const char* peqFreq[6] { "peq1Freq", "peq2Freq", "peq3Freq", "peq4Freq", "peq5Freq", "peq6Freq" };
    inline constexpr const char* peqGain[6] { "peq1Gain", "peq2Gain", "peq3Gain", "peq4Gain", "peq5Gain", "peq6Gain" };
    inline constexpr const char* peqQ[6]    { "peq1Q", "peq2Q", "peq3Q", "peq4Q", "peq5Q", "peq6Q" };

    // 1176
    inline constexpr const char* fetOn      = "fetOn";
    inline constexpr const char* fetIn      = "fetInput";
    inline constexpr const char* fetOut     = "fetOutput";
    inline constexpr const char* fetAttack  = "fetAttack";
    inline constexpr const char* fetRelease = "fetRelease";
    inline constexpr const char* fetRatio   = "fetRatio";
    inline constexpr const char* fetMix     = "fetMix";

    // MULTIBAND
    inline constexpr const char* mbOn       = "mbOn";
    inline constexpr const char* mbX[3]     { "mbXover1", "mbXover2", "mbXover3" };
    inline constexpr const char* mbThr[4]   { "mbThresh1", "mbThresh2", "mbThresh3", "mbThresh4" };
    inline constexpr const char* mbGain[4]  { "mbGain1", "mbGain2", "mbGain3", "mbGain4" };
    inline constexpr const char* mbAttack   = "mbAttack";
    inline constexpr const char* mbRelease  = "mbRelease";
    inline constexpr const char* mbSolo[4]  { "mbSolo1", "mbSolo2", "mbSolo3", "mbSolo4" };

    // DE-ESSER / PLOSIVES
    inline constexpr const char* deessOn    = "deessOn";
    inline constexpr const char* deessFreq  = "deessFreq";
    inline constexpr const char* deessAmt   = "deessAmt";
    inline constexpr const char* plosive    = "plosive";

    // SATURATION
    inline constexpr const char* satOn      = "satOn";
    inline constexpr const char* satType    = "satType";
    inline constexpr const char* drive      = "drive";

    // LIMITER de sortie
    inline constexpr const char* limOn      = "limOn";
    inline constexpr const char* limCeiling = "limCeiling";

    // retour casque (réglages de session, hors presets)
    inline constexpr const char* monOn      = "monitorOn";
    inline constexpr const char* monVol     = "monitorVolume";
}

// ---- ordre de traitement des 6 modules (GATE, EQ, 1176, MULTIBAND, DE-ESSER, SATURATION) :
//      une permutation, rangée dans un seul paramètre entier 0..719 (code de Lehmer, 0 = ordre d'origine)
constexpr int kOrderModules = 6;
enum Module { modGate = 0, modEq, modFet, modMulti, modDeess, modSat };

inline const char* orderName (int module)
{
    static const char* n[kOrderModules] { "GATE", "EQ", "1176", "MULTI", "DE-ESS", "SAT" };
    return n[juce::jlimit (0, kOrderModules - 1, module)];
}

inline std::array<int, kOrderModules> decodeOrder (int index)
{
    static constexpr int fact[kOrderModules] { 120, 24, 6, 2, 1, 1 };
    int pool[kOrderModules] { 0, 1, 2, 3, 4, 5 };
    int left = kOrderModules;
    std::array<int, kOrderModules> out {};
    index = juce::jlimit (0, 719, index);
    for (int i = 0; i < kOrderModules; ++i)
    {
        const int k = index / fact[i];
        index %= fact[i];
        out[(size_t) i] = pool[k];
        for (int j = k; j < left - 1; ++j) pool[j] = pool[j + 1];   // sans allocation : appelé par le thread audio
        --left;
    }
    return out;
}

inline int encodeOrder (const std::array<int, kOrderModules>& order)
{
    static constexpr int fact[kOrderModules] { 120, 24, 6, 2, 1, 1 };
    std::vector<int> pool { 0, 1, 2, 3, 4, 5 };
    int index = 0;
    for (int i = 0; i < kOrderModules; ++i)
    {
        const auto it = std::find (pool.begin(), pool.end(), order[(size_t) i]);
        const int k = (int) (it - pool.begin());
        index += k * fact[i];
        pool.erase (it);
    }
    return index;
}

inline juce::String orderText (int index)
{
    juce::StringArray s;
    for (int m : decodeOrder (index)) s.add (orderName (m));
    return s.joinIntoString (" > ");
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto dbText = [] (float v, int)
    {
        v = std::round (v * 10.0f) / 10.0f + 0.0f;   // évite "-0.0"
        return String (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
    };
    auto knobText = [] (float v, int) { return String (v, 1); };
    auto pctText  = [] (float v, int) { return String (juce::roundToInt (v)) + " %"; };
    auto msText   = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10.0f ? 1 : 0) + " ms"; };
    auto hzText   = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (juce::roundToInt (v)) + " Hz"; };
    auto addBool  = [&p] (const char* id, const String& name, bool def)
    { p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto addFloat = [&p] (const char* id, const String& name, NormalisableRange<float> r, float def, const String& label,
                          std::function<String (float, int)> text)
    { p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, Attr().withLabel (label).withStringFromValueFunction (std::move (text)))); };

    // ---- général
    addFloat (ids::input,  "Input",  { -24.0f, 24.0f, 0.1f }, 0.0f, "dB", dbText);
    addFloat (ids::output, "Output", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB", dbText);
    addBool (ids::bypass, "Bypass", false);
    addBool (ids::mute, "Mute", false);
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { ids::chainOrder, 1 }, "Processing Order", 0, 719, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return orderText (v); })));

    // ---- NOISE GATE
    addBool (ids::gateOn, "Gate On", false);
    addFloat (ids::gateThr,   "Gate Threshold", { -80.0f, 0.0f, 0.1f }, -48.0f, "dB", dbText);
    addFloat (ids::gateRange, "Gate Range",     { 0.0f, 80.0f, 0.1f }, 24.0f, "dB",
              [] (float v, int) { return v >= 79.5f ? String ("MUTE") : "-" + String (v, 1) + " dB"; });
    {
        NormalisableRange<float> atk (0.1f, 50.0f, 0.01f), hold (0.0f, 500.0f, 1.0f), rel (10.0f, 2000.0f, 1.0f);
        atk.setSkewForCentre (3.0f);
        hold.setSkewForCentre (100.0f);
        rel.setSkewForCentre (200.0f);
        addFloat (ids::gateAttack,  "Gate Attack",  atk,  1.5f,   "ms", msText);
        addFloat (ids::gateHold,    "Gate Hold",    hold, 120.0f, "ms", msText);
        addFloat (ids::gateRelease, "Gate Release", rel,  220.0f, "ms", msText);
    }
    addFloat (ids::gateHyst, "Gate Hysteresis", { 0.0f, 12.0f, 0.1f }, 5.0f, "dB", [] (float v, int) { return String (v, 1) + " dB"; });
    addBool (ids::gateGuard, "Echo Guard", true);   // voir NoiseGate : rend le micro compatible avec la suppression d'écho de Discord

    // ---- EQ PARAMÉTRIQUE
    addBool (ids::eqOn, "EQ On", true);
    {
        auto qText = [] (float v, int) { return String (v, 2); };
        const int defType[6] { 3, 1, 0, 0, 0, 2 };                         // LOW CUT, LOW SHELF, BELL x3, HIGH SHELF
        const float defFreq[6] { 80.0f, 150.0f, 400.0f, 2500.0f, 5000.0f, 10000.0f };
        const float defQ[6] { 0.71f, 0.71f, 1.0f, 1.0f, 1.0f, 0.71f };
        for (int b = 0; b < 6; ++b)
        {
            const String n = "EQ " + String (b + 1) + " ";
            addBool (ids::peqOn[b], n + "On", false);
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::peqType[b], 1 }, n + "Type",
                StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut" }, defType[b]));
            NormalisableRange<float> fr (20.0f, 20000.0f, 0.1f);
            fr.setSkewForCentre (1000.0f);
            addFloat (ids::peqFreq[b], n + "Freq", fr, defFreq[b], "Hz", hzText);
            addFloat (ids::peqGain[b], n + "Gain", { -18.0f, 18.0f, 0.1f }, 0.0f, "dB", dbText);
            NormalisableRange<float> qr (0.1f, 10.0f, 0.01f);
            qr.setSkewForCentre (1.0f);
            addFloat (ids::peqQ[b], n + "Q", qr, defQ[b], "", qText);
        }
    }

    // ---- 1176
    addBool (ids::fetOn, "1176 On", false);
    {
        auto timeText = [] (float s)
        {
            return s < 0.001f ? String (s * 1000.0f, s < 0.0001f ? 3 : 2) + " ms"
                              : (s < 1.0f ? String (juce::roundToInt (s * 1000.0f)) + " ms" : String (s, 2) + " s");
        };
        addFloat (ids::fetIn,  "1176 Input",  { -12.0f, 36.0f, 0.1f }, 0.0f, "dB", dbText);
        addFloat (ids::fetOut, "1176 Output", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB", dbText);
        addFloat (ids::fetAttack, "1176 Attack", { 1.0f, 7.0f, 0.01f }, 3.0f, "",
                  [timeText] (float v, int) { return timeText (800.0e-6f * std::pow (20.0f / 800.0f, (v - 1.0f) / 6.0f)); });
        addFloat (ids::fetRelease, "1176 Release", { 1.0f, 7.0f, 0.01f }, 5.0f, "",
                  [timeText] (float v, int) { return timeText (1.1f * std::pow (0.05f / 1.1f, (v - 1.0f) / 6.0f)); });
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::fetRatio, 1 }, "1176 Ratio",
                                                             StringArray { "4", "8", "12", "20", "All" }, 0));
        addFloat (ids::fetMix, "1176 Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, "%", pctText);
    }

    // ---- MULTIBAND
    addBool (ids::mbOn, "Multiband On", false);
    {
        struct X { float lo, hi, centre, def; const char* name; };
        const X xs[3] { { 40.0f, 1000.0f, 200.0f, 200.0f, "Multiband Low / Lo-Mid" },
                        { 300.0f, 8000.0f, 1500.0f, 1500.0f, "Multiband Lo-Mid / Hi-Mid" },
                        { 1500.0f, 16000.0f, 6000.0f, 6000.0f, "Multiband Hi-Mid / High" } };
        for (int i = 0; i < 3; ++i)
        {
            NormalisableRange<float> r (xs[i].lo, xs[i].hi, 1.0f);
            r.setSkewForCentre (xs[i].centre);
            addFloat (ids::mbX[i], xs[i].name, r, xs[i].def, "Hz", hzText);
        }
        const char* bandNames[4] { "Low", "Lo-Mid", "Hi-Mid", "High" };
        for (int b = 0; b < 4; ++b)
        {
            addFloat (ids::mbThr[b],  String ("Multiband ") + bandNames[b] + " Threshold", { -60.0f, 0.0f, 0.1f }, -20.0f, "dB", dbText);
            addFloat (ids::mbGain[b], String ("Multiband ") + bandNames[b] + " Gain", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB", dbText);
        }
        NormalisableRange<float> atk (1.0f, 200.0f, 0.1f), rel (20.0f, 2000.0f, 1.0f);
        atk.setSkewForCentre (20.0f);
        rel.setSkewForCentre (250.0f);
        addFloat (ids::mbAttack,  "Multiband Attack",  atk, 15.0f,  "ms", msText);
        addFloat (ids::mbRelease, "Multiband Release", rel, 200.0f, "ms", msText);
        for (int b = 0; b < 4; ++b)
            p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::mbSolo[b], 1 }, "Multiband Listen " + String (b + 1), false));
    }

    // ---- DE-ESSER / PLOSIVES
    addBool (ids::deessOn, "De-Esser On", false);
    {
        NormalisableRange<float> fr (3000.0f, 10000.0f, 1.0f);
        fr.setSkewForCentre (5500.0f);
        addFloat (ids::deessFreq, "De-Ess Freq", fr, 5500.0f, "Hz", [] (float v, int) { return String (juce::roundToInt (v)) + " Hz"; });
    }
    addFloat (ids::deessAmt, "De-Ess Amount", { 0.0f, 100.0f, 0.1f }, 30.0f, "%", pctText);
    addFloat (ids::plosive,  "Plosives",      { 0.0f, 100.0f, 0.1f }, 0.0f,  "%", pctText);

    // ---- SATURATION
    addBool (ids::satOn, "Saturation On", false);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::satType, 1 }, "Saturation Type",
                                                         StringArray { "Tube", "Analog", "Tape" }, 0));
    addFloat (ids::drive, "Drive", { 0.0f, 10.0f, 0.01f }, 2.0f, "", knobText);

    // ---- LIMITER
    addBool (ids::limOn, "Limiter On", true);
    addFloat (ids::limCeiling, "Limiter Ceiling", { -12.0f, 0.0f, 0.1f }, -1.0f, "dB", dbText);

    // ---- retour casque
    addBool (ids::monOn, "Monitor On", false);
    addFloat (ids::monVol, "Monitor Volume", { -60.0f, 6.0f, 0.1f }, -6.0f, "dB", dbText);

    return { p.begin(), p.end() };
}
} // namespace onirique
