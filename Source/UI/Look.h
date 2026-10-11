#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace onirique::ui
{
// ---- DA ONIRIQUE (reprise du VocalBox) : nuit / sang, accent braise, lignes fines, lueurs
inline const juce::Colour night  { 0xff0D0809 };
inline const juce::Colour blood  { 0xff5C0008 };
inline const juce::Colour deep   { 0xff1A080A };
inline const juce::Colour deep2  { 0xff2A0A0E };
inline const juce::Colour mist   { 0xffF6EEEE };
inline const juce::Colour haze   { 0xffA88F92 };
inline const juce::Colour ember  { 0xffE23444 };
inline const juce::Colour bone   { 0xffFFECEE };
inline const juce::Colour live   { 0xff3BD98A };   // vert "en ligne" (statut du micro virtuel)
inline const juce::Colour line   { (juce::uint8) 255, (juce::uint8) 210, (juce::uint8) 214, 0.13f };

// largeur de référence de la fenêtre (toutes les dimensions sont exprimées pour cette largeur)
constexpr float kBaseW = 1640.0f, kBaseH = 950.0f;

// Polices : display (titres), body (libellés), mono (valeurs), équivalents système des polices du site.
enum class Face { display, body, mono };
juce::Font font (Face, float height, bool bold = false, float kerning = 0.0f);
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
