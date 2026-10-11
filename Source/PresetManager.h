#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include "Parameters.h"

namespace onirique
{
/*  Presets : usine (définis ici) + utilisateur (fichiers .obpreset dans
    %APPDATA%/Onirique/Broadcast/Presets). Thread de l'interface uniquement.

    - Un preset contient tous les réglages de son SAUF les réglages de session : Bypass, Mute, retour casque,
      écoute des bandes du multibande (charger un preset ne coupe jamais le micro et ne change pas le casque).
    - Un paramètre absent d'un fichier reprend sa valeur par défaut.
    - Le nom du preset courant est rangé dans l'état : il revient au prochain lancement.
    - "Modifié" : comparaison des valeurs actuelles avec celles du chargement.
*/
class PresetManager
{
public:
    struct Entry { juce::String name; bool factory = false; juce::File file; int factoryIndex = -1; };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        folder().createDirectory();
    }

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Onirique").getChildFile ("Broadcast").getChildFile ("Presets");
    }

    std::vector<Entry> list() const
    {
        std::vector<Entry> v;
        for (int i = 0; i < (int) factory().size(); ++i)
            v.push_back ({ factory()[(size_t) i].name, true, {}, i });
        auto files = folder().findChildFiles (juce::File::findFiles, false, "*.obpreset");
        files.sort();
        for (auto& f : files) v.push_back ({ f.getFileNameWithoutExtension(), false, f, -1 });
        return v;
    }

    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }

    bool isModified() const
    {
        if (snapshot.empty()) return false;
        size_t i = 0;
        for (auto* p : params())
            if (i < snapshot.size() && std::abs (p->getValue() - snapshot[i++]) > 1.0e-4f) return true;
        return false;
    }

    void load (const Entry& e)
    {
        std::map<juce::String, float> values;
        if (e.factory)
            values = factory()[(size_t) e.factoryIndex].values;
        else if (auto xml = juce::XmlDocument::parse (e.file))
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");

        for (auto* p : params())
        {
            const auto it = values.find (p->getParameterID());
            const float norm = it != values.end() ? p->convertTo0to1 (it->second) : p->getDefaultValue();
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
        apvts.state.setProperty ("presetName", e.name, nullptr);
        apvts.state.setProperty ("mbPreset", "Custom", nullptr);
        takeSnapshot();
    }

    bool loadByName (const juce::String& name)
    {
        for (const auto& e : list())
            if (e.name == name) { load (e); return true; }
        return false;
    }

    // renvoie false si le nom est vide ; un nom d'usine est suffixé pour ne jamais l'écraser
    bool save (juce::String name)
    {
        name = juce::File::createLegalFileName (name.trim());
        if (name.isEmpty()) return false;
        for (const auto& f : factory()) if (f.name.equalsIgnoreCase (name)) name << " (user)";

        juce::XmlElement xml ("OniriqueBroadcastPreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (auto* p : params())
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", p->getParameterID());
            e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
        }
        if (! xml.writeTo (folder().getChildFile (name + ".obpreset"))) return false;
        apvts.state.setProperty ("presetName", name, nullptr);
        takeSnapshot();
        return true;
    }

    void remove (const Entry& e)
    {
        if (! e.factory) e.file.deleteFile();
    }

    // charge le preset précédent / suivant dans la liste (boucle)
    void step (int delta)
    {
        const auto v = list();
        if (v.empty()) return;
        int idx = 0;
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i].name == getCurrentName()) idx = i;
        load (v[(size_t) ((idx + delta + (int) v.size()) % (int) v.size())]);
    }

    void takeSnapshot()
    {
        snapshot.clear();
        for (auto* p : params()) snapshot.push_back (p->getValue());
    }

private:
    struct Factory { juce::String name; std::map<juce::String, float> values; };

    std::vector<juce::RangedAudioParameter*> params() const
    {
        std::vector<juce::RangedAudioParameter*> v;
        for (auto* p : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto id = r->getParameterID(); id != ids::bypass && id != ids::mute && id != ids::monOn
                                                         && id != ids::monVol && ! id.startsWith ("mbSolo"))
                    v.push_back (r);
        return v;
    }

    // valeurs en unités réelles (dB, %, ms, 0..10, index de choix, 0/1)
    static const std::vector<Factory>& factory()
    {
        using namespace ids;
        // bande d'EQ : on, type (0 bell, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut), fréquence, gain, Q
        auto band = [] (std::map<juce::String, float>& m, int b, int type, float f, float g, float q)
        {
            m[peqOn[b]] = 1.0f; m[peqType[b]] = (float) type; m[peqFreq[b]] = f; m[peqGain[b]] = g; m[peqQ[b]] = q;
        };
        auto mb = [] (std::map<juce::String, float>& m, std::array<float, 3> x, std::array<float, 4> t, std::array<float, 4> g, float a, float r)
        {
            m[mbOn] = 1.0f;
            for (int i = 0; i < 3; ++i) m[mbX[i]] = x[(size_t) i];
            for (int i = 0; i < 4; ++i) { m[mbThr[i]] = t[(size_t) i]; m[mbGain[i]] = g[(size_t) i]; }
            m[mbAttack] = a; m[mbRelease] = r;
        };

        static const std::vector<Factory> f = [&]
        {
            std::vector<Factory> v;
            v.push_back ({ "Init", {} });

            {   // voix de stream propre, présente, sans souffle de pièce
                std::map<juce::String, float> m { { gateOn, 1.0f }, { gateThr, -50.0f }, { gateRange, 22.0f }, { gateAttack, 1.5f }, { gateHold, 140.0f }, { gateRelease, 220.0f },
                                                   { fetOn, 1.0f }, { fetIn, 8.0f }, { fetAttack, 3.0f }, { fetRelease, 5.0f }, { fetRatio, 0.0f }, { fetMix, 100.0f }, { fetOut, 12.5f },
                                                   { deessOn, 1.0f }, { deessFreq, 5500.0f }, { deessAmt, 35.0f }, { plosive, 35.0f },
                                                   { limOn, 1.0f }, { limCeiling, -1.0f } };
                band (m, 0, 3, 80, 0, 0.71f); band (m, 2, 0, 320, -2.5f, 1.0f); band (m, 3, 0, 3200, 1.5f, 0.9f); band (m, 5, 2, 9000, 2.5f, 0.71f);
                v.push_back ({ "Streamer Clean", m });
            }
            {   // podcast : chaud, dense, régulier
                std::map<juce::String, float> m { { gateOn, 1.0f }, { gateThr, -54.0f }, { gateRange, 16.0f }, { gateAttack, 2.0f }, { gateHold, 180.0f }, { gateRelease, 300.0f },
                                                   { fetOn, 1.0f }, { fetIn, 6.0f }, { fetAttack, 3.0f }, { fetRelease, 5.0f }, { fetRatio, 0.0f }, { fetMix, 100.0f }, { fetOut, 12.0f }, { output, 5.0f },
                                                   { deessOn, 1.0f }, { deessFreq, 5200.0f }, { deessAmt, 30.0f }, { plosive, 40.0f },
                                                   { satOn, 1.0f }, { satType, 1.0f }, { drive, 2.5f }, { limOn, 1.0f }, { limCeiling, -1.0f } };
                band (m, 0, 3, 70, 0, 0.71f); band (m, 1, 1, 140, 2.0f, 0.71f); band (m, 2, 0, 350, -2.0f, 1.1f); band (m, 5, 2, 10000, 2.0f, 0.71f);
                mb (m, { 180, 1500, 6000 }, { -24, -22, -24, -26 }, { 0, 0, 0, 0.5f }, 20, 250);
                v.push_back ({ "Podcast Warm", m });
            }
            {   // radio : très dense, niveau constant, aigus brillants
                std::map<juce::String, float> m { { gateOn, 1.0f }, { gateThr, -48.0f }, { gateRange, 30.0f }, { gateAttack, 1.0f }, { gateHold, 120.0f }, { gateRelease, 200.0f },
                                                   { fetOn, 1.0f }, { fetIn, 14.0f }, { fetAttack, 5.0f }, { fetRelease, 6.0f }, { fetRatio, 1.0f }, { fetMix, 100.0f }, { fetOut, 13.0f }, { output, 6.5f },
                                                   { deessOn, 1.0f }, { deessFreq, 6000.0f }, { deessAmt, 45.0f }, { plosive, 50.0f },
                                                   { satOn, 1.0f }, { satType, 0.0f }, { drive, 3.0f }, { limOn, 1.0f }, { limCeiling, -0.5f } };
                band (m, 0, 3, 90, 0, 0.71f); band (m, 1, 1, 120, 3.0f, 0.71f); band (m, 3, 0, 2500, 2.0f, 0.8f); band (m, 5, 2, 9000, 3.0f, 0.71f);
                mb (m, { 160, 1600, 6500 }, { -26, -26, -26, -26 }, { 1, 1, 1, 1 }, 15, 220);
                v.push_back ({ "Radio Broadcast", m });
            }
            {   // jeu / chat vocal : intelligibilité maximale, aucun bruit de clavier
                std::map<juce::String, float> m { { gateOn, 1.0f }, { gateThr, -44.0f }, { gateRange, 40.0f }, { gateAttack, 0.8f }, { gateHold, 90.0f }, { gateRelease, 150.0f },
                                                   { fetOn, 1.0f }, { fetIn, 10.0f }, { fetAttack, 4.0f }, { fetRelease, 6.0f }, { fetRatio, 1.0f }, { fetMix, 100.0f }, { fetOut, 14.0f },
                                                   { deessOn, 1.0f }, { deessFreq, 6000.0f }, { deessAmt, 30.0f }, { plosive, 50.0f },
                                                   { limOn, 1.0f }, { limCeiling, -1.0f } };
                band (m, 0, 3, 120, 0, 0.71f); band (m, 2, 0, 300, -3.0f, 1.0f); band (m, 3, 0, 3000, 3.0f, 1.0f); band (m, 5, 4, 12000, 0, 0.71f);
                v.push_back ({ "Gaming Comms", m });
            }
            {   // voix grave et enveloppante
                std::map<juce::String, float> m { { gateOn, 1.0f }, { gateThr, -52.0f }, { gateRange, 20.0f }, { gateAttack, 2.0f }, { gateHold, 150.0f }, { gateRelease, 260.0f },
                                                   { fetOn, 1.0f }, { fetIn, 8.0f }, { fetAttack, 3.0f }, { fetRelease, 5.0f }, { fetRatio, 0.0f }, { fetMix, 100.0f }, { fetOut, 10.0f },
                                                   { deessOn, 1.0f }, { deessAmt, 25.0f }, { plosive, 45.0f },
                                                   { satOn, 1.0f }, { satType, 2.0f }, { drive, 3.0f }, { limOn, 1.0f }, { limCeiling, -1.0f } };
                band (m, 0, 3, 50, 0, 0.71f); band (m, 1, 1, 110, 4.0f, 0.71f); band (m, 2, 0, 260, -1.5f, 1.2f); band (m, 5, 2, 8000, 1.5f, 0.71f);
                v.push_back ({ "Deep Voice", m });
            }
            {   // naturel : juste un nettoyage léger
                std::map<juce::String, float> m { { gateOn, 1.0f }, { gateThr, -56.0f }, { gateRange, 12.0f }, { gateAttack, 2.0f }, { gateHold, 200.0f }, { gateRelease, 350.0f },
                                                   { fetOn, 1.0f }, { fetIn, 4.0f }, { fetAttack, 2.0f }, { fetRelease, 5.0f }, { fetRatio, 0.0f }, { fetMix, 60.0f }, { output, 11.0f },
                                                   { deessOn, 1.0f }, { deessAmt, 20.0f }, { plosive, 25.0f }, { limOn, 1.0f }, { limCeiling, -1.0f } };
                band (m, 0, 3, 75, 0, 0.71f);
                v.push_back ({ "Natural Light", m });
            }
            return v;
        }();
        return f;
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<float> snapshot;
};
} // namespace onirique
