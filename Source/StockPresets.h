#pragma once

#include <array>
#include <map>
#include "Parameters.h"
#include "UI/OniriquePresets.h"

namespace onirique
{
/*  Presets d'usine de BROADCAST, rangés par usage pour le navigateur de la barre du haut (les tags). Valeurs en unités
    réelles (dB, %, ms, 0..10, index de choix, 0/1) ; un réglage non cité reprend sa valeur par défaut.
    Bande d'EQ : type 0 bell, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut. fetRatio : 0 4, 1 8, 2 12, 3 20, 4 ALL ;
    satType : 0 TUBE, 1 ANALOG, 2 TAPE. Chaque preset garde un niveau de sortie proche des autres (essai : Tests.cpp). */
namespace broadcast
{
enum Tags { stream = 1, podcast = 2, gaming = 4, radio = 8, voiceover = 16, natural = 32, cleanup = 64 };

inline juce::StringArray tagNames() { return { "Stream", "Podcast", "Gaming", "Radio", "Voice-over", "Natural", "Clean-up" }; }

/** Les réglages de session : un preset ne coupe jamais le micro et ne touche pas au casque. */
inline juce::StringArray notStored()
{
    juce::StringArray s { ids::bypass, ids::mute, ids::monOn, ids::monVol };
    for (auto* id : ids::mbSolo) s.add (id);
    return s;
}

inline std::vector<PresetBank::Stock> stockPresets()
{
    using namespace ids;
    using Map = std::map<juce::String, float>;
    auto band = [] (Map& m, int b, int type, float f, float g, float q)
    {
        m[peqOn[b]] = 1.0f; m[peqType[b]] = (float) type; m[peqFreq[b]] = f; m[peqGain[b]] = g; m[peqQ[b]] = q;
    };
    auto mb = [] (Map& m, std::array<float, 3> x, std::array<float, 4> t, std::array<float, 4> g, float a, float r)
    {
        m[mbOn] = 1.0f;
        for (int i = 0; i < 3; ++i) m[mbX[i]] = x[(size_t) i];
        for (int i = 0; i < 4; ++i) { m[mbThr[i]] = t[(size_t) i]; m[mbGain[i]] = g[(size_t) i]; }
        m[mbAttack] = a; m[mbRelease] = r;
    };
    auto stock = [] (const char* name, int tags, const Map& m)
    {
        PresetBank::Stock s { name, tags, {} };
        for (const auto& [id, value] : m) s.values.push_back ({ id, value });
        return s;
    };

    std::vector<PresetBank::Stock> v;
    v.push_back ({ "Init", 0, {} });

    {   // voix de stream propre, présente, sans souffle de pièce
        Map m { { gateOn, 1 }, { gateThr, -50 }, { gateRange, 22 }, { gateAttack, 1.5f }, { gateHold, 140 }, { gateRelease, 220 },
                { fetOn, 1 }, { fetIn, 8 }, { fetAttack, 3 }, { fetRelease, 5 }, { fetRatio, 0 }, { fetMix, 100 }, { fetOut, 12.5f },
                { deessOn, 1 }, { deessFreq, 5500 }, { deessAmt, 35 }, { plosive, 35 }, { limOn, 1 }, { limCeiling, -1 } };
        band (m, 0, 3, 80, 0, 0.71f); band (m, 2, 0, 320, -2.5f, 1.0f); band (m, 3, 0, 3200, 1.5f, 0.9f); band (m, 5, 2, 9000, 2.5f, 0.71f);
        v.push_back (stock ("Streamer Clean", stream, m));

        // la même voix, plus serrée et un peu de grain
        m[fetIn] = 11; m[fetRatio] = 1; m[fetOut] = 13;
        m[satOn] = 1; m[satType] = 1; m[drive] = 2;
        band (m, 3, 0, 3000, 2.5f, 0.9f);
        v.push_back (stock ("Streamer Punchy", stream, m));
    }
    {   // stream tard le soir : doux, rien qui agresse
        Map m { { gateOn, 1 }, { gateThr, -54 }, { gateRange, 14 }, { gateAttack, 2 }, { gateHold, 180 }, { gateRelease, 300 },
                { fetOn, 1 }, { fetIn, 6 }, { fetAttack, 2 }, { fetRelease, 5 }, { fetRatio, 0 }, { fetMix, 80 }, { fetOut, 11 },
                { deessOn, 1 }, { deessAmt, 30 }, { plosive, 35 }, { limOn, 1 }, { limCeiling, -1 }, { output, 2 } };
        band (m, 0, 3, 75, 0, 0.71f); band (m, 1, 1, 120, 1.5f, 0.71f); band (m, 5, 2, 10000, 1.5f, 0.71f);
        v.push_back (stock ("Late Night Stream", stream | natural, m));
    }
    {   // podcast : chaud, dense, régulier
        Map m { { gateOn, 1 }, { gateThr, -54 }, { gateRange, 16 }, { gateAttack, 2 }, { gateHold, 180 }, { gateRelease, 300 },
                { fetOn, 1 }, { fetIn, 6 }, { fetAttack, 3 }, { fetRelease, 5 }, { fetRatio, 0 }, { fetMix, 100 }, { fetOut, 12 }, { output, 5 },
                { deessOn, 1 }, { deessFreq, 5200 }, { deessAmt, 30 }, { plosive, 40 },
                { satOn, 1 }, { satType, 1 }, { drive, 2.5f }, { limOn, 1 }, { limCeiling, -1 } };
        band (m, 0, 3, 70, 0, 0.71f); band (m, 1, 1, 140, 2.0f, 0.71f); band (m, 2, 0, 350, -2.0f, 1.1f); band (m, 5, 2, 10000, 2.0f, 0.71f);
        mb (m, { 180, 1500, 6000 }, { -24, -22, -24, -26 }, { 0, 0, 0, 0.5f }, 20, 250);
        v.push_back (stock ("Podcast Warm", podcast, m));

        // deux voix qui se répondent : moins de grave, plus de présence
        band (m, 1, 1, 140, 1.0f, 0.71f); band (m, 3, 0, 3200, 2.0f, 0.9f);
        v.push_back (stock ("Podcast Duo Clear", podcast, m));
    }
    {   // voix grave et enveloppante
        Map m { { gateOn, 1 }, { gateThr, -52 }, { gateRange, 20 }, { gateAttack, 2 }, { gateHold, 150 }, { gateRelease, 260 },
                { fetOn, 1 }, { fetIn, 8 }, { fetAttack, 3 }, { fetRelease, 5 }, { fetRatio, 0 }, { fetMix, 100 }, { fetOut, 10 },
                { deessOn, 1 }, { deessAmt, 25 }, { plosive, 45 },
                { satOn, 1 }, { satType, 2 }, { drive, 3 }, { limOn, 1 }, { limCeiling, -1 } };
        band (m, 0, 3, 50, 0, 0.71f); band (m, 1, 1, 110, 4.0f, 0.71f); band (m, 2, 0, 260, -1.5f, 1.2f); band (m, 5, 2, 8000, 1.5f, 0.71f);
        v.push_back (stock ("Deep Voice", podcast | voiceover, m));
    }
    {   // voix off : posée, pleine, très régulière
        Map m { { gateOn, 1 }, { gateThr, -54 }, { gateRange, 18 }, { gateAttack, 2 }, { gateHold, 160 }, { gateRelease, 260 },
                { fetOn, 1 }, { fetIn, 9 }, { fetAttack, 3 }, { fetRelease, 5 }, { fetRatio, 0 }, { fetMix, 100 }, { fetOut, 12 },
                { deessOn, 1 }, { deessAmt, 35 }, { plosive, 45 }, { satOn, 1 }, { satType, 1 }, { drive, 2 },
                { limOn, 1 }, { limCeiling, -1 }, { output, 4.5f } };
        band (m, 0, 3, 70, 0, 0.71f); band (m, 1, 1, 120, 2.5f, 0.71f); band (m, 2, 0, 300, -2.0f, 1.1f); band (m, 3, 0, 3500, 1.5f, 0.9f);
        band (m, 5, 2, 10000, 2.0f, 0.71f);
        mb (m, { 170, 1500, 6000 }, { -24, -23, -24, -26 }, { 0, 0, 0, 0 }, 20, 250);
        v.push_back (stock ("Voice-over Pro", voiceover | podcast, m));
    }
    {   // radio : très dense, niveau constant, aigus brillants
        Map m { { gateOn, 1 }, { gateThr, -48 }, { gateRange, 30 }, { gateAttack, 1 }, { gateHold, 120 }, { gateRelease, 200 },
                { fetOn, 1 }, { fetIn, 14 }, { fetAttack, 5 }, { fetRelease, 6 }, { fetRatio, 1 }, { fetMix, 100 }, { fetOut, 13 }, { output, 6.5f },
                { deessOn, 1 }, { deessFreq, 6000 }, { deessAmt, 45 }, { plosive, 50 },
                { satOn, 1 }, { satType, 0 }, { drive, 3 }, { limOn, 1 }, { limCeiling, -0.5f } };
        band (m, 0, 3, 90, 0, 0.71f); band (m, 1, 1, 120, 3.0f, 0.71f); band (m, 3, 0, 2500, 2.0f, 0.8f); band (m, 5, 2, 9000, 3.0f, 0.71f);
        mb (m, { 160, 1600, 6500 }, { -26, -26, -26, -26 }, { 1, 1, 1, 1 }, 15, 220);
        v.push_back (stock ("Radio Broadcast", radio, m));

        // la radio qui pousse : plus de compression, plus de grain
        m[fetIn] = 16; m[fetRatio] = 2; m[drive] = 4;
        band (m, 1, 1, 110, 3.5f, 0.71f); band (m, 3, 0, 2500, 2.5f, 0.8f); band (m, 5, 2, 9000, 3.5f, 0.71f);
        for (auto* t : mbThr) m[t] = -28.0f;
        for (auto* g : mbGain) m[g] = 1.5f;
        v.push_back (stock ("Radio Hype", radio, m));
    }
    {   // jeu / chat vocal : intelligibilité maximale, aucun bruit de clavier
        Map m { { gateOn, 1 }, { gateThr, -44 }, { gateRange, 40 }, { gateAttack, 0.8f }, { gateHold, 90 }, { gateRelease, 150 },
                { fetOn, 1 }, { fetIn, 10 }, { fetAttack, 4 }, { fetRelease, 6 }, { fetRatio, 1 }, { fetMix, 100 }, { fetOut, 14 },
                { deessOn, 1 }, { deessFreq, 6000 }, { deessAmt, 30 }, { plosive, 50 }, { limOn, 1 }, { limCeiling, -1 } };
        band (m, 0, 3, 120, 0, 0.71f); band (m, 2, 0, 300, -3.0f, 1.0f); band (m, 3, 0, 3000, 3.0f, 1.0f); band (m, 5, 4, 12000, 0, 0.71f);
        v.push_back (stock ("Gaming Comms", gaming, m));

        // pièce bruyante (ventilateurs, clavier) : la porte plus ferme, le grave coupé plus haut
        m[gateThr] = -40; m[gateRange] = 45; m[fetIn] = 12; m[deessAmt] = 35; m[plosive] = 55;
        band (m, 0, 3, 140, 0, 0.71f); band (m, 3, 0, 2800, 3.5f, 1.0f); band (m, 5, 4, 11000, 0, 0.71f);
        v.push_back (stock ("Gaming Loud Room", gaming | cleanup, m));
    }
    {   // micro d'ordinateur ou pièce qui résonne : nettoyer avant tout
        Map m { { gateOn, 1 }, { gateThr, -42 }, { gateRange, 40 }, { gateAttack, 1 }, { gateHold, 80 }, { gateRelease, 140 },
                { fetOn, 1 }, { fetIn, 8 }, { fetAttack, 3 }, { fetRelease, 5 }, { fetRatio, 0 }, { fetMix, 100 }, { fetOut, 12 },
                { deessOn, 1 }, { deessAmt, 30 }, { plosive, 55 }, { limOn, 1 }, { limCeiling, -1 }, { output, 2 } };
        band (m, 0, 3, 120, 0, 0.71f); band (m, 2, 0, 250, -2.0f, 1.0f); band (m, 3, 0, 3000, 1.5f, 1.0f);
        v.push_back (stock ("Noisy Room Rescue", cleanup, m));
    }
    {   // naturel : juste un nettoyage léger
        Map m { { gateOn, 1 }, { gateThr, -56 }, { gateRange, 12 }, { gateAttack, 2 }, { gateHold, 200 }, { gateRelease, 350 },
                { fetOn, 1 }, { fetIn, 4 }, { fetAttack, 2 }, { fetRelease, 5 }, { fetRatio, 0 }, { fetMix, 60 }, { output, 11 },
                { deessOn, 1 }, { deessAmt, 20 }, { plosive, 25 }, { limOn, 1 }, { limCeiling, -1 } };
        band (m, 0, 3, 75, 0, 0.71f);
        v.push_back (stock ("Natural Light", natural, m));
    }
    {   // voix douce, proche du micro
        Map m { { gateOn, 1 }, { gateThr, -60 }, { gateRange, 8 }, { gateAttack, 2 }, { gateHold, 220 }, { gateRelease, 380 },
                { fetOn, 1 }, { fetIn, 3 }, { fetAttack, 2 }, { fetRelease, 5 }, { fetRatio, 0 }, { fetMix, 50 }, { output, 11 },
                { deessOn, 1 }, { deessAmt, 15 }, { plosive, 30 }, { limOn, 1 }, { limCeiling, -1 } };
        band (m, 0, 3, 60, 0, 0.71f); band (m, 1, 1, 120, 1.5f, 0.71f); band (m, 5, 2, 12000, 2.5f, 0.71f);
        v.push_back (stock ("Soft & Close", natural | stream, m));
    }
    return v;
}

/** Les presets enregistrés avec l'ancienne barre (fichiers .obpreset du même dossier) passent dans le navigateur,
    une fois : chacun est recopié au format de la nouvelle barre, l'original reste en place. */
inline void importOldUserPresets (const PresetBank& bank)
{
    for (const auto& file : bank.getFolder().findChildFiles (juce::File::findFiles, false, "*.obpreset"))
    {
        const auto target = file.withFileExtension (bank.getExtension());
        if (target.existsAsFile())
            continue;

        if (const auto xml = juce::XmlDocument::parse (file))
        {
            juce::ValueTree snapshot ("Snapshot");
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                if (const auto id = p->getStringAttribute ("id"); ! notStored().contains (id))
                    snapshot.setProperty (id, (float) p->getDoubleAttribute ("value"), nullptr);

            if (const auto out = snapshot.createXml())
                out->writeTo (target);
        }
    }
}
} // namespace broadcast
} // namespace onirique
