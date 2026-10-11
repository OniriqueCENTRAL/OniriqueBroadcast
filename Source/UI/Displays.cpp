#include "Displays.h"

using namespace onirique;

//==============================================================================
SpectrumAnalyzer::SpectrumAnalyzer (BroadcastEngine& e) : engine (e)
{
    window.resize ((size_t) kN);
    double sum = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / kN));
        sum += window[(size_t) i];
    }
    windowGain = (float) (2.0 / sum);          // un sinus d'amplitude A lit 20 log10 A (dBFS)
    histIn.assign ((size_t) kN, 0.0f);
    histOut.assign ((size_t) kN, 0.0f);
    scratch.assign ((size_t) SpectrumTap::kSize, 0.0f);
    fftBuf.assign ((size_t) (2 * kN), 0.0f);
    specIn.fill (-140.0f);
    specOut.fill (-140.0f);
    curve.fill (0.0f);
}

void SpectrumAnalyzer::analyse (SpectrumTap& tap, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec)
{
    const int got = tap.pull (scratch.data(), (int) scratch.size());
    for (int i = std::max (0, got - kN); i < got; ++i)
    {
        hist[(size_t) writePos] = scratch[(size_t) i];
        writePos = (writePos + 1) & (kN - 1);
    }

    for (int i = 0; i < kN; ++i)
        fftBuf[(size_t) i] = hist[(size_t) ((writePos + i) & (kN - 1))] * window[(size_t) i];
    std::fill (fftBuf.begin() + kN, fftBuf.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);

    const double fs = engine.getEngineSampleRate();
    const double binHz = fs / kN;
    for (int i = 0; i < kPts; ++i)
    {
        const double f0 = hzAt (std::max (0, i - 1)) * 0.5 + hzAt (i) * 0.5;
        const double f1 = hzAt (std::min (kPts - 1, i + 1)) * 0.5 + hzAt (i) * 0.5;
        int b0 = (int) std::ceil (f0 / binHz), b1 = (int) std::floor (f1 / binHz);
        if (b1 < b0) b0 = b1 = (int) std::lround (hzAt (i) / binHz);
        b0 = juce::jlimit (1, kN / 2, b0);
        b1 = juce::jlimit (1, kN / 2, b1);
        float mag = 0.0f;
        for (int b = b0; b <= b1; ++b) mag = std::max (mag, fftBuf[(size_t) b]);
        // pente de +3 dB/oct (autour de 1 kHz) : une voix paraît équilibrée à l'écran
        const float db = 20.0f * std::log10 (mag * windowGain + 1.0e-9f) + 3.0f * std::log2 (hzAt (i) / 1000.0f);
        auto& s = spec[(size_t) i];
        s = std::max (db, s - 1.2f);
    }
}

void SpectrumAnalyzer::tick()
{
    if ((++frame & 1) != 0) return;   // 30 images/s

    analyse (engine.inputTap, histIn, writeIn, specIn);
    analyse (engine.outputTap, histOut, writeOut, specOut);

    const bool on = param (ids::eqOn) > 0.5f && param (ids::bypass) < 0.5f;
    ui::approach (eqAmt, on ? 1.0f : 0.0f, 0.2f);

    const double fs = engine.getEngineSampleRate();
    BiquadCoeffs pc[kPeq];
    bool pOn[kPeq];
    for (int b = 0; b < kPeq; ++b)
    {
        const auto bd = band (b);
        pOn[b] = bd.on;
        pc[b] = ParametricEQ::design (fs, bd.type, bd.freq, bd.gainDb, bd.q);
        ui::approach (nodeGlow[b], (b == selected ? 0.6f : 0.0f) + (b == hoverNode || b == dragNode || b == qFlashBand ? 0.4f : 0.0f), 0.2f);
    }
    if (qFlashFrames > 0 && --qFlashFrames == 0) qFlashBand = -1;
    for (int i = 0; i < kPts; ++i)
    {
        const double f = std::min ((double) hzAt (i), 0.49 * fs);
        std::complex<double> hp (1.0, 0.0);
        for (int b = 0; b < kPeq; ++b) if (pOn[b]) hp *= pc[b].response (fs, f);
        const float target = eqAmt * (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (hp))));
        curve[(size_t) i] += 0.45f * (target - curve[(size_t) i]);   // la courbe glisse vers sa nouvelle forme
        selCurve[(size_t) i] = (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (pc[selected].response (fs, f)))));
    }

    repaint();
}

juce::String SpectrumAnalyzer::getTooltip()
{
    if (hoverNode >= 0)
        return "Band " + juce::String (hoverNode + 1) + ": drag = frequency / gain, mouse wheel = Q (up = narrower), double-click = on/off, "
               "right-click = filter type.";
    return "Live analyzer + parametric EQ: double-click an empty spot to add a band there, drag the numbered dots to shape it. "
           "Grey = microphone, white = virtual mic output, red = EQ curve.";
}

juce::Rectangle<float> SpectrumAnalyzer::plotArea() const noexcept
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getHeight() / 200.0f;
    return area.reduced (12.0f * u, 10.0f * u).withTrimmedBottom (14.0f * u).withTrimmedRight (40.0f * u).withTrimmedTop (16.0f * u);
}

float SpectrumAnalyzer::xOf (float hz) const noexcept
{
    const auto p = plotArea();
    return p.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * p.getWidth();
}

float SpectrumAnalyzer::hzOf (float x) const noexcept
{
    const auto p = plotArea();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth()));
}

float SpectrumAnalyzer::yEq (float db) const noexcept
{
    const auto p = plotArea();
    return p.getCentreY() - juce::jlimit (-kEqRange, kEqRange, db) / kEqRange * p.getHeight() * 0.5f;
}

float SpectrumAnalyzer::dbOf (float y) const noexcept
{
    const auto p = plotArea();
    return juce::jlimit (-kEqRange, kEqRange, (p.getCentreY() - y) / (p.getHeight() * 0.5f) * kEqRange);
}

ParametricEQ::Band SpectrumAnalyzer::band (int b) const
{
    ParametricEQ::Band bd;
    bd.on = param (ids::peqOn[b]) > 0.5f;
    bd.type = juce::roundToInt (param (ids::peqType[b]));
    bd.freq = param (ids::peqFreq[b]);
    bd.gainDb = param (ids::peqGain[b]);
    bd.q = param (ids::peqQ[b]);
    return bd;
}

juce::Point<float> SpectrumAnalyzer::nodePos (int b) const
{
    const auto bd = band (b);
    const bool hasGain = bd.type <= ParametricEQ::highShelf;
    return { xOf (bd.freq), yEq (hasGain ? bd.gainDb : 0.0f) };
}

int SpectrumAnalyzer::nodeAt (juce::Point<float> p) const
{
    const float r = 14.0f * (float) getHeight() / 200.0f;
    int best = -1;
    float bestD = r;
    for (int b = 0; b < kPeq; ++b)
        if (const float d = nodePos (b).getDistanceFrom (p); d < bestD) { bestD = d; best = b; }
    return best;
}

void SpectrumAnalyzer::setParam (const char* id, float v, bool gesture)
{
    if (auto* p = engine.apvts.getParameter (id))
    {
        if (gesture) p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        if (gesture) p->endChangeGesture();
    }
}

void SpectrumAnalyzer::select (int b)
{
    selected = juce::jlimit (0, kPeq - 1, b);
    if (onSelect) onSelect (selected);
}

void SpectrumAnalyzer::mouseMove (const juce::MouseEvent& e)
{
    hoverNode = nodeAt (e.position);
    setMouseCursor (hoverNode >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
}

void SpectrumAnalyzer::mouseExit (const juce::MouseEvent&)
{
    if (dragNode < 0) hoverNode = -1;
}

void SpectrumAnalyzer::mouseDown (const juce::MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n < 0) return;
    select (n);
    if (e.mods.isPopupMenu()) { showTypeMenu (*this, engine.apvts, n); return; }
    dragNode = n;
    for (auto* id : { ids::peqFreq[n], ids::peqGain[n] })
        if (auto* p = engine.apvts.getParameter (id)) p->beginChangeGesture();
}

void SpectrumAnalyzer::mouseDrag (const juce::MouseEvent& e)
{
    if (dragNode < 0) return;
    if (! band (dragNode).on) setParam (ids::peqOn[dragNode], 1.0f);   // tirer une bande coupée l'allume
    setParam (ids::peqFreq[dragNode], juce::jlimit (20.0f, 20000.0f, hzOf (e.position.x)), false);
    if (band (dragNode).type <= ParametricEQ::highShelf)
        setParam (ids::peqGain[dragNode], std::round (dbOf (e.position.y) * 10.0f) / 10.0f, false);
}

void SpectrumAnalyzer::mouseUp (const juce::MouseEvent&)
{
    if (dragNode < 0) return;
    for (auto* id : { ids::peqFreq[dragNode], ids::peqGain[dragNode] })
        if (auto* p = engine.apvts.getParameter (id)) p->endChangeGesture();
    dragNode = -1;
}

void SpectrumAnalyzer::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int n = nodeAt (e.position); n >= 0)
    {
        setParam (ids::peqOn[n], band (n).on ? 0.0f : 1.0f);
        select (n);
        return;
    }
    // double-clic dans le vide : la première bande libre devient une cloche à cet endroit
    for (int b = 0; b < kPeq; ++b)
        if (! band (b).on)
        {
            setParam (ids::peqType[b], (float) ParametricEQ::bell);
            setParam (ids::peqFreq[b], juce::jlimit (20.0f, 20000.0f, hzOf (e.position.x)));
            setParam (ids::peqGain[b], std::round (dbOf (e.position.y) * 10.0f) / 10.0f);
            setParam (ids::peqQ[b], 1.0f);
            setParam (ids::peqOn[b], 1.0f);
            select (b);
            return;
        }
}

void SpectrumAnalyzer::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float delta = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    if (delta == 0.0f) return;

    int n = nodeAt (e.position);
    if (n < 0)
    {
        const float hz = hzOf (e.position.x);
        float best = 0.75f;
        for (int b = 0; b < kPeq; ++b)
        {
            const auto bd = band (b);
            const float d = std::abs (std::log2 (hz / bd.freq));
            if (bd.on && d < best) { best = d; n = b; }
        }
    }
    if (n < 0) n = selected;

    const float q = band (n).q * std::pow (2.0f, delta * 1.6f);
    setParam (ids::peqQ[n], juce::jlimit (0.1f, 10.0f, q));
    select (n);
    qFlashBand = n;
    qFlashFrames = 40;
}

void SpectrumAnalyzer::showTypeMenu (juce::Component& target, juce::AudioProcessorValueTreeState& apvts, int b)
{
    static const char* names[] { "Bell", "Low Shelf", "High Shelf", "Low Cut (12 dB/oct)", "High Cut (12 dB/oct)" };
    const int cur = juce::roundToInt (apvts.getRawParameterValue (ids::peqType[b])->load());
    const bool on = apvts.getRawParameterValue (ids::peqOn[b])->load() > 0.5f;
    juce::PopupMenu menu;
    menu.setLookAndFeel (&target.getLookAndFeel());
    menu.addSectionHeader ("BAND " + juce::String (b + 1));
    for (int t = 0; t < ParametricEQ::numTypes; ++t) menu.addItem (t + 1, names[t], true, t == cur);
    menu.addSeparator();
    menu.addItem (100, on ? "Switch band off" : "Switch band on");
    menu.addItem (101, "Reset band");

    auto* ap = &apvts;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withParentComponent (ui::popupParent (target)),
                        [ap, b] (int r)
                        {
                            auto set = [ap] (const char* id, float v)
                            {
                                if (auto* p = ap->getParameter (id))
                                { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
                            };
                            if (r >= 1 && r <= ParametricEQ::numTypes) { set (ids::peqType[b], (float) (r - 1)); set (ids::peqOn[b], 1.0f); }
                            else if (r == 100) set (ids::peqOn[b], ap->getRawParameterValue (ids::peqOn[b])->load() > 0.5f ? 0.0f : 1.0f);
                            else if (r == 101)
                                for (auto* id : { ids::peqType[b], ids::peqFreq[b], ids::peqGain[b], ids::peqQ[b], ids::peqOn[b] })
                                    if (auto* p = ap->getParameter (id))
                                    { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
                        });
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getHeight() / 200.0f;
    const float es = ui::editorScale (*this);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.85f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    const auto plot = plotArea();
    auto ySpec = [&] (float db) { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 88.0f) * plot.getHeight(); };

    // ---- grille
    g.setFont (ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * es)));
    for (auto [hz, txt] : { std::pair { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                            { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
    {
        const float x = xOf (hz);
        g.setColour (ui::line.withMultipliedAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (x, plot.getY() - 6.0f * u, 1.0f, plot.getHeight() + 6.0f * u));
        g.setColour (ui::haze.withAlpha (0.8f));
        g.drawText (txt, juce::Rectangle<float> (x + 4.0f * u, plot.getBottom() + 2.0f * u, 40.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = yEq (db);
        g.setColour (db == 0.0f ? ui::line.withMultipliedAlpha (1.7f) : ui::line.withMultipliedAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        g.setColour (ui::haze.withAlpha (0.6f));
        g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db) + " dB",
                    juce::Rectangle<float> (plot.getRight() + 4.0f * u, y - 6.0f * u, 40.0f * u, 12.0f * u), juce::Justification::centredLeft, false);
    }

    // ---- spectres : entrée (trait discret), sortie (dégradé os + trait)
    auto specPath = [&] (const std::array<float, kPts>& s, bool closed)
    {
        juce::Path p;
        if (closed) p.startNewSubPath (plot.getX(), plot.getBottom());
        for (int i = 0; i < kPts; ++i)
        {
            const float x = xOf (hzAt (i)), y = ySpec (s[(size_t) i]);
            if (i == 0 && ! closed) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
        return p;
    };
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (ui::haze.withAlpha (0.45f));
        g.strokePath (specPath (specIn, false), juce::PathStrokeType (std::max (1.0f, 1.0f * u)));

        const auto out = specPath (specOut, true);
        g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.42f), 0.0f, plot.getY(), ui::bone.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
        g.fillPath (out);
        g.setColour (ui::bone.withAlpha (0.8f));
        g.strokePath (specPath (specOut, false), juce::PathStrokeType (std::max (1.0f, 1.2f * u)));

        // ---- courbe d'EQ : trait braise lumineux + remplissage jusqu'à 0 dB
        juce::Path eqp;
        for (int i = 0; i < kPts; ++i)
        {
            const float x = xOf (hzAt (i)), y = yEq (curve[(size_t) i]);
            if (i == 0) eqp.startNewSubPath (x, y); else eqp.lineTo (x, y);
        }
        juce::Path fill (eqp);
        fill.lineTo (plot.getRight(), yEq (0.0f));
        fill.lineTo (plot.getX(), yEq (0.0f));
        fill.closeSubPath();
        g.setColour (ui::ember.withAlpha (0.10f * (0.3f + 0.7f * eqAmt)));
        g.fillPath (fill);
        ui::strokeGlow (g, eqp, ui::ember, 2.5f * u, 0.3f + 0.7f * eqAmt);
        g.setColour (ui::ember.withAlpha (0.45f + 0.55f * eqAmt));
        g.strokePath (eqp, juce::PathStrokeType (std::max (1.5f, 2.5f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- zone de la bande sélectionnée + points numérotés (glissables)
    {
        const auto sb = band (selected);
        if (sb.on)
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (plot.toNearestInt());
            juce::Path p;
            p.startNewSubPath (plot.getX(), yEq (0.0f));
            for (int i = 0; i < kPts; ++i) p.lineTo (xOf (hzAt (i)), yEq (selCurve[(size_t) i]));
            p.lineTo (plot.getRight(), yEq (0.0f));
            p.closeSubPath();
            g.setColour (ui::bone.withAlpha (0.10f * eqAmt));
            g.fillPath (p);
        }

        for (int b = 0; b < kPeq; ++b)
        {
            const auto bd = band (b);
            const auto c = nodePos (b);
            const float gl = nodeGlow[b];
            const float r = std::max (7.0f * es, (8.0f + 2.0f * gl) * u);
            const auto dot = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
            juce::Path ring;
            ring.addEllipse (dot);
            if (bd.on)
            {
                ui::strokeGlow (g, ring, ui::ember, 1.4f * u, (0.4f + 0.6f * gl) * eqAmt);
                g.setColour (b == selected ? ui::ember : ui::night);
                g.fillPath (ring);
                g.setColour (b == selected ? ui::bone : ui::ember);
                g.strokePath (ring, juce::PathStrokeType (std::max (1.2f, 1.8f * u)));
                g.setColour (b == selected ? ui::night : ui::bone);
            }
            else
            {
                g.setColour (ui::night.withAlpha (0.7f));
                g.fillPath (ring);
                g.setColour (ui::haze.withAlpha (0.35f + 0.4f * gl));
                g.strokePath (ring, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
                g.setColour (ui::haze.withAlpha (0.5f + 0.4f * gl));
            }
            g.setFont (ui::font (ui::Face::mono, r * 1.15f, true));
            g.drawText (juce::String (b + 1), dot, juce::Justification::centred, false);
        }

        if (qFlashBand >= 0)
        {
            const float a = juce::jlimit (0.0f, 1.0f, (float) qFlashFrames / 10.0f);
            const auto c = nodePos (qFlashBand);
            const juce::String txt = "Q " + juce::String (band (qFlashBand).q, 2);
            const auto f = ui::font (ui::Face::mono, std::max (8.0f, 11.0f * u), true);
            const float tw = ui::textWidth (f, txt) + 14.0f * u;
            auto box = juce::Rectangle<float> (tw, 18.0f * u).withCentre ({ c.x, c.y - 22.0f * u });
            if (box.getY() < area.getY() + 2.0f) box.setCentre (c.x, c.y + 22.0f * u);
            box = box.withX (juce::jlimit (area.getX() + 2.0f, area.getRight() - tw - 2.0f, box.getX()));
            g.setColour (ui::night.withAlpha (0.92f * a));
            g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
            g.setColour (ui::ember.withAlpha (a));
            g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
            g.setColour (ui::bone.withAlpha (a));
            g.setFont (f);
            g.drawText (txt, box, juce::Justification::centred, false);
        }
    }

    // ---- légende
    {
        float x = plot.getX();
        const float y = area.getY() + 7.0f * u;
        g.setFont (ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * es)));
        for (auto [c, txt] : { std::pair { ui::ember, "EQ CURVE" }, { ui::bone, "VIRTUAL MIC" }, { ui::haze, "MICROPHONE" } })
        {
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (x, y + 4.5f * u, 14.0f * u, 3.0f * u), 1.5f * u);
            g.setColour (ui::haze);
            const float tw = ui::textWidth (g.getCurrentFont(), txt) + 4.0f * u;
            g.drawText (txt, juce::Rectangle<float> (x + 20.0f * u, y, tw, 12.0f * u), juce::Justification::centredLeft, false);
            x += 20.0f * u + tw + 16.0f * u;
        }
    }
}

//------------------------------------------------------------------------------
void PeqBandChip::tick()
{
    static const char* names[] { "BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT" };
    const bool on = apvts.getRawParameterValue (ids::peqOn[band])->load() > 0.5f;
    const juce::String t = names[juce::jlimit (0, 4, juce::roundToInt (apvts.getRawParameterValue (ids::peqType[band])->load()))];
    bool changed = t != shownType;
    shownType = t;
    changed |= ui::approach (lit, on ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void PeqBandChip::mouseDown (const juce::MouseEvent& e)
{
    if (e.position.x < (float) getHeight() * 2.6f)
    {
        if (auto* p = apvts.getParameter (ids::peqOn[band]))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    }
    else
        SpectrumAnalyzer::showTypeMenu (*this, apvts, band);
}

void PeqBandChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 24.0f;
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (r, h * 0.5f);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.7f * lit));
    g.drawRoundedRectangle (r.reduced (0.5f), h * 0.5f, std::max (1.0f, u));

    const float d = h * 0.32f;
    const auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + h * 0.55f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.4f * lit));
    g.fillEllipse (led.expanded (d * 0.7f));
    g.setColour (ui::haze.withAlpha (0.4f).interpolatedWith (ui::ember, lit));
    g.fillEllipse (led);

    auto t = r.withTrimmedLeft (h * 1.0f).withTrimmedRight (h * 0.6f);
    const juce::String num = "BAND " + juce::String (band + 1);
    const auto f = ui::font (ui::Face::mono, h * 0.52f, true, 0.04f);
    const float nw = ui::textWidth (f, num) + 8.0f * u;
    g.setColour (ui::mist);
    g.setFont (f);
    g.drawText (num, t.removeFromLeft (nw), juce::Justification::centredLeft, false);
    g.setColour (ui::haze.interpolatedWith (ui::bone, lit));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.52f, t.getWidth(), shownType, true, 0.06f));
    g.drawText (shownType, t, juce::Justification::centredLeft, false);

    ui::drawMenuArrow (g, { r.getRight() - h * 0.45f, r.getCentreY() }, u * 0.9f, ui::haze);
}

//==============================================================================
MultibandDisplay::MultibandDisplay (BroadcastEngine& e) : engine (e)
{
    for (int i = 0; i < 3; ++i) { xP[i] = e.apvts.getParameter (ids::mbX[i]); x[i] = value (xP[i]); }
    for (int b = 0; b < kB; ++b)
    {
        thrP[b] = e.apvts.getParameter (ids::mbThr[b]);
        gainP[b] = e.apvts.getParameter (ids::mbGain[b]);
        thr[b] = value (thrP[b]);
        gain[b] = value (gainP[b]);
    }
}

juce::Rectangle<float> MultibandDisplay::plotArea() const noexcept
{
    const float u = (float) getHeight() / 200.0f;
    return getLocalBounds().toFloat().reduced (10.0f * u, 8.0f * u).withTrimmedBottom (16.0f * u).withTrimmedRight (30.0f * u);
}

float MultibandDisplay::xOf (float hz) const noexcept
{
    const auto p = plotArea();
    return p.getX() + juce::jlimit (0.0f, 1.0f, std::log (hz / 20.0f) / std::log (1000.0f)) * p.getWidth();
}

float MultibandDisplay::hzOf (float px) const noexcept
{
    const auto p = plotArea();
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (px - p.getX()) / p.getWidth()));
}

float MultibandDisplay::yOf (float db) const noexcept
{
    const auto p = plotArea();
    return p.getY() + juce::jlimit (0.0f, 1.0f, -db / 60.0f) * p.getHeight();
}

float MultibandDisplay::dbOf (float py) const noexcept
{
    const auto p = plotArea();
    return -60.0f * juce::jlimit (0.0f, 1.0f, (py - p.getY()) / p.getHeight());
}

float MultibandDisplay::bandLeft (int b) const noexcept  { return b == 0 ? plotArea().getX() : xOf (x[b - 1]); }
float MultibandDisplay::bandRight (int b) const noexcept { return b == kB - 1 ? plotArea().getRight() : xOf (x[b]); }

int MultibandDisplay::crossoverAt (float px) const noexcept
{
    const float tol = 7.0f * (float) getHeight() / 200.0f;
    for (int i = 0; i < 3; ++i) if (std::abs (px - xOf (x[i])) < tol) return i;
    return -1;
}

int MultibandDisplay::bandAt (float px) const noexcept
{
    for (int b = 0; b < kB; ++b) if (px >= bandLeft (b) && px < bandRight (b)) return b;
    return px < plotArea().getX() ? 0 : kB - 1;
}

void MultibandDisplay::tick()
{
    bool changed = false;
    for (int i = 0; i < 3; ++i)
    {
        changed |= ui::approach (x[i], value (xP[i]), 0.3f);
        changed |= ui::approach (xGlow[i], (i == dragX || i == hoverX) ? 1.0f : 0.0f, 0.2f);
    }
    for (int b = 0; b < kB; ++b)
    {
        changed |= ui::approach (thr[b], value (thrP[b]), 0.3f);
        changed |= ui::approach (gain[b], value (gainP[b]), 0.3f);
        changed |= ui::approach (hoverGlow[b], (b == dragBand || (b == hoverBand && hoverX < 0)) ? 1.0f : 0.0f, 0.2f);
    }

    active = engine.apvts.getRawParameterValue (ids::mbOn)->load() > 0.5f
             && engine.apvts.getRawParameterValue (ids::bypass)->load() < 0.5f;
    const auto& m = engine.getMultibandMeter();
    for (int b = 0; b < kB; ++b)
    {
        const float tg = active ? m.grDb[(size_t) b].load (std::memory_order_relaxed) : 0.0f;
        const float tl = active ? m.levelDb[(size_t) b].load (std::memory_order_relaxed) : -100.0f;
        gr[b] += (tg > gr[b] ? 0.5f : 0.12f) * (tg - gr[b]);
        lvl[b] += (tl > lvl[b] ? 0.6f : 0.10f) * (tl - lvl[b]);
    }
    if (changed || active) repaint();
}

void MultibandDisplay::mouseMove (const juce::MouseEvent& e)
{
    hoverX = crossoverAt (e.position.x);
    hoverBand = bandAt (e.position.x);
    setMouseCursor (hoverX >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
}

void MultibandDisplay::mouseExit (const juce::MouseEvent&)
{
    if (dragX < 0 && dragBand < 0) { hoverX = -1; hoverBand = -1; }
}

void MultibandDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragX = crossoverAt (e.position.x);
    if (dragX >= 0) { xP[dragX]->beginChangeGesture(); return; }
    dragBand = bandAt (e.position.x);
    thrP[dragBand]->beginChangeGesture();
    setValue (thrP[dragBand], juce::jlimit (-60.0f, 0.0f, dbOf (e.position.y)));
}

void MultibandDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragX >= 0)
    {
        const auto range = xP[dragX]->getNormalisableRange();
        float lo = range.start, hi = range.end;
        if (dragX > 0) lo = std::max (lo, value (xP[dragX - 1]) * 1.26f);
        if (dragX < 2) hi = std::min (hi, value (xP[dragX + 1]) / 1.26f);
        setValue (xP[dragX], juce::jlimit (lo, std::max (lo, hi), hzOf (e.position.x)));
    }
    else if (dragBand >= 0)
        setValue (thrP[dragBand], juce::jlimit (-60.0f, 0.0f, dbOf (e.position.y)));
}

void MultibandDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragX >= 0) xP[dragX]->endChangeGesture();
    if (dragBand >= 0) thrP[dragBand]->endChangeGesture();
    dragX = dragBand = -1;
}

void MultibandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    std::vector<juce::RangedAudioParameter*> reset;
    if (const int c = crossoverAt (e.position.x); c >= 0) reset.push_back (xP[c]);
    else { const int b = bandAt (e.position.x); reset.push_back (thrP[b]); reset.push_back (gainP[b]); }
    for (auto* p : reset)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

void MultibandDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto* p = gainP[bandAt (e.position.x)];
    p->beginChangeGesture();
    setValue (p, juce::jlimit (-12.0f, 12.0f, value (p) + (w.deltaY > 0.0f ? 0.5f : -0.5f)));
    p->endChangeGesture();
}

juce::String MultibandDisplay::getTooltip()
{
    return "Multiband: drag inside a band to set its threshold, drag a separator to move a crossover, "
           "mouse wheel = band gain, double-click = reset. Red curtain from the top = gain reduction.";
}

void MultibandDisplay::paint (juce::Graphics& g)
{
    static const char* names[kB] { "LOW", "LO-MID", "HI-MID", "HIGH" };
    const auto area = getLocalBounds().toFloat();
    const float u = area.getHeight() / 200.0f;
    const float es = ui::editorScale (*this);
    const auto plot = plotArea();

    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.85f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    g.setFont (ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * es)));
    for (auto [hz, txt] : { std::pair { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                            { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
    {
        const float px = xOf (hz);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (juce::Rectangle<float> (px, plot.getY(), 1.0f, plot.getHeight()));
        g.setColour (ui::haze.withAlpha (0.7f));
        g.drawText (txt, juce::Rectangle<float> (40.0f * u, 14.0f * u).withCentre ({ px, plot.getBottom() + 8.0f * u }),
                    juce::Justification::centred, false);
    }
    for (int db = -10; db >= -50; db -= 10)
    {
        const float py = yOf ((float) db);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), py, plot.getWidth(), 1.0f));
        g.setColour (ui::haze.withAlpha (0.6f));
        g.drawText (juce::String (db), juce::Rectangle<float> (plot.getRight() + 4.0f * u, py - 6.0f * u, 26.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    const float a = active ? 1.0f : 0.45f;
    for (int b = 0; b < kB; ++b)
    {
        const float x0 = bandLeft (b), x1 = bandRight (b), bw = x1 - x0, cx = 0.5f * (x0 + x1);
        const float grN = juce::jlimit (0.0f, 1.0f, gr[b] / 18.0f);

        g.setColour (ui::ember.withAlpha ((0.03f + 0.12f * grN + 0.05f * hoverGlow[b]) * a));
        g.fillRect (juce::Rectangle<float> (x0, plot.getY(), bw, plot.getHeight()));

        const float barW = std::min (26.0f * u, bw * 0.34f);
        const float ly = yOf (lvl[b]), ty = yOf (thr[b]);
        const auto bar = juce::Rectangle<float> (cx - barW * 0.5f, ly, barW, plot.getBottom() - ly);
        if (bar.getHeight() > 0.5f)
        {
            g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.38f * a), 0.0f, ly, ui::bone.withAlpha (0.05f), 0.0f, plot.getBottom(), false));
            g.fillRoundedRectangle (bar, 3.0f * u);
            if (ly < ty)
            {
                g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.85f * a), 0.0f, ly, ui::ember.withAlpha (0.35f * a), 0.0f, ty, false));
                g.fillRoundedRectangle (bar.withBottom (ty), 3.0f * u);
            }
        }

        if (gr[b] > 0.05f)
        {
            const auto curtain = juce::Rectangle<float> (x0 + 3.0f * u, plot.getY(), bw - 6.0f * u, grN * plot.getHeight() * 0.55f);
            g.setGradientFill (juce::ColourGradient (ui::bone.withAlpha (0.85f * a), 0.0f, curtain.getY(),
                                                     ui::ember.withAlpha (0.85f * a), 0.0f, curtain.getBottom(), false));
            g.fillRoundedRectangle (curtain, 4.0f * u);
            juce::Path edge;
            edge.startNewSubPath (curtain.getX(), curtain.getBottom());
            edge.lineTo (curtain.getRight(), curtain.getBottom());
            ui::strokeGlow (g, edge, ui::ember, 1.5f * u, 0.7f * a);
            g.setColour (ui::night);
            g.setFont (ui::font (ui::Face::mono, std::max (10.5f * u, 10.5f * es), true));
            if (curtain.getHeight() > 13.0f * u)
                g.drawText ("-" + juce::String (gr[b], 1), curtain.withTrimmedBottom (1.0f * u), juce::Justification::centredBottom, false);
        }

        juce::Path tl;
        tl.startNewSubPath (x0 + 4.0f * u, ty);
        tl.lineTo (x1 - 4.0f * u, ty);
        ui::strokeGlow (g, tl, ui::ember, 1.2f * u, (0.3f + 0.7f * hoverGlow[b]) * a);
        g.setColour (ui::ember.withAlpha ((0.7f + 0.3f * hoverGlow[b]) * a));
        g.strokePath (tl, juce::PathStrokeType (std::max (1.0f, 1.6f * u)));
        const float hr = (4.5f + 1.5f * hoverGlow[b]) * u;
        g.setColour (ui::night);
        g.fillEllipse (juce::Rectangle<float> (2.0f * hr, 2.0f * hr).withCentre ({ cx, ty }));
        g.setColour (ui::ember);
        g.drawEllipse (juce::Rectangle<float> (2.0f * hr, 2.0f * hr).withCentre ({ cx, ty }), std::max (1.0f, 1.5f * u));
        g.setColour (ui::mist.withAlpha (0.55f + 0.45f * hoverGlow[b]));
        g.setFont (ui::font (ui::Face::mono, std::max (9.5f * u, 9.5f * es)));
        g.drawText (juce::String (thr[b], 1), juce::Rectangle<float> (bw - 8.0f * u, 12.0f * u).withPosition (x0 + 4.0f * u, ty + 3.0f * u),
                    juce::Justification::centred, false);

        juce::String label (names[b]);
        if (std::abs (gain[b]) > 0.05f) label << (gain[b] > 0.0f ? "  +" : "  ") << juce::String (gain[b], 1);
        g.setColour (ui::haze.interpolatedWith (ui::mist, hoverGlow[b]));
        g.setFont (ui::fitFont (ui::Face::body, std::max (10.5f * u, 11.0f * es), bw - 6.0f * u, label, true, 0.1f));
        g.drawText (label, juce::Rectangle<float> (x0, plot.getBottom() - 16.0f * u, bw, 14.0f * u), juce::Justification::centred, false);
    }

    for (int i = 0; i < 3; ++i)
    {
        const float px = xOf (x[i]);
        juce::Path l;
        l.startNewSubPath (px, plot.getY());
        l.lineTo (px, plot.getBottom());
        ui::strokeGlow (g, l, ui::ember, 1.0f * u, 0.15f + 0.6f * xGlow[i]);
        g.setColour (ui::ember.withAlpha (0.45f + 0.5f * xGlow[i]));
        g.strokePath (l, juce::PathStrokeType (std::max (1.0f, (1.2f + 0.8f * xGlow[i]) * u)));
        const auto grip = juce::Rectangle<float> (8.0f * u, 18.0f * u).withCentre ({ px, plot.getCentreY() });
        g.setColour (ui::night);
        g.fillRoundedRectangle (grip, 4.0f * u);
        g.setColour (ui::ember);
        g.drawRoundedRectangle (grip, 4.0f * u, std::max (1.0f, 1.2f * u));
        if (xGlow[i] > 0.05f)
        {
            const auto txt = xP[i]->getCurrentValueAsText();
            const auto f = ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * es), true);
            const float tw = ui::textWidth (f, txt) + 12.0f * u;
            const auto box = juce::Rectangle<float> (tw, 16.0f * u).withCentre ({ px, plot.getY() + 12.0f * u });
            g.setColour (ui::night.withAlpha (0.9f * xGlow[i]));
            g.fillRoundedRectangle (box, 8.0f * u);
            g.setColour (ui::ember.withAlpha (xGlow[i]));
            g.drawRoundedRectangle (box, 8.0f * u, 1.0f);
            g.setColour (ui::bone.withAlpha (xGlow[i]));
            g.setFont (f);
            g.drawText (txt, box, juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void MbPresetBox::tick()
{
    const auto n = apvts.state.getProperty ("mbPreset", "Init").toString();
    bool changed = n != shown;
    shown = n;
    changed |= ui::approach (glow, hover ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void MbPresetBox::mouseDown (const juce::MouseEvent&)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());
    menu.addSectionHeader ("MULTIBAND PRESETS");
    const auto& list = multibandPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        menu.addItem (i + 1, list[(size_t) i].name, true, shown == list[(size_t) i].name);

    juce::Component::SafePointer<MbPresetBox> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (ui::popupParent (*this)),
                        [self] (int r)
                        {
                            const auto& l = multibandPresets();
                            if (self != nullptr && r >= 1 && r <= (int) l.size())
                                applyMultibandPreset (self->apvts, l[(size_t) (r - 1)]);
                        });
}

void MbPresetBox::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 28.0f;
    juce::Path p;
    p.addRoundedRectangle (r, h * 0.5f);
    ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.5f * glow);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillPath (p);
    g.setColour (ui::line.interpolatedWith (ui::ember, glow));
    g.strokePath (p, juce::PathStrokeType (std::max (1.0f, 1.1f * u)));

    auto inner = r.reduced (h * 0.5f, 0.0f);
    g.setColour (ui::haze);
    g.setFont (ui::font (ui::Face::mono, h * 0.38f, false, 0.10f));
    const juce::String tag ("PRESET");
    const float tw = ui::textWidth (g.getCurrentFont(), tag) + 10.0f * u;
    g.drawText (tag, inner.removeFromLeft (tw), juce::Justification::centredLeft, false);

    juce::String desc;
    for (const auto& pr : multibandPresets()) if (shown == pr.name) desc = pr.description;

    const auto nf = ui::fitFont (ui::Face::body, h * 0.44f, inner.getWidth() * 0.4f, shown, true);
    const float nw = ui::textWidth (nf, shown) + 10.0f * u;
    g.setColour (ui::mist.interpolatedWith (ui::bone, glow));
    g.setFont (nf);
    g.drawText (shown, inner.removeFromLeft (nw), juce::Justification::centredLeft, false);
    inner.removeFromRight (16.0f * u);
    g.setColour (ui::haze.withAlpha (0.8f));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.40f, inner.getWidth(), desc));
    g.drawText (desc, inner, juce::Justification::centredLeft, true);

    ui::drawMenuArrow (g, { r.getRight() - h * 0.55f, r.getCentreY() }, u, ui::haze.interpolatedWith (ui::ember, glow));
}

//==============================================================================
GateDisplay::GateDisplay (BroadcastEngine& e) : engine (e)
{
    thrP = e.apvts.getParameter (ids::gateThr);
    level.fill (-100.0f);
    gainHist.fill (0.0f);
    thrShown = param (ids::gateThr);
    hystShown = param (ids::gateHyst);
}

juce::Rectangle<float> GateDisplay::plotArea() const noexcept
{
    const float u = ui::editorScale (*this);
    return getLocalBounds().toFloat().reduced (10.0f * u, 8.0f * u).withTrimmedRight (30.0f * u).withTrimmedTop (26.0f * u);
}

juce::Rectangle<float> GateDisplay::learnArea() const noexcept
{
    const float u = ui::editorScale (*this);
    const auto a = getLocalBounds().toFloat();
    return { a.getRight() - 104.0f * u, a.getY() + 6.0f * u, 96.0f * u, 22.0f * u };
}

float GateDisplay::yOf (float db) const noexcept
{
    const auto p = plotArea();
    return p.getY() + juce::jlimit (0.0f, 1.0f, db / kMinDb) * p.getHeight();
}

float GateDisplay::dbOf (float y) const noexcept
{
    const auto p = plotArea();
    return kMinDb * juce::jlimit (0.0f, 1.0f, (y - p.getY()) / p.getHeight());
}

void GateDisplay::tick()
{
    const auto& m = engine.getGateMeter();
    const bool on = param (ids::gateOn) > 0.5f && param (ids::bypass) < 0.5f;
    const float lv = m.levelDb.load (std::memory_order_relaxed);
    level[(size_t) head] = lv;
    const bool bypassed = param (ids::bypass) > 0.5f;
    gainHist[(size_t) head] = bypassed ? 0.0f : m.gainDb.load (std::memory_order_relaxed);   // gate ou garde d'écho
    echoBlocked = ! bypassed && engine.getEchoGuardMeter().blocking.load (std::memory_order_relaxed);
    head = (head + 1) % kHist;

    ui::approach (thrShown, param (ids::gateThr), 0.3f);
    ui::approach (hystShown, param (ids::gateHyst), 0.3f);
    ui::approach (floorShown, m.floorDb.load (std::memory_order_relaxed), 0.05f);
    ui::approach (openLed, on && m.open.load (std::memory_order_relaxed) ? 1.0f : 0.0f, 0.25f);
    ui::approach (active, on ? 1.0f : 0.0f, 0.15f);
    ui::approach (thrGlow, (dragging || hoverThr) ? 1.0f : 0.0f, 0.2f);
    ui::approach (learnGlow, (hoverLearn || learnFrames > 0) ? 1.0f : 0.0f, 0.2f);

    // LEARN : on écoute le bruit de la pièce 1.5 s, le seuil se place 6 dB au-dessus de sa crête
    if (learnFrames > 0)
    {
        learnMin = std::max (learnMin, lv);
        if (--learnFrames == 0 && thrP != nullptr)
        {
            const float t = juce::jlimit (-80.0f, -20.0f, learnMin + 6.0f);
            thrP->beginChangeGesture();
            thrP->setValueNotifyingHost (thrP->convertTo0to1 (t));
            thrP->endChangeGesture();
            learnMessage = learnMin + 6.0f > -20.0f ? juce::String ("TOO LOUD - STAY SILENT AND RETRY")
                                                    : "THRESHOLD " + juce::String (t, 1) + " dB";
            messageFrames = 150;
        }
    }
    if (messageFrames > 0) --messageFrames;
    repaint();
}

void GateDisplay::mouseMove (const juce::MouseEvent& e)
{
    hoverLearn = learnArea().contains (e.position);
    hoverThr = ! hoverLearn && std::abs (e.position.y - yOf (thrShown)) < 8.0f * ui::editorScale (*this);
    setMouseCursor (hoverLearn ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
}

void GateDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverLearn = false;
    if (! dragging) hoverThr = false;
}

void GateDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (learnArea().contains (e.position))
    {
        learnFrames = 90;
        learnMin = -120.0f;
        messageFrames = 0;
        return;
    }
    if (thrP == nullptr) return;
    dragging = true;
    thrP->beginChangeGesture();
    thrP->setValueNotifyingHost (thrP->convertTo0to1 (juce::jlimit (-80.0f, 0.0f, dbOf (e.position.y))));
}

void GateDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging && thrP != nullptr)
        thrP->setValueNotifyingHost (thrP->convertTo0to1 (juce::jlimit (-80.0f, 0.0f, dbOf (e.position.y))));
}

void GateDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging && thrP != nullptr) thrP->endChangeGesture();
    dragging = false;
}

void GateDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (thrP == nullptr || learnArea().contains (e.position)) return;
    thrP->beginChangeGesture();
    thrP->setValueNotifyingHost (thrP->getDefaultValue());
    thrP->endChangeGesture();
}

juce::String GateDisplay::getTooltip()
{
    if (hoverLearn)
        return "LEARN: stay silent for 1.5 s - the gate measures your room noise and sets the threshold just above it.";
    return "Noise gate: the voice level scrolls from right to left. Drag up/down to set the threshold (red line); the dashed "
           "line below is where it closes again (hysteresis). The white curve on top is the gain applied (drops by RANGE when closed). Double-click = reset.";
}

void GateDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = ui::editorScale (*this);
    const float es = ui::editorScale (*this);
    const auto plot = plotArea();

    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             ui::night.withAlpha (0.85f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (area, 12.0f * u);
    g.setColour (ui::line);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f * u, std::max (1.0f, u));

    // ---- graduations
    g.setFont (ui::font (ui::Face::mono, std::max (9.5f * u, 9.5f * es)));
    for (int db = -10; db >= -70; db -= 10)
    {
        const float y = yOf ((float) db);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 1.0f));
        if (db % 20 == 0)
        {
            g.setColour (ui::haze.withAlpha (0.6f));
            g.drawText (juce::String (db), juce::Rectangle<float> (plot.getRight() + 4.0f * u, y - 6.0f * u, 26.0f * u, 12.0f * u),
                        juce::Justification::centredLeft, false);
        }
    }

    const float a = 0.45f + 0.55f * active;
    const float colW = plot.getWidth() / (float) (kHist - 1);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());

        // ---- niveau : colonnes os quand le gate est ouvert, estompées quand il ferme
        for (int i = 0; i < kHist; ++i)
        {
            const int idx = (head + i) % kHist;
            const float x = plot.getX() + (float) i * colW;
            const float y = yOf (level[(size_t) idx]);
            if (y >= plot.getBottom() - 0.5f) continue;
            const float openness = juce::jlimit (0.0f, 1.0f, 1.0f + gainHist[(size_t) idx] / 12.0f);
            g.setColour (ui::haze.withAlpha (0.18f * a).interpolatedWith (ui::bone.withAlpha (0.55f * a), openness));
            g.fillRect (juce::Rectangle<float> (x, y, colW + 0.6f, plot.getBottom() - y));
        }

        // ---- bruit de fond mesuré
        if (floorShown > kMinDb)
        {
            const float fy = yOf (floorShown);
            const float dash[] { 2.0f * u, 4.0f * u };
            g.setColour (ui::haze.withAlpha (0.5f));
            g.drawDashedLine ({ plot.getX(), fy, plot.getRight(), fy }, dash, 2, std::max (1.0f, u));
            g.setFont (ui::font (ui::Face::mono, std::max (9.0f * u, 9.0f * es), true));
            g.drawText ("ROOM " + juce::String (floorShown, 0), juce::Rectangle<float> (plot.getX() + 6.0f * u, fy + 2.0f * u, 90.0f * u, 12.0f * u),
                        juce::Justification::centredLeft, false);
        }

        // ---- courbe de gain : en haut quand le gate est ouvert, descend de RANGE quand il ferme
        juce::Path gp;
        for (int i = 0; i < kHist; ++i)
        {
            const int idx = (head + i) % kHist;
            const float x = plot.getX() + (float) i * colW;
            const float y = plot.getY() + 2.0f * u + juce::jlimit (0.0f, 1.0f, -gainHist[(size_t) idx] / 80.0f) * plot.getHeight();
            if (i == 0) gp.startNewSubPath (x, y); else gp.lineTo (x, y);
        }
        ui::strokeGlow (g, gp, ui::bone, 1.0f * u, 0.25f * active);
        g.setColour (ui::bone.withAlpha (0.25f + 0.5f * active));
        g.strokePath (gp, juce::PathStrokeType (std::max (1.2f, 1.8f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // ---- hystérésis (fermeture) puis seuil (ouverture)
        const float hy = yOf (thrShown - hystShown);
        const float dash[] { 4.0f * u, 4.0f * u };
        g.setColour (ui::ember.withAlpha (0.35f * a));
        g.drawDashedLine ({ plot.getX(), hy, plot.getRight(), hy }, dash, 2, std::max (1.0f, u));
    }

    const float ty = yOf (thrShown);
    juce::Path tl;
    tl.startNewSubPath (plot.getX(), ty);
    tl.lineTo (plot.getRight(), ty);
    ui::strokeGlow (g, tl, ui::ember, 1.4f * u, (0.4f + 0.6f * thrGlow) * a);
    g.setColour (ui::ember.withAlpha ((0.75f + 0.25f * thrGlow) * a));
    g.strokePath (tl, juce::PathStrokeType (std::max (1.2f, (1.6f + 0.6f * thrGlow) * u)));
    {
        const juce::String txt = "THR " + juce::String (thrShown, 1);
        const auto f = ui::font (ui::Face::mono, std::max (10.0f * u, 10.0f * es), true);
        const float tw = ui::textWidth (f, txt) + 14.0f * u;
        auto box = juce::Rectangle<float> (tw, 17.0f * u).withRightX (plot.getRight() - 4.0f * u).withCentre ({ plot.getRight() - 4.0f * u - tw * 0.5f, ty - 11.0f * u });
        if (box.getY() < plot.getY()) box.setY (ty + 3.0f * u);
        g.setColour (ui::night.withAlpha (0.92f));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::ember.withAlpha (0.6f + 0.4f * thrGlow));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::bone);
        g.setFont (f);
        g.drawText (txt, box, juce::Justification::centred, false);
    }

    // ---- état : OPEN / CLOSED
    {
        const float d = 8.0f * u;
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ area.getX() + 18.0f * u, area.getY() + 18.0f * u });
        const auto c = active < 0.5f ? ui::haze : ui::live.interpolatedWith (ui::ember, 1.0f - openLed);
        g.setColour (c.withAlpha (0.4f * active));
        g.fillEllipse (led.expanded (d * 0.8f));
        g.setColour (c.withAlpha (0.4f + 0.6f * active));
        g.fillEllipse (led);
        const juce::String st = echoBlocked ? "ECHO BLOCKED" : active < 0.5f ? "BYPASSED" : (openLed > 0.5f ? "OPEN" : "CLOSED");
        g.setColour (ui::mist.withAlpha (0.85f));
        g.setFont (ui::font (ui::Face::mono, std::max (11.0f * u, 10.5f * es), true, 0.1f));
        g.drawText (st, juce::Rectangle<float> (led.getRight() + 8.0f * u, led.getCentreY() - 8.0f * u, 140.0f * u, 16.0f * u),
                    juce::Justification::centredLeft, false);
    }

    // ---- LEARN
    {
        const auto r = learnArea();
        juce::Path p;
        p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        const float pulse = learnFrames > 0 ? 0.5f + 0.5f * std::sin ((float) learnFrames * 0.35f) : 0.0f;
        ui::strokeGlow (g, p, ui::ember, 1.0f * u, 0.6f * learnGlow + 0.4f * pulse);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillPath (p);
        g.setColour (ui::ember.withAlpha (0.12f * learnGlow + 0.25f * pulse));
        g.fillPath (p);
        g.setColour (ui::line.interpolatedWith (ui::ember, learnGlow));
        g.strokePath (p, juce::PathStrokeType (std::max (1.0f, u)));
        const juce::String txt = learnFrames > 0 ? "LISTENING..." : "LEARN";
        g.setColour (ui::haze.interpolatedWith (ui::bone, learnGlow));
        g.setFont (ui::fitFont (ui::Face::body, r.getHeight() * 0.48f, r.getWidth() - 12.0f * u, txt, true, 0.12f));
        g.drawText (txt, r, juce::Justification::centred, false);
    }

    if (messageFrames > 0)
    {
        const float al = juce::jlimit (0.0f, 1.0f, (float) messageFrames / 20.0f);
        const auto f = ui::font (ui::Face::mono, std::max (10.5f * u, 10.5f * es), true);
        const float tw = ui::textWidth (f, learnMessage) + 20.0f * u;
        const auto box = juce::Rectangle<float> (tw, 20.0f * u).withCentre (plot.getCentre());
        g.setColour (ui::night.withAlpha (0.92f * al));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::ember.withAlpha (al));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::bone.withAlpha (al));
        g.setFont (f);
        g.drawText (learnMessage, box, juce::Justification::centred, false);
    }
}
