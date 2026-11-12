// ============================================================================
// Bibliotheque de captures (design valide, nov. 2026) : fenetre en verre fume au-dessus d'Ampire, style des menus Octopus
// (Poppins, accent bleu clair). Deux onglets :
//   Library  : recherche + 3 colonnes (dossiers, sous-dossiers, captures), une barre de defilement par colonne
//   TONE3000 : ecran de partenariat puis cartes des tones (Favorites / Synced / Created / Trending / Latest) qui defilent
// Toutes les mesures sont celles de la maquette (1494 x 598 pour la fenetre 60 %), converties au dessin.
// Inclus par PluginEditor.cpp (utilise AmpireEditor, menuLookFor, toInt).
// ============================================================================
#include "Tone3000.h"
#include <optional>

namespace lv
{
    using namespace juce;
    constexpr float kMw = 1494.0f, kMh = 598.0f;                     // repere de la maquette
    const Colour BG (0xff13171d), BORDER (0xff262c36), HOVER (0xff1c222b), SEP (0xff20252e), TXT (0xffeef1f5),
                 DIM (0xff8d96a4), FAINT (0xff3a414d), ACC (0xff4fc3f7), TILE (0xff0e1115), TLINE (0xff2a303a),
                 GOLD (0xffffc446), GREEN (0xff6ed68c), RED (0xffe86a5a), INK (0xff0a141c);
    const Colour cAmp (0xffff8a4c), cPedal (0xff78d678), cRig (0xff4fc3f7), cCab (0xffba8cff);
    inline Colour tint (Colour c, float a, Colour bg = BG) { return bg.interpolatedWith (c, a); }
    inline Font pop (int w, float px)
    {
        auto& f = MenuStyle::fonts();
        return Font (FontOptions (w == 0 ? f.reg : w == 1 ? f.med : f.bold).withPointHeight (px));
    }
    inline Rectangle<float> box (float x0, float y0, float x1, float y1) { return { x0, y0, x1 - x0, y1 - y0 }; }
    inline void rr (Graphics& g, Rectangle<float> b, float r, std::optional<Colour> fill, std::optional<Colour> line = {}, float w = 2.0f)
    {
        if (fill) { g.setColour (*fill); g.fillRoundedRectangle (b, r); }
        if (line) { g.setColour (*line); g.drawRoundedRectangle (b.reduced (w * 0.5f), juce::jmax (0.0f, r - w * 0.5f), w); }
    }
    // texte : ancre a gauche / centre / a droite, centre verticalement sur cy
    inline void text (Graphics& g, const String& s, float x, float cy, const Font& f, Colour c, int align = 0, float maxW = 2000.0f)
    {
        g.setFont (f); g.setColour (c);
        const float h = f.getHeight() + 4.0f;
        Rectangle<float> r = align == 0 ? Rectangle<float> (x, cy - h * 0.5f, maxW, h)
                           : align == 1 ? Rectangle<float> (x - maxW * 0.5f, cy - h * 0.5f, maxW, h)
                                        : Rectangle<float> (x - maxW, cy - h * 0.5f, maxW, h);
        g.drawText (s, r, align == 0 ? Justification::centredLeft : align == 1 ? Justification::centred : Justification::centredRight, true);
    }
    inline String fitText (const String& s, const Font& f, float w)
    {
        if (GlyphArrangement::getStringWidth (f, s) <= w) return s;
        auto t = s;
        while (t.isNotEmpty() && GlyphArrangement::getStringWidth (f, t + String::fromUTF8 ("\xe2\x80\xa6")) > w) t = t.dropLastCharacters (1);
        return t.trimEnd() + String::fromUTF8 ("\xe2\x80\xa6");
    }
    inline float textW (const String& s, const Font& f) { return GlyphArrangement::getStringWidth (f, s); }

    // ---- icones (grille de 16, comme les icones des menus Octopus)
    enum Ico { iFolder, iSearch, iStar, iStarF, iClock, iAll, iAmp, iPedal, iRig, iCab, iRefresh, iFile, iX, iChev, iDl, iCheck, iTrend, iUser, iExt, iPen, iTrash };
    inline void icon (Graphics& g, Ico kind, float cx, float cy, float s, Colour c, float w = 3.0f)
    {
        const float k = s / 16.0f;
        auto P = [&] (float x, float y) { return Point<float> (cx + (x - 8) * k, cy + (y - 8) * k); };
        Path p;
        auto poly = [&] (std::initializer_list<Point<float>> pts, bool close = false)
        {
            bool first = true;
            for (auto q : pts) { auto pt = P (q.x, q.y); if (first) p.startNewSubPath (pt); else p.lineTo (pt); first = false; }
            if (close) p.closeSubPath();
        };
        auto ell = [&] (float x0, float y0, float x1, float y1) { p.addEllipse (cx + x0 * k, cy + y0 * k, (x1 - x0) * k, (y1 - y0) * k); };
        auto rrect = [&] (float x0, float y0, float x1, float y1, float r) { p.addRoundedRectangle (cx + x0 * k, cy + y0 * k, (x1 - x0) * k, (y1 - y0) * k, r * k); };
        bool fillIt = false;
        switch (kind)
        {
            case iFolder:  poly ({ { 1, 4 }, { 6, 4 }, { 7.5f, 5.5f }, { 15, 5.5f }, { 15, 13.5f }, { 1, 13.5f } }, true); break;
            case iSearch:  ell (-5, -5, 3, 3); poly ({ { 10.5f, 10.5f }, { 14.5f, 14.5f } }); break;
            case iStar: case iStarF:
            {
                for (int i = 0; i < 10; ++i)
                {
                    const float a = -MathConstants<float>::halfPi + i * MathConstants<float>::pi / 5.0f, r = (i % 2 == 0 ? 7.0f : 3.1f) * k;
                    const Point<float> pt (cx + std::cos (a) * r, cy + std::sin (a) * r);
                    if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                }
                p.closeSubPath();
                fillIt = kind == iStarF;
                break;
            }
            case iClock:   ell (-7, -7, 7, 7); poly ({ { 8, 4 }, { 8, 8 }, { 11, 10 } }); break;
            case iAll:     for (float x : { 2.0f, 9.0f }) for (float y : { 2.0f, 9.0f }) rrect (x - 8, y - 8, x - 3, y - 3, 1.0f); break;
            case iAmp:
                rrect (-7.5f, -5, 7.5f, 6, 2); poly ({ { 1.5f, 6 }, { 14.5f, 6 } });
                g.setColour (c);
                for (float x : { 4.0f, 7.0f, 10.0f }) g.fillEllipse (cx + (x - 8 - 1.4f) * k, cy - 4.4f * k, 2.8f * k, 2.8f * k);
                break;
            case iPedal:
                rrect (-5.5f, -7.5f, 5.5f, 7.5f, 2); ell (-2.3f, -5, 2.3f, -0.4f);
                g.setColour (c); g.fillEllipse (cx - 1.6f * k, cy + 2.4f * k, 3.2f * k, 3.2f * k);
                break;
            case iRig:     rrect (-7, -7.5f, 7, -1.5f, 1.5f); rrect (-7, 0.5f, 7, 7.5f, 1.5f); ell (-2.4f, 1.6f, 2.4f, 6.4f); break;
            case iCab:
                rrect (-7, -7, 7, 7, 2);
                for (auto q : { Point<float> (4.5f, 4.5f), { 11.5f, 4.5f }, { 4.5f, 11.5f }, { 11.5f, 11.5f } }) ell (q.x - 8 - 2.4f, q.y - 8 - 2.4f, q.x - 8 + 2.4f, q.y - 8 + 2.4f);
                break;
            case iRefresh:
                p.addCentredArc (cx, cy, 6.5f * k, 6.5f * k, 0.0f, degreesToRadians (120.0f), degreesToRadians (420.0f), true);
                { Path t; t.addTriangle (P (14.5f, 2.5f), P (14.5f, 7.5f), P (9.5f, 7.5f)); g.setColour (c); g.fillPath (t); }
                break;
            case iFile:    poly ({ { 3, 1 }, { 10, 1 }, { 13.5f, 4.5f }, { 13.5f, 15 }, { 3, 15 } }, true); break;
            case iX:       poly ({ { 3, 3 }, { 13, 13 } }); poly ({ { 13, 3 }, { 3, 13 } }); break;
            case iChev:    poly ({ { 6, 3 }, { 11, 8 }, { 6, 13 } }); break;
            case iDl:      poly ({ { 8, 1.5f }, { 8, 10.5f } }); poly ({ { 4, 7 }, { 8, 11 }, { 12, 7 } }); poly ({ { 2, 14 }, { 14, 14 } }); break;
            case iCheck:   poly ({ { 2.5f, 8.5f }, { 6.5f, 12.5f }, { 13.5f, 4 } }); break;
            case iTrend:   poly ({ { 1.5f, 12.5f }, { 6, 8 }, { 9, 10.5f }, { 14.5f, 4.5f } }); poly ({ { 10.5f, 4.5f }, { 14.5f, 4.5f }, { 14.5f, 8.5f } }); break;
            case iUser:
                ell (-3.2f, -7, 3.2f, -0.6f);
                p.addCentredArc (cx, cy + 7.5f * k, 6.5f * k, 6.5f * k, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
                break;
            case iPen:     poly ({ { 3, 13 }, { 4, 9.5f }, { 11.5f, 2 }, { 14, 4.5f }, { 6.5f, 12 } }, true); break;
            case iTrash:   poly ({ { 2, 4 }, { 14, 4 } }); poly ({ { 6, 4 }, { 6.5f, 2 }, { 9.5f, 2 }, { 10, 4 } }); poly ({ { 3.5f, 4 }, { 4.5f, 14.5f }, { 11.5f, 14.5f }, { 12.5f, 4 } });
                           poly ({ { 6.5f, 7 }, { 6.8f, 12 } }); poly ({ { 9.5f, 7 }, { 9.2f, 12 } }); break;
            case iExt:     poly ({ { 9, 2 }, { 14, 2 }, { 14, 7 } }); poly ({ { 14, 2 }, { 7, 9 } }); poly ({ { 12, 10 }, { 12, 14 }, { 2, 14 }, { 2, 4 }, { 6, 4 } }); break;
        }
        g.setColour (c);
        if (fillIt) g.fillPath (p);
        else g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
    }
    inline Ico typeIcon (const String& t) { return t == "Amp" ? iAmp : t == "Pedal" ? iPedal : t == "Full rig" ? iRig : iCab; }
    inline Colour typeColour (const String& t) { return t == "Amp" ? cAmp : t == "Pedal" ? cPedal : t == "Full rig" ? cRig : cCab; }
    inline String typeOf (const CaptureMeta& m)
    {
        const auto t = m.gearType.toLowerCase();
        if (t.contains ("cab") || t.contains ("rig")) return "Full rig";
        if (t.startsWith ("pedal")) return "Pedal";
        if (t == "amp" || t == "preamp" || t.startsWith ("amp")) return "Amp";
        return {};
    }
    inline String toneType (const t3k::Tone& t)
    {
        if (t.gear == "amp-cab" || t.gear == "full-rig") return "Full rig";
        if (t.gear == "amp") return "Amp";
        if (t.gear == "pedal") return "Pedal";
        return "Cab";
    }
    // pastille de format (A2 en bleu, IR en violet, autres en gris)
    inline float formatBadge (Graphics& g, float x, float cy, const String& fmt, bool dim = false)
    {
        auto c = fmt == "A2" ? ACC : fmt == "IR" ? cCab : DIM;
        if (dim) c = FAINT;
        rr (g, box (x, cy - 13, x + 42, cy + 13), 7, tint (c, 0.16f), tint (c, 0.7f), 2);
        text (g, fmt, x + 21, cy + 1, pop (2, fmt.length() > 2 ? 12.0f : 15.0f), (fmt == "A2" || fmt == "IR" || dim) ? c : TXT, 1, 42);
        return x + 42;
    }
    inline void scrollbar (Graphics& g, float x, float y0, float y1, float frac, float pos)
    {
        if (frac >= 1.0f) return;
        rr (g, box (x, y0, x + 5, y1), 3, SEP);
        const float h = juce::jmax (24.0f, (y1 - y0) * frac), t = y0 + (y1 - y0 - h) * jlimit (0.0f, 1.0f, pos);
        rr (g, box (x, t, x + 5, t + h), 3, FAINT);
    }
    inline void drawLogo (Graphics& g, const Drawable* d, bool full, float x, float y, float h)
    {
        if (d != nullptr)
        {
            const float w = h * (full ? 210.0f / 32.0f : 94.0f / 32.0f);
            d->drawWithin (g, { x, y, w, h }, RectanglePlacement::stretchToFit, 1.0f);
        }
    }
    // flou (3 passes de boite, separable) pour le verre fume
    inline Image blurred (const Image& src, int radius)
    {
        Image img = src.convertedToFormat (Image::ARGB);
        img.duplicateIfShared();
        const int w = img.getWidth(), h = img.getHeight();
        if (w < 2 || h < 2) return img;
        Image::BitmapData bd (img, Image::BitmapData::readWrite);
        std::vector<float> line ((size_t) juce::jmax (w, h) * 4), out (line.size());
        for (int pass = 0; pass < 3; ++pass)
            for (int dir = 0; dir < 2; ++dir)
            {
                const int n = dir == 0 ? w : h, lines = dir == 0 ? h : w;
                for (int l = 0; l < lines; ++l)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        auto* px = dir == 0 ? bd.getPixelPointer (i, l) : bd.getPixelPointer (l, i);
                        for (int c = 0; c < 4; ++c) line[(size_t) (i * 4 + c)] = px[c];
                    }
                    for (int c = 0; c < 4; ++c)
                    {
                        float acc = 0; int cnt = 0;
                        for (int i = -radius; i <= radius; ++i) { const int j = jlimit (0, n - 1, i); acc += line[(size_t) (j * 4 + c)]; ++cnt; }
                        for (int i = 0; i < n; ++i)
                        {
                            out[(size_t) (i * 4 + c)] = acc / (float) cnt;
                            const int add = jlimit (0, n - 1, i + radius + 1), rem = jlimit (0, n - 1, i - radius);
                            acc += line[(size_t) (add * 4 + c)] - line[(size_t) (rem * 4 + c)];
                        }
                    }
                    for (int i = 0; i < n; ++i)
                    {
                        auto* px = dir == 0 ? bd.getPixelPointer (i, l) : bd.getPixelPointer (l, i);
                        for (int c = 0; c < 4; ++c) px[c] = (uint8) jlimit (0, 255, roundToInt (out[(size_t) (i * 4 + c)]));
                    }
                }
            }
        return img;
    }
}

class LibraryPanel : public juce::Component,
                     private juce::Timer,
                     private juce::ChangeListener,
                     private juce::KeyListener
{
public:
    explicit LibraryPanel (AmpireProcessor& p) : proc (p)
    {
        using namespace lv;
        setOpaque (false);
        setWantsKeyboardFocus (true);
        search.setTextToShowWhenEmpty ("Search captures...", FAINT);
        search.setFont (pop (0, 16.0f * kSy()));
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::textColourId, TXT);
        search.setColour (juce::TextEditor::highlightColourId, ACC.withAlpha (0.35f));
        search.setColour (juce::CaretComponent::caretColourId, ACC);
        search.setIndents (0, 0);
        search.setJustification (juce::Justification::centredLeft);
        search.onTextChange = [this] { sc3 = 0; rebuild(); };
        search.onEscapeKey = [this] { if (search.isEmpty()) close(); else search.clear(); rebuild(); };
        search.onReturnKey = [this] { if (! rows.isEmpty()) loadRow (juce::jmax (0, rowIndexOfLoaded()), true); };
        search.addKeyListener (this);
        addAndMakeVisible (search);
        nameEd.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        nameEd.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        nameEd.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        nameEd.setColour (juce::TextEditor::textColourId, TXT);
        nameEd.setColour (juce::TextEditor::highlightColourId, ACC.withAlpha (0.35f));
        nameEd.setColour (juce::CaretComponent::caretColourId, ACC);
        nameEd.setIndents (0, 0);
        nameEd.setJustification (juce::Justification::centredLeft);
        nameEd.onTextChange = [this] { dlgError.clear(); repaint(); };
        nameEd.onReturnKey = [this] { confirmDialog(); };
        nameEd.onEscapeKey = [this] { closeDialog(); };
        addChildComponent (nameEd);
        client->addChangeListener (this);
        drawnSelect = client->getSelectCount();
        drawnLibVersion = client->getLibraryVersion();
        mark = juce::Drawable::createFromImageData (BinaryData::T3K_logo_svg, BinaryData::T3K_logo_svgSize);
        whole = juce::Drawable::createFromImageData (BinaryData::TONE3000_logo_svg, BinaryData::TONE3000_logo_svgSize);
        startTimerHz (10);
    }
    ~LibraryPanel() override { client->removeChangeListener (this); }

    int targetSlot = 0;
    std::function<void()> onClose;

    void open (int slot)
    {
        targetSlot = slot;
        entries = proc.library.getEntries();
        drawnVersion = proc.library.getVersion();
        client->setRoot (proc.library.getRoot());
        // verre fume : image floutee de la facade (prise avant l'ouverture)
        if (auto* parent = getParentComponent())
        {
            auto snap = parent->createComponentSnapshot (parent->getLocalBounds(), true, 1.0f);
            backdrop = lv::blurred (snap.rescaled (juce::jmax (1, snap.getWidth() / 2), juce::jmax (1, snap.getHeight() / 2)), 3);
        }
        revealLoaded();
        revealPending = true;
        setVisible (true);
        toFront (true);
        if (tab == 0) search.grabKeyboardFocus(); else grabKeyboardFocus();
        if (tab == 1 && client->isSignedIn()) ensureLoaded (true);
        repaint();
    }
    void close() { if (dlgMode != 0) { dlgMode = 0; placeSearch(); } setVisible (false); if (onClose) onClose(); }
    void setTab (int t) { tab = t; placeSearch(); if (tab == 1 && client->isSignedIn()) ensureLoaded (true); repaint(); }
   #if AMPIRE_TOOL
    void previewDemo (bool signedIn) { client->previewDemo (signedIn); repaint(); }
    void previewDialog (int mode)
    {
        if (mode >= 3) { const auto f = proc.library.getRoot().getChildFile ("Mesa Boogie Dual Rectifier Rev G"); openFileDialog (mode, mode == 4 ? f.getChildFile ("DI").getChildFile ("MESRectoRevG_Green_BalancedClean_DI.nam") : f, 4); return; }
        const auto l = client->getList (t3k::favorites); if (l.tones.size() > 1) openDialog (mode, l.tones[mode == 2 ? 0 : 1]);
    }
   #endif

    void resized() override { placeSearch(); }

    // ======================================================================== dessin
    void paint (juce::Graphics& g) override
    {
        using namespace lv;
        g.addTransform (juce::AffineTransform::scale ((float) getWidth() / kMw, (float) getHeight() / kMh));
        const auto panel = box (40, 36, 1454, 558);
        g.setColour (juce::Colours::black.withAlpha (70.0f / 255.0f));
        g.fillRect (box (0, 0, kMw, kMh));
        juce::Path pp; pp.addRoundedRectangle (panel, 16.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 26, { 6, 12 }).drawForPath (g, pp);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (pp);
            g.setOpacity (1.0f);
            if (backdrop.isValid()) g.drawImage (backdrop, box (0, 0, kMw, kMh));
            g.setColour (BG.withAlpha (198.0f / 255.0f));
            g.fillRect (panel);
        }
        rr (g, panel, 16, {}, BORDER, 2);

        // en-tete commun
        rr (g, box (62, 56, 106, 100), 10, tint (ACC, 0.16f), tint (ACC, 0.7f), 2);
        icon (g, iFolder, 84, 78, 24, ACC, 3);
        text (g, "Capture Library", 120, 72, pop (2, 21), TXT);
        text (g, subtitle(), 120, 93, pop (0, 14), DIM);
        rr (g, box (318, 56, 598, 100), 11, TILE, BORDER, 2);
        for (int i = 0; i < 2; ++i)
        {
            const float x0 = i == 0 ? 321.0f : 457.0f;
            const bool s = tab == i;
            if (s) rr (g, box (x0, 59, x0 + 138, 97), 9, tint (ACC, 0.16f), tint (ACC, 0.7f), 2);
            else if (hover == hTab0 + i) rr (g, box (x0, 59, x0 + 138, 97), 9, HOVER);
            if (i == 0) { icon (g, iFolder, x0 + 26, 78, 16, s ? ACC : DIM, 2); text (g, "Library", x0 + 44, 79, pop (1, 15), s ? ACC : DIM); }
            else { drawLogo (g, mark.get(), false, x0 + 12, 72, 12); text (g, "TONE3000", x0 + 56, 79, pop (1, 15), s ? ACC : DIM); }
        }
        iconButton (g, 1390, iX, hover == hClose);

        if (tab == 0) paintLibrary (g); else paintTone3000 (g);
        if (dlgMode != 0) paintDialog (g);
    }

    // ======================================================================== souris / clavier
    void mouseMove (const juce::MouseEvent& e) override { setHover (hitAt (m (e))); }
    void mouseExit (const juce::MouseEvent&) override  { setHover ({}); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto h = hitAt (m (e));
        tipShown = false;
        if (e.mods.isPopupMenu()) { contextMenu (h); return; }
        click (h, e.getNumberOfClicks() >= 2);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (dlgMode != 0) return;
        const auto p = m (e);
        tipShown = false;
        const float d = -w.deltaY * 260.0f;
        if (tab == 0)
        {
            if (p.x < 333) sc1 = juce::jlimit (0.0f, maxScroll (col1.size()), sc1 + d);
            else if (p.x < 617) sc2 = juce::jlimit (0.0f, maxScroll (col2.size()), sc2 + d);
            else sc3 = juce::jlimit (0.0f, maxScroll (rows.size()), sc3 + d);
        }
        else
        {
            scGrid = juce::jlimit (0.0f, gridMaxScroll(), scGrid + d);
            if (scGrid > gridMaxScroll() - 320.0f) client->loadMore ((t3k::Tab) ttab);
        }
        setHover (hitAt (p));
        repaint();
    }
    bool keyPressed (const juce::KeyPress& k) override { return handleKey (k); }
    bool keyPressed (const juce::KeyPress& k, juce::Component*) override { return handleKey (k); }

private:
    // ---------------------------------------------------------------- zones cliquables
    enum HitKind { hNone, hTab0, hTab1, hClose, hFolderBtn, hRescan, hCol1, hCol2, hRowStar, hRow,
                   hPill, hVersions, hBrowse, hAvatar, hSignOut, hCard, hCardBtn, hConnect, hCancel, hRetry, hChoose, hResync,
                   hDlgCancel, hDlgOk, hDlgCheck, hDlgBlock };
    struct Hit
    {
        int kind = hNone, index = -1;
        bool operator== (const Hit& o) const { return kind == o.kind && index == o.index; }
        bool operator!= (const Hit& o) const { return ! (*this == o); }
        bool operator== (int k) const { return kind == k; }
    };
    struct Item { juce::String key, label; int count = 0; lv::Ico ico = lv::iFolder; };

    static float kSx() { return 750.0f / lv::kMw; }
    static float kSy() { return 300.0f / lv::kMh; }
    juce::Point<float> m (const juce::MouseEvent& e) const { return { e.position.x * lv::kMw / (float) juce::jmax (1, getWidth()), e.position.y * lv::kMh / (float) juce::jmax (1, getHeight()) }; }
    void setHover (Hit h) { if (h != hover) { hover = h; hoverSince = juce::Time::getMillisecondCounter(); tipShown = false; repaint(); } }

    void placeSearch()
    {
        const float sx = (float) getWidth() / lv::kMw, sy = (float) getHeight() / lv::kMh;
        search.setBounds (juce::Rectangle<float> (924 * sx, 58 * sy, 346 * sx, 40 * sy).toNearestInt());
        search.setFont (lv::pop (0, 16.0f * sy));
        search.setVisible (tab == 0 && dlgMode == 0);
        nameEd.setBounds (juce::Rectangle<float> (489 * sx, 274 * sy, 520 * sx, 38 * sy).toNearestInt());
        nameEd.setFont (lv::pop (0, 16.0f * sy));
        nameEd.setVisible (dlgMode != 0);
    }

    juce::String subtitle() const
    {
        if (tab == 0) return search.isEmpty() ? juce::String ("Browse by folder") : juce::String (rows.size()) + " results";
        if (! client->isSignedIn()) return "TONE3000";
        const auto l = client->getList ((t3k::Tab) ttab);
        const char* names[] = { "favorites", "synced", "created", "trending", "latest" };
        return juce::String ("TONE3000") + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (l.total) + " " + names[ttab];
    }

    void iconButton (juce::Graphics& g, float x, lv::Ico ic, bool hov)
    {
        using namespace lv;
        rr (g, box (x, 56, x + 44, 100), 10, hov ? tint (TXT, 0.08f, HOVER) : HOVER, BORDER, 2);
        icon (g, ic, x + 22, 78, 18, hov ? TXT : DIM, ic == iX ? 3.0f : 2.4f);
    }

    // ================================================================ LIBRARY (3 colonnes)
    static constexpr float kItemH = 46.0f, kTop = 166.0f, kVisible = 372.0f;
    float maxScroll (int n) const { return juce::jmax (0.0f, n * kItemH - 6.0f - kVisible); }

    void paintLibrary (juce::Graphics& g)
    {
        using namespace lv;
        // recherche, dossier, rescan (colles a la croix, meme ecart)
        rr (g, box (878, 56, 1278, 100), 11, TILE, search.hasKeyboardFocus (true) ? tint (ACC, 0.55f) : BORDER, 2);
        icon (g, iSearch, 902, 78, 20, DIM, 3);
        iconButton (g, 1286, iFolder, hover == hFolderBtn);
        iconButton (g, 1338, iRefresh, hover == hRescan);

        const auto root = proc.library.getRoot();
        if (! root.isDirectory())
        {
            text (g, "Choose your capture folder", 747, 270, pop (2, 22), TXT, 1, 900);
            text (g, "Ampire lists every .nam file in it, subfolders included.", 747, 306, pop (0, 15), DIM, 1, 900);
            rr (g, box (607, 336, 887, 382), 11, hover == hChoose ? ACC.brighter (0.15f) : ACC);
            icon (g, iFolder, 641, 359, 18, INK, 2.4f);
            text (g, "CHOOSE FOLDER", 761, 360, pop (2, 15), INK, 1, 220);
            return;
        }
        // fil d'Ariane
        juce::StringArray parts { root.getFileName() };
        if (! search.isEmpty()) parts.add ("Search");
        else
        {
            if (auto* it = findItem (col1, sel1)) parts.add (it->label);
            if (auto* it = findItem (col2, sel2)) parts.add (it->label);
        }
        float bx = 62;
        for (int i = 0; i < parts.size(); ++i)
        {
            if (i) { icon (g, iChev, bx + 8, 131, 14, FAINT, 2); bx += 22; }
            const auto f = pop (1, 14);
            text (g, parts[i], bx, 131, f, i == parts.size() - 1 ? TXT : DIM);
            bx += textW (parts[i], f) + 8;
        }
        if (proc.library.isScanning())
            text (g, "Scanning... " + juce::String (proc.library.scannedCount()), 1432, 131, pop (0, 13), DIM, 2, 300);

        paintColumn (g, 56, 326, col1, sel1, sc1, hCol1);
        paintColumn (g, 340, 610, col2, sel2, sc2, hCol2);

        // captures
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (box (618, 160, 1452, 540).toNearestInt());
            const auto loaded = proc.getSlotInfo (targetSlot).meta.path;
            const auto fItem = pop (1, 15);
            for (int j = 0; j < rows.size(); ++j)
            {
                const float yy = kTop + j * kItemH - sc3;
                if (yy + 40 < 160 || yy > 540) continue;
                const auto& r = rows.getReference (j);
                const float x0 = 624, x1 = 1432, cy = yy + 20;
                const bool sel = r.path == loaded, hov = (hover == hRow || hover == hRowStar) && hover.index == j;
                if (sel) { rr (g, box (x0, yy, x1, yy + 40), 9, tint (ACC, 0.10f)); rr (g, box (x0, yy + 8, x0 + 4, yy + 32), 2, ACC); }
                else if (hov) rr (g, box (x0, yy, x1, yy + 40), 9, HOVER);
                const bool fav = proc.library.isFavourite (r.path);
                icon (g, fav ? iStarF : iStar, x0 + 26, cy, 20, fav ? GOLD : (hover == hRowStar && hover.index == j ? DIM : FAINT), 2);
                const auto fam = r.isA2 ? juce::String ("A2") : r.family.isNotEmpty() ? r.family.substring (0, 4) : juce::String ("A1");
                const float xb = formatBadge (g, x0 + 50, cy, fam);
                const auto ty = typeOf (r);
                float right = x1 - 16;
                if (ty.isNotEmpty())
                {
                    const auto tf = pop (1, 14);
                    const float tw = textW (ty, tf) + 22;
                    const auto c = typeColour (ty);
                    rr (g, box (x1 - 16 - tw, cy - 13, x1 - 16, cy + 13), 7, tint (c, 0.13f));
                    text (g, ty, x1 - 16 - tw * 0.5f, cy + 1, tf, c, 1, tw);
                    right = x1 - 16 - tw - 12;
                }
                const auto name = r.title.isNotEmpty() ? r.title : r.fileName;
                text (g, fitText (name, fItem, right - xb - 16), xb + 16, cy + 1, fItem, sel ? ACC : TXT);
            }
            if (rows.isEmpty())
                text (g, search.isEmpty() ? (proc.library.isScanning() ? "Scanning your captures..." : "No capture in this folder") : "No capture matches",
                      1028, 300, pop (1, 15), DIM, 1, 800);
        }
        scrollbar (g, 1440, 168, 528, kVisible / juce::jmax (1.0f, rows.size() * kItemH - 6.0f), sc3 / juce::jmax (1.0f, maxScroll (rows.size())));
        scrollbar (g, 604, 168, 528, kVisible / juce::jmax (1.0f, col2.size() * kItemH - 6.0f), sc2 / juce::jmax (1.0f, maxScroll (col2.size())));
        scrollbar (g, 320, 168, 528, kVisible / juce::jmax (1.0f, col1.size() * kItemH - 6.0f), sc1 / juce::jmax (1.0f, maxScroll (col1.size())));
        if (tipShown) paintExpanded (g);
    }

    // nom complet au survol (800 ms) : la ligne s'allonge et montre le nom entier, sans cadre
    bool hoverTruncated() const
    {
        const auto f = lv::pop (1, 15);
        if (hover == hCol1 || hover == hCol2)
        {
            const auto& items = hover == hCol1 ? col1 : col2;
            if (hover.index < 0 || hover.index >= items.size()) return false;
            return lv::textW (items[hover.index].label, f) > 270 - 120;
        }
        if (hover == hRow || hover == hRowStar)
        {
            if (hover.index < 0 || hover.index >= rows.size()) return false;
            const auto& r = rows.getReference (hover.index);
            const auto name = r.title.isNotEmpty() ? r.title : r.fileName;
            const auto ty = lv::typeOf (r);
            const float right = ty.isNotEmpty() ? 1432 - 16 - (lv::textW (ty, lv::pop (1, 14)) + 22) - 12 : 1432 - 16;
            return lv::textW (name, f) > right - 716 - 16;
        }
        return false;
    }
    void paintExpanded (juce::Graphics& g)
    {
        using namespace lv;
        if (! hoverTruncated()) return;
        const auto f = pop (1, 15);
        if (hover == hCol1 || hover == hCol2)
        {
            const bool c1 = hover == hCol1;
            const auto& it = (c1 ? col1 : col2).getReference (hover.index);
            const float x0 = c1 ? 56.0f : 340.0f, yy = kTop + hover.index * kItemH - (c1 ? sc1 : sc2);
            const bool s = it.key == (c1 ? sel1 : sel2) && search.isEmpty();
            const float w = jmin (1440.0f - x0, 50 + textW (it.label, f) + 18);
            rr (g, box (x0, yy, x0 + w, yy + 40), 8, s ? tint (ACC, 0.14f) : HOVER);
            icon (g, it.ico, x0 + 26, yy + 20, it.ico == iStarF ? 20.0f : 18.0f, s ? ACC : (it.ico == iStarF ? GOLD : DIM), 2);
            text (g, it.label, x0 + 50, yy + 21, f, s ? ACC : TXT, 0, w - 50);
        }
        else
        {
            const auto& r = rows.getReference (hover.index);
            const auto name = r.title.isNotEmpty() ? r.title : r.fileName;
            const float yy = kTop + hover.index * kItemH - sc3, xb = 716;
            const bool sel = r.path == proc.getSlotInfo (targetSlot).meta.path;
            const float w = jmin (1446.0f - xb, 16 + textW (name, f) + 18);
            rr (g, box (xb + 6, yy, xb + w, yy + 40), 8, sel ? tint (ACC, 0.10f) : HOVER);
            text (g, name, xb + 16, yy + 21, f, sel ? ACC : TXT, 0, w - 16);
        }
    }

    void paintColumn (juce::Graphics& g, float x0, float x1, const juce::Array<Item>& items, const juce::String& sel, float sc, int hk)
    {
        using namespace lv;
        g.setColour (SEP);
        g.fillRect (box (x1 + 6, 160, x1 + 8, 536));
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (box (x0 - 4, 160, x1 + 2, 540).toNearestInt());
        const auto f = pop (1, 15);
        for (int j = 0; j < items.size(); ++j)
        {
            const float yy = kTop + j * kItemH - sc;
            if (yy + 40 < 160 || yy > 540) continue;
            const auto& it = items.getReference (j);
            const bool s = it.key == sel && search.isEmpty(), hov = hover == hk && hover.index == j;
            if (s) rr (g, box (x0, yy, x1 - 14, yy + 40), 8, tint (ACC, 0.14f));
            else if (hov) rr (g, box (x0, yy, x1 - 14, yy + 40), 8, HOVER);
            const auto ic = it.ico;
            icon (g, ic, x0 + 26, yy + 20, ic == iStarF ? 20.0f : 18.0f, s ? ACC : (ic == iStarF ? GOLD : DIM), 2);
            text (g, fitText (it.label, f, x1 - x0 - 120), x0 + 50, yy + 21, f, s ? ACC : TXT);
            text (g, juce::String (it.count), x1 - 52, yy + 21, pop (0, 13), DIM, 2, 60);
            icon (g, iChev, x1 - 34, yy + 20, 13, s ? ACC : FAINT, 2);
        }
    }

    static const Item* findItem (const juce::Array<Item>& a, const juce::String& key)
    {
        for (auto& i : a) if (i.key == key) return &i;
        return nullptr;
    }

    // ---- modele : colonnes construites d'apres les chemins relatifs au dossier de captures
    static juce::StringArray partsOf (const juce::File& root, const juce::String& path)
    {
        const auto rel = juce::File (path).getRelativePathFrom (root);
        if (rel.startsWith ("..")) return {};
        return juce::StringArray::fromTokens (rel, "\\/", "");
    }
    static juce::String key1 (const juce::StringArray& p) { return p.size() > 1 ? p[0] : juce::String (kLoose); }
    static juce::String key2 (const juce::StringArray& p) { return p.size() > 2 ? p[1] : juce::String (kLoose); }
    static constexpr const char* kFav = "\x01" "fav";
    static constexpr const char* kRecent = "\x01" "recent";
    static constexpr const char* kLoose = "\x01" "loose";

    void rebuild()
    {
        using namespace lv;
        const auto root = proc.library.getRoot();
        recents = proc.library.getRecents();
        juce::Array<juce::StringArray> parts;
        for (auto& e : entries) parts.add (partsOf (root, e.path));

        // colonne 1 : Favourites, Recent, puis les dossiers (ceux commencant par « _ » a la fin), captures isolees
        col1.clearQuick();
        int nFav = 0;
        for (auto& e : entries) if (proc.library.isFavourite (e.path)) ++nFav;
        col1.add ({ kFav, "Favourites", nFav, iStarF });
        col1.add ({ kRecent, "Recent", recents.size(), iClock });
        juce::StringArray folders;
        juce::HashMap<juce::String, int> counts;
        int loose1 = 0;
        for (auto& p : parts)
        {
            if (p.isEmpty()) continue;
            const auto k = key1 (p);
            if (k == kLoose) { ++loose1; continue; }
            if (! counts.contains (k)) { counts.set (k, 0); folders.add (k); }
            counts.set (k, counts[k] + 1);
        }
        std::sort (folders.begin(), folders.end(), [] (const juce::String& a, const juce::String& b)
        {
            const bool ua = a.startsWith ("_"), ub = b.startsWith ("_");
            if (ua != ub) return ub;
            return a.compareNatural (b) < 0;
        });
        for (auto& f : folders) col1.add ({ f, f, counts[f], iFolder });
        if (loose1 > 0) col1.add ({ kLoose, "Loose captures", loose1, iFile });
        if (findItem (col1, sel1) == nullptr) sel1 = folders.isEmpty() ? juce::String (kFav) : folders[0];

        // colonne 2 : sous-dossiers du dossier choisi
        col2.clearQuick();
        if (sel1 != kFav && sel1 != kRecent && sel1 != kLoose)
        {
            juce::StringArray subs;
            juce::HashMap<juce::String, int> c2;
            int loose2 = 0;
            for (auto& p : parts)
            {
                if (p.isEmpty() || key1 (p) != sel1) continue;
                const auto k = key2 (p);
                if (k == kLoose) { ++loose2; continue; }
                if (! c2.contains (k)) { c2.set (k, 0); subs.add (k); }
                c2.set (k, c2[k] + 1);
            }
            subs.sortNatural();
            if (loose2 > 0) col2.add ({ kLoose, "Loose captures", loose2, iFile });
            for (auto& s : subs) col2.add ({ s, s, c2[s], iFolder });
        }
        if (findItem (col2, sel2) == nullptr) sel2 = col2.isEmpty() ? juce::String() : col2[0].key;

        // captures
        rows.clearQuick();
        const auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "\"");
        auto matches = [&] (const CaptureMeta& mm)
        {
            if (words.isEmpty()) return true;
            const auto hay = (mm.title + " " + mm.fileName + " " + mm.gearMake + " " + mm.gearModel + " " + mm.gearType + " "
                              + mm.toneType + " " + mm.modeledBy + " " + mm.family + " " + mm.path).toLowerCase();
            for (auto& w : words) if (! hay.contains (w)) return false;
            return true;
        };
        if (! words.isEmpty())
        {
            for (auto& e : entries) if (matches (e)) rows.add (e);
        }
        else if (sel1 == kFav)
        {
            for (auto& e : entries) if (proc.library.isFavourite (e.path)) rows.add (e);
        }
        else if (sel1 == kRecent)
        {
            for (auto& path : recents)
            {
                CaptureMeta mm;
                bool found = false;
                for (auto& e : entries) if (e.path == path) { mm = e; found = true; break; }
                if (! found)
                {
                    if (! metaCache.contains (path)) { CaptureMeta x; if (CaptureBuilder::readMeta (juce::File (path), x)) metaCache.set (path, x); else continue; }
                    mm = metaCache[path];
                }
                rows.add (mm);
            }
        }
        else
        {
            for (int i = 0; i < entries.size(); ++i)
            {
                const auto& p = parts.getReference (i);
                if (p.isEmpty() || key1 (p) != sel1) continue;
                if (sel1 != kLoose && key2 (p) != sel2) continue;
                rows.add (entries[i]);
            }
        }
        sc1 = juce::jlimit (0.0f, maxScroll (col1.size()), sc1);
        sc2 = juce::jlimit (0.0f, maxScroll (col2.size()), sc2);
        sc3 = juce::jlimit (0.0f, maxScroll (rows.size()), sc3);
        repaint();
    }

    // ouvre la colonne de la capture chargee et la fait apparaitre
    void revealLoaded()
    {
        const auto path = proc.getSlotInfo (targetSlot).meta.path;
        const auto p = partsOf (proc.library.getRoot(), path);
        if (path.isNotEmpty() && ! p.isEmpty()) { sel1 = key1 (p); sel2 = key2 (p); }
        rebuild();
        const int i = rowIndexOfLoaded();
        if (i >= 0) sc3 = juce::jlimit (0.0f, maxScroll (rows.size()), i * kItemH - kVisible * 0.5f);
        for (int j = 0; j < col1.size(); ++j) if (col1[j].key == sel1) sc1 = juce::jlimit (0.0f, maxScroll (col1.size()), j * kItemH - kVisible * 0.5f);
        for (int j = 0; j < col2.size(); ++j) if (col2[j].key == sel2) sc2 = juce::jlimit (0.0f, maxScroll (col2.size()), j * kItemH - kVisible * 0.5f);
    }
    int rowIndexOfLoaded() const
    {
        const auto cur = proc.getSlotInfo (targetSlot).meta.path;
        for (int i = 0; i < rows.size(); ++i) if (rows.getReference (i).path == cur) return i;
        return -1;
    }
    void loadRow (int row, bool andClose)
    {
        if (row < 0 || row >= rows.size()) return;
        proc.loadCapture (targetSlot, juce::File (rows[row].path));
        repaint();
        if (andClose) close();
    }
    void ensureRowVisible (int i)
    {
        const float y = i * kItemH;
        if (y < sc3) sc3 = y;
        else if (y + 40 > sc3 + kVisible) sc3 = y + 40 - kVisible;
        sc3 = juce::jlimit (0.0f, maxScroll (rows.size()), sc3);
    }
    bool handleKey (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey) { if (dlgMode != 0) closeDialog(); else close(); return true; }
        if (tab != 0 || rows.isEmpty()) return false;
        if (k == juce::KeyPress::downKey || k == juce::KeyPress::upKey)
        {
            const int cur = rowIndexOfLoaded();
            const int next = juce::jlimit (0, rows.size() - 1, cur < 0 ? 0 : cur + (k == juce::KeyPress::downKey ? 1 : -1));
            ensureRowVisible (next);
            loadRow (next, false);
            return true;
        }
        if (k == juce::KeyPress::returnKey) { loadRow (juce::jmax (0, rowIndexOfLoaded()), true); return true; }
        return false;
    }

    // ================================================================ TONE3000
    static constexpr float kCardW = 260, kCardH = 300, kGapX = 16, kGapY = 16, kGridTop = 172, kGridBottom = 542;
    juce::Rectangle<float> cardRect (int i) const { return { 62 + (i % 5) * (kCardW + kGapX), kGridTop + (i / 5) * (kCardH + kGapY) - scGrid, kCardW, kCardH }; }
    float gridContentH (int n) const { const int rowsN = (n + 4) / 5; return rowsN * (kCardH + kGapY) - kGapY + 20.0f; }
    float gridMaxScroll() const { return juce::jmax (0.0f, gridContentH (tones.size()) - (kGridBottom - kGridTop)); }

    juce::Rectangle<float> pillRect (int i) const
    {
        const char* n[] = { "Favorites", "Synced", "Created", "Trending", "Latest" };
        float x = 62;
        for (int k = 0; k < i; ++k) x += lv::textW (n[k], lv::pop (1, 15)) + 34 + 8;
        return lv::box (x, 118, x + lv::textW (n[i], lv::pop (1, 15)) + 34, 154);
    }

    void ensureLoaded (bool force = false)
    {
        const auto l = client->getList ((t3k::Tab) ttab);
        if (ttab == t3k::synced ? force : (! l.loaded && ! l.loading && l.error.isEmpty())) client->refresh ((t3k::Tab) ttab);
    }

    enum CardState { csSync, csSynced, csUpdate, csDownloading, csFailed };
    CardState stateOf (const t3k::Tone& t, t3k::Job* jobOut = nullptr) const
    {
        const auto j = client->getJob (t.id);
        if (jobOut != nullptr) *jobOut = j;
        if (j.running) return csDownloading;
        if (j.failed) return csFailed;
        t3k::Local l;
        if (! client->findLocal (t.id, l)) return csSync;
        auto remote = client->remoteUpdatedAt (t.id);
        if (remote.isEmpty() && ttab != t3k::synced) remote = t.updatedAt;
        const auto have = l.tone.updatedAt;
        if (remote.isNotEmpty() && have.isNotEmpty() && remote > have) return csUpdate;
        return csSynced;
    }

    void paintTone3000 (juce::Graphics& g)
    {
        using namespace lv;
        if (! client->isSignedIn()) { paintSplash (g); return; }

        // compte + Browse TONE3000
        const auto user = client->getUsername();
        const auto av = client->getImage (client->getAvatarUrl());
        const auto circ = box (1000, 62, 1032, 94);
        if (av.isValid())
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path c; c.addEllipse (circ); g.reduceClipRegion (c);
            g.setOpacity (1.0f); g.drawImage (av, circ, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        }
        else
        {
            g.setColour (juce::Colour (0xff46608c)); g.fillEllipse (circ);
            text (g, user.substring (0, 1).toUpperCase(), 1016, 79, pop (2, 15), TXT, 1, 32);
        }
        text (g, fitText (user.isNotEmpty() ? user : juce::String ("TONE3000"), pop (1, 15), 132), 1042, 72, pop (1, 15), TXT);
        text (g, "Sign out", 1042, 91, pop (0, 12), hover == hSignOut ? TXT : DIM);
        rr (g, box (1182, 56, 1382, 100), 10, hover == hBrowse ? tint (TXT, 0.08f, HOVER) : HOVER, BORDER, 2);
        drawLogo (g, mark.get(), false, 1196, 72, 13);
        text (g, "Browse TONE3000", 1240, 79, pop (1, 15), TXT);

        // onglets
        const char* n[] = { "Favorites", "Synced", "Created", "Trending", "Latest" };
        for (int i = 0; i < 5; ++i)
        {
            const auto r = pillRect (i);
            const bool s = i == ttab;
            rr (g, r, 9, s ? tint (ACC, 0.18f) : (hover == hPill && hover.index == i ? tint (TXT, 0.06f, HOVER) : HOVER), s ? tint (ACC, 0.8f) : BORDER, 2);
            text (g, n[i], r.getCentreX(), 137, pop (1, 15), s ? ACC : DIM, 1, r.getWidth());
        }
        const auto vtxt = "Versions: " + t3k::Client::versionModeName (client->getVersionMode());
        {
            const auto rb = resyncRect();
            rr (g, rb, 9, hover == hResync ? tint (TXT, 0.08f, HOVER) : HOVER, BORDER, 2);
            icon (g, iRefresh, rb.getX() + 24, 136, 16, hover == hResync ? TXT : DIM, 2);
            text (g, "Re-sync", rb.getX() + 42, 137, pop (1, 15), TXT);
        }
        text (g, vtxt, 1414, 137, pop (1, 14), hover == hVersions ? TXT : DIM, 2, 400);
        juce::Path tri; tri.addTriangle (1422, 134, 1432, 134, 1427, 140);
        g.setColour (hover == hVersions ? TXT : DIM); g.fillPath (tri);

        // cartes
        const auto l = client->getList ((t3k::Tab) ttab);
        tones = l.tones;
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (box (42, 162, 1452, kGridBottom).toNearestInt());
            for (int i = 0; i < tones.size(); ++i)
            {
                const auto r = cardRect (i);
                if (r.getBottom() < 162 || r.getY() > kGridBottom) continue;
                paintCard (g, i, r);
            }
        }
        // fondu : la rangee suivante passe sous le bord
        juce::ColourGradient fade (BG.withAlpha (0.0f), 0, 506, BG.withAlpha (150.0f / 255.0f), 0, kGridBottom, false);
        g.setGradientFill (fade);
        g.fillRect (box (42, 506, 1452, kGridBottom));
        scrollbar (g, 1440, 174, 528, (kGridBottom - kGridTop) / juce::jmax (1.0f, gridContentH (tones.size())), scGrid / juce::jmax (1.0f, gridMaxScroll()));

        // etats vides / chargement / erreur
        juce::String msg;
        if (! l.error.isEmpty()) msg = l.error;
        else if (tones.isEmpty() && (l.loading || ! l.loaded)) msg = "Loading...";
        else if (tones.isEmpty())
        {
            const char* e[] = { "No favorites yet: favorite tones on tone3000.com, or use Browse TONE3000",
                                "Nothing synced yet: click SYNC on a tone to download it into your capture folder",
                                "You have not published any tone yet", "Nothing trending right now", "Nothing new right now" };
            msg = e[ttab];
        }
        if (msg.isNotEmpty())
        {
            text (g, msg, 747, 330, pop (1, 15), l.error.isEmpty() ? DIM : RED, 1, 1200);
            if (l.error.isNotEmpty()) text (g, "Click to retry", 747, 360, pop (0, 13), hover == hRetry ? TXT : DIM, 1, 400);
        }
        else if (l.loading)
            text (g, "Loading more...", 747, 528, pop (0, 13), DIM, 1, 400);
        if (! proc.library.getRoot().isDirectory())
            text (g, "Choose your capture folder in the Library tab first: tones are synced into it.", 747, 548, pop (0, 12), RED.withAlpha (0.9f), 1, 1200);
        else if (const auto n = client->getNotice(); n.isNotEmpty())
        {
            const auto f = pop (1, 13);
            const float w = textW (n, f) + 36;
            rr (g, box (747 - w * 0.5f, 520, 747 + w * 0.5f, 552), 9, tint (GREEN, 0.14f, BG), tint (GREEN, 0.6f), 2);
            text (g, n, 747, 537, f, GREEN, 1, w);
        }
    }

    void paintCard (juce::Graphics& g, int i, juce::Rectangle<float> r)
    {
        using namespace lv;
        const auto& t = tones.getReference (i);
        const float cx = r.getX(), cy = r.getY();
        const bool hov = (hover == hCard || hover == hCardBtn) && hover.index == i;
        rr (g, r, 14, TILE, hov ? tint (ACC, 0.35f, TLINE) : TLINE, 2);
        // vignette
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (cx + 1, cy + 1, kCardW - 2, 112, 13, 13, true, true, false, false);
            g.reduceClipRegion (clip);
            const auto img = client->getImage (t.image);
            const auto ty = toneType (t);
            if (img.isValid())
                g.setOpacity (1.0f), g.drawImage (img, box (cx + 1, cy + 1, cx + kCardW - 1, cy + 113), juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
            else
            {
                const auto base = typeColour (ty).withSaturation (0.35f).withBrightness (0.42f);
                g.setGradientFill (juce::ColourGradient (base, 0, cy, base.darker (0.5f), 0, cy + 112, false));
                g.fillRect (box (cx, cy, cx + kCardW, cy + 113));
                icon (g, typeIcon (ty), cx + kCardW * 0.5f, cy + 56, 40, juce::Colours::white.withAlpha (0.6f), 4);
            }
        }
        // titre sur deux lignes
        const auto f = pop (1, 15);
        juce::StringArray words = juce::StringArray::fromTokens (t.title, " ", "");
        juce::StringArray lines; juce::String cur;
        for (auto& w : words)
        {
            const auto tt = (cur + " " + w).trim();
            if (textW (tt, f) > kCardW - 28 && cur.isNotEmpty()) { lines.add (cur); cur = w; }
            else cur = tt;
        }
        lines.add (cur);
        for (int k = 0; k < juce::jmin (2, lines.size()); ++k)
        {
            auto s = lines[k];
            if (k == 1 && lines.size() > 2) s = fitText (s + " " + lines[2], f, kCardW - 28);
            text (g, fitText (s, f, kCardW - 28), cx + 14, cy + 135 + k * 22, f, TXT);
        }
        {
            const auto jb = client->getJob (t.id);
            if (jb.failed) text (g, fitText (jb.error, pop (0, 12), kCardW - 80), cx + 14, cy + 186, pop (0, 12), RED);
            else text (g, t.gearLabel(), cx + 14, cy + 186, pop (0, 13), DIM);
        }
        formatBadge (g, cx + kCardW - 56, cy + 186, t.formatBadge());
        // auteur
        const auto av = client->getImage (t.avatar);
        const auto circ = box (cx + 14, cy + 208, cx + 34, cy + 228);
        if (av.isValid())
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path c; c.addEllipse (circ); g.reduceClipRegion (c);
            g.setOpacity (1.0f); g.drawImage (av, circ, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        }
        else
        {
            g.setColour (juce::Colour (0xff5a5c62)); g.fillEllipse (circ);
            text (g, t.user.substring (0, 1).toUpperCase(), cx + 24, cy + 218, pop (2, 11), TXT, 1, 20);
        }
        text (g, fitText (t.user, pop (0, 13), kCardW - 60), cx + 42, cy + 219, pop (0, 13), DIM);
        // bouton
        t3k::Job job;
        const auto st = stateOf (t, &job);
        const auto bb = box (cx + 14, cy + 250, cx + kCardW - 14, cy + 286);
        const bool bh = hover == hCardBtn && hover.index == i;
        const float by = cy + 268;
        const bool ir = t.formatBadge() == "IR";
        switch (st)
        {
            case csSynced:
                rr (g, bb, 9, bh ? tint (GREEN, 0.10f, TILE) : std::optional<juce::Colour>(), tint (GREEN, 0.7f), 2);
                icon (g, iCheck, cx + (ir ? 88.0f : 80.0f), by, 13, GREEN, 2);
                text (g, ir ? juce::String ("SYNCED") + juce::String::fromUTF8 (" \xc2\xb7 ") + "SHOW" : juce::String ("SYNCED") + juce::String::fromUTF8 (" \xc2\xb7 ") + "LOAD",
                      cx + (ir ? 106.0f : 98.0f), by + 1, pop (2, 13), GREEN);
                break;
            case csSync:
                rr (g, bb, 9, bh ? ACC.brighter (0.15f) : ACC);
                icon (g, iDl, cx + 104, by, 15, INK, 2);
                text (g, "SYNC", cx + 122, by + 1, pop (2, 13), INK);
                break;
            case csUpdate:
                rr (g, bb, 9, bh ? tint (ACC, 0.12f, TILE) : std::optional<juce::Colour>(), tint (ACC, 0.8f), 2);
                text (g, "UPDATE AVAILABLE", cx + kCardW * 0.5f, by + 1, pop (2, 13), ACC, 1, kCardW);
                break;
            case csDownloading:
            {
                rr (g, bb, 9, HOVER, BORDER, 2);
                const float frac = job.total > 0 ? (float) job.done / (float) job.total : 0.0f;
                if (frac > 0) rr (g, box (cx + 16, cy + 252, cx + 16 + (kCardW - 32) * frac, cy + 284), 8, tint (ACC, 0.35f));
                text (g, job.total > 0 ? "DOWNLOADING  " + juce::String (job.done) + " / " + juce::String (job.total) : juce::String ("PREPARING..."),
                      cx + kCardW * 0.5f, by + 1, pop (2, 13), TXT, 1, kCardW);
                break;
            }
            case csFailed:
                rr (g, bb, 9, bh ? tint (RED, 0.10f, TILE) : std::optional<juce::Colour>(), tint (RED, 0.8f), 2);
                text (g, "FAILED " + juce::String::fromUTF8 ("\xc2\xb7") + " RETRY", cx + kCardW * 0.5f, by + 1, pop (2, 13), RED, 1, kCardW);
                break;
        }
    }

    void paintSplash (juce::Graphics& g)
    {
        using namespace lv;
        drawLogo (g, whole.get(), true, 747 - 131, 150, 40);
        text (g, "Ampire has partnered with TONE3000", 747, 232, pop (2, 22), TXT, 1, 1200);
        text (g, "Thousands of free NAM captures and IRs shared by the community.", 747, 272, pop (0, 15), DIM, 1, 1200);
        text (g, "Sign in to sync your favorites and downloads straight into your capture folder.", 747, 296, pop (0, 15), DIM, 1, 1200);
        const auto b = box (587, 330, 907, 376);
        if (client->isConnecting())
        {
            rr (g, b, 11, HOVER, tint (ACC, 0.7f), 2);
            text (g, "WAITING FOR YOUR BROWSER...", 747, 354, pop (2, 15), ACC, 1, 320);
            text (g, "Cancel", 747, 404, pop (0, 13), hover == hCancel ? TXT : DIM, 1, 200);
        }
        else
        {
            rr (g, b, 11, hover == hConnect ? ACC.brighter (0.15f) : ACC);
            drawLogo (g, mark.get(), false, 615, 346, 14);
            text (g, "CONNECT WITH TONE3000", 769, 354, pop (2, 15), INK, 1, 260);
            text (g, "Opens your web browser: sign in with your email (6-digit code), then come back to Ampire.", 747, 404, pop (0, 13), DIM, 1, 1200);
        }
        const auto err = client->getAuthError();
        if (err.isNotEmpty()) text (g, err, 747, 440, pop (1, 14), RED, 1, 1200);
        text (g, juce::String ("Powered by TONE3000") + juce::String::fromUTF8 ("  \xc2\xb7  ") + "tone3000.com", 747, 520, pop (0, 12), FAINT, 1, 600);
    }

    // ================================================================ zones
    Hit hitAt (juce::Point<float> p) const
    {
        using namespace lv;
        auto in = [&] (juce::Rectangle<float> r) { return r.contains (p); };
        if (dlgMode != 0)
        {
            const float by = dlgMode >= 3 && dlgMode <= 4 ? 372.0f : 402.0f;
            if (in (box (749, by, 881, by + 40))) return { hDlgCancel };
            if (in (box (891, by, 1023, by + 40))) return { hDlgOk };
            if (dlgMode == 1 && in (box (471, 356, 820, 380))) return { hDlgCheck };
            return { hDlgBlock };
        }
        if (in (box (321, 59, 459, 97))) return { hTab0 };
        if (in (box (457, 59, 595, 97))) return { hTab1 };
        if (in (box (1390, 56, 1434, 100))) return { hClose };
        if (tab == 0)
        {
            if (in (box (1286, 56, 1330, 100))) return { hFolderBtn };
            if (in (box (1338, 56, 1382, 100))) return { hRescan };
            if (! proc.library.getRoot().isDirectory()) return in (box (607, 336, 887, 382)) ? Hit { hChoose } : Hit {};
            if (p.y >= 160 && p.y <= 540)
            {
                auto idx = [&] (float sc, int n) { const int j = (int) std::floor ((p.y - kTop + sc) / kItemH); return (j >= 0 && j < n && std::fmod (p.y - kTop + sc, kItemH) <= 40.0f) ? j : -1; };
                if (p.x >= 56 && p.x <= 312) { const int j = idx (sc1, col1.size()); if (j >= 0) return { hCol1, j }; }
                else if (p.x >= 340 && p.x <= 596) { const int j = idx (sc2, col2.size()); if (j >= 0) return { hCol2, j }; }
                else if (p.x >= 624 && p.x <= 1432) { const int j = idx (sc3, rows.size()); if (j >= 0) return { p.x < 666 ? hRowStar : hRow, j }; }
            }
            return {};
        }
        if (! client->isSignedIn())
        {
            if (client->isConnecting()) return in (box (697, 394, 797, 414)) ? Hit { hCancel } : Hit {};
            return in (box (587, 330, 907, 376)) ? Hit { hConnect } : Hit {};
        }
        if (in (box (1182, 56, 1382, 100))) return { hBrowse };
        if (in (box (1042, 84, 1100, 98))) return { hSignOut };
        if (in (box (1000, 62, 1032, 94))) return { hAvatar };
        for (int i = 0; i < 5; ++i) if (in (pillRect (i))) return { hPill, i };
        if (in (resyncRect())) return { hResync };
        if (in (box (1414 - versionsW() - 4, 120, 1434, 154))) return { hVersions };
        if (p.y >= 162 && p.y <= kGridBottom)
        {
            const auto l = client->getList ((t3k::Tab) ttab);
            if (l.error.isNotEmpty() && in (box (547, 316, 947, 372))) return { hRetry };
            for (int i = 0; i < tones.size(); ++i)
            {
                const auto r = cardRect (i);
                if (! r.contains (p)) continue;
                if (lv::box (r.getX() + 14, r.getY() + 250, r.getRight() - 14, r.getY() + 286).contains (p)) return { hCardBtn, i };
                return { hCard, i };
            }
        }
        return {};
    }

    void click (Hit h, bool dbl)
    {
        switch (h.kind)
        {
            case hDlgCancel: closeDialog(); return;
            case hDlgOk: confirmDialog(); return;
            case hDlgCheck: dlgAsk = ! dlgAsk; repaint(); return;
            case hDlgBlock: return;
            case hTab0: case hTab1:
                tab = h.kind == hTab1 ? 1 : 0;
                placeSearch();
                if (tab == 0) search.grabKeyboardFocus(); else { grabKeyboardFocus(); if (client->isSignedIn()) ensureLoaded (true); }
                break;
            case hClose: close(); return;
            case hFolderBtn: folderMenu(); break;
            case hRescan: proc.library.rescan(); break;
            case hChoose: if (auto* ed = findParentComponentOfClass<AmpireEditor>()) ed->chooseFolder (true); break;
            case hCol1:
                if (col1[h.index].key != sel1) { sel1 = col1[h.index].key; sel2.clear(); sc2 = sc3 = 0; }
                if (! search.isEmpty()) search.clear();
                rebuild();
                break;
            case hCol2:
                if (col2[h.index].key != sel2) { sel2 = col2[h.index].key; sc3 = 0; }
                if (! search.isEmpty()) search.clear();
                rebuild();
                break;
            case hRowStar: proc.library.toggleFavourite (rows[h.index].path); break;
            case hRow: loadRow (h.index, dbl); return;
            case hConnect: client->signIn (false); break;
            case hCancel: client->cancelSignIn(); break;
            case hBrowse:
                if (! proc.library.getRoot().isDirectory()) { if (auto* ed = findParentComponentOfClass<AmpireEditor>()) ed->chooseFolder (true); break; }
                client->signIn (true);
                break;
            case hSignOut: client->signOut(); break;
            case hAvatar: juce::URL ("https://www.tone3000.com").launchInDefaultBrowser(); break;
            case hPill: ttab = h.index; scGrid = 0; ensureLoaded (true); break;
            case hVersions: versionsMenu(); break;
            case hResync: resyncMenu(); break;
            case hRetry: client->refresh ((t3k::Tab) ttab); break;
            case hCardBtn: cardAction (h.index); break;
            case hCard: if (dbl && h.index < tones.size()) juce::URL (tones[h.index].url).launchInDefaultBrowser(); break;
            default: break;
        }
        repaint();
    }

    void cardAction (int i)
    {
        if (i < 0 || i >= tones.size()) return;
        const auto t = tones[i];
        switch (stateOf (t))
        {
            case csSync: case csUpdate: case csFailed:
            {
                if (! proc.library.getRoot().isDirectory()) { if (auto* ed = findParentComponentOfClass<AmpireEditor>()) ed->chooseFolder (true); return; }
                t3k::Local l;
                if (! client->findLocal (t.id, l) && client->getAskFolderName()) openDialog (1, t);
                else client->sync (t);
                break;
            }
            case csSynced: loadTone (t); break;
            default: break;
        }
    }

    // SYNCED · LOAD : onglet Library, colonnes du tone, premiere capture (A2 d'abord) chargee
    void loadTone (const t3k::Tone& t)
    {
        t3k::Local l;
        if (! client->findLocal (t.id, l)) return;
        if (t.formatBadge() == "IR") { l.folder.revealToUser(); return; }
        auto files = l.folder.findChildFiles (juce::File::findFiles, false, "*.nam");
        if (files.isEmpty()) { l.folder.revealToUser(); return; }
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        juce::File pick = files[0];
        for (auto& f : files) { CaptureMeta mm; if (CaptureBuilder::readMeta (f, mm) && mm.isA2) { pick = f; break; } }
        proc.loadCapture (targetSlot, pick);
        const auto p = partsOf (proc.library.getRoot(), pick.getFullPathName());
        if (! p.isEmpty()) { sel1 = key1 (p); sel2 = key2 (p); }
        pendingReveal = pick.getFullPathName();
        tab = 0;
        search.clear();
        placeSearch();
        proc.library.rescan();
        rebuild();
        search.grabKeyboardFocus();
    }

    void folderMenu()
    {
        juce::PopupMenu mm;
        const auto root = proc.library.getRoot();
        mm.addSectionHeader (root.isDirectory() ? root.getFullPathName() : juce::String ("No capture folder yet"));
        mm.addItem (1, "Choose capture folder...");
        mm.addItem (2, "Load a single file...");
        mm.addItem (3, "Show capture folder", root.isDirectory());
        juce::Component::SafePointer<LibraryPanel> sp (this);
        MenuStyle::styled (mm, menuLookFor (this)).showMenuAsync (juce::PopupMenu::Options(), [sp] (int r)
        {
            if (sp == nullptr) return;
            if (r == 1) { if (auto* ed = sp->findParentComponentOfClass<AmpireEditor>()) ed->chooseFolder (true); }
            else if (r == 3) sp->proc.library.getRoot().revealToUser();
            else if (r == 2)
            {
                sp->chooser = std::make_unique<juce::FileChooser> ("Load NAM capture", sp->proc.startFolder (sp->targetSlot), "*.nam");
                sp->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [sp] (const juce::FileChooser& fc)
                {
                    if (sp == nullptr) return;
                    const auto f = fc.getResult();
                    if (f.existsAsFile()) { sp->proc.loadCapture (sp->targetSlot, f); sp->close(); }
                });
            }
        });
    }

    float versionsW() const { return lv::textW ("Versions: " + t3k::Client::versionModeName (client->getVersionMode()), lv::pop (1, 14)); }
    juce::Rectangle<float> resyncRect() const { const float r = 1414 - versionsW() - 24; return lv::box (r - 130, 118, r, 154); }

    // Re-sync : relire le dossier de captures, importer un dossier de tones deja synchronises, tout mettre a jour
    void resyncMenu()
    {
        juce::PopupMenu mm;
        MenuStyle::header (mm, MenuStyle::icoLoad, "Re-sync", "Tones synced by Ampire or NAMMAN");
        mm.addItem (1, "Scan my capture folder");
        mm.addItem (2, "Add a folder...");
        mm.addSeparator();
        mm.addItem (3, "Update all synced tones");
        juce::Component::SafePointer<LibraryPanel> sp (this);
        MenuStyle::styled (mm, menuLookFor (this)).showMenuAsync (juce::PopupMenu::Options(), [sp] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            auto& c = *sp->client;
            if (! sp->proc.library.getRoot().isDirectory()) { if (auto* ed = sp->findParentComponentOfClass<AmpireEditor>()) ed->chooseFolder (true); return; }
            if (r == 1)
            {
                sp->proc.library.rescan();
                c.rescanAll();
                sp->ttab = t3k::synced; sp->scGrid = 0;
            }
            else if (r == 2)
            {
                sp->chooser = std::make_unique<juce::FileChooser> ("Folder with tones already synced (TONE3000 / NAMMAN)", sp->proc.library.getRoot().getParentDirectory());
                sp->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [sp] (const juce::FileChooser& fc)
                {
                    if (sp == nullptr || ! fc.getResult().isDirectory()) return;
                    sp->client->importFolder (fc.getResult());
                    sp->ttab = t3k::synced; sp->scGrid = 0; sp->repaint();
                });
            }
            else if (r == 3) c.updateAll();
            sp->repaint();
        });
    }

    void versionsMenu()
    {
        juce::PopupMenu mm;
        mm.addSectionHeader ("Captures kept when syncing");
        const char* desc[] = { "Best version (A2, else A1)", "A2 only", "A1 only", "Custom only", "All versions" };
        for (int i = 0; i < t3k::numVersionModes; ++i) mm.addItem (i + 1, desc[i], true, client->getVersionMode() == i);
        juce::Component::SafePointer<LibraryPanel> sp (this);
        MenuStyle::styled (mm, menuLookFor (this)).showMenuAsync (juce::PopupMenu::Options(), [sp] (int r)
        {
            if (sp != nullptr && r > 0) { sp->client->setVersionMode (r - 1); sp->repaint(); }
        });
    }

    void contextMenu (Hit h)
    {
        juce::PopupMenu mm;
        juce::Component::SafePointer<LibraryPanel> sp (this);
        auto red = [] (int id, const juce::String& t) { juce::PopupMenu::Item it (t); it.setID (id); it.setColour (lv::RED); return it; };
        if (h.kind == hRow || h.kind == hRowStar)
        {
            const auto r = rows[h.index];
            const auto name = r.title.isNotEmpty() ? r.title : r.fileName;
            MenuStyle::header (mm, MenuStyle::icoLoad, name.length() > 34 ? name.substring (0, 32) + juce::String::fromUTF8 ("\xe2\x80\xa6") : name,
                               (r.isA2 ? juce::String ("A2") : r.family) + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::File (r.path).getParentDirectory().getFileName());
            mm.addItem (1, "Load");
            mm.addItem (2, "Favourite", true, proc.library.isFavourite (r.path));
            mm.addItem (3, "Show in folder");
            mm.addSeparator();
            mm.addItem (red (4, "Delete capture..."));
            MenuStyle::styled (mm, menuLookFor (this)).showMenuAsync (juce::PopupMenu::Options(), [sp, r, i = h.index] (int res)
            {
                if (sp == nullptr) return;
                if (res == 1) sp->loadRow (i, false);
                else if (res == 2) sp->proc.library.toggleFavourite (r.path);
                else if (res == 3) juce::File (r.path).revealToUser();
                else if (res == 4) sp->openFileDialog (4, juce::File (r.path), 1);
            });
        }
        else if ((h.kind == hCol1 || h.kind == hCol2) && ! folderOf (h.kind == hCol1 ? 1 : 2, h.index).getFullPathName().isEmpty())
        {
            const auto f = folderOf (h.kind == hCol1 ? 1 : 2, h.index);
            const int n = (h.kind == hCol1 ? col1 : col2)[h.index].count;
            MenuStyle::header (mm, MenuStyle::icoLoad, f.getFileName().length() > 34 ? f.getFileName().substring (0, 32) + juce::String::fromUTF8 ("\xe2\x80\xa6") : f.getFileName(),
                               "Folder" + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (n) + (n == 1 ? " capture" : " captures"));
            mm.addItem (1, "Show in folder");
            mm.addItem (2, "Rename folder...");
            mm.addSeparator();
            mm.addItem (red (3, "Delete folder..."));
            MenuStyle::styled (mm, menuLookFor (this)).showMenuAsync (juce::PopupMenu::Options(), [sp, f, n] (int res)
            {
                if (sp == nullptr) return;
                if (res == 1) f.revealToUser();
                else if (res == 2) sp->openFileDialog (5, f, n);
                else if (res == 3) sp->openFileDialog (3, f, n);
            });
        }
        else if ((h.kind == hCard || h.kind == hCardBtn) && h.index < tones.size())
        {
            const auto t = tones[h.index];
            t3k::Local l;
            const bool have = client->findLocal (t.id, l);
            mm.addItem (1, have ? "Sync again" : "Sync");
            mm.addItem (4, "Rename folder...", have);
            mm.addItem (2, "Show in folder", have);
            mm.addItem (3, "Open on TONE3000");
            mm.addSeparator();
            mm.addItem (5, "Ask for the folder name when syncing", true, client->getAskFolderName());
            MenuStyle::styled (mm, menuLookFor (this)).showMenuAsync (juce::PopupMenu::Options(), [sp, t, have, folder = l.folder] (int res)
            {
                if (sp == nullptr) return;
                if (res == 1) { if (! have && sp->client->getAskFolderName()) sp->openDialog (1, t); else sp->client->sync (t); }
                else if (res == 2) folder.revealToUser();
                else if (res == 3) juce::URL (t.url).launchInDefaultBrowser();
                else if (res == 4) sp->openDialog (2, t);
                else if (res == 5) sp->client->setAskFolderName (! sp->client->getAskFolderName());
            });
        }
    }

    // ================================================================ nom du dossier (au SYNC, ou Rename folder...)
    void openDialog (int mode, const t3k::Tone& t)
    {
        dlgMode = mode;
        dlgTone = t;
        dlgError.clear();
        dlgAsk = client->getAskFolderName();
        t3k::Local l;
        const auto name = mode == 2 && client->findLocal (t.id, l) ? l.folder.getFileName() : t3k::Client::safeName (t.title);
        nameEd.setText (name, false);
        placeSearch();
        nameEd.grabKeyboardFocus();
        nameEd.selectAll();
        repaint();
    }
    void closeDialog()
    {
        dlgMode = 0;
        placeSearch();
        if (tab == 0) search.grabKeyboardFocus(); else grabKeyboardFocus();
        repaint();
    }
    void confirmDialog()
    {
        if (dlgMode == 3 || dlgMode == 4)
        {
            if (client->isBusy (dlgFile)) { dlgError = "Wait for the TONE3000 download to finish"; repaint(); return; }
            if (! dlgFile.moveToTrash()) { dlgError = "Could not move it to the " + trashName(); repaint(); return; }
            proc.library.rescan();
            client->refreshLocal();
            closeDialog();
            return;
        }
        const auto name = nameEd.getText().trim();
        if (name.isEmpty()) { dlgError = "Type a folder name"; repaint(); return; }
        if (dlgMode == 5)
        {
            const auto target = dlgFile.getParentDirectory().getChildFile (t3k::Client::safeName (name));
            if (target != dlgFile)
            {
                if (target.exists()) { dlgError = "A folder with this name already exists"; repaint(); return; }
                if (client->isBusy (dlgFile)) { dlgError = "Wait for the TONE3000 download to finish"; repaint(); return; }
                if (! dlgFile.moveFileTo (target)) { dlgError = "Could not rename the folder (is a file in use?)"; repaint(); return; }
                if (sel1 == dlgFile.getFileName() && dlgFile.getParentDirectory() == proc.library.getRoot()) sel1 = target.getFileName();
                else if (sel2 == dlgFile.getFileName()) sel2 = target.getFileName();
                proc.library.rescan();
                client->refreshLocal();
            }
            closeDialog();
            return;
        }
        if (dlgMode == 1)
        {
            if (client->folderTaken (dlgTone, name)) { dlgError = "A folder with this name already exists"; repaint(); return; }
            if (dlgAsk != client->getAskFolderName()) client->setAskFolderName (dlgAsk);
            client->sync (dlgTone, name);
        }
        else
        {
            juce::String err;
            if (! client->renameLocal (dlgTone.id, name, err)) { dlgError = err; repaint(); return; }
            proc.library.rescan();
        }
        closeDialog();
    }
    static juce::String trashName()
    {
       #if JUCE_MAC
        return "Trash";
       #else
        return "Recycle Bin";
       #endif
    }
    void openFileDialog (int mode, const juce::File& f, int count)
    {
        dlgMode = mode;
        dlgFile = f;
        dlgCount = count;
        dlgError.clear();
        if (mode == 5) { nameEd.setText (f.getFileName(), false); placeSearch(); nameEd.grabKeyboardFocus(); nameEd.selectAll(); }
        else { placeSearch(); grabKeyboardFocus(); }
        repaint();
    }
    juce::File folderOf (int col, int index) const
    {
        const auto root = proc.library.getRoot();
        const auto& items = col == 1 ? col1 : col2;
        if (index < 0 || index >= items.size()) return {};
        const auto key = items[index].key;
        if (key.startsWith ("\x01")) return {};
        return col == 1 ? root.getChildFile (key) : root.getChildFile (sel1).getChildFile (key);
    }
    void paintConfirm (juce::Graphics& g)
    {
        using namespace lv;
        const auto r = box (447, 180, 1047, 432);
        juce::Path pp; pp.addRoundedRectangle (r, 16.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.65f), 26, { 5, 10 }).drawForPath (g, pp);
        rr (g, r, 16, BG, BORDER, 2);
        rr (g, box (471, 202, 515, 246), 10, tint (RED, 0.16f), tint (RED, 0.7f), 2);
        icon (g, iTrash, 493, 224, 22, RED, 3);
        const bool folder = dlgMode == 3;
        text (g, folder ? "Delete folder" : "Delete capture", 529, 216, pop (2, 19), TXT);
        text (g, fitText (folder ? dlgFile.getFileName() : dlgFile.getFileNameWithoutExtension(), pop (0, 13), 490), 529, 239, pop (0, 13), DIM);
        const auto line1 = folder ? "Move this folder and its " + juce::String (dlgCount) + (dlgCount == 1 ? " capture" : " captures") + " to the " + trashName() + "?"
                                  : "Move this capture to the " + trashName() + "?";
        const bool loaded = ! folder && dlgFile.getFullPathName() == proc.getSlotInfo (targetSlot).meta.path;
        text (g, line1, 471, 282, pop (0, 14), TXT, 0, 552);
        if (dlgError.isNotEmpty()) text (g, dlgError, 471, 306, pop (1, 13), RED, 0, 552);
        else text (g, loaded ? "It stays loaded in Ampire until you load another one." : "You can restore it from the " + trashName() + " if needed.", 471, 306, pop (0, 14), DIM, 0, 552);
        rr (g, box (749, 372, 881, 412), 10, hover == hDlgCancel ? tint (TXT, 0.08f, HOVER) : HOVER, BORDER, 2);
        text (g, "Cancel", 815, 392, pop (1, 14), TXT, 1, 132);
        rr (g, box (891, 372, 1023, 412), 10, hover == hDlgOk ? RED.brighter (0.15f) : RED);
        text (g, "DELETE", 957, 392, pop (2, 14), INK, 1, 132);
    }
    void paintDialog (juce::Graphics& g)
    {
        using namespace lv;
        g.setColour (juce::Colours::black.withAlpha (120.0f / 255.0f));
        g.fillRect (box (40, 36, 1454, 558));
        if (dlgMode == 3 || dlgMode == 4) { paintConfirm (g); return; }
        const auto r = box (447, 160, 1047, 462);
        juce::Path pp; pp.addRoundedRectangle (r, 16.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.65f), 26, { 5, 10 }).drawForPath (g, pp);
        rr (g, r, 16, BG, BORDER, 2);
        rr (g, box (471, 182, 515, 226), 10, tint (ACC, 0.16f), tint (ACC, 0.7f), 2);
        icon (g, dlgMode == 1 ? iDl : iPen, 493, 204, 22, ACC, 3);
        text (g, dlgMode == 1 ? "Sync tone" : "Rename folder", 529, 196, pop (2, 19), TXT);
        text (g, fitText (dlgMode == 5 ? dlgFile.getFileName() : dlgTone.title, pop (0, 13), 490), 529, 219, pop (0, 13), DIM);
        text (g, "Folder name", 471, 259, pop (1, 13), DIM);
        rr (g, box (471, 270, 1023, 314), 11, TILE, tint (ACC, 0.75f), 2);
        const auto name = t3k::Client::safeName (nameEd.getText().trim().isEmpty() ? juce::String ("...") : nameEd.getText().trim());
        t3k::Local l;
        const bool have = client->findLocal (dlgTone.id, l);
        const auto cat = have ? l.folder.getParentDirectory().getFileName() : dlgTone.category();
        const auto rootName = proc.library.getRoot().getFileName();
        const auto sep = juce::String ("  >  ");
        if (dlgError.isNotEmpty()) text (g, dlgError, 471, 334, pop (1, 13), RED, 0, 552);
        else if (dlgMode == 5)
        {
            juce::String in = dlgFile.getParentDirectory().getRelativePathFrom (proc.library.getRoot().getParentDirectory()).replace ("\\", sep).replace ("/", sep);
            text (g, fitText ("In:  " + in, pop (0, 13), 552), 471, 334, pop (0, 13), DIM, 0, 552);
        }
        else if (dlgMode == 1) text (g, fitText ("Saved in:  " + rootName + sep + cat + sep + name, pop (0, 13), 552), 471, 334, pop (0, 13), DIM, 0, 552);
        else text (g, fitText ("In:  " + rootName + sep + cat + "   (the tone stays Synced, updates go here)", pop (0, 13), 552), 471, 334, pop (0, 13), DIM, 0, 552);
        if (dlgMode == 1)
        {
            const auto cb = box (471, 358, 491, 378);
            if (dlgAsk)
            {
                rr (g, cb, 5, tint (ACC, 0.2f), tint (ACC, 0.8f), 2);
                juce::Path ck; ck.startNewSubPath (475, 368); ck.lineTo (480, 373); ck.lineTo (488, 362);
                g.setColour (ACC); g.strokePath (ck, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else rr (g, cb, 5, TILE, hover == hDlgCheck ? DIM : FAINT, 2);
            text (g, "Ask for the folder name each time I sync", 501, 368, pop (0, 13), hover == hDlgCheck ? TXT : DIM, 0, 400);
        }
        rr (g, box (749, 402, 881, 442), 10, hover == hDlgCancel ? tint (TXT, 0.08f, HOVER) : HOVER, BORDER, 2);
        text (g, "Cancel", 815, 422, pop (1, 14), TXT, 1, 132);
        rr (g, box (891, 402, 1023, 442), 10, hover == hDlgOk ? ACC.brighter (0.15f) : ACC);
        text (g, dlgMode == 1 ? "SYNC" : "RENAME", 957, 422, pop (2, 14), INK, 1, 132);
    }

    // ================================================================ mises a jour
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // tone choisi par Browse TONE3000 : nom du dossier demande (si reglé), puis affiche dans Synced
        const auto sc = client->getSelectCount();
        if (sc != drawnSelect)
        {
            drawnSelect = sc;
            const auto t = client->takeSelectedTone();
            if (t.id.isNotEmpty())
            {
                ttab = t3k::synced; scGrid = 0; tab = 1;
                if (isVisible() && client->getAskFolderName()) openDialog (1, t);
                else client->sync (t);
                placeSearch();
            }
        }
        if (! isVisible()) return;
        const auto lvv = client->getLibraryVersion();
        if (lvv != drawnLibVersion) { drawnLibVersion = lvv; proc.library.rescan(); }
        repaint();
    }

    void timerCallback() override
    {
        if (! tipShown && dlgMode == 0 && (hover == hCol1 || hover == hCol2 || hover == hRow || hover == hRowStar)
             && juce::Time::getMillisecondCounter() - hoverSince >= 800 && hoverTruncated())
        { tipShown = true; repaint(); }
        if (! isVisible()) return;
        client->setRoot (proc.library.getRoot());
        const auto v = proc.library.getVersion();
        if (v != drawnVersion)
        {
            drawnVersion = v;
            entries = proc.library.getEntries();
            if (revealPending) revealLoaded(); else rebuild();
            if (! proc.library.isScanning()) revealPending = false;
            if (pendingReveal.isNotEmpty() && ! proc.library.isScanning())
            {
                const int i = rowIndexOfLoaded();
                if (i >= 0) ensureRowVisible (i);
                pendingReveal.clear();
            }
        }
        else if (proc.library.isScanning()) repaint();
        const auto loaded = proc.getSlotInfo (targetSlot).meta.path;
        if (loaded != drawnLoaded) { drawnLoaded = loaded; repaint(); }
        if (tab == 1 && client->isSignedIn() && ++tick % 10 == 0) ensureLoaded();
    }

    AmpireProcessor& proc;
    juce::SharedResourcePointer<t3k::Client> client;
    juce::TextEditor search;
    juce::Image backdrop;
    juce::Array<CaptureMeta> entries, rows;
    juce::HashMap<juce::String, CaptureMeta> metaCache;
    juce::StringArray recents;
    juce::Array<Item> col1, col2;
    juce::String sel1, sel2, pendingReveal, drawnLoaded;
    float sc1 = 0, sc2 = 0, sc3 = 0, scGrid = 0;
    int tab = 0, ttab = t3k::favorites, tick = 0;
    bool revealPending = false, dlgAsk = true;
    int dlgMode = 0;
    t3k::Tone dlgTone;
    juce::File dlgFile;
    int dlgCount = 0;
    juce::uint32 hoverSince = 0;
    bool tipShown = false;
    juce::String dlgError;
    juce::TextEditor nameEd;
    juce::Array<t3k::Tone> tones;
    Hit hover;
    juce::uint32 drawnVersion = 0, drawnLibVersion = 0, drawnSelect = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::Drawable> mark, whole;
};
