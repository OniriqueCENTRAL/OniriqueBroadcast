#include "Widgets.h"

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
}

//==============================================================================
KnobComponent::KnobComponent (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                              const juce::String& t, const juce::String& minLabel, const juce::String& maxLabel,
                              Layout l, const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar)
    : title (t), layout (l)
{
    slider.setSliderStyle (l == Layout::fader ? juce::Slider::LinearVertical : l == Layout::horizontal ? juce::Slider::LinearHorizontal
                                              : juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (220);
    slider.setLookAndFeel (&lnf);
    slider.setTooltip (tooltip);
    auto& props = slider.getProperties();
    props.set ("minLabel", minLabel);
    props.set ("maxLabel", maxLabel);
    props.set ("bipolar", bipolar);
    props.set ("shown", 0.0f);     // à l'ouverture, les arcs se déploient jusqu'à leur valeur
    props.set ("glow", 0.0f);

    if (auto* param = apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    slider.onDragStart = [this] { setFocus (hovering, true); };
    slider.onDragEnd   = [this] { setFocus (hovering, false); };
    slider.onValueChange = [this] { repaint (valueArea.getSmallestIntegerContainer().expanded (2)); };
    slider.addMouseListener (this, false);

    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);
}

KnobComponent::~KnobComponent()
{
    slider.removeMouseListener (this);
    slider.setLookAndFeel (nullptr);
}

void KnobComponent::setFocus (bool hover, bool drag)
{
    const bool was = hovering || dragging;
    hovering = hover;
    dragging = drag;
    if ((hovering || dragging) != was && onFocusChange) onFocusChange (hovering || dragging);
}

void KnobComponent::mouseEnter (const juce::MouseEvent&) { setFocus (true, dragging); }
void KnobComponent::mouseExit (const juce::MouseEvent&)  { setFocus (false, dragging); }

void KnobComponent::tick()
{
    const float target = (float) slider.valueToProportionOfLength (slider.getValue());
    bool changed = ui::approach (shown, target, 0.30f);
    if (activity) changed |= ui::approach (act, activity() > 0.5f ? 1.0f : 0.0f, 0.3f);
    changed |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f);
    if (changed)
    {
        slider.getProperties().set ("shown", shown);
        slider.getProperties().set ("glow", glow);
        slider.repaint();
        repaint (nameArea.getUnion (valueArea).getSmallestIntegerContainer().expanded (2));
    }
}

void KnobComponent::resized()
{
    const auto area = getLocalBounds().toFloat();
    juce::Rectangle<float> knob;

    if (layout == Layout::horizontal)
    {
        // une seule ligne : nom (34 %) | curseur | pastille de valeur (26 %)
        const float h = area.getHeight();
        unit = h / 30.0f;
        nameArea = area.withWidth (area.getWidth() * 0.34f);
        valueArea = area.withLeft (area.getRight() - area.getWidth() * 0.26f).reduced (0.0f, h * 0.08f);
        knob = area.withLeft (nameArea.getRight() + 4.0f * unit).withRight (valueArea.getX() - 6.0f * unit);
    }
    else
    {
        // nom + bouton ou curseur (D) + pastille de valeur. Le nom et la pastille ont une taille MINIMALE liée à
        // la fenêtre : sur les petits boutons les textes restent lisibles, c'est le bouton qui rétrécit.
        const bool fader = (layout == Layout::fader);
        const float es = ui::editorScale (*this);
        float D = std::max (10.0f, juce::jmin (maxD, fader ? area.getHeight() / 1.47f
                                                           : juce::jmin (area.getWidth() - 2.0f, area.getHeight() / 1.47f)));
        const float nameH = std::max (0.15f * D, 16.0f * es), pillH = std::max (0.21f * D, 22.0f * es);
        D = std::max (10.0f, std::min (D, (area.getHeight() - nameH - pillH) / 1.11f));
        unit = D / 150.0f;
        const float total = nameH + 0.05f * D + D + 0.06f * D + pillH;
        const float top = area.getY() + (area.getHeight() - total) * 0.5f;
        nameArea = { area.getX(), top, area.getWidth(), nameH };
        const float knobTop = top + nameH + 0.05f * D;
        knob = fader ? juce::Rectangle<float> (area.getX(), knobTop, area.getWidth(), D)
                     : juce::Rectangle<float> (D, D).withCentre ({ area.getCentreX(), knobTop + D * 0.5f });
        const float pillW = juce::jmin (area.getWidth() * (fader ? 0.98f : 0.94f), std::max (D * 0.95f, 88.0f * es));
        valueArea = juce::Rectangle<float> (pillW, pillH).withCentre ({ area.getCentreX(), knob.getBottom() + 0.06f * D + pillH * 0.5f });
    }

    slider.setBounds (knob.toNearestInt());
}

void KnobComponent::paint (juce::Graphics& g)
{
    const float u = unit;
    auto nameR = nameArea;
    if (activity)
    {
        const float d = nameR.getHeight() * 0.36f;
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ nameR.getX() + d, nameR.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.45f * act));
        g.fillEllipse (led.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, act));
        g.fillEllipse (led);
        nameR.removeFromLeft (d * 2.4f);
    }
    g.setColour (ui::haze.interpolatedWith (ui::mist, glow));
    g.setFont (ui::fitFont (ui::Face::body, layout == Layout::horizontal ? nameArea.getHeight() * 0.46f
                                          : std::max ({ 8.0f, 15.0f * u, nameArea.getHeight() * 0.72f }),
                            nameR.getWidth() - 2.0f, title, true, 0.14f));
    g.drawText (title, nameR, layout == Layout::horizontal ? juce::Justification::centredLeft : juce::Justification::centred, false);

    ui::drawValuePill (g, valueArea, slider.getTextFromValue (slider.getValue()), glow, u);
}

//==============================================================================
DragValue::DragValue (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& tooltip)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    param = apvts.getParameter (paramId);
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }
}

void DragValue::tick()
{
    if (ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f)) repaint();
}

void DragValue::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (! dragging && onFocusChange) onFocusChange (true);
}

void DragValue::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDown (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = true;
    startNorm = param->getValue();
    attachment->beginGesture();
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    if (param == nullptr) return;
    const float range = e.mods.isShiftDown() ? 900.0f : 220.0f;   // Maj = réglage fin
    const float norm = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / range);
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (norm));
}

void DragValue::mouseUp (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = false;
    attachment->endGesture();
    if (! hovering && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr) attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void DragValue::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float norm = juce::jlimit (0.0f, 1.0f, param->getValue() + w.deltaY * 0.06f);
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (norm));
}

void DragValue::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    ui::drawValuePill (g, r, prefix + param->getCurrentValueAsText(), glow, r.getHeight() / 30.0f);
}

//==============================================================================
void PowerButton::tick()
{
    if (ui::approach (on, getToggleState() ? 1.0f : 0.0f, 0.2f)) repaint();
}

void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getWidth(), b.getHeight()) * 0.72f;
    if (d < 6.0f) return;
    const auto c = b.getCentre();
    const auto disk = juce::Rectangle<float> (d, d).withCentre (c);
    const float u = d / 24.0f;

    juce::Path ring;
    ring.addEllipse (disk);
    ui::strokeGlow (g, ring, ui::ember, 1.4f * u, on * (highlighted ? 1.0f : 0.7f));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (disk);
    g.setColour (ui::ember.withAlpha (0.16f * on));
    g.fillEllipse (disk);
    g.setColour (ui::line.interpolatedWith (ui::ember, on).withMultipliedAlpha (highlighted ? 1.0f : 0.9f));
    g.drawEllipse (disk, std::max (1.0f, 1.4f * u));

    // pictogramme power : arc ouvert en haut + barre verticale
    const float r = 0.26f * d, stroke = std::max (1.3f, 0.09f * d);
    juce::Path p;
    p.addCentredArc (c.x, c.y + 0.02f * d, r, r, 0.0f, 0.23f * kPi, 1.77f * kPi, true);
    p.startNewSubPath (c.x, c.y - 0.32f * d);
    p.lineTo (c.x, c.y - 0.04f * d);
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.5f : 0.0f)));
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
void PillToggle::tick()
{
    if (ui::approach (on, getToggleState() ? 1.0f : 0.0f, 0.16f)) repaint();
}

void PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const bool big = (style == Style::big);
    const float u = b.getHeight() / 36.0f;
    const auto r = b.reduced (style == Style::mini ? 2.0f * u : (big ? 3.0f * u : 6.0f * u));
    const float radius = r.getHeight() * 0.5f;

    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    ui::strokeGlow (g, shape, ui::ember, 1.5f * u, on * 0.8f);
    g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.28f : 0.36f));
    g.fillPath (shape);
    if (on > 0.001f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha ((big ? 0.55f : 0.30f) * on), r.getX(), r.getY(),
                                                 ui::blood.withAlpha ((big ? 0.5f : 0.25f) * on), r.getRight(), r.getBottom(), false));
        g.fillPath (shape);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, on));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.4f * u)));

    const float d = r.getHeight() * 0.26f;
    const float pad = r.getHeight() * 0.40f;
    const auto ledR = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + pad + d * 0.5f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.35f * on));
    g.fillEllipse (ledR.expanded (d * 0.8f));
    g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (big ? ui::bone : ui::ember, on));
    g.fillEllipse (ledR);

    const auto textR = r.withTrimmedLeft (pad * 2.0f + d).withTrimmedRight (pad * 0.8f);
    const juce::String txt = big && on > 0.5f && caption.isNotEmpty() ? caption : getButtonText();
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.6f : 0.0f)));
    g.setFont (ui::fitFont (big ? ui::Face::display : ui::Face::body, r.getHeight() * (style == Style::small ? 0.40f : 0.50f),
                            textR.getWidth(), txt, true, 0.10f));
    g.drawText (txt, textR, juce::Justification::centred, false);
}

//==============================================================================
SegmentSelector::SegmentSelector (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                                  const juce::StringArray& l, const juce::StringArray& t)
    : labels (l), tips (t)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    if (auto* param = apvts.getParameter (paramId))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            const int idx = juce::jlimit (0, labels.size() - 1, juce::roundToInt (v));
            if (idx != selected) { selected = idx; repaint(); }
        });
        attachment->sendInitialUpdate();
    }
}

SegmentSelector::SegmentSelector (const juce::StringArray& l, const juce::StringArray& t) : labels (l), tips (t)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> SegmentSelector::segmentArea() const noexcept
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (title.isNotEmpty())
    {
        const auto f = ui::font (ui::Face::mono, r.getHeight() * 0.40f, true, 0.08f);
        r.removeFromLeft (ui::textWidth (f, title) + r.getHeight() * 0.9f);
    }
    return r;
}

int SegmentSelector::segmentAt (float x) const noexcept
{
    const auto a = segmentArea();
    if (labels.isEmpty() || a.getWidth() <= 0.0f || x < a.getX()) return -1;
    return juce::jlimit (0, labels.size() - 1, (int) ((x - a.getX()) * (float) labels.size() / a.getWidth()));
}

void SegmentSelector::tick()
{
    bool changed = false;
    if (slide < 0.0f) { slide = (float) selected; changed = true; }
    changed |= ui::approach (slide, (float) selected, 0.22f);
    if (changed) repaint();
}

void SegmentSelector::mouseDown (const juce::MouseEvent& e)
{
    const int idx = segmentAt (e.position.x);
    if (idx < 0 || idx == selected) return;
    if (attachment != nullptr) attachment->setValueAsCompleteGesture ((float) idx);
    else
    {
        selected = idx;
        repaint();
        if (onChange) onChange (idx);
    }
}

void SegmentSelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = segmentAt (e.position.x);
    if (h != hover) { hover = h; repaint(); }
}

void SegmentSelector::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

juce::String SegmentSelector::getTooltip()
{
    const int idx = segmentAt ((float) getMouseXYRelative().x);
    return juce::isPositiveAndBelow (idx, tips.size()) ? tips[idx] : titleTip;
}

void SegmentSelector::paint (juce::Graphics& g)
{
    const int n = labels.size();
    if (n == 0) return;

    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 26.0f;
    const float radius = r.getHeight() * 0.5f;
    const auto a = segmentArea();
    const float segW = a.getWidth() / (float) n;
    const float pos = slide < 0.0f ? (float) selected : slide;

    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, std::max (1.0f, 1.1f * u));

    if (title.isNotEmpty())
    {
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::mono, r.getHeight() * 0.40f, true, 0.08f));
        g.drawText (title, juce::Rectangle<float> (r.getX(), r.getY(), a.getX() - r.getX(), r.getHeight()), juce::Justification::centred, false);
    }

    // pastille braise qui glisse vers le choix
    auto pill = juce::Rectangle<float> (a.getX() + segW * pos, a.getY(), segW, a.getHeight()).reduced (2.5f * u);
    if (pill.getWidth() < pill.getHeight())
        pill = pill.withSizeKeepingCentre (pill.getWidth(), pill.getWidth());
    juce::Path pp;
    pp.addRoundedRectangle (pill, pill.getHeight() * 0.5f);
    ui::strokeGlow (g, pp, ui::ember, 1.0f * u, 0.6f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffEE4555), pill.getX(), pill.getY(),
                                             juce::Colour (0xffB81F30), pill.getX(), pill.getBottom(), false));
    g.fillPath (pp);

    const bool round = pill.getWidth() <= pill.getHeight() + 0.5f && segW < a.getHeight();
    const float fitW = round ? pill.getWidth() * 0.74f : segW - 6.0f * u;
    auto labelFont = ui::font (ui::Face::body, r.getHeight() * 0.50f, true, 0.06f);
    for (const auto& l : labels)
    {
        const float tw = ui::textWidth (labelFont, l);
        if (tw > fitW && tw > 0.0f) labelFont.setHeight (std::max (4.0f, labelFont.getHeight() * fitW / tw));
    }

    for (int i = 0; i < n; ++i)
    {
        const juce::Rectangle<float> seg (a.getX() + segW * (float) i, a.getY(), segW, a.getHeight());
        const float sel = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (pos - (float) i));
        const auto base = i == hover ? ui::mist : ui::haze;
        g.setColour (base.interpolatedWith (ui::night, sel));
        g.setFont (labelFont);
        g.drawText (labels[i], seg, juce::Justification::centred, false);
    }
}

//==============================================================================
void VuMeter::tick()
{
    const float target = active ? juce::jlimit (0.0f, 30.0f, read()) : 0.0f;
    bool changed = ui::approach (needle, target, 0.16f);
    changed |= ui::approach (shownDb, target, 0.10f);
    if (changed) repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 100.0f;
    juce::Path face;
    face.addRoundedRectangle (r, 10.0f * u);

    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, r.getY(), ui::night, 0.0f, r.getBottom(), false));
    g.fillPath (face);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.13f), r.getCentreX(), r.getBottom(),
                                             ui::ember.withAlpha (0.0f), r.getCentreX(), r.getY(), true));
    g.fillPath (face);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (face);

        const juce::Point<float> pivot { r.getCentreX(), r.getBottom() + r.getHeight() * 0.22f };
        const float R = r.getHeight() * 1.02f;
        auto angleFor = [] (float gr) { return 0.66f - 1.32f * std::pow (juce::jlimit (0.0f, 1.0f, gr / 20.0f), 0.6f); };
        auto at = [&] (float ang, float rad) { return juce::Point<float> (pivot.x + std::sin (ang) * rad, pivot.y - std::cos (ang) * rad); };

        juce::Path arc, red;
        arc.addCentredArc (pivot.x, pivot.y, R * 0.90f, R * 0.90f, 0.0f, angleFor (20.0f), angleFor (0.0f), true);
        red.addCentredArc (pivot.x, pivot.y, R * 0.90f, R * 0.90f, 0.0f, angleFor (20.0f), angleFor (10.0f), true);
        g.setColour (ui::haze.withAlpha (0.45f));
        g.strokePath (arc, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
        g.setColour (ui::ember.withAlpha (0.8f));
        g.strokePath (red, juce::PathStrokeType (std::max (1.5f, 2.6f * u)));

        g.setFont (ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * ui::editorScale (*this))));
        for (float db : { 0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 10.0f, 15.0f, 20.0f })
        {
            const bool major = db == 0.0f || db == 3.0f || db == 5.0f || db == 10.0f || db == 20.0f;
            const float a = angleFor (db);
            g.setColour (db >= 10.0f ? ui::ember : ui::haze.withAlpha (0.8f));
            g.drawLine ({ at (a, R * 0.90f), at (a, R * (major ? 0.99f : 0.95f)) }, std::max (1.0f, (major ? 1.6f : 1.1f) * u));
            if (major)
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (24.0f * u, 12.0f * u).withCentre (at (a, R * 0.80f)),
                            juce::Justification::centred, false);
        }

        juce::Path nd;
        nd.startNewSubPath (pivot);
        nd.lineTo (at (angleFor (needle), R * 0.98f));
        ui::strokeGlow (g, nd, ui::ember, 1.4f * u, active ? 0.6f : 0.0f);
        g.setColour (active ? ui::bone : ui::haze.withAlpha (0.5f));
        g.strokePath (nd, juce::PathStrokeType (std::max (1.2f, 1.8f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const juce::String txt = "GR " + (shownDb < 0.05f ? juce::String ("0.0") : "-" + juce::String (shownDb, 1));
    g.setColour (ui::mist.withAlpha (active ? 0.9f : 0.4f));
    g.setFont (ui::fitFont (ui::Face::mono, 12.0f * u, r.getWidth() * 0.8f, txt, true));
    g.drawText (txt, r.withTrimmedTop (r.getHeight() * 0.62f).withTrimmedBottom (6.0f * u), juce::Justification::centredBottom, false);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, r.getCentreY(), false));
    g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.5f), 10.0f * u);
    g.setColour (ui::line);
    g.strokePath (face, juce::PathStrokeType (std::max (1.0f, 1.1f * u)));
}

//==============================================================================
void LevelMeter::tick()
{
    const float pk = consume ? consume() : 0.0f;
    const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
    levelDb = db > levelDb ? db : std::max (-100.0f, levelDb - 0.33f);   // retombée ~20 dB/s
    if (db >= holdDb) { holdDb = db; holdFrames = 90; }                   // maintien 1.5 s
    else if (--holdFrames <= 0) holdDb = std::max (levelDb, holdDb - 0.5f);
    if (db >= maxDb) { maxDb = db; maxFrames = 180; }
    else if (--maxFrames <= 0) maxDb = holdDb;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getWidth() / 46.0f;
    auto r = area;
    const auto readout = r.removeFromBottom (18.0f * u);
    r.removeFromBottom (4.0f * u);
    const auto scale = r.removeFromRight (18.0f * u);
    const auto bar = r.reduced (2.0f * u, 0.0f);
    auto yOf = [&] (float db) { return bar.getBottom() - norm (db) * bar.getHeight(); };

    g.setFont (ui::font (ui::Face::mono, std::max (8.0f * u, 9.0f * ui::editorScale (*this))));
    for (int db : { 0, -6, -12, -24, -48 })
    {
        const float y = yOf ((float) db);
        g.setColour (db == 0 ? ui::ember.withAlpha (0.8f) : ui::haze.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (scale.getX() + 1.0f * u, y, 3.0f * u, 1.0f));
        if (db != -6)
            g.drawText (juce::String (db), juce::Rectangle<float> (scale.getX() + 4.0f * u, y - 6.0f * u, scale.getWidth() - 4.0f * u, 12.0f * u),
                        juce::Justification::centredLeft, false);
    }

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bar, 3.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (bar.reduced (0.5f), 3.0f * u, 1.0f);

    const float top = yOf (levelDb);
    if (top < bar.getBottom() - 0.5f)
    {
        juce::ColourGradient cg (ui::bone.withAlpha (0.30f), 0.0f, bar.getBottom(), juce::Colour (0xffff2a3a), 0.0f, yOf (2.0f), false);
        cg.addColour ((double) norm (-12.0f), ui::bone.withAlpha (0.85f));
        cg.addColour ((double) norm (-3.0f), ui::ember);
        g.setGradientFill (cg);
        g.fillRoundedRectangle (bar.withTop (top).reduced (1.5f * u, 1.0f), 2.0f * u);
    }
    if (holdDb > -60.0f)
    {
        g.setColour (holdDb > 0.0f ? juce::Colour (0xffff2a3a) : ui::bone);
        g.fillRect (juce::Rectangle<float> (bar.getX() + 1.5f * u, yOf (holdDb) - 1.0f * u, bar.getWidth() - 3.0f * u, 2.0f * u));
    }

    const juce::String txt = maxDb <= -99.0f ? juce::String ("-inf") : (maxDb > 0.0f ? "+" : "") + juce::String (maxDb, 1);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (readout, readout.getHeight() * 0.5f);
    g.setColour (maxDb > 0.0f ? juce::Colour (0xffff2a3a) : ui::line);
    g.drawRoundedRectangle (readout.reduced (0.5f), readout.getHeight() * 0.5f, 1.0f);
    g.setColour (maxDb > 0.0f ? ui::bone : ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, readout.getHeight() * 0.62f, readout.getWidth() - 6.0f * u, txt, true));
    g.drawText (txt, readout, juce::Justification::centred, false);
}

//==============================================================================
void SignalBar::tick()
{
    const float pk = consume ? consume() : 0.0f;
    const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
    levelDb = db > levelDb ? db : std::max (-100.0f, levelDb - 0.4f);
    if (db >= holdDb) { holdDb = db; holdFrames = 80; }
    else if (--holdFrames <= 0) holdDb = std::max (levelDb, holdDb - 0.6f);
    repaint();
}

void SignalBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 12.0f;
    auto xOf = [&] (float db) { return r.getX() + juce::jlimit (0.0f, 1.0f, (db + 70.0f) / 70.0f) * r.getWidth(); };

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r, h * 0.5f);

    // graduations discrètes tous les 10 dB
    for (int db = -60; db <= 0; db += 10)
    {
        g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.2f : 0.07f));
        g.fillRect (juce::Rectangle<float> (xOf ((float) db), r.getY() + 2.0f * u, 1.0f, h - 4.0f * u));
    }

    const float x = xOf (levelDb);
    if (x > r.getX() + 1.0f)
    {
        juce::ColourGradient cg (ui::bone.withAlpha (0.35f), r.getX(), 0.0f, juce::Colour (0xffff2a3a), r.getRight(), 0.0f, false);
        cg.addColour (0.70, ui::bone.withAlpha (0.9f));
        cg.addColour (0.92, ui::ember);
        g.setGradientFill (cg);
        g.fillRoundedRectangle (r.withRight (x).reduced (0.0f, 1.5f * u), (h - 3.0f * u) * 0.5f);
    }
    if (holdDb > -70.0f)
    {
        g.setColour (holdDb > -1.0f ? juce::Colour (0xffff2a3a) : ui::bone);
        g.fillRect (juce::Rectangle<float> (xOf (holdDb) - 1.0f, r.getY() + 1.0f, 2.0f, h - 2.0f));
    }

    // repère du seuil du gate
    if (markerDb)
    {
        const float m = markerDb();
        if (! std::isnan (m))
        {
            const float mx = xOf (m);
            juce::Path tri;
            tri.addTriangle (mx - 4.0f * u, r.getY() - 1.0f, mx + 4.0f * u, r.getY() - 1.0f, mx, r.getY() + 4.0f * u);
            g.setColour (ui::ember);
            g.fillPath (tri);
            g.fillRect (juce::Rectangle<float> (mx - 0.75f, r.getY(), 1.5f, h));
        }
    }
    g.setColour (ui::line);
    g.drawRoundedRectangle (r, h * 0.5f, 1.0f);
}

//==============================================================================
void GainReductionBar::tick()
{
    const float target = active && read ? juce::jlimit (0.0f, range + 6.0f, read()) : 0.0f;
    const float before = gr, beforeHold = hold;
    gr = target > gr ? target : gr + 0.12f * (target - gr);
    if (gr >= hold) { hold = gr; holdFrames = 120; }
    else if (--holdFrames <= 0) hold = std::max (gr, hold - 0.1f);
    if (std::abs (gr - before) > 1.0e-3f || std::abs (hold - beforeHold) > 1.0e-3f) repaint();
}

void GainReductionBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getWidth() / 46.0f;
    const float es = ui::editorScale (*this);
    auto r = area;
    const auto title = r.removeFromTop (16.0f * u);
    const auto readout = r.removeFromBottom (18.0f * u);
    r.removeFromBottom (4.0f * u);
    r.removeFromTop (2.0f * u);
    const auto scale = r.removeFromRight (18.0f * u);
    const auto bar = r.reduced (2.0f * u, 0.0f);
    auto yOf = [&] (float db) { return bar.getY() + juce::jlimit (0.0f, 1.0f, db / range) * bar.getHeight(); };

    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::body, std::max (11.0f * u, 10.5f * es), true, 0.12f));
    g.drawText ("GR", title, juce::Justification::centred, false);

    g.setFont (ui::font (ui::Face::mono, std::max (8.0f * u, 9.0f * es)));
    for (int db : { 0, juce::roundToInt (range / 3.0f), juce::roundToInt (range * 2.0f / 3.0f), juce::roundToInt (range) })
    {
        const float y = yOf ((float) db);
        g.setColour (ui::haze.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (scale.getX() + 1.0f * u, y, 3.0f * u, 1.0f));
        g.drawText (db == 0 ? juce::String ("0") : "-" + juce::String (db),
                    juce::Rectangle<float> (scale.getX() + 4.0f * u, y - 6.0f * u, scale.getWidth() - 3.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bar, 3.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (bar.reduced (0.5f), 3.0f * u, 1.0f);
    if (gr > 0.02f)
    {
        const auto fill = bar.withBottom (yOf (gr)).reduced (1.5f * u, 1.0f);
        juce::Path p;
        p.addRoundedRectangle (fill, 2.0f * u);
        ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.5f);
        g.setGradientFill (juce::ColourGradient (ui::bone, 0.0f, bar.getY(), ui::ember, 0.0f, bar.getY() + bar.getHeight() * 0.5f, false));
        g.fillPath (p);
    }

    const juce::String txt = hold < 0.05f ? juce::String ("0.0") : "-" + juce::String (hold, 1);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (readout, readout.getHeight() * 0.5f);
    g.setColour (hold > 0.05f ? ui::ember.withAlpha (0.8f) : ui::line);
    g.drawRoundedRectangle (readout.reduced (0.5f), readout.getHeight() * 0.5f, 1.0f);
    g.setColour (ui::mist.withAlpha (active ? 1.0f : 0.5f));
    g.setFont (ui::fitFont (ui::Face::mono, readout.getHeight() * 0.62f, readout.getWidth() - 6.0f * u, txt, true));
    g.drawText (txt, readout, juce::Justification::centred, false);
}

//==============================================================================
void PillMenu::tick()
{
    bool changed = ui::approach (glow, hover ? 1.0f : 0.0f, 0.2f);
    changed |= ui::approach (led, statusLevel, 0.15f);
    if (changed) repaint();
}

void PillMenu::mouseDown (const juce::MouseEvent&)
{
    if (! items) return;
    const auto list = items();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& it = list[(size_t) i];
        if (it.header) { menu.addSectionHeader (it.text); continue; }
        if (it.text.isEmpty()) { menu.addSeparator(); continue; }
        const auto shown = it.label.isNotEmpty() ? it.label : it.text;
        menu.addItem (i + 1, it.badge.isNotEmpty() ? shown + "   " + it.badge : shown, it.enabled, it.text == value);
    }

    juce::Component::SafePointer<PillMenu> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (ui::popupParent (*this)),
                        [self, list] (int r)
                        {
                            if (self == nullptr || r <= 0 || r > (int) list.size()) return;
                            if (self->onSelect) self->onSelect (r - 1, list[(size_t) (r - 1)].text);
                        });
}

void PillMenu::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 34.0f;
    juce::Path p;
    p.addRoundedRectangle (r, h * 0.5f);
    ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.5f * glow);
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillPath (p);
    g.setColour (ui::ember.withAlpha (0.08f * glow));
    g.fillPath (p);
    g.setColour ((warning ? ui::ember.withAlpha (0.7f) : ui::line).interpolatedWith (ui::ember, glow));
    g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.1f * u)));

    auto inner = r.reduced (h * 0.45f, 0.0f);

    // LED d'état (périphérique actif / en erreur)
    const float d = h * 0.24f;
    const auto ledR = juce::Rectangle<float> (d, d).withCentre ({ inner.getX() + d * 0.5f, r.getCentreY() });
    g.setColour (statusColour.withAlpha (0.35f * led));
    g.fillEllipse (ledR.expanded (d * 0.8f));
    g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (statusColour, led));
    g.fillEllipse (ledR);
    inner.removeFromLeft (d + 8.0f * u);

    if (tagText.isNotEmpty())
    {
        const auto tf = ui::font (ui::Face::mono, h * 0.32f, true, 0.10f);
        const float tw = ui::textWidth (tf, tagText) + 10.0f * u;
        g.setColour (ui::haze);
        g.setFont (tf);
        g.drawText (tagText, inner.removeFromLeft (tw), juce::Justification::centredLeft, false);
    }
    inner.removeFromRight (16.0f * u);
    const juce::String txt = value.isEmpty() ? juce::String ("None") : (display.isNotEmpty() ? display : value);
    g.setColour ((value.isEmpty() ? ui::haze : ui::mist).interpolatedWith (ui::bone, glow));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.40f, inner.getWidth(), txt, true, 0.02f));
    g.drawText (txt, inner, juce::Justification::centredLeft, true);

    ui::drawMenuArrow (g, { r.getRight() - h * 0.5f, r.getCentreY() }, u * 1.1f, ui::haze.interpolatedWith (ui::ember, glow));
}

//==============================================================================
ProcessingOrderBar::ProcessingOrderBar (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    if (auto* p = apvts.getParameter (ids::chainOrder))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v)
        {
            order = decodeOrder (juce::roundToInt (v));
            repaint();
        });
        attachment->sendInitialUpdate();
    }
}

float ProcessingOrderBar::chipW() const noexcept
{
    return ((float) getWidth() - (float) (kN - 1) * gapW()) / (float) kN;
}

bool ProcessingOrderBar::moduleOn (int module) const
{
    static const char* powerIds[kN] { ids::gateOn, ids::eqOn, ids::fetOn, ids::mbOn, ids::deessOn, ids::satOn };
    return apvts.getRawParameterValue (powerIds[module])->load() > 0.5f;
}

int ProcessingOrderBar::moduleAt (juce::Point<float> p) const noexcept
{
    if (p.y < 0.0f || p.y > (float) getHeight()) return -1;
    for (int m = 0; m < kN; ++m)
        if (p.x >= chipX[(size_t) m] && p.x <= chipX[(size_t) m] + chipW()) return m;
    return -1;
}

std::array<int, ProcessingOrderBar::kN> ProcessingOrderBar::previewOrder() const noexcept
{
    if (dragModule < 0) return order;
    const float centre = dragX + chipW() * 0.5f;
    const int slot = juce::jlimit (0, kN - 1, (int) std::floor (centre / (chipW() + gapW())));
    std::vector<int> rest;
    for (int m : order) if (m != dragModule) rest.push_back (m);
    rest.insert (rest.begin() + slot, dragModule);
    std::array<int, kN> out {};
    std::copy (rest.begin(), rest.end(), out.begin());
    return out;
}

void ProcessingOrderBar::tick()
{
    const auto shown = previewOrder();
    bool changed = false;
    for (int s = 0; s < kN; ++s)
    {
        const int m = shown[(size_t) s];
        const float target = m == dragModule ? dragX : slotX (s);
        if (! placed) chipX[(size_t) m] = target;
        changed |= ui::approach (chipX[(size_t) m], target, m == dragModule ? 1.0f : 0.25f);
    }
    if (! placed) { placed = true; changed = true; }
    for (int m = 0; m < kN; ++m)
    {
        changed |= ui::approach (lit[(size_t) m], moduleOn (m) ? 1.0f : 0.0f, 0.15f);
        changed |= ui::approach (hoverGlow[(size_t) m], (m == hoverModule || m == dragModule) ? 1.0f : 0.0f, 0.2f);
    }
    if (changed) repaint();
}

void ProcessingOrderBar::mouseDown (const juce::MouseEvent& e)
{
    dragModule = moduleAt (e.position);
    if (dragModule < 0) return;
    dragOffset = e.position.x - chipX[(size_t) dragModule];
    dragX = chipX[(size_t) dragModule];
    dragMoved = false;
}

void ProcessingOrderBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragModule < 0) return;
    dragX = juce::jlimit (0.0f, (float) getWidth() - chipW(), e.position.x - dragOffset);
    if (e.getDistanceFromDragStart() > 3) dragMoved = true;
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void ProcessingOrderBar::mouseUp (const juce::MouseEvent&)
{
    if (dragModule < 0) return;
    const auto next = previewOrder();
    dragModule = -1;
    if (dragMoved && next != order && attachment != nullptr)
    {
        order = next;
        attachment->setValueAsCompleteGesture ((float) encodeOrder (next));
    }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void ProcessingOrderBar::mouseMove (const juce::MouseEvent& e)
{
    const int h = moduleAt (e.position);
    if (h != hoverModule) { hoverModule = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ProcessingOrderBar::mouseExit (const juce::MouseEvent&) { hoverModule = -1; }

void ProcessingOrderBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (attachment != nullptr) attachment->setValueAsCompleteGesture (0.0f);
}

juce::String ProcessingOrderBar::getTooltip()
{
    return "Processing order: drag a module left or right to move it in the chain (smooth crossfade). "
           "Lit = module on. Double-click = original order.";
}

void ProcessingOrderBar::paint (juce::Graphics& g)
{
    const float h = (float) getHeight(), w = chipW(), u = h / 20.0f;

    for (int s = 0; s < kN - 1; ++s)
    {
        const float cx = slotX (s) + w + gapW() * 0.5f, cy = h * 0.5f;
        juce::Path ch;
        ch.startNewSubPath (cx - 2.0f * u, cy - 4.0f * u);
        ch.lineTo (cx + 2.0f * u, cy);
        ch.lineTo (cx - 2.0f * u, cy + 4.0f * u);
        g.setColour (ui::haze.withAlpha (0.6f));
        g.strokePath (ch, juce::PathStrokeType (std::max (1.0f, 1.5f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    for (int pass = 0; pass < 2; ++pass)
        for (int m = 0; m < kN; ++m)
        {
            if ((pass == 1) != (m == dragModule)) continue;
            const auto r = juce::Rectangle<float> (chipX[(size_t) m], 0.0f, w, h).reduced (0.5f, 1.0f);
            const float l = lit[(size_t) m], hv = hoverGlow[(size_t) m];
            juce::Path p;
            p.addRoundedRectangle (r, r.getHeight() * 0.5f);
            ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.7f * hv);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillPath (p);
            g.setColour (ui::ember.withAlpha (0.10f + 0.14f * l + 0.12f * hv));
            g.fillPath (p);
            g.setColour (ui::line.interpolatedWith (ui::ember, 0.35f * l + 0.65f * hv));
            g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.0f * u)));
            g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.55f * l, hv)));
            g.setFont (ui::fitFont (ui::Face::mono, h * 0.56f, r.getWidth() - 6.0f * u, orderName (m), true, 0.02f));
            g.drawText (orderName (m), r, juce::Justification::centred, false);
        }
}

//==============================================================================
PresetBar::PresetBar (onirique::PresetManager& p) : pm (p)
{
    addChildComponent (editor);
    editor.setJustification (juce::Justification::centred);
    editor.setTextToShowWhenEmpty ("Preset name", ui::haze);
    editor.onReturnKey = [this] { endSave (true); };
    editor.onEscapeKey = [this] { endSave (false); };
    editor.onFocusLost = [this] { endSave (false); };
    shownName = pm.getCurrentName();
}

void PresetBar::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto rest = r;
    zones[save] = rest.removeFromRight (h * 2.1f);
    rest.removeFromRight (h * 0.2f);
    zones[prev] = rest.removeFromLeft (h);
    zones[next] = rest.removeFromRight (h);
    zones[name] = rest;
    editor.setBounds (zones[name].reduced (2.0f, h * 0.14f).toNearestInt());
    editor.setFont (ui::font (ui::Face::body, h * 0.40f, true));
}

PresetBar::Zone PresetBar::zoneAt (juce::Point<float> p) const noexcept
{
    for (int z = 0; z < 4; ++z) if (zones[z].contains (p)) return (Zone) z;
    return none;
}

void PresetBar::tick()
{
    bool changed = false;
    for (int z = 0; z < 4; ++z) changed |= ui::approach (glow[z], hover == z ? 1.0f : 0.0f, 0.2f);
    if ((++frame % 10) == 0)
    {
        const auto n = pm.getCurrentName();
        const bool mod = pm.isModified();
        if (n != shownName || mod != modified) { shownName = n; modified = mod; changed = true; }
    }
    if (changed) repaint();
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
}

void PresetBar::mouseExit (const juce::MouseEvent&) { hover = none; }

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case prev: pm.step (-1); break;
        case next: pm.step (1); break;
        case name: showMenu(); break;
        case save: startSave(); break;
        default: break;
    }
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::showMenu()
{
    const auto entries = pm.list();
    const auto current = pm.getCurrentName();
    juce::PopupMenu menu;
    bool userHeader = false;
    menu.addSectionHeader ("FACTORY");
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (! e.factory && ! userHeader) { menu.addSectionHeader ("USER"); userHeader = true; }
        menu.addItem (i + 1, e.name, true, e.name == current);
    }
    menu.addSeparator();
    menu.addItem (1000, "Save as new preset...");
    bool currentIsUser = false;
    for (const auto& e : entries) if (! e.factory && e.name == current) currentIsUser = true;
    menu.addItem (1001, "Delete \"" + current + "\"", currentIsUser);
    menu.addItem (1002, "Open presets folder");

    menu.setLookAndFeel (&getLookAndFeel());
    juce::Component::SafePointer<PresetBar> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (ui::popupParent (*this)),
                        [self, entries] (int r)
                        {
                            if (self == nullptr || r == 0) return;
                            auto& bar = *self;
                            if (r >= 1 && r <= (int) entries.size()) bar.pm.load (entries[(size_t) (r - 1)]);
                            else if (r == 1000) bar.startSave();
                            else if (r == 1001)
                            {
                                for (const auto& e : entries)
                                    if (! e.factory && e.name == bar.pm.getCurrentName()) bar.pm.remove (e);
                                bar.pm.load (entries.front());
                            }
                            else if (r == 1002) onirique::PresetManager::folder().startAsProcess();
                            bar.repaint();
                        });
}

void PresetBar::startSave()
{
    editor.setText (shownName == "Init" ? juce::String() : shownName, false);
    editor.setVisible (true);
    editor.selectAll();
    editor.grabKeyboardFocus();
    repaint();
}

void PresetBar::endSave (bool commit)
{
    if (! editor.isVisible()) return;
    const auto text = editor.getText();
    editor.setVisible (false);
    if (commit) pm.save (text);
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 40.0f;

    const auto box = zones[prev].getUnion (zones[next]).reduced (0.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.6f * glow[name]));
    g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, std::max (1.0f, 1.2f * u));

    for (int z : { (int) prev, (int) next })
    {
        const auto c = zones[z].getCentre();
        const float d = (z == prev ? -1.0f : 1.0f) * 4.0f * u;
        juce::Path chev;
        chev.startNewSubPath (c.x - d, c.y - 6.0f * u);
        chev.lineTo (c.x + d, c.y);
        chev.lineTo (c.x - d, c.y + 6.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::bone, glow[z]));
        g.strokePath (chev, juce::PathStrokeType (std::max (1.3f, 2.0f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! editor.isVisible())
    {
        auto nr = zones[name].reduced (4.0f * u, 0.0f);
        g.setColour (ui::haze.withAlpha (0.8f));
        g.setFont (ui::font (ui::Face::mono, 9.0f * u, false, 0.1f));
        g.drawText ("PRESET", nr.removeFromTop (h * 0.42f).withTrimmedTop (h * 0.10f), juce::Justification::centredBottom, false);
        const juce::String txt = shownName + (modified ? " *" : "");
        g.setColour (ui::mist.interpolatedWith (ui::bone, glow[name]));
        g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, nr.getWidth() - 18.0f * u, txt, true, 0.04f));
        g.drawText (txt, nr.withTrimmedBottom (h * 0.08f), juce::Justification::centredTop, false);
        ui::drawMenuArrow (g, { zones[name].getRight() - 8.0f * u, zones[name].getCentreY() }, u, ui::haze.interpolatedWith (ui::ember, glow[name]));
    }

    const auto s = zones[save].reduced (0.0f, 1.0f);
    juce::Path sp;
    sp.addRoundedRectangle (s, s.getHeight() * 0.5f);
    ui::strokeGlow (g, sp, ui::ember, 1.2f * u, glow[save] * 0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillPath (sp);
    g.setColour (ui::ember.withAlpha (0.18f * glow[save]));
    g.fillPath (sp);
    g.setColour (ui::line.interpolatedWith (ui::ember, glow[save]));
    g.strokePath (sp, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
    g.setColour (ui::haze.interpolatedWith (ui::bone, glow[save]));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, s.getWidth() - 10.0f * u, "SAVE", true, 0.16f));
    g.drawText ("SAVE", s, juce::Justification::centred, false);
}
