#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace onirique::ui
{
// ---- VOCAL MINI PRO : le noir et rouge d'ONIRIQUE, en plus moderne : noir profond, charbon neutre, rouge sombre pour la
// profondeur ; le rouge vif est l'accent, utilisé avec mesure (arcs, témoins, choix actif) ; rose pâle pour les reflets ;
// blanc cassé pour les textes, gris doux pour le secondaire. Pas de néon : des lueurs discrètes, du verre sombre.
// (Les noms historiques sont gardés : « ember » est l'accent, « lilac » le reflet clair.)
inline const juce::Colour night  { 0xff09080A };   // noir profond (fond)
inline const juce::Colour blood  { 0xff3D0910 };   // rouge sombre (dégradés, profondeur)
inline const juce::Colour deep   { 0xff121012 };   // charbon très sombre (panneaux)
inline const juce::Colour deep2  { 0xff1C1517 };   // charbon chaud (menus, info-bulles, panneaux ouverts)
inline const juce::Colour mist   { 0xffF2ECEC };   // blanc cassé (textes)
inline const juce::Colour haze   { 0xff978C8F };   // gris doux (informations secondaires)
inline const juce::Colour ember  { 0xffE5384A };   // rouge ONIRIQUE (accent)
inline const juce::Colour lilac  { 0xffFFB3BC };   // rose pâle (queues de reverb, reflets, extrémités d'arcs)
inline const juce::Colour bone   { 0xffFFF4F5 };   // blanc le plus clair (aiguilles, crêtes)
inline const juce::Colour mint   { 0xff7DE3B6 };   // témoin discret « ça agit » (comme le voyant d'un rack)
inline const juce::Colour live   { 0xff3BD98A };   // vert "en ligne" (statut du micro virtuel)
inline const juce::Colour line   { (juce::uint8) 255, (juce::uint8) 214, (juce::uint8) 218, 0.10f };

// largeur de référence de la fenêtre (toutes les dimensions sont exprimées pour cette largeur)
constexpr float kBaseW = 1640.0f, kBaseH = 950.0f;

// Polices : display (titres), body (libellés), mono (valeurs) : Unbounded, Hanken Grotesk et JetBrains Mono, les polices
// du site ONIRIQUE (dans les ressources de l'application). Chargées par FontScope (une par fenêtre ouverte : rien de
// statique qui survive à l'application) ; sans fenêtre ouverte, des équivalents système.
enum class Face { display, body, mono };
juce::Font font (Face, float height, bool bold = false, float kerning = 0.0f);
struct FontScope
{
    FontScope();
    ~FontScope();
    JUCE_DECLARE_NON_COPYABLE (FontScope)
};
float textWidth (const juce::Font&, const juce::String&);
// police de hauteur maxH, réduite si nécessaire pour que le texte tienne dans maxW (jamais coupé)
juce::Font fitFont (Face, float maxH, float maxW, const juce::String&, bool bold = false, float kerning = 0.0f);

// Composant racine de l'interface : sa largeur donne l'échelle, il accueille les menus déroulants.
struct ScaledRoot : public juce::Component {};

// échelle de la fenêtre (1 = kBaseW px de large) : PLANCHER des tailles de texte (libellés toujours lisibles)
float editorScale (const juce::Component&);
juce::Component* popupParent (juce::Component&);

// trait lumineux : plusieurs passes de plus en plus larges et transparentes sous le trait net
void strokeGlow (juce::Graphics&, const juce::Path&, juce::Colour, float width, float intensity);

// pastille de valeur (boutons, curseurs, champs) : fond sombre, liseré, point braise
void drawValuePill (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, float glow, float u);

// petit triangle "menu"
void drawMenuArrow (juce::Graphics&, juce::Point<float> centre, float u, juce::Colour);

// approche exponentielle d'une valeur animée ; renvoie true tant qu'elle bouge
inline bool approach (float& v, float target, float k) noexcept
{
    if (std::abs (target - v) < 1.0e-3f) { const bool moved = v != target; v = target; return moved; }
    v += k * (target - v);
    return true;
}

inline juce::Point<float> onCircle (juce::Point<float> c, float r, float ang)
{
    return { c.x + std::sin (ang) * r, c.y - std::cos (ang) * r };
}
} // namespace onirique::ui

// Tout ce qui s'anime reçoit tick() à 60 Hz depuis la fenêtre principale (une seule horloge).
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick() = 0;
};

class OniriqueLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OniriqueLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Font getPopupMenuFont() override { return onirique::ui::font (onirique::ui::Face::body, 15.0f); }

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 7; }
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};
