#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// Menus clic droit : meme design que les menus d'Octopus (repris de Octopus/MenuStyle.h) : fond sombre arrondi,
// icones devant les commandes, coche bleue, fleche des sous-menus, en-tete avec pastille + titre + sous-titre.
// Police Poppins (comme Octopus).
namespace MenuStyle
{
    using namespace juce;

    const Colour kBg (0xff13171d), kBorder (0xff262c36), kHover (0xff1c222b), kSep (0xff20252e),
                 kText (0xffeef1f5), kDim (0xff8d96a4), kFaint (0xff3a414d), kAccent (0xff4fc3f7), kTileBg (0xff0e1115), kTileLine (0xff2a303a);

    struct Fonts { Typeface::Ptr reg, med, bold; };
    inline Font fnt (const Typeface::Ptr& tf, float h) { return Font (FontOptions (tf).withHeight (h)); }

    // ------------------------------------------------------------------ instance partagee (polices Poppins integrees)
    inline Fonts& fonts()
    {
        static Fonts f { Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf, BinaryData::PoppinsRegular_ttfSize),
                         Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize),
                         Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize) };
        return f;
    }

    // petites icônes des commandes, dessinées sur une grille de 16 (choisies d'après le texte de la commande)
    enum Ico { icoNone = 0, icoSave, icoLoad, icoStomp, icoClear, icoMidi, icoLatch, icoColour, icoAdd, icoBanks, icoSize, icoParams,
               icoWindow, icoSplit, icoMove, icoRemove, icoBypass, icoMix, icoSwap, icoPreset };

    inline int iconFor (const String& t)
    {
        const auto s = t.toLowerCase();
        if (s.startsWith ("save")) return icoSave;
        if (s.startsWith ("load")) return icoLoad;
        if (s.contains ("clear") || s.startsWith ("reset") || s.startsWith ("cancel")) return icoClear;
        if (s.startsWith ("stomp") || s.startsWith ("assign") || s.startsWith ("footswitch")) return icoStomp;
        if (s.contains ("latching")) return icoLatch;
        if (s.startsWith ("midi") || s.startsWith ("learn") || s.startsWith ("program change")) return icoMidi;
        if (s.contains ("color") || s.contains ("colour") || s.contains ("glow")) return icoColour;
        if (s.startsWith ("add") || s.startsWith ("insert")) return icoAdd;
        if (s.startsWith ("bank")) return icoBanks;
        if (s.contains ("size") || s.endsWith ("%")) return icoSize;
        if (s.startsWith ("parameters") || s.startsWith ("edit")) return icoParams;
        if (s.contains ("window")) return icoWindow;
        if (s.startsWith ("split") || s.contains ("path c") || s.contains ("parallel")) return icoSplit;
        if (s.startsWith ("move")) return icoMove;
        if (s.startsWith ("remove") || s.startsWith ("delete") || s.startsWith ("unsplit")) return icoRemove;
        if (s.contains ("bypass") || s.startsWith ("turn") || s.startsWith ("on ") || s.startsWith ("switch")) return icoBypass;
        if (s.startsWith ("mixer") || s.startsWith ("mix")) return icoMix;
        if (s.contains ("a <-> b") || s.contains ("swap")) return icoSwap;
        if (s.startsWith ("preset") || s.startsWith ("rename") || s.startsWith ("user preset")) return icoPreset;
        return icoNone;
    }

    inline void drawIco (Graphics& g, int ico, Rectangle<float> r, Colour c)
    {
        if (ico == icoNone) return;
        const float k = r.getWidth() / 16.0f;
        auto P = [&] (float x, float y) { return Point<float> (r.getX() + x * k, r.getY() + y * k); };
        Path p;
        auto line = [&] (float x1, float y1, float x2, float y2) { p.startNewSubPath (P (x1, y1)); p.lineTo (P (x2, y2)); };
        auto circ = [&] (float cx, float cy, float rad) { p.addEllipse (P (cx - rad, cy - rad).x, P (cx - rad, cy - rad).y, rad * 2 * k, rad * 2 * k); };
        auto rect = [&] (float x, float y, float w, float h, float rr) { p.addRoundedRectangle (P (x, y).x, P (x, y).y, w * k, h * k, rr * k); };
        bool fillDots = false;
        switch (ico)
        {
            case icoSave:   p.startNewSubPath (P (2.5f, 2.5f)); p.lineTo (P (11, 2.5f)); p.lineTo (P (13.5f, 5)); p.lineTo (P (13.5f, 13.5f)); p.lineTo (P (2.5f, 13.5f)); p.closeSubPath();
                            p.startNewSubPath (P (5, 2.5f)); p.lineTo (P (5, 6)); p.lineTo (P (10, 6)); p.lineTo (P (10, 2.5f)); break;
            case icoLoad:   line (8, 2, 8, 10); line (4.5f, 6.5f, 8, 10); line (11.5f, 6.5f, 8, 10); line (3, 13.5f, 13, 13.5f); break;
            case icoStomp:  circ (8, 8, 5.5f); circ (8, 8, 2.0f); break;
            case icoClear:  line (4, 4, 12, 12); line (12, 4, 4, 12); break;
            case icoMidi:   circ (8, 8, 6.0f); fillDots = true; break;
            case icoLatch:  p.startNewSubPath (P (2, 11)); p.lineTo (P (5, 11)); p.lineTo (P (5, 5)); p.lineTo (P (8, 5)); p.lineTo (P (8, 11)); p.lineTo (P (11, 11)); p.lineTo (P (11, 5)); p.lineTo (P (14, 5)); break;
            case icoColour: circ (8, 8, 6.0f); break;
            case icoAdd:    line (8, 3, 8, 13); line (3, 8, 13, 8); break;
            case icoBanks:  rect (2.5f, 3, 11, 3, 1); rect (2.5f, 7, 11, 3, 1); rect (2.5f, 11, 11, 3, 1); break;
            case icoSize:   line (3, 3, 13, 13); line (3, 3, 3, 7); line (3, 3, 7, 3); line (13, 13, 13, 9); line (13, 13, 9, 13); break;
            case icoParams: line (3, 4, 13, 4); line (3, 8, 13, 8); line (3, 12, 13, 12); circ (6, 4, 1.4f); circ (10, 8, 1.4f); circ (5, 12, 1.4f); break;
            case icoWindow: rect (2, 3, 12, 10, 1.5f); line (2, 6, 14, 6); break;
            case icoSplit:  line (2, 8, 5, 8); line (11, 8, 14, 8); p.startNewSubPath (P (5, 8)); p.quadraticTo (P (8, 3), P (11, 8)); p.startNewSubPath (P (5, 8)); p.quadraticTo (P (8, 13), P (11, 8)); break;
            case icoMove:   line (8, 2, 8, 14); line (5, 5, 8, 2); line (11, 5, 8, 2); line (5, 11, 8, 14); line (11, 11, 8, 14); break;
            case icoRemove: line (3, 4.5f, 13, 4.5f); p.startNewSubPath (P (4.5f, 4.5f)); p.lineTo (P (5.5f, 13.5f)); p.lineTo (P (10.5f, 13.5f)); p.lineTo (P (11.5f, 4.5f)); line (6.5f, 2.5f, 9.5f, 2.5f); break;
            case icoBypass: p.addCentredArc (P (8, 8.5f).x, P (8, 8.5f).y, 5.5f * k, 5.5f * k, 0.0f, 0.75f, MathConstants<float>::twoPi - 0.75f, true); line (8, 2, 8, 8); break;
            case icoMix:    line (4, 2.5f, 4, 13.5f); line (8, 2.5f, 8, 13.5f); line (12, 2.5f, 12, 13.5f); rect (2.5f, 9, 3, 2, 0.5f); rect (6.5f, 5, 3, 2, 0.5f); rect (10.5f, 10, 3, 2, 0.5f); break;
            case icoSwap:   line (3, 5.5f, 13, 5.5f); line (10, 2.5f, 13, 5.5f); line (13, 10.5f, 3, 10.5f); line (6, 13.5f, 3, 10.5f); break;
            case icoPreset: rect (3, 2.5f, 10, 11, 1.5f); line (5.5f, 6, 10.5f, 6); line (5.5f, 9, 9, 9); break;
            default: break;
        }
        g.setColour (c);
        g.strokePath (p, PathStrokeType (1.5f * k, PathStrokeType::curved, PathStrokeType::rounded));
        if (ico == icoColour)
        {
            Path h; h.addPieSegment (P (2, 2).x, P (2, 2).y, 12 * k, 12 * k, MathConstants<float>::pi, MathConstants<float>::twoPi, 0.0f);
            g.fillPath (h);
        }
        if (fillDots)
            for (auto d : { Point<float> (5, 7), Point<float> (11, 7), Point<float> (6.3f, 10.3f), Point<float> (9.7f, 10.3f), Point<float> (8, 4.7f) })
                g.fillEllipse (Rectangle<float> (1.8f * k, 1.8f * k).withCentre (P (d.x, d.y)));
    }

    inline void drawCheck (Graphics& g, Rectangle<float> r, Colour c, float w)
    {
        Path p;
        p.startNewSubPath (r.getX() + r.getWidth() * 0.18f, r.getY() + r.getHeight() * 0.52f);
        p.lineTo (r.getX() + r.getWidth() * 0.42f, r.getY() + r.getHeight() * 0.76f);
        p.lineTo (r.getX() + r.getWidth() * 0.84f, r.getY() + r.getHeight() * 0.26f);
        g.setColour (c);
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ------------------------------------------------------------------ apparence de tous les menus
    struct MenuLnf : public LookAndFeel_V4
    {
        Fonts f;
        MenuLnf()
        {
            f = fonts();
            setColour (PopupMenu::backgroundColourId, Colours::transparentBlack);       // fenêtre transparente : coins arrondis
            setColour (PopupMenu::textColourId, kText);
            setColour (PopupMenu::headerTextColourId, kDim);
            setColour (PopupMenu::highlightedBackgroundColourId, kHover);
            setColour (PopupMenu::highlightedTextColourId, Colours::white);
        }
        Font getPopupMenuFont() override { return fnt (f.med, 15.0f); }
        // champs de texte d'Octopus : fond et cadre aux coins arrondis, comme le reste de l'interface
        void fillTextEditorBackground (Graphics& g, int w, int h, TextEditor& ed) override
        {
            const auto bg = ed.getProperties()["roundBg"];
            if (bg.isVoid()) { LookAndFeel_V4::fillTextEditorBackground (g, w, h, ed); return; }
            g.setColour (Colour ((uint32) (int) bg));
            g.fillRoundedRectangle (Rectangle<float> ((float) w, (float) h), jmin (10.0f, (float) h * 0.2f));
        }
        void drawTextEditorOutline (Graphics& g, int w, int h, TextEditor& ed) override
        {
            if (ed.getProperties()["roundBg"].isVoid()) { LookAndFeel_V4::drawTextEditorOutline (g, w, h, ed); return; }
            if (! ed.isEnabled()) return;
            const bool foc = ed.hasKeyboardFocus (true) && ! ed.isReadOnly();
            const float t = foc ? 1.6f : 1.1f;
            g.setColour (ed.findColour (foc ? TextEditor::focusedOutlineColourId : TextEditor::outlineColourId));
            g.drawRoundedRectangle (Rectangle<float> ((float) w, (float) h).reduced (t * 0.5f), jmin (10.0f, (float) h * 0.2f), t);
        }
        int getPopupMenuBorderSize() override { return 7; }
        void drawPopupMenuBackground (Graphics& g, int w, int h) override
        {
            const auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
            g.setColour (kBg);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (kBorder);
            g.drawRoundedRectangle (r, 10.0f, 1.0f);
        }
        void getIdealPopupMenuItemSize (const String& text, bool isSeparator, int standardHeight, int& w, int& h) override
        {
            if (isSeparator) { w = 50; h = 11; return; }
            h = standardHeight > 0 ? standardHeight : 32;
            w = (int) std::ceil (GlyphArrangement::getStringWidth (getPopupMenuFont(), text)) + 34 + 40;
        }
        void drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name) override
        {
            g.setFont (fnt (f.bold, 11.5f).withExtraKerningFactor (0.14f));
            g.setColour (kDim);
            g.drawFittedText (name.toUpperCase(), area.withTrimmedLeft (14).withTrimmedRight (10).withTrimmedTop (6), Justification::centredLeft, 1);
        }
        void drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                bool hasSubMenu, const String& text, const String& shortcut, const Drawable* icon, const Colour* textColour) override
        {
            ignoreUnused (shortcut, icon);
            const auto a = area.toFloat();
            if (isSeparator)
            {
                g.setColour (kSep);
                g.fillRect (a.withSizeKeepingCentre (a.getWidth() - 20.0f, 1.0f));
                return;
            }
            const bool hi = isHighlighted && isActive;
            if (hi)
            {
                g.setColour (kHover);
                g.fillRoundedRectangle (a.reduced (5.0f, 1.0f), 7.0f);
            }
            const auto ib = Rectangle<float> (15.0f, 15.0f).withCentre ({ a.getX() + 22.0f, a.getCentreY() });
            if (isTicked) drawCheck (g, ib.expanded (1.0f), kAccent, 2.0f);
            else drawIco (g, iconFor (text), ib, ! isActive ? kFaint : hi ? kAccent : kDim);
            auto tr = a.withTrimmedLeft (38.0f).withTrimmedRight (hasSubMenu ? 30.0f : 14.0f);
            g.setFont (getPopupMenuFont());
            g.setColour (textColour != nullptr ? *textColour : ! isActive ? kDim.withAlpha (0.55f) : hi ? Colours::white : kText);
            g.drawFittedText (text, tr.toNearestInt(), Justification::centredLeft, 1, 0.9f);
            if (hasSubMenu)                                                           // ›
            {
                const float cx = a.getRight() - 18.0f, cy = a.getCentreY();
                Path p; p.startNewSubPath (cx - 2.5f, cy - 5.0f); p.lineTo (cx + 2.5f, cy); p.lineTo (cx - 2.5f, cy + 5.0f);
                g.setColour (hi ? Colours::white : kDim);
                g.strokePath (p, PathStrokeType (1.7f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
    };

    // ------------------------------------------------------------------ en-tête des menus : pastille + titre + sous-titre
    struct Badge
    {
        int ico = icoNone; Colour col { kAccent };
        static Badge ofUi (int ico, Colour c = kAccent) { Badge b; b.ico = ico; b.col = c; return b; }
    };

    struct Header : public PopupMenu::CustomComponent
    {
        Fonts f; Badge badge; String title, sub;
        Header (Fonts fo, Badge b, String t, String s) : CustomComponent (false), f (fo), badge (b), title (t), sub (s) {}
        void getIdealSize (int& w, int& h) override
        {
            w = jmax (240, (int) std::ceil (jmax (GlyphArrangement::getStringWidth (fnt (f.bold, 15.5f), title), GlyphArrangement::getStringWidth (fnt (f.reg, 12.5f), sub))) + 72);
            h = 52;
        }
        void paint (Graphics& g) override
        {
            const auto b = Rectangle<float> (30.0f, 30.0f).withPosition (12.0f, 10.0f);
            g.setColour (badge.col.withAlpha (0.16f));
            g.fillRoundedRectangle (b, 8.0f);
            g.setColour (badge.col.withAlpha (0.7f));
            g.drawRoundedRectangle (b.reduced (0.6f), 8.0f, 1.2f);
            drawIco (g, badge.ico, b.reduced (7.0f), badge.col);
            g.setColour (kText);
            g.setFont (fnt (f.bold, 15.5f));
            g.drawText (title, Rectangle<float> (52.0f, sub.isEmpty() ? 15.0f : 8.0f, (float) getWidth() - 60.0f, 19.0f), Justification::centredLeft, true);
            if (sub.isNotEmpty())
            {
                g.setColour (kDim);
                g.setFont (fnt (f.reg, 12.5f));
                g.drawText (sub, Rectangle<float> (52.0f, 27.0f, (float) getWidth() - 60.0f, 16.0f), Justification::centredLeft, true);
            }
        }
    };

    // menu au style Octopus (la LookAndFeel appartient a l'editeur)
    inline PopupMenu& styled (PopupMenu& m, LookAndFeel* l) { if (l != nullptr) m.setLookAndFeel (l); return m; }
    inline void header (PopupMenu& m, int ico, const String& title, const String& sub = {})
    {
        m.addCustomItem (9001, std::make_unique<Header> (fonts(), Badge::ofUi (ico), title, sub), nullptr, title);
        m.addSeparator();
    }
}
