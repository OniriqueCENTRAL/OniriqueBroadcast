#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
//  BROADCAST (application gratuite) n'a pas le module de licence des plugins ONIRIQUE. La barre du haut (UI/OniriqueBar.h)
//  en attend seulement les polices de la charte ; les voici, tirées des ressources de BROADCAST (les mêmes fichiers).
//==============================================================================
namespace onirique::license::theme
{

/** Unbounded (titres), Hanken Grotesk (texte), JetBrains Mono (étiquettes). */
struct FontSet
{
    FontSet()
    {
        display = juce::Typeface::createSystemTypefaceFor (BinaryData::Unbounded800_ttf, BinaryData::Unbounded800_ttfSize);
        body = juce::Typeface::createSystemTypefaceFor (BinaryData::HankenGrotesk600_ttf, BinaryData::HankenGrotesk600_ttfSize);
        mono = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMono500_ttf, BinaryData::JetBrainsMono500_ttfSize);
        monoBold = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMono600_ttf, BinaryData::JetBrainsMono600_ttfSize);
    }

    static juce::Font make (const juce::Typeface::Ptr& face, float height, float tracking)
    {
        return juce::Font (juce::FontOptions (face).withHeight (height)).withExtraKerningFactor (tracking);
    }

    juce::Font displayFont (float height, float tracking = 0.0f) const  { return make (display, height, tracking); }
    juce::Font bodyFont (float height, float tracking = 0.0f) const     { return make (body, height, tracking); }
    juce::Font monoFont (float height, float tracking = 0.0f) const     { return make (mono, height, tracking); }
    juce::Font monoBoldFont (float height, float tracking = 0.0f) const { return make (monoBold, height, tracking); }

    juce::Typeface::Ptr display, body, mono, monoBold;
};

using Fonts = juce::SharedResourcePointer<FontSet>;

} // namespace onirique::license::theme
