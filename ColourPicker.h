#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "WigaWindowSize.h"

// ============================================================================
// Reglages communs a toutes les fenetres d'Ampire (fichier settings.xml de WigaWindowSize) :
// couleur d'accent des nouvelles fenetres et rangee « MY COLOURS » du nuancier.
// ============================================================================
namespace AmpireSettings
{
    constexpr juce::uint32 kDefaultAccent = 0xffe9e7e1;   // blanc chaud (afficheurs d'Ampire)

    inline std::unique_ptr<juce::XmlElement> load (const juce::String& plugin)
    {
        const auto f = WigaWindowSize::file (plugin);
        if (f.existsAsFile()) if (auto x = juce::XmlDocument::parse (f)) return x;
        return std::make_unique<juce::XmlElement> ("StudioWigaSettings");
    }
    inline void save (const juce::String& plugin, const juce::XmlElement& x)
    {
        const auto f = WigaWindowSize::file (plugin);
        f.getParentDirectory().createDirectory();
        x.writeTo (f);
    }
    inline juce::uint32 accent (const juce::String& plugin)
    {
        const auto x = load (plugin);
        const auto s = x->getStringAttribute ("accent");
        return s.isNotEmpty() ? (juce::uint32) s.getHexValue64() | 0xff000000u : kDefaultAccent;
    }
    inline void setAccent (const juce::String& plugin, juce::uint32 c)
    {
        auto x = load (plugin);
        x->setAttribute ("accent", juce::String::toHexString ((juce::int64) c));
        save (plugin, *x);
    }
    // couleurs des vu-metres : LEDs et 3 LEDs de saturation du haut (communes a toutes les fenetres)
    constexpr juce::uint32 kDefaultMeter = 0xffff8014, kDefaultSaturation = 0xffff2e0d;
    inline juce::uint32 colourAttr (const juce::String& plugin, const char* name, juce::uint32 def)
    {
        const auto s = load (plugin)->getStringAttribute (name);
        return s.isNotEmpty() ? (juce::uint32) s.getHexValue64() | 0xff000000u : def;
    }
    inline void setColourAttr (const juce::String& plugin, const char* name, juce::uint32 c)
    {
        auto x = load (plugin);
        x->setAttribute (name, juce::String::toHexString ((juce::int64) c));
        save (plugin, *x);
    }
    inline juce::uint32 meterColour (const juce::String& plugin)      { return colourAttr (plugin, "meterColour", kDefaultMeter); }
    inline juce::uint32 saturationColour (const juce::String& plugin) { return colourAttr (plugin, "saturationColour", kDefaultSaturation); }
    // dossier des IR par defaut (vide = dernier dossier utilise), commun a toutes les fenetres
    inline juce::File irFolder (const juce::String& plugin)
    {
        const auto p = load (plugin)->getStringAttribute ("irFolder");
        return p.isNotEmpty() && juce::File::isAbsolutePath (p) ? juce::File (p) : juce::File();
    }
    inline void setIrFolder (const juce::String& plugin, const juce::File& f)
    {
        auto x = load (plugin);
        x->setAttribute ("irFolder", f == juce::File() ? juce::String() : f.getFullPathName());
        save (plugin, *x);
    }
    inline juce::Array<juce::uint32> userColours (const juce::String& plugin)
    {
        juce::Array<juce::uint32> out;
        out.insertMultiple (0, 0u, 11);
        auto t = juce::StringArray::fromTokens (load (plugin)->getStringAttribute ("userColours"), ",", "");
        for (int i = 0; i < juce::jmin (11, t.size()); ++i) out.set (i, (juce::uint32) t[i].getHexValue64());
        return out;
    }
    inline void setUserColours (const juce::String& plugin, const juce::Array<juce::uint32>& c)
    {
        auto x = load (plugin);
        juce::StringArray t;
        for (auto v : c) t.add (juce::String::toHexString ((juce::int64) v));
        x->setAttribute ("userColours", t.joinIntoString (","));
        save (plugin, *x);
    }
}

// ============================================================================
// Nuancier (repris d'Octopus) : couleurs rapides, rangee « MY COLOURS » (clic sur un emplacement vide =
// enregistre la couleur, clic droit = efface), teinte / saturation / luminosite, code hexadecimal a taper.
// La couleur s'applique en direct ; OK ferme ; « Default » rend le bleu d'origine.
// ============================================================================
class WigaColourPicker : public juce::Component
{
public:
    WigaColourPicker (const juce::String& titleText, juce::Colour start, float scale, const juce::String& pluginName)
        : title (titleText), k (scale), plugin (pluginName)
    {
        user = AmpireSettings::userColours (plugin);
        setHsv (start);
        setSize (juce::roundToInt (kW * k), juce::roundToInt (kH * k));

        hex.setFont (juce::FontOptions (12.5f * k, juce::Font::bold));
        hex.setJustification (juce::Justification::centred);
        hex.setInputRestrictions (7, "#0123456789abcdefABCDEF");
        hex.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1a1e26));
        hex.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3a414d));
        hex.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff8d96a4));
        hex.setColour (juce::TextEditor::textColourId, juce::Colour (0xffeef1f5));
        hex.setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
        hex.setTooltip ("Type a colour code, e.g. #172633");
        // le code n'est relu que s'il a ete tape (sinon OK remettait l'ancienne couleur : la case garde le
        // focus clavier a l'ouverture et son texte n'etait plus mis a jour quand on choisissait une couleur)
        hex.onTextChange = [this] { hexEdited = true; };
        hex.onReturnKey = [this] { applyHex(); };
        hex.onFocusLost = [this] { applyHex(); };
        hex.setWantsKeyboardFocus (true);
        hex.setExplicitFocusOrder (0);
        addAndMakeVisible (hex);
        hex.setBounds (R (hexRect()).toNearestInt());
        updateHex();
    }

    std::function<void (juce::Colour)> onChange;
    std::function<void()> onDefault;
    std::function<void()> onClose;
    std::function<void (juce::Colour)> onCommit;   // OK / Default : couleur finale (ecriture des reglages une seule fois)
    juce::Colour colour() const { return juce::Colour::fromHSV (hue, sat, bri, 1.0f); }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        g.setColour (Colour (0xff10131a));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f * k);
        g.setColour (Colour (0xff3a414d));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f * k, 1.0f);

        g.setColour (Colour (0xffeef1f5));
        g.setFont (FontOptions (13.5f * k, Font::bold));
        g.drawText (title.toUpperCase(), R (14, 10, kW - 28, 18), Justification::centredLeft, true);

        for (int i = 0; i < kNumQuick; ++i)
        {
            const auto c = R (swatch (i));
            g.setColour (Colour (kQuick[i]));
            g.fillEllipse (c);
            const bool sel = colour().getARGB() == kQuick[i];
            g.setColour (sel ? Colours::white : Colours::white.withAlpha (0.18f));
            g.drawEllipse (c.expanded (sel ? 2.0f * k : 0.0f), sel ? 1.8f * k : 1.0f * k);
        }

        g.setColour (Colour (0xff8d96a4));
        g.setFont (FontOptions (10.5f * k, Font::bold));
        g.drawText ("MY COLOURS", R (16, 63, 140, 13), Justification::centredLeft);
        g.drawText ("click empty : save  -  right-click : remove", R (110, 63, kW - 126, 13), Justification::centredRight);
        for (int i = 0; i < kNumQuick; ++i)
        {
            const auto c = R (userSlot (i));
            const auto v = user[i];
            if (v != 0)
            {
                g.setColour (Colour (v));
                g.fillEllipse (c);
                const bool sel = colour().getARGB() == v;
                g.setColour (sel ? Colours::white : Colours::white.withAlpha (0.18f));
                g.drawEllipse (c.expanded (sel ? 2.0f * k : 0.0f), sel ? 1.8f * k : 1.0f * k);
                continue;
            }
            Path ring; ring.addEllipse (c.reduced (0.6f * k));
            Path dashed;
            const float dash[] = { 2.2f * k, 2.2f * k };
            PathStrokeType (1.0f * k).createDashedStroke (dashed, ring, dash, 2);
            g.setColour (hoverUser == i ? Colours::white.withAlpha (0.7f) : Colour (0xff59616e));
            g.fillPath (dashed);
            if (hoverUser == i)
            {
                g.setFont (FontOptions (14.0f * k, Font::bold));
                g.drawText ("+", c.translated (0.0f, -0.5f * k), Justification::centred);
            }
        }

        const char* names[3] = { "HUE", "SATURATION", "BRIGHTNESS" };
        const float vals[3] = { hue, sat, bri };
        for (int b = 0; b < 3; ++b)
        {
            const auto br = R (bar (b));
            g.setColour (Colour (0xff8d96a4));
            g.setFont (FontOptions (10.5f * k, Font::bold));
            g.drawText (names[b], R (bar (b).translated (0.0f, -15.0f).withHeight (13.0f)), Justification::centredLeft);
            g.drawText (String (roundToInt (vals[b] * (b == 0 ? 360.0f : 100.0f))) + (b == 0 ? String (CharPointer_UTF8 ("\xc2\xb0")) : String (" %")),
                        R (bar (b).translated (0.0f, -15.0f).withHeight (13.0f)), Justification::centredRight);
            Path clip; clip.addRoundedRectangle (br, br.getHeight() * 0.5f);
            g.saveState();
            g.reduceClipRegion (clip);
            if (b == 0)
            {
                ColourGradient cg (Colour::fromHSV (0.0f, 1.0f, 1.0f, 1.0f), br.getX(), 0.0f, Colour::fromHSV (0.9999f, 1.0f, 1.0f, 1.0f), br.getRight(), 0.0f, false);
                for (int i = 1; i < 12; ++i) cg.addColour ((double) i / 12.0, Colour::fromHSV ((float) i / 12.0f, 1.0f, 1.0f, 1.0f));
                g.setGradientFill (cg);
            }
            else if (b == 1)
                g.setGradientFill (ColourGradient (Colour::fromHSV (hue, 0.0f, jmax (0.35f, bri), 1.0f), br.getX(), 0.0f,
                                                   Colour::fromHSV (hue, 1.0f, jmax (0.35f, bri), 1.0f), br.getRight(), 0.0f, false));
            else
                g.setGradientFill (ColourGradient (Colours::black, br.getX(), 0.0f, Colour::fromHSV (hue, sat, 1.0f, 1.0f), br.getRight(), 0.0f, false));
            g.fillRect (br);
            g.restoreState();
            g.setColour (Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (br, br.getHeight() * 0.5f, 1.0f);
            const float x = br.getX() + vals[b] * br.getWidth();
            const auto knob = Rectangle<float> (br.getHeight() + 4.0f * k, br.getHeight() + 4.0f * k).withCentre ({ jlimit (br.getX() + 4.0f * k, br.getRight() - 4.0f * k, x), br.getCentreY() });
            g.setColour (b == 0 ? Colour::fromHSV (hue, 1.0f, 1.0f, 1.0f) : b == 1 ? Colour::fromHSV (hue, sat, jmax (0.35f, bri), 1.0f) : colour());
            g.fillEllipse (knob);
            g.setColour (Colours::white);
            g.drawEllipse (knob, 2.0f * k);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.drawEllipse (knob.expanded (1.2f * k), 1.0f * k);
        }

        // apercu (le code hexadecimal se tape dans la case a cote)
        const auto pv = R (previewRect());
        g.setColour (colour());
        g.fillRoundedRectangle (pv, 6.0f * k);
        g.setColour (Colours::white.withAlpha (0.2f));
        g.drawRoundedRectangle (pv, 6.0f * k, 1.0f);
        button (g, defaultRect(), "DEFAULT", false);
        button (g, okRect(), "OK", true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position / k;
        drag = -1;
        for (int i = 0; i < kNumQuick; ++i)
            if (swatch (i).expanded (3.0f).contains (p)) { setHsv (juce::Colour (kQuick[i])); changed(); return; }
        for (int i = 0; i < kNumQuick; ++i)
            if (userSlot (i).expanded (3.0f).contains (p))
            {
                if (e.mods.isPopupMenu()) { if (user[i] != 0) { user.set (i, 0); AmpireSettings::setUserColours (plugin, user); repaint(); } }
                else if (user[i] == 0) { user.set (i, colour().getARGB()); AmpireSettings::setUserColours (plugin, user); repaint(); }
                else { setHsv (juce::Colour (user[i])); changed(); }
                return;
            }
        for (int b = 0; b < 3; ++b)
            if (bar (b).expanded (6.0f, 8.0f).contains (p)) { drag = b; mouseDrag (e); return; }
        if (defaultRect().contains (p)) { if (onDefault) onDefault(); close(); return; }
        if (okRect().contains (p)) close();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        int h = -1;
        for (int i = 0; i < kNumQuick; ++i) if (user[i] == 0 && userSlot (i).expanded (3.0f).contains (e.position / k)) h = i;
        if (h != hoverUser) { hoverUser = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { if (hoverUser >= 0) { hoverUser = -1; repaint(); } }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const auto br = bar (drag);
        const float v = juce::jlimit (0.0f, 1.0f, (e.position.x / k - br.getX()) / br.getWidth());
        if (drag == 0) hue = juce::jmin (v, 0.9999f); else if (drag == 1) sat = v; else bri = v;
        changed();
    }

private:
    static constexpr float kW = 320.0f, kH = 278.0f;
    static constexpr int kNumQuick = 11;
    static constexpr juce::uint32 kQuick[kNumQuick] = { 0xff4fc3f7, 0xff3ce0e8, 0xff45e070, 0xffb6e83a, 0xffffe23a, 0xffeba947,
                                                       0xffff7a2e, 0xffff4040, 0xffff5fb4, 0xffb07aff, 0xfff2f4f8 };
    juce::String title;
    float k = 1.0f, hue = 0.0f, sat = 1.0f, bri = 1.0f;
    juce::String plugin;
    int drag = -1, hoverUser = -1;
    juce::Array<juce::uint32> user;
    juce::TextEditor hex;
    bool hexEdited = false;

    juce::Rectangle<float> R (float x, float y, float w, float h) const { return { x * k, y * k, w * k, h * k }; }
    juce::Rectangle<float> R (juce::Rectangle<float> u) const { return u * k; }
    static juce::Rectangle<float> swatch (int i) { return { 16.0f + 27.0f * (float) i, 36.0f, 20.0f, 20.0f }; }
    static juce::Rectangle<float> userSlot (int i) { return { 16.0f + 27.0f * (float) i, 80.0f, 20.0f, 20.0f }; }
    static juce::Rectangle<float> bar (int b) { return { 16.0f, 128.0f + 34.0f * (float) b, kW - 32.0f, 12.0f }; }
    static juce::Rectangle<float> previewRect() { return { 14.0f, 230.0f, 34.0f, 34.0f }; }
    static juce::Rectangle<float> hexRect() { return { 54.0f, 230.0f, 80.0f, 34.0f }; }
    static juce::Rectangle<float> okRect() { return { kW - 14.0f - 62.0f, 230.0f, 62.0f, 34.0f }; }
    static juce::Rectangle<float> defaultRect() { return { kW - 14.0f - 62.0f - 8.0f - 90.0f, 230.0f, 90.0f, 34.0f }; }

    void button (juce::Graphics& g, juce::Rectangle<float> u, const juce::String& t, bool primary) const
    {
        using namespace juce;
        const auto r = R (u);
        g.setColour (primary ? Colour (0xff1d2a36) : Colour (0xff1a1e26));
        g.fillRoundedRectangle (r, 6.0f * k);
        g.setColour (primary ? Colour (0xff4fc3f7) : Colour (0xff3a414d));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f * k, 1.2f * k);
        g.setColour (primary ? Colour (0xffeef1f5) : Colour (0xff8d96a4));
        g.setFont (FontOptions (12.5f * k, Font::bold));
        g.drawText (t, r, Justification::centred);
    }

    void setHsv (juce::Colour c)
    {
        float h = 0, s = 0, v = 0;
        c.getHSB (h, s, v);
        if (s > 0.001f) hue = h;
        sat = s; bri = v;
    }
    void updateHex() { hex.setText ("#" + colour().toDisplayString (false), false); hexEdited = false; }
    void applyHex()
    {
        if (! hexEdited) return;
        hexEdited = false;
        auto t = hex.getText().trim().removeCharacters ("#");
        if (t.length() == 3) t = juce::String::charToString (t[0]) + t[0] + t[1] + t[1] + t[2] + t[2];
        if (t.length() == 6) { setHsv (juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) t.getHexValue32()))); if (onChange) onChange (colour()); repaint(); }
        hex.setText ("#" + colour().toDisplayString (false), false);
    }
    void changed() { if (onChange) onChange (colour()); updateHex(); repaint(); }
    void close() { applyHex(); if (onCommit) onCommit (colour()); if (onClose) onClose(); }
};

// calque pose sur toute la fenetre : legerement assombri, le nuancier pres de ce qu'il colore ; un clic a cote ferme
class WigaPickerLayer : public juce::Component
{
public:
    WigaPickerLayer (std::unique_ptr<WigaColourPicker> p, juce::Rectangle<int> near, juce::Rectangle<int> bounds)
        : picker (std::move (p))
    {
        setBounds (bounds);
        addAndMakeVisible (*picker);
        auto r = picker->getBounds().withPosition (near.getCentreX() - picker->getWidth() / 2, near.getBottom() + 8);
        if (r.getBottom() > getHeight() - 6) r.setY (near.getY() - 8 - r.getHeight());
        r.setX (juce::jlimit (6, juce::jmax (6, getWidth() - r.getWidth() - 6), r.getX()));
        r.setY (juce::jlimit (6, juce::jmax (6, getHeight() - r.getHeight() - 6), r.getY()));
        picker->setBounds (r);
        juce::Component::SafePointer<WigaPickerLayer> sp (this);
        picker->onClose = [sp] { if (sp != nullptr) sp->dismiss(); };
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.28f));
        const auto r = picker->getBounds().toFloat();
        for (int i = 1; i <= 6; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.07f));
            g.fillRoundedRectangle (r.expanded ((float) i * 2.0f).translated (0.0f, 3.0f), 10.0f + (float) i * 2.0f);
        }
    }
    void mouseDown (const juce::MouseEvent&) override { dismiss(); }
    void dismiss()
    {
        setVisible (false);
        juce::Component::SafePointer<WigaPickerLayer> sp (this);
        juce::MessageManager::callAsync ([sp] { if (sp != nullptr) delete sp.getComponent(); });
    }
private:
    std::unique_ptr<WigaColourPicker> picker;
};
