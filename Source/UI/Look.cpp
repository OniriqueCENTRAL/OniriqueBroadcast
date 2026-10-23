#include "Look.h"
#include "OniriqueKnob.h"
#include <BinaryData.h>

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
}

//==============================================================================
namespace
{
// les polices de la charte, partagées par les fenêtres ouvertes (thread de l'interface uniquement)
struct Faces
{
    juce::Typeface::Ptr display, body, mono, monoBold;
};
Faces* faces = nullptr;
int faceUsers = 0;
} // namespace

ui::FontScope::FontScope()
{
    if (faceUsers++ == 0)
    {
        faces = new Faces();
        faces->display = juce::Typeface::createSystemTypefaceFor (BinaryData::Unbounded800_ttf, BinaryData::Unbounded800_ttfSize);
        faces->body = juce::Typeface::createSystemTypefaceFor (BinaryData::HankenGrotesk600_ttf, BinaryData::HankenGrotesk600_ttfSize);
        faces->mono = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMono500_ttf, BinaryData::JetBrainsMono500_ttfSize);
        faces->monoBold = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMono600_ttf, BinaryData::JetBrainsMono600_ttfSize);
    }
}

ui::FontScope::~FontScope()
{
    if (--faceUsers == 0) { delete faces; faces = nullptr; }
}

juce::Font ui::font (Face f, float h, bool bold, float kerning)
{
    h = std::max (4.0f, h);
    const juce::Typeface::Ptr tf = faces == nullptr ? nullptr
                                 : f == Face::display ? faces->display : f == Face::body ? faces->body : (bold ? faces->monoBold : faces->mono);
    // Unbounded est très large : un peu plus petit que la hauteur demandée, pour garder les mêmes encombrements
    juce::Font fnt = tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (f == Face::display ? h * 0.86f : h))
                                   : juce::Font (juce::FontOptions (f == Face::display ? "Bahnschrift" : f == Face::body ? "Segoe UI" : "Cascadia Mono",
                                                                    h, bold ? juce::Font::bold : juce::Font::plain));
    if (kerning != 0.0f) fnt.setExtraKerningFactor (kerning);
    return fnt;
}

float ui::editorScale (const juce::Component& c)
{
    if (auto* r = dynamic_cast<const ScaledRoot*> (&c)) return (float) r->getWidth() / kBaseW;
    if (auto* r = c.findParentComponentOfClass<ScaledRoot>()) return (float) r->getWidth() / kBaseW;
    return 1.0f;
}

juce::Component* ui::popupParent (juce::Component& c)
{
    return c.findParentComponentOfClass<ScaledRoot>();
}

float ui::textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, t, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

juce::Font ui::fitFont (Face face, float maxH, float maxW, const juce::String& text, bool bold, float kerning)
{
    auto f = font (face, maxH, bold, kerning);
    const float w = textWidth (f, text);
    if (w > maxW && w > 0.0f) f.setHeight (std::max (4.0f, f.getHeight() * maxW / w));
    return f;
}

void ui::strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity)
{
    if (intensity <= 0.002f) return;
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, intensity * 0.17f / (float) i)));
        g.strokePath (p, juce::PathStrokeType (width * (1.0f + 1.15f * (float) i), juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void ui::drawValuePill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float glow, float u)
{
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ember.withAlpha (0.14f * glow));
    g.fillRoundedRectangle (r, rad);
    g.setColour (line.interpolatedWith (ember.withAlpha (0.8f), glow));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, 1.2f * u));

    const float dot = r.getHeight() * 0.22f;
    const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + rad * 0.95f, r.getCentreY() });
    g.setColour (ember.withAlpha (0.25f + 0.35f * glow));
    g.fillEllipse (dotR.expanded (dot * 0.6f * glow));
    g.setColour (ember.interpolatedWith (bone, 0.35f * glow));
    g.fillEllipse (dotR);

    const auto textR = r.withTrimmedLeft (rad * 1.5f).withTrimmedRight (rad * 0.6f);
    g.setColour (mist);
    g.setFont (fitFont (Face::mono, r.getHeight() * 0.56f, textR.getWidth(), text));
    g.drawText (text, textR, juce::Justification::centred, false);
}

void ui::drawMenuArrow (juce::Graphics& g, juce::Point<float> c, float u, juce::Colour col)
{
    juce::Path tri;
    tri.addTriangle (c.x - 4.0f * u, c.y - 2.0f * u, c.x + 4.0f * u, c.y - 2.0f * u, c.x, c.y + 3.0f * u);
    g.setColour (col);
    g.fillPath (tri);
}

//==============================================================================
OniriqueLookAndFeel::OniriqueLookAndFeel()
{
    using namespace ui;
    setDefaultSansSerifTypefaceName ("Segoe UI");
    setColour (juce::TooltipWindow::backgroundColourId, deep2);
    setColour (juce::TooltipWindow::textColourId, mist);
    setColour (juce::TooltipWindow::outlineColourId, line);
    setColour (juce::PopupMenu::backgroundColourId, deep2.withAlpha (0.99f));   // non opaque : coins arrondis transparents
    setColour (juce::PopupMenu::textColourId, mist);
    setColour (juce::PopupMenu::headerTextColourId, haze);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ember.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, bone);
    setColour (juce::TextEditor::backgroundColourId, night.withAlpha (0.9f));
    setColour (juce::TextEditor::textColourId, mist);
    setColour (juce::TextEditor::outlineColourId, ember);
    setColour (juce::TextEditor::focusedOutlineColourId, ember);
    setColour (juce::TextEditor::highlightColourId, ember.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, bone);
    setColour (juce::AlertWindow::backgroundColourId, deep2);
    setColour (juce::AlertWindow::textColourId, mist);
    setColour (juce::AlertWindow::outlineColourId, line);
}

void OniriqueLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float a0, float a1, juce::Slider& s)
{
    using namespace ui;
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    if (D < 8.0f) return;

    const auto c = b.getCentre();
    const float R = D * 0.5f, u = D / 150.0f;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", pos);
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const bool bipolar = (bool) props.getWithDefault ("bipolar", false);

    // ---- le potard ONIRIQUE « RONCE », le même que dans les plugins (UI/OniriqueKnob.h)
    onirique::ui::drawKnob (g, juce::Rectangle<float> (D, D).withCentre (c), shown, a0, a1, ember, bipolar, glow > 0.3f);

    // ---- bornes de l'échelle
    const auto minL = props["minLabel"].toString(), maxL = props["maxLabel"].toString();
    if (minL.isNotEmpty() || maxL.isNotEmpty())
    {
        g.setColour (haze.withAlpha (0.8f));
        for (int side = 0; side < 2; ++side)
        {
            const auto& txt = side == 0 ? minL : maxL;
            const float es = editorScale (s);
            const auto f = fitFont (Face::mono, std::max (11.0f * u, 10.0f * es), std::max (0.34f * D, 30.0f * es), txt);
            const float tw = textWidth (f, txt) + 2.0f;
            const auto pt = onCircle (c, R * 0.9f, side == 0 ? a0 : a1);
            const float cx = juce::jlimit (b.getX() + tw * 0.5f, b.getRight() - tw * 0.5f, pt.x);
            g.setFont (f);
            g.drawText (txt, juce::Rectangle<float> (tw, f.getHeight() * 1.3f).withCentre ({ cx, pt.y + 0.17f * R }),
                        juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void OniriqueLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    using namespace ui;
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (deep2, 0.0f, 0.0f, deep, 0.0f, (float) height, false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setGradientFill (juce::ColourGradient (ember.withAlpha (0.10f), (float) width * 0.8f, 0.0f,
                                             ember.withAlpha (0.0f), (float) width * 0.8f, (float) height * 0.6f, true));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);
}

void OniriqueLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 50; h = 11; return; }
    w = juce::roundToInt (ui::textWidth (getPopupMenuFont(), text)) + 56;
    h = 30;
}

void OniriqueLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    using namespace ui;
    const auto r = area.toFloat().withTrimmedLeft (16.0f).withTrimmedTop (4.0f);
    const auto f = font (Face::mono, 10.5f, true, 0.18f);
    g.setFont (f);
    g.setColour (haze);
    g.drawText (name, r, juce::Justification::centredLeft, false);
    const float tw = textWidth (f, name);
    g.setColour (line);
    g.fillRect (juce::Rectangle<float> (r.getX() + tw + 10.0f, r.getCentreY(), r.getWidth() - tw - 26.0f, 1.0f));
}

void OniriqueLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                             bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                             const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    using namespace ui;
    if (isSeparator)
    {
        g.setColour (line);
        g.fillRect (juce::Rectangle<float> (14.0f, (float) area.getCentreY(), (float) area.getWidth() - 28.0f, 1.0f));
        return;
    }

    const auto r = area.toFloat().reduced (5.0f, 2.0f);
    if (isHighlighted && isActive)
    {
        juce::Path p;
        p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        strokeGlow (g, p, ember, 1.0f, 0.45f);
        g.setGradientFill (juce::ColourGradient (ember.withAlpha (0.38f), r.getX(), r.getY(),
                                                 blood.withAlpha (0.35f), r.getRight(), r.getBottom(), false));
        g.fillPath (p);
        g.setColour (ember.withAlpha (0.7f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // élément actif : point braise lumineux
    const juce::Point<float> dot { r.getX() + 14.0f, r.getCentreY() };
    if (isTicked)
    {
        g.setColour (ember.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
        g.setColour (ember.interpolatedWith (bone, 0.25f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }

    g.setColour (! isActive ? haze.withAlpha (0.45f) : (isHighlighted || isTicked ? bone : mist.withAlpha (0.88f)));
    g.setFont (getPopupMenuFont().withStyle (isTicked ? juce::Font::bold : juce::Font::plain));
    g.drawText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft, true);
}

//==============================================================================
int OniriqueLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    if (s.getSliderStyle() == juce::Slider::LinearVertical)
        return juce::roundToInt (s.getHeight() / 150.0f * 9.0f);
    if (s.getSliderStyle() == juce::Slider::LinearHorizontal)
        return juce::roundToInt (s.getHeight() * 0.32f);
    return LookAndFeel_V4::getSliderThumbRadius (s);
}

void OniriqueLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    using namespace ui;
    if (style == juce::Slider::LinearHorizontal)
    {
        // curseur horizontal : rail fin, remplissage braise lumineux depuis la gauche (ou le centre), pastille claire
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float u = b.getHeight() / 30.0f, cy = b.getCentreY();
        const float r = (float) getSliderThumbRadius (s);
        const float left = b.getX() + r, right = b.getRight() - r;
        const float shown = (float) s.getProperties().getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
        const float glow = (float) s.getProperties().getWithDefault ("glow", 0.0f);
        const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
        const float tx = left + shown * (right - left);
        const float from = bipolar ? 0.5f * (left + right) : left;
        const float trackH = std::max (3.0f, 4.0f * u);

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - trackH * 0.5f, right - left, trackH), trackH * 0.5f);
        if (std::abs (tx - from) > 0.5f)
        {
            juce::Path fill;
            fill.startNewSubPath (from, cy);
            fill.lineTo (tx, cy);
            strokeGlow (g, fill, ember, trackH, 0.5f + 0.5f * glow);
            g.setGradientFill (juce::ColourGradient (ember, from, cy, bone, tx, cy, false));
            g.strokePath (fill, juce::PathStrokeType (trackH, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto thumb = juce::Rectangle<float> (1.3f * r, 2.0f * r).withCentre ({ tx, cy });
        g.setColour (ember.withAlpha (0.22f + 0.25f * glow));
        g.fillRoundedRectangle (thumb.expanded (3.0f * u), thumb.getWidth() * 0.5f + 3.0f * u);
        g.setGradientFill (juce::ColourGradient (mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb), thumb.getX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, thumb.getWidth() * 0.5f);
        g.setColour (ember);
        g.drawLine (tx, thumb.getY() + 4.0f * u, tx, thumb.getBottom() - 4.0f * u, std::max (1.2f, 1.8f * u));
        return;
    }

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float u = b.getHeight() / 150.0f, cx = b.getCentreX();
    const float thumbR = (float) getSliderThumbRadius (s);
    const float top = b.getY() + thumbR, bottom = b.getBottom() - thumbR;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const float ty = bottom - shown * (bottom - top);

    for (int i = 0; i <= 10; ++i)
    {
        const float ti = (float) i / 10.0f;
        const float yy = bottom - ti * (bottom - top);
        const float d = (i % 5 == 0 ? 3.4f : 2.2f) * u;
        g.setColour (ti <= shown + 1.0e-3f ? ember.withAlpha (0.75f) : juce::Colours::white.withAlpha (0.16f));
        for (float dx : { -13.0f * u, 13.0f * u })
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ cx + dx, yy }));
    }

    const float trackW = std::max (3.0f, 5.0f * u);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - trackW * 0.5f, top, trackW, bottom - top), trackW * 0.5f);
    if (bottom - ty > 0.5f)
    {
        juce::Path fill;
        fill.startNewSubPath (cx, bottom);
        fill.lineTo (cx, ty);
        strokeGlow (g, fill, ember, trackW, 0.5f + 0.5f * glow);
        g.setGradientFill (juce::ColourGradient (ember, cx, bottom, bone, cx, ty, false));
        g.strokePath (fill, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float tw = std::min (b.getWidth() * 0.56f, 34.0f * u), th = 1.5f * thumbR;
    const auto thumb = juce::Rectangle<float> (tw, th).withCentre ({ cx, ty });
    g.setColour (ember.withAlpha (0.22f + 0.25f * glow));
    g.fillRoundedRectangle (thumb.expanded (4.0f * u), th * 0.5f + 4.0f * u);
    juce::Path tp;
    tp.addRoundedRectangle (thumb, th * 0.5f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), juce::roundToInt (8.0f * u), { 0, juce::roundToInt (3.0f * u) })
        .drawForPath (g, tp);
    g.setGradientFill (juce::ColourGradient (mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb),
                                             thumb.getX(), thumb.getBottom(), false));
    g.fillPath (tp);
    g.setColour (ember);
    g.drawLine (thumb.getX() + 7.0f * u, ty, thumb.getRight() - 7.0f * u, ty, std::max (1.5f, 2.4f * u));
}

// coin de redimensionnement de la fenêtre : trois points discrets en diagonale, braise au survol
void OniriqueLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    const float s = (float) juce::jmin (w, h), d = std::max (2.0f, s * 0.14f);
    g.setColour ((over || dragging) ? onirique::ui::ember : onirique::ui::haze.withAlpha (0.45f));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j <= i; ++j)
            g.fillEllipse ((float) w - s * 0.22f * (float) (i + 1) - d * 0.5f + s * 0.22f * (float) j,
                           (float) h - s * 0.22f * (float) (j + 1) - d * 0.5f, d, d);
}
