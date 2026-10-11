# ONIRIQUE BROADCAST

Application Windows qui fait passer le micro dans une chaîne d'effets en temps réel puis l'envoie vers un
**micro virtuel** utilisable dans Discord, OBS, les jeux et toute application qui accepte un microphone.
Interface et animations reprises de l'ONIRIQUE VOCALBOX (même identité visuelle, mêmes composants).

```
MICRO ──► GAIN ──► [ GATE · EQ · 1176 · MULTIBAND · DE-ESSER/PLOSIVES · SATURATION ] ──► OUTPUT ──► LIMITER ──► MICRO VIRTUEL
                    (ordre libre : glisser les pastilles dans l'en-tête)                                 └──► casque (facultatif)
```

## Le micro virtuel

Windows ne permet pas à une application seule de créer un périphérique audio : il faut un pilote signé.
ONIRIQUE BROADCAST s'appuie donc sur un **câble audio virtuel**, détecté automatiquement :

- **VB-CABLE** (gratuit, https://vb-audio.com/Cable/) : recommandé, déjà installé sur ce PC ;
- VoiceMeeter, Virtual Audio Cable, Hi-Fi Cable... sont aussi reconnus.

Dans la carte AUDIO ROUTING, la sortie **VIRTUAL MICROPHONE** est réglée sur `CABLE Input`.
Dans Discord / OBS / le jeu, on choisit comme microphone **`CABLE Output (VB-Audio Virtual Cable)`**
(le nom exact est affiché dans l'application, bouton COPY).
**RENAME** renomme le micro virtuel tel qu'il apparaît dans les applications (ex. `OB` -> `OB (VB-Audio Virtual Cable)`),
sans droits administrateur, comme le panneau Son de Windows. SOUND ouvre ce panneau (micro par défaut, etc.).

Si aucun câble n'est installé, **INSTALL VB-CABLE (FREE)** fait tout : téléchargement du pack officiel (download.vb-audio.com),
vérification de la signature Authenticode de l'éditeur de VB-Audio (sinon rien n'est exécuté), installation silencieuse
avec l'autorisation administrateur de Windows, sélection automatique du câble, réglage en 48 kHz, proposition de redémarrage.

**Partage d'écran Discord** : la sortie vers le câble virtuel est ouverte en **mode exclusif** (EXCL dans AUDIO ENGINE) : elle
ne passe pas par le mélangeur de Windows, donc aucune capture du son du PC (partage d'écran avec son, son du bureau d'OBS)
ne peut la reprendre. Mesuré : capture du processus en mode partagé -38 dBFS (la voix repartait en écho), en exclusif
silence numérique. Diagnostic : ONIRIQUE_SHARED_OUTPUT=1 rétablit le mode partagé ;
`OniriqueBroadcastTests.exe --capture <pid> <s>` capture un processus comme le fait Discord.

**Discord** : le micro virtuel doit être en 48 kHz des deux côtés du câble (fréquence de Discord) ; sinon Windows
rééchantillonne la voix avant Discord, ce qui dégrade sa suppression d'écho et de bruit. L'application le vérifie
en permanence et propose **SET 48 KHZ** (comme Propriétés > Avancé > Format par défaut du panneau Son).

## Modules

| Module | Origine | Détail |
|---|---|---|
| ECHO GUARD | **nouveau** | en tête de chaîne : écoute ce que joue le PC (tout sauf l'application, Windows 10 2004+), mesure le retard et la fuite casque / haut-parleurs -> micro, et retient cette fuite (-30 dB) tant que personne ne parle. Discord ne reçoit plus d'écho déformé par la compression : sa suppression d'écho peut rester activée. Inactif si aucune fuite n'est mesurée |
| NOISE GATE | **nouveau** | ultra transparent : chaîne latérale filtrée (90 Hz – 7 kHz), anticipation 1.5 ms, hystérésis, HOLD, gain lissé en courbe en S, RANGE (atténuation naturelle ou mute), écran défilant + seuil à glisser, **LEARN** (mesure le bruit de la pièce et place le seuil) |
| PARAMETRIC EQ | VocalBox | 6 bandes « appariées à l'analogique », éditées dans l'analyseur temps réel (entrée / sortie / courbe exacte) |
| 1176 | VocalBox | modèle FET en contre-réaction, ratios 4/8/12/20/ALL, VU-mètre + **MIX** (compression parallèle, nouveau) |
| MULTIBAND | VocalBox | opto 4 bandes Linkwitz-Riley, écran interactif, presets internes, écoute par bande |
| DE-ESSER / PLOSIVES | VocalBox | de-esser dynamique (sans anticipation : latence nulle) + suppresseur de plosives |
| SATURATION | VocalBox | TUBE / ANALOG / TAPE, niveau compensé ; suréchantillonnage x4 passé en IIR polyphase (latence de quelques échantillons) |
| LIMITER | **nouveau** | anticipation 1 ms + écrêtage doux de sécurité : le micro virtuel ne sature jamais |

Chaque module a son bouton power (fondu sans clic) ; MUTE (silence propre pour les applications) et
BYPASS (micro brut) dans l'en-tête ; témoin **ON AIR**. Presets d'usine calibrés à ~ -16 dBFS RMS en sortie :
Streamer Clean, Podcast Warm, Radio Broadcast, Gaming Comms, Deep Voice, Natural Light (+ presets utilisateur,
`%APPDATA%\Onirique\Broadcast\Presets\*.obpreset`).

## Moteur audio

- Trois flux WASAPI indépendants : micro, micro virtuel, casque. Mode SHARED / LOW LAT / EXCLUSIVE, tampon AUTO ou fixe.
- Les horloges des périphériques ne sont jamais identiques : chaque sortie passe par un **pont à rééchantillonnage
  adaptatif** (sinc 32 points, régulation PI à amortissement critique sur un remplissage indépendant de la phase,
  marge anti-gigue adaptative). Latence constante, aucune dérive, THD+N ≈ -90 dB.
- Périphérique débranché / réveil de veille : réouverture automatique dès qu'il revient.
- Traitement mono 32 bits flottants, ~1.7 % d'un cœur CPU avec tous les modules actifs.
- Barre de titre intégrée au dégradé de l'application (- [] X, glisser pour déplacer, double-clic pour agrandir sans déformer).
- Fermer la fenêtre laisse le micro virtuel actif dans la zone de notification (MUTE / BYPASS / écoute / Quitter
  depuis l'icône) ; démarrage avec Windows en option ; une seule instance.
- Réglages : `%APPDATA%\Onirique\Broadcast.settings`.

## Compiler

Prérequis : Visual Studio Build Tools 2022 (C++), CMake. JUCE 8.0.6 est repris du dossier
`..\OniriqueVocalBox\build\_deps\juce-src` s'il existe, sinon téléchargé.

    build_windows.bat

Résultat : `build\OniriqueBroadcast_artefacts\Release\ONIRIQUE BROADCAST.exe`

Tests hors ligne du cœur audio (pont entre horloges avec dérive / gigue / fréquences différentes, chaîne complète,
niveaux des presets) :

    cmake --build build --config Release --target OniriqueBroadcastTests
    build\OniriqueBroadcastTests_artefacts\Release\OniriqueBroadcastTests.exe

## Structure

- `Source/Main.cpp` : application, fenêtre, zone de notification, sauvegarde des réglages
- `Source/MainComponent.*` : interface (en-tête, carte ROUTING, cartes des modules, animations 60 i/s)
- `Source/Engine.*` : chaîne de traitement, ordre des modules, fondus, mesures
- `Source/Audio/AudioRouter.*` : périphériques WASAPI, détection des câbles virtuels, chien de garde
- `Source/Audio/DriftResampler.h` : pont entre horloges (rééchantillonneur adaptatif)
- `Source/DSP/` : NoiseGate, Limiter (nouveaux) + modules du VocalBox
- `Source/UI/` : Look (DA, LookAndFeel), Widgets (boutons, sélecteurs, mètres...), Displays (analyseur, gate, multibande)
- `Source/WindowsPlatform.cpp` : fenêtre sans cadre (coins arrondis, ombre, déplacement natif), renommage du micro virtuel
- `Tests/Tests.cpp` : tests hors ligne
