#include "PluginEditor.h"
#include <BinaryData.h>
#include "WigaWindowSize.h"
#include "ColourPicker.h"
#include <complex>

// ============================================================================
// AMPIRE - interface d'apres la maquette photographique de l'utilisateur (assets/design/ampire_design.png).
// Le fond (assets/bg.png, calcule par tools/design_assets.py) porte tout ce qui est fixe ; le plugin dessine
// par-dessus ce qui bouge : repere et valeur des potards, ecran de la capture, vu-metres a LEDs (48 LEDs,
// memoire de crete, reflet dans le verre), voyant et inverseur POWER.
// Repere logique 760 x 317 points (taille « 60 % ») ; anti-gel v2 (fond mis en cache par le timer).
// ============================================================================
namespace
{
    constexpr int kW = 760, kH = 317;
    // partie visible : le boitier seul, sans le fond perdu (marge noire du fond : 22 / 22 / 18 / 45 px sur 2560)
    constexpr float kVisX = 22.0f * 760.0f / 2560.0f, kVisY = 18.0f * 760.0f / 2560.0f;
    constexpr float kVisW = 760.0f - 2.0f * kVisX, kVisH = 317.0f - kVisY - 45.0f * 760.0f / 2560.0f;
    constexpr int kWin = 747, kWinH = 298;      // fenetre « 60 % » (meme largeur qu'Impulse)
    const float kScales[3] = { 1.0f, 1.25f, 5.0f / 3.0f };   // 60 % (defaut) / 75 % / 100 %

    namespace col
    {
        const juce::Colour outer   (0xff151618), tolex (0xff1a1b1d), goldHi (0xffd8d6d0), gold (0xffb9b7b0), goldLo (0xff6f6e6a),
                           ink     (0xff101112), bay (0xff161719), bayLo (0xff111214), lcd (0xff0b0c0d),
                           header  (0xff1a1b1d), nameBox (0xff0b0c0d), btnHi (0xff2f3033), btnLo (0xff1f2022),
                           panelLo (0xff141517), waveTop (0xff0b0c0d), label (0xffa9aaa8), text (0xffeceae4),
                           dim     (0xff6a6b6a), knobTrack (0xff0b0c0d), err (0xffe05a4a), ruby (0xffffa040);
        juce::Colour accent (AmpireSettings::kDefaultAccent);   // couleur des afficheurs (nuancier)
    }

    struct Fonts
    {
        juce::Typeface::Ptr semi, bold, medium;
        Fonts()
        {
            semi   = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
            bold   = juce::Typeface::createSystemTypefaceFor (BinaryData::InterBold_otf,     BinaryData::InterBold_otfSize);
            medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_otf,   BinaryData::InterMedium_otfSize);
        }
        static const Fonts& get() { static Fonts f; return f; }
    };
    juce::Font fSemi (float h)   { return juce::Font (juce::FontOptions (Fonts::get().semi).withHeight (h)); }
    juce::Font fBold (float h)   { return juce::Font (juce::FontOptions (Fonts::get().bold).withHeight (h)); }
    juce::Font fMedium (float h) { return juce::Font (juce::FontOptions (Fonts::get().medium).withHeight (h)); }
    juce::Font fName (float h)
    {
        auto f = fSemi (h);
        f.setFeatureSetting ({ juce::FontFeatureTag ("calt"), juce::FontFeatureSetting::featureDisabled });
        return f;
    }
    // echelle RTW (peak meter plasma) : position 0..1 en fonction des dBFS, dilatee en haut ; table partagee avec tools/render_art.py
    inline float rtwPos (float db)
    {
        static const float D[] = { -50.0f, -40.0f, -30.0f, -20.0f, -10.0f, 0.0f };
        static const float P[] = {  0.0f,  0.10f,  0.22f,  0.38f,  0.64f, 1.0f };
        if (db <= D[0]) return 0.0f;
        for (int i = 1; i < 6; ++i)
            if (db <= D[i]) return P[i - 1] + (P[i] - P[i - 1]) * (db - D[i - 1]) / (D[i] - D[i - 1]);
        return 1.0f;
    }
    juce::String dotSep() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }

    // ---- geometrie : points logiques = unites de la maquette (1999 de large) x 760 / 1999
    constexpr float kDs = 760.0f / 1999.0f;
    constexpr float D (float v) { return v * kDs; }
    const juce::Rectangle<float> R (float x0, float y0, float x1, float y1) { return { D (x0), D (y0), D (x1 - x0), D (y1 - y0) }; }

    const float kKnobX[6] = { D (331.5f), D (547.2f), D (760.7f), D (974.5f), D (1187.7f), D (1402.4f) };
    const float kKnobY = D (531.0f);
    const float kIndR0 = D (31.6f), kIndR1 = D (71.8f), kIndW = D (4.4f);    // repere blanc arrondi (image du potard fournie)
    const float kValueBase = D (412.2f);                                       // valeur au-dessus du potard
    const juce::Point<float> kJewel (D (1618.4f), D (531.0f));             // voyant et inverseur sur l'axe des potards
    const juce::Rectangle<float> rLedSprite = R (1570, 482.5f, 1667, 579.5f);
    const juce::Point<float> kToggle (D (1721.0f), D (531.0f));
    const float kToggleSprite = D (170.0f);
    const juce::Point<float> kGear (D (1913.0f), D (75.0f));
    const float kGearR = D (39.0f);
    // ecran : infos de la capture a gauche, etat (CPU, latence, messages) a droite du filet
    const juce::Rectangle<float> rLcd[1] = { R (156, 126, 1684, 296) };
    const juce::Rectangle<float> rRow[1] = { R (140, 110, 1700, 312) };
    const juce::Rectangle<float> rLcdInfo = R (225, 126, 1195, 296);
    const juce::Rectangle<float> rStatus = R (1222, 126, 1684, 296), rStatusText = R (1222, 126, 1684, 296);
    const juce::Rectangle<float> rLib = R (1707, 165, 1783, 241), rPrev = R (1791, 165, 1867, 241), rNext = R (1874, 165, 1950, 241);
    // vu-metres : 48 LEDs, reflet sur le bord du verre cote exterieur, voyant d'ecretage au-dessus
    const juce::Rectangle<float> rMeterWinIn = R (40, 300, 140, 752), rMeterWinOut = R (1856, 300, 1958, 752);
    const juce::Rectangle<float> rMeterIn = R (66, 390, 87, 714), rMeterOut = R (1916, 390, 1937, 714);
    const float kReflXIn = D (51.1f), kReflXOut = D (1947.5f);
    const juce::Point<float> kClipIn (D (76.1f), D (369.7f)), kClipOut (D (1926.4f), D (369.5f));
    // visibilite du reflet de chaque LED (bas -> haut), relevee sur la maquette par tools/design_assets.py
    const float kMeterVis[48] = { 0.048f, 0.182f, 0.064f, 0.046f, 0.110f, 0.308f, 0.352f, 0.217f, 0.127f, 0.472f, 0.533f, 0.224f, 0.216f, 0.337f, 0.395f, 0.327f, 0.225f, 0.242f, 0.396f, 0.695f, 0.114f, 0.136f, 0.689f, 0.371f, 0.691f, 0.403f, 0.297f, 0.709f, 0.244f, 0.484f, 0.375f, 0.110f, 0.115f, 0.436f, 0.202f, 0.243f, 0.145f, 0.278f, 0.205f, 0.053f, 0.012f, 0.033f, 0.012f, 0.011f, 0.009f, 0.003f, 0.008f, 0.002f };
    // echelle gravee de la maquette (dBFS -> position 0..1 sur la hauteur de la barre)
    inline float meterPos (float db)
    {
        static const float Dd[] = { -50.0f, -40.0f, -30.0f, -20.0f, -12.0f, -6.0f, 0.0f };
        static const float Pp[] = { 0.0f, 0.125f, 0.248f, 0.41f, 0.605f, 0.79f, 1.0f };
        if (db <= Dd[0]) return 0.0f;
        for (int i = 1; i < 7; ++i)
            if (db <= Dd[i]) return Pp[i - 1] + (Pp[i] - Pp[i - 1]) * (db - Dd[i - 1]) / (Dd[i] - Dd[i - 1]);
        return 1.0f;
    }
    const juce::Colour kSilk (0xfff7f7f5);           // serigraphie blanche de la maquette

    // visuels photo-realistes calcules par tools/render_art.py (2 pixels par point)
    struct Art
    {
        juce::Image bg, ledOn, toggleUp, toggleDown;
        Art()
        {
            bg = juce::ImageCache::getFromMemory (BinaryData::bg_png, BinaryData::bg_pngSize);
            ledOn = juce::ImageCache::getFromMemory (BinaryData::led_on_png, BinaryData::led_on_pngSize);
            toggleUp = juce::ImageCache::getFromMemory (BinaryData::toggle_up_png, BinaryData::toggle_up_pngSize);
            toggleDown = juce::ImageCache::getFromMemory (BinaryData::toggle_down_png, BinaryData::toggle_down_pngSize);
        }
        static const Art& get() { static Art a; return a; }
    };
    void drawSpriteIn (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> r)
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (img, r);
    }

    juce::Rectangle<int> toInt (juce::Rectangle<float> r) { return r.getSmallestIntegerContainer(); }

    void drawButtonBg (juce::Graphics& g, juce::Rectangle<float> r, bool pressed, bool hover)
    {
        const float hb = hover && ! pressed ? 0.10f : 0.0f;
        juce::ColourGradient gr ((pressed ? col::btnLo : col::btnHi).brighter (hb), r.getX(), r.getY(),
                                 (pressed ? col::btnHi : col::btnLo).brighter (hb), r.getX(), r.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (col::gold.withAlpha (0.18f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawLine (r.getX() + 3, r.getY() + 1.0f, r.getRight() - 3, r.getY() + 1.0f, 1.0f);
    }

}

static void showContextMenu (juce::Component& c, int learnIdx);
static void showGearMenu (juce::Component& c);

// ============================================================================ commandes de base
struct LearnBlink
{
    static bool shown (AmpireProcessor& p, int idx)
    {
        return idx >= 0 && p.learnTarget.load() == idx && (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    }
    static void draw (juce::Graphics& g, juce::Rectangle<float> r, float radius)
    {
        g.setColour (col::accent);
        g.drawRoundedRectangle (r.reduced (1.0f), radius, 1.6f);
    }
};

class ParamControl : public juce::Component
{
public:
    ParamControl (AmpireProcessor& p, const char* paramId, int learn)
        : proc (p), param (*p.apvts.getParameter (paramId)), learnIdx (learn),
          attachment (param, [this] (float) { repaint(); if (onValue) onValue(); }, nullptr)
    {
        attachment.sendInitialUpdate();
    }
    void useAccent() const { col::accent = juce::Colour (proc.accentColour.load()); }
    float norm() const { return param.getValue(); }
    float value() const { return param.convertFrom0to1 (param.getValue()); }
    void setNorm (float n) { attachment.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n))); }
    std::function<void()> onValue;
    AmpireProcessor& proc;
    juce::RangedAudioParameter& param;
    int learnIdx;
    juce::ParameterAttachment attachment;
};

// ---- potard « chicken-head » en bakelite (0 - 10 sur 300 degres)
//      seul le pointeur tourne ; reflets et ombre portee restent fixes (lumiere en haut a gauche)
class Knob : public ParamControl
{
public:
    Knob (AmpireProcessor& p, const char* id, int learn) : ParamControl (p, id, learn) {}
    std::function<juce::String (float)> format;   // texte de la ligne d'etat
    bool dimmed = false;                           // SIZE sans capture A2 : repere grise

    void paint (juce::Graphics& g) override
    {
        useAccent();
        // le potard est dans le fond (photo) : seul le repere blanc tourne (300 degres), les reflets restent fixes
        const auto c = getLocalBounds().toFloat().getCentre();
        const float ang = juce::degreesToRadians (-150.0f + 300.0f * norm());
        const float sn = std::sin (ang), cs = std::cos (ang);
        const juce::Point<float> p0 (c.x + sn * kIndR0, c.y - cs * kIndR0), p1 (c.x + sn * kIndR1, c.y - cs * kIndR1);
        const float a = dimmed ? 0.35f : 1.0f;
        auto bar = [&] (juce::Point<float> q0, juce::Point<float> q1, float w)
        {
            juce::Path pth; pth.startNewSubPath (q0); pth.lineTo (q1);
            juce::Path out; juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (out, pth);
            return out;
        };
        const juce::Point<float> sh (0.35f, 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.55f * a));
        g.fillPath (bar (p0 + sh, p1 + sh, kIndW + 0.5f));
        g.setColour (juce::Colour (0xffe9e9e7).withAlpha (a));
        g.fillPath (bar (p0, p1, kIndW));
        g.setColour (juce::Colours::white.withAlpha (0.55f * a));
        g.fillPath (bar (p0 + juce::Point<float> (-0.15f, -0.15f), p1 + juce::Point<float> (-0.15f, -0.15f), kIndW * 0.45f));
        if (LearnBlink::shown (proc, learnIdx)) LearnBlink::draw (g, getLocalBounds().toFloat().reduced (2.0f), 12.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showContextMenu (*this, learnIdx); return; }
        dragging = true; startNorm = norm();
        attachment.beginGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float sens = e.mods.isShiftDown() ? 900.0f : 200.0f;
        setNorm (startNorm - (float) e.getDistanceFromDragStartY() / sens + (float) e.getDistanceFromDragStartX() / (sens * 2.0f));
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) attachment.endGesture(); dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        attachment.beginGesture(); setNorm (norm() + w.deltaY * 0.05f); attachment.endGesture();
    }
private:
    bool dragging = false;
    float startNorm = 0.0f;
};

// ---- inverseur a levier (nickel), deux positions : haut = valeur 0, bas = valeur 1
class Lever : public ParamControl
{
public:
    Lever (AmpireProcessor& p, const char* id, int learn, bool upIsOn = false) : ParamControl (p, id, learn), upOn (upIsOn) {}
    void paint (juce::Graphics& g) override
    {
        const bool down = upOn ? norm() < 0.5f : norm() > 0.5f;
        drawSpriteIn (g, down ? Art::get().toggleDown : Art::get().toggleUp, getLocalBounds().toFloat());
        if (LearnBlink::shown (proc, learnIdx)) LearnBlink::draw (g, getLocalBounds().toFloat().reduced (6.0f), 6.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showContextMenu (*this, learnIdx); return; }
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (norm() > 0.5f ? 0.0f : 1.0f));
    }
private:
    bool upOn;
};

// ---- bouton lumineux de la baie (ON de capture, EQ, baffle)
class ToggleBtn : public ParamControl
{
public:
    ToggleBtn (AmpireProcessor& p, const char* id, int learn, juce::String txt = "ON")
        : ParamControl (p, id, learn), text (std::move (txt)) {}
    void paint (juce::Graphics& g) override
    {
        useAccent();
        const auto r = getLocalBounds().toFloat();
        const bool on = norm() > 0.5f;
        drawButtonBg (g, r, on, isMouseOver());
        // petite LED a gauche du texte
        const juce::Point<float> led (r.getX() + 11.0f, r.getCentreY());
        g.setColour (juce::Colours::black); g.fillEllipse (led.x - 4, led.y - 4, 8, 8);
        if (on)
        {
            g.setColour (col::accent.withAlpha (0.35f)); g.fillEllipse (led.x - 6, led.y - 6, 12, 12);
            g.setColour (col::accent); g.fillEllipse (led.x - 3, led.y - 3, 6, 6);
        }
        g.setColour (on ? col::text : col::dim);
        g.setFont (fBold (11.0f));
        g.drawText (text, r.withTrimmedLeft (18), juce::Justification::centred);
        if (LearnBlink::shown (proc, learnIdx)) LearnBlink::draw (g, r, 3.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showContextMenu (*this, learnIdx); return; }
        attachment.setValueAsCompleteGesture (norm() > 0.5f ? 0.0f : 1.0f);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
private:
    juce::String text;
};

// ---- bouton segmente (FULL | LITE)
class SegButton : public ParamControl
{
public:
    SegButton (AmpireProcessor& p, const char* id, int learn, juce::StringArray labelsIn)
        : ParamControl (p, id, learn), labels (std::move (labelsIn)) {}
    void paint (juce::Graphics& g) override
    {
        useAccent();
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::lcd);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (col::gold.withAlpha (0.18f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        const int n = labels.size();
        const int sel = juce::jlimit (0, n - 1, (int) std::lround (norm() * (float) (n - 1)));
        const float w = r.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + w * i, r.getY(), w, r.getHeight()).reduced (2.0f);
            if (i == sel) { g.setColour (col::accent.withAlpha (0.18f)); g.fillRoundedRectangle (cell, 2.0f); }
            g.setColour (i == sel ? col::accent : col::dim);
            g.setFont (fBold (10.5f));
            g.drawText (labels[i], cell, juce::Justification::centred);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showContextMenu (*this, learnIdx); return; }
        const int n = labels.size();
        const int i = juce::jlimit (0, n - 1, (int) ((float) e.x / ((float) getWidth() / (float) n)));
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 ((float) i / (float) juce::jmax (1, n - 1)));
    }
private:
    juce::StringArray labels;
};

// ---- bouton a icone (laiton sur fond sombre)
class IconBtn : public juce::Component
{
public:
    enum Kind { folder, prev, next, save, gear, library };
    IconBtn (Kind k, int learn = -1) : learnIdx (learn), kind (k) {}
    std::function<void()> onClick;
    AmpireProcessor* proc = nullptr;
    int learnIdx;
    bool small = false;
    bool ghost = false;          // bouton dessine dans le fond (photo) : seulement l'enfoncement et le MIDI Learn
    bool round = false;
    void paint (juce::Graphics& g) override
    {
        if (proc != nullptr) col::accent = juce::Colour (proc->accentColour.load());
        const auto r = getLocalBounds().toFloat();
        if (ghost)
        {
            const float rad = round ? r.getWidth() * 0.5f : D (10.0f);
            if (pressed) { g.setColour (juce::Colours::black.withAlpha (0.30f)); g.fillRoundedRectangle (r.reduced (1.0f), rad); }
            else if (isMouseOver()) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r.reduced (1.0f), rad); }
            if (proc != nullptr && LearnBlink::shown (*proc, learnIdx)) LearnBlink::draw (g, r, rad);
            return;
        }
        drawButtonBg (g, r, pressed, isMouseOver());
        const auto c = r.getCentre() + juce::Point<float> (0.0f, pressed ? 0.5f : 0.0f);
        const float k = small ? 0.62f : 0.8f;
        const juce::Colour ic = col::goldHi;
        juce::Path p;
        if (kind == folder)
        {
            p.startNewSubPath (c.x - 11 * k, c.y - 8 * k);
            p.lineTo (c.x - 4 * k, c.y - 8 * k); p.lineTo (c.x - 2 * k, c.y - 5.5f * k); p.lineTo (c.x + 10 * k, c.y - 5.5f * k);
            p.lineTo (c.x + 10 * k, c.y + 8 * k); p.lineTo (c.x - 11 * k, c.y + 8 * k); p.closeSubPath();
            g.setColour (ic.darker (0.45f)); g.fillPath (p);
            juce::Path front;
            front.startNewSubPath (c.x - 9 * k, c.y - 2.5f * k); front.lineTo (c.x + 13 * k, c.y - 2.5f * k);
            front.lineTo (c.x + 9.5f * k, c.y + 8 * k); front.lineTo (c.x - 12 * k, c.y + 8 * k); front.closeSubPath();
            g.setColour (ic); g.fillPath (front);
            return;
        }
        if (kind == library)
        {
            g.setColour (ic);
            const float q = small ? 0.74f : 1.0f;
            for (int i = 0; i < 3; ++i) g.fillRoundedRectangle (c.x - 11 * q, c.y + (-8 + i * 6.0f) * q, (i == 2 ? 10.0f : 16.0f) * q, 3.0f * q, 1.2f * q);
            g.drawEllipse (c.x + 2.0f * q, c.y + 0.5f * q, 7.0f * q, 7.0f * q, 1.8f * q);
            g.drawLine (c.x + 8.0f * q, c.y + 6.5f * q, c.x + 11.0f * q, c.y + 9.5f * q, 2.0f * q);
            return;
        }
        if (kind == prev || kind == next)
        {
            const float s = kind == next ? 1.0f : -1.0f;
            p.addTriangle (c.x - 5.5f * s * k, c.y - 9.0f * k, c.x - 5.5f * s * k, c.y + 9.0f * k, c.x + 6.5f * s * k, c.y);
            g.setColour (ic);
            g.fillPath (p);
            if (proc != nullptr && LearnBlink::shown (*proc, learnIdx)) LearnBlink::draw (g, r, 3.0f);
            return;
        }
        g.setColour (ic);
        if (kind == save)
        {
            juce::Path body; body.addRoundedRectangle (c.x - 7.5f, c.y - 7.5f, 13.5f, 15.0f, 1.5f);
            g.strokePath (body, juce::PathStrokeType (1.6f));
            g.fillRect (c.x - 4.0f, c.y - 7.5f, 6.0f, 4.5f);
            g.fillRoundedRectangle (c.x - 4.5f, c.y + 1.5f, 7.0f, 4.5f, 1.0f);
            g.setColour (col::btnLo);
            g.fillEllipse (c.x + 1.0f, c.y + 1.0f, 10.0f, 10.0f);
            g.setColour (ic);
            g.drawLine (c.x + 6.0f, c.y + 2.5f, c.x + 6.0f, c.y + 9.5f, 1.7f);
            g.drawLine (c.x + 2.5f, c.y + 6.0f, c.x + 9.5f, c.y + 6.0f, 1.7f);
            return;
        }
        juce::Path gp;
        const int teeth = 8;
        for (int i = 0; i < teeth * 2; ++i)
        {
            const float a0 = juce::MathConstants<float>::twoPi * (float) i / (teeth * 2) - 0.12f;
            const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (teeth * 2) - 0.12f;
            const float rr = (i % 2 == 0) ? 8.6f : 6.4f;
            if (i == 0) gp.startNewSubPath (c.x + rr * std::sin (a0), c.y - rr * std::cos (a0));
            else gp.lineTo (c.x + rr * std::sin (a0), c.y - rr * std::cos (a0));
            gp.lineTo (c.x + rr * std::sin (a1), c.y - rr * std::cos (a1));
        }
        gp.closeSubPath();
        gp.addEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
        gp.setUsingNonZeroWinding (false);
        g.fillPath (gp);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showContextMenu (*this, learnIdx); return; }
        pressed = true; repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! pressed) return;
        pressed = false; repaint();
        if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
private:
    Kind kind;
    bool pressed = false;
};

class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel,
                     private juce::Timer
{
public:
    LibraryPanel (AmpireProcessor& p) : proc (p)
    {
        setOpaque (true);
        search.setTextToShowWhenEmpty ("Search captures (name, gear, maker, tone...)", col::dim);
        search.setFont (fMedium (13.0f));
        search.setColour (juce::TextEditor::backgroundColourId, col::nameBox);
        search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::focusedOutlineColourId, col::accent.withAlpha (0.6f));
        search.setColour (juce::TextEditor::textColourId, col::text);
        search.setIndents (10, 7);
        search.onTextChange = [this] { refilter(); };
        search.onEscapeKey = [this] { if (search.isEmpty()) close(); else search.clear(); refilter(); };
        search.onReturnKey = [this] { if (! shown.isEmpty()) loadRow (juce::jmax (0, list.getSelectedRow()), true); };
        addAndMakeVisible (search);
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, col::waveTop);
        list.setOutlineThickness (0);
        list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, col::btnHi);
        addAndMakeVisible (list);
        startTimerHz (5);
    }
    int targetSlot = 0;
    std::function<void()> onClose;

    void open (int slot)
    {
        targetSlot = slot;
        entries = proc.library.getEntries();
        drawnVersion = proc.library.getVersion();
        refilter();
        setVisible (true);
        toFront (true);
        search.grabKeyboardFocus();
        // selection : capture du slot
        const auto cur = proc.getSlotInfo (slot).meta.path;
        for (int i = 0; i < shown.size(); ++i) if (shown[i].path == cur) { list.selectRow (i, false, true); list.scrollToEnsureRowIsOnscreen (i); break; }
    }
    void close() { setVisible (false); if (onClose) onClose(); }

    void resized() override
    {
        auto r = getLocalBounds();
        search.setBounds (r.getX() + 12, 46, 330, 28);
        list.setBounds (r.getX() + 12, 112, r.getWidth() - 24, r.getHeight() - 124);
    }

    void paint (juce::Graphics& g) override
    {
        col::accent = juce::Colour (proc.accentColour.load());
        g.fillAll (col::panelLo);
        const auto r = getLocalBounds().toFloat();
        // titre + slot cible
        g.setColour (col::text);
        g.setFont (fBold (15.0f));
        g.drawText ("CAPTURE LIBRARY", juce::Rectangle<float> (14, 10, 200, 26), juce::Justification::centredLeft);
        for (auto [b, t] : { std::pair { btnFolder(), "FOLDER..." }, std::pair { btnFile(), "FILE..." }, std::pair { btnRescan(), "RESCAN" } })
        {
            drawButtonBg (g, b, false, false);
            g.setColour (col::label); g.setFont (fBold (11.0f));
            g.drawText (t, b, juce::Justification::centred);
        }
        // fermer
        const auto x = btnClose();
        drawButtonBg (g, x, false, false);
        g.setColour (col::text);
        g.drawLine (x.getCentreX() - 5, x.getCentreY() - 5, x.getCentreX() + 5, x.getCentreY() + 5, 1.8f);
        g.drawLine (x.getCentreX() + 5, x.getCentreY() - 5, x.getCentreX() - 5, x.getCentreY() + 5, 1.8f);
        // filtres
        for (int i = 0; i < numFilters; ++i)
        {
            const auto b = filterBtn (i);
            const bool on = filter == i;
            if (on) { g.setColour (col::accent.withAlpha (0.18f)); g.fillRoundedRectangle (b, 11.0f); g.setColour (col::accent); g.drawRoundedRectangle (b.reduced (0.5f), 11.0f, 1.0f); }
            else { g.setColour (col::nameBox); g.fillRoundedRectangle (b, 11.0f); }
            g.setColour (on ? col::accent : col::label);
            g.setFont (fSemi (11.5f));
            g.drawText (filterName (i), b, juce::Justification::centred);
        }
        // pied : dossier + nombre
        const auto root = proc.library.getRoot();
        juce::String foot = root.isDirectory() ? root.getFullPathName() : juce::String ("No capture folder yet: click FOLDER... to choose where your .nam files are");
        g.setColour (col::dim);
        g.setFont (fMedium (11.0f));
        g.drawText (foot, juce::Rectangle<float> (352, 46, r.getWidth() - 364, 28), juce::Justification::centredRight, true);
        juce::String cnt = juce::String (shown.size()) + " / " + juce::String (entries.size()) + " captures";
        if (proc.library.isScanning()) cnt = "Scanning... " + juce::String (proc.library.scannedCount());
        g.drawText (cnt, juce::Rectangle<float> (r.getWidth() - 214, 82, 200, 22), juce::Justification::centredRight);
        if (shown.isEmpty())
        {
            g.setColour (col::dim);
            g.setFont (fMedium (13.0f));
            g.drawText (entries.isEmpty() ? "Choose your capture folder (FOLDER...) or load a single file (FILE...)" : "No capture matches",
                        list.getBounds().toFloat(), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        for (int i = 0; i < numFilters; ++i) if (filterBtn (i).contains (p)) { filter = i; refilter(); return; }
        if (btnClose().contains (p)) { close(); return; }
        if (btnRescan().contains (p)) { proc.library.rescan(); repaint(); return; }
        if (btnFolder().contains (p)) { if (auto* ed = findParentComponentOfClass<AmpireEditor>()) ed->chooseFolder (true); return; }
        if (btnFile().contains (p))
        {
            chooser = std::make_unique<juce::FileChooser> ("Load NAM capture", proc.startFolder (targetSlot), "*.nam");
            juce::Component::SafePointer<LibraryPanel> sp (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [sp] (const juce::FileChooser& fc)
            {
                if (sp == nullptr) return;
                const auto f = fc.getResult();
                if (f.existsAsFile()) { sp->proc.loadCapture (sp->targetSlot, f); sp->close(); }
            });
        }
    }

private:
    enum { fAll, fFav, fRecent, fA2, fA1, fAmp, fPedal, fRig, numFilters };
    static juce::String filterName (int i)
    {
        const char* n[numFilters] = { "All", "Favourites", "Recent", "A2", "A1", "Amps", "Pedals", "Full rigs" };
        return n[i];
    }
    juce::Rectangle<float> btnFolder() const { return { (float) getWidth() - 334, 10, 90, 26 }; }
    juce::Rectangle<float> btnFile() const   { return { (float) getWidth() - 238, 10, 74, 26 }; }
    juce::Rectangle<float> btnRescan() const { return { (float) getWidth() - 158, 10, 74, 26 }; }
    juce::Rectangle<float> btnClose() const  { return { (float) getWidth() - 40, 10, 26, 26 }; }
    juce::Rectangle<float> filterBtn (int i) const { return { 12.0f + i * 76.0f, 83.0f, 70.0f, 22.0f }; }

    bool matches (const CaptureMeta& m, const juce::StringArray& words) const
    {
        const auto type = m.gearType.toLowerCase();
        switch (filter)
        {
            case fFav:    if (! proc.library.isFavourite (m.path)) return false; break;
            case fRecent: if (! recents.contains (m.path)) return false; break;
            case fA2:     if (! m.isA2) return false; break;
            case fA1:     if (m.family != "A1") return false; break;
            case fAmp:    if (! (type == "amp" || type == "preamp")) return false; break;
            case fPedal:  if (! type.startsWith ("pedal")) return false; break;
            case fRig:    if (! type.contains ("cab")) return false; break;
            default: break;
        }
        if (words.isEmpty()) return true;
        const auto hay = (m.title + " " + m.fileName + " " + m.gearMake + " " + m.gearModel + " " + m.gearType + " "
                          + m.toneType + " " + m.modeledBy + " " + m.family).toLowerCase();
        for (auto& w : words) if (! hay.contains (w)) return false;
        return true;
    }

    void refilter()
    {
        recents = proc.library.getRecents();
        const auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "\"");
        shown.clearQuick();
        if (filter == fRecent)
        {
            // dans l'ordre d'utilisation, meme hors du dossier de la bibliotheque
            for (auto& path : recents)
            {
                CaptureMeta m;
                bool found = false;
                for (auto& e : entries) if (e.path == path) { m = e; found = true; break; }
                if (! found && ! CaptureBuilder::readMeta (juce::File (path), m)) continue;
                if (matches (m, words)) shown.add (m);
            }
        }
        else for (auto& e : entries) if (matches (e, words)) shown.add (e);
        list.updateContent();
        list.repaint();
        repaint();
    }

    void loadRow (int row, bool andClose)
    {
        if (row < 0 || row >= shown.size()) return;
        proc.loadCapture (targetSlot, juce::File (shown[row].path));
        if (andClose) close();
    }

    // ---- ListBoxModel
    int getNumRows() override { return shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= shown.size()) return;
        const auto& m = shown.getReference (row);
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
        if (selected) { g.setColour (col::accent.withAlpha (0.16f)); g.fillRect (r); g.setColour (col::accent); g.fillRect (0.0f, 0.0f, 3.0f, (float) h); }
        else if (row % 2) { g.setColour (juce::Colours::white.withAlpha (0.02f)); g.fillRect (r); }
        // favori
        const bool fav = proc.library.isFavourite (m.path);
        juce::Path star;
        star.addStar ({ 18.0f, h * 0.5f }, 5, 3.2f, 7.5f, 0.0f);
        g.setColour (fav ? col::accent : col::dim.darker (0.4f));
        if (fav) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.1f));
        // famille (A2 / A1...)
        const auto badge = juce::Rectangle<float> (34, h * 0.5f - 8, 30, 16);
        g.setColour (m.isA2 ? col::accent : col::btnHi);
        g.fillRoundedRectangle (badge, 2.5f);
        g.setColour (m.isA2 ? juce::Colour (0xff151515) : col::text);
        g.setFont (fBold (10.0f));
        g.drawText (m.family.isNotEmpty() ? m.family.substring (0, 4) : "?", badge, juce::Justification::centred);
        // nom + materiel
        const auto title = m.title.isNotEmpty() ? m.title : m.fileName;
        g.setColour (col::text);
        g.setFont (fName (13.0f));
        g.drawText (title, juce::Rectangle<float> (74, 0, (float) w * 0.46f - 74, (float) h), juce::Justification::centredLeft, true);
        g.setColour (col::label);
        g.setFont (fMedium (11.5f));
        const auto gear = (m.gearMake + " " + m.gearModel).trim();
        g.drawText (gear, juce::Rectangle<float> ((float) w * 0.47f, 0, (float) w * 0.25f, (float) h), juce::Justification::centredLeft, true);
        auto pretty = [] (juce::String s) { return s.replace ("_", " "); };
        g.drawText (pretty (m.gearType), juce::Rectangle<float> ((float) w * 0.73f, 0, (float) w * 0.12f, (float) h), juce::Justification::centredLeft, true);
        g.drawText (pretty (m.toneType), juce::Rectangle<float> ((float) w * 0.85f, 0, (float) w * 0.14f, (float) h), juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.x < 30 && row >= 0 && row < shown.size()) { proc.library.toggleFavourite (shown[row].path); list.repaintRow (row); return; }
        loadRow (row, false);           // un clic : ecoute immediate
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { loadRow (row, true); }
    void selectedRowsChanged (int row) override
    {
        // fleches haut / bas du clavier : la capture selectionnee se charge (ecoute rapide)
        if (row >= 0 && list.hasKeyboardFocus (true) && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) loadRow (row, false);
    }
    void returnKeyPressed (int row) override { loadRow (row, true); }

    void timerCallback() override
    {
        if (! isVisible()) return;
        const auto v = proc.library.getVersion();
        if (v != drawnVersion) { drawnVersion = v; entries = proc.library.getEntries(); refilter(); }
        else if (proc.library.isScanning()) repaint (toInt ({ (float) getWidth() - 214, 82, 200, 22 }));
    }

    AmpireProcessor& proc;
    juce::TextEditor search;
    juce::ListBox list;
    juce::Array<CaptureMeta> entries, shown;
    juce::StringArray recents;
    int filter = fAll;
    juce::uint32 drawnVersion = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ============================================================================ contenu de la fenetre
class AmpireContent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit AmpireContent (AmpireProcessor& p)
        : proc (p),
          power (p, "active", AmpireProcessor::lActive, true),
          gear (IconBtn::gear),
          library (p)
    {
        setSize (kW, kH);
        setOpaque (true);

        meterMain = juce::Colour (AmpireSettings::meterColour (JucePlugin_Name));
        meterSat  = juce::Colour (AmpireSettings::saturationColour (JucePlugin_Name));
        gear.proc = &proc;
        // plus de bouton de reglages : le menu complet (reglages, presets, couleurs, MIDI, taille) s'ouvre au clic droit

        // ---- plaque : potards
        struct K { const char* id; int learn; const char* name; };
        // les commandes du plugin NAM officiel (A1 et A2) : rien d'autre
        const K defs[6] = { { "input", AmpireProcessor::lInput, "INPUT" },
                            { "bass", AmpireProcessor::lBass, "BASS" }, { "middle", AmpireProcessor::lMiddle, "MIDDLE" },
                            { "treble", AmpireProcessor::lTreble, "TREBLE" }, { "output", AmpireProcessor::lOutput, "OUTPUT" },
                            { "size", AmpireProcessor::lSize, "A2 SIZE" } };
        for (int i = 0; i < 6; ++i)
        {
            knobs[i] = std::make_unique<Knob> (proc, defs[i].id, defs[i].learn);
            const int col = i;
            knobs[i]->setBounds (toInt ({ kKnobX[col] - D (70.0f), kKnobY - D (70.0f), D (140.0f), D (140.0f) }));
            addChildComponent (*knobs[i]);
            auto* k = knobs[i].get();
            const juce::String name (defs[i].name);
            const bool isSize = i == 5;
            k->onValue = [this, k, name, isSize]
            {
                const auto& prm = k->param;
                if (isSize)
                {
                    const int n = numSizes();
                    const int st = sizeStep();
                    askRepaint (sizeLabelArea());
                    askRepaint (valueArea (5));
                    if (n == 0) showStatus ("A2 SIZE   only for A2 captures with several sizes");
                    else if (n == 2) showStatus (juce::String ("A2 SIZE   ") + (st == 1 ? "FULL (best quality)" : "LITE (less CPU)"));
                    else showStatus ("GAIN STEP   " + juce::String (st + 1) + " / " + juce::String (n));
                    return;
                }
                askRepaint (valueArea (k == knobs[0].get() ? 0 : k == knobs[1].get() ? 1 : k == knobs[2].get() ? 2 : k == knobs[3].get() ? 3 : 4));
                showStatus (name + "   " + prm.getText (prm.getValue(), 32) + " " + prm.getLabel());
            };
        }
        // ---- plaque : inverseurs + voyant
        addAndMakeVisible (power);
        power.setBounds (toInt ({ kToggle.x - kToggleSprite * 0.5f, kToggle.y - kToggleSprite * 0.5f, kToggleSprite, kToggleSprite }));
        power.onValue = [this] { askRepaint (toInt (rLedSprite)); };

        // ---- baie : la capture
        {
            auto& sl = slot[0];
            sl.lib  = std::make_unique<IconBtn> (IconBtn::library);
            sl.prev = std::make_unique<IconBtn> (IconBtn::prev, AmpireProcessor::lPrev);
            sl.next = std::make_unique<IconBtn> (IconBtn::next, AmpireProcessor::lNext);
            sl.lib->proc = sl.prev->proc = sl.next->proc = &proc;
            for (auto* b : { sl.lib.get(), sl.prev.get(), sl.next.get() }) { b->ghost = true; addAndMakeVisible (*b); }
            sl.lib->setBounds (toInt (rLib));
            sl.prev->setBounds (toInt (rPrev));
            sl.next->setBounds (toInt (rNext));
            sl.lib->onClick = [this] { openLibrary (0); };
            sl.prev->onClick = [this] { proc.stepCapture (0, -1); };
            sl.next->onClick = [this] { proc.stepCapture (0, +1); };
        }

        addChildComponent (library);
        library.setBounds (toInt ({ 12, 48, 736, (float) kH - 60 }));
        library.onClose = [this] { askRepaint (getLocalBounds()); };

        updateKnobSet();
        refreshInfos();
        rebuildLedImage();
        startTimerHz (30);
    }

    ~AmpireContent() override = default;
    void openLibrary (int s) { library.open (s); }

    // ---------------------------------------------------------------- dessin
    void paint (juce::Graphics& g) override
    {
        useAccent();
        lastPaintMs = juce::Time::getMillisecondCounter();
        repaintAskedMs = 0;
        g.fillAll (col::outer);
        if (bgCache.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            g.drawImageTransformed (bgCache, juce::AffineTransform::scale ((float) kW / (float) bgCache.getWidth(), (float) kH / (float) bgCache.getHeight()));
        }
        else drawStatic (g, false);

        drawJewel (g);
        drawFaceLabels (g);
        drawKnobValues (g);
        drawSizeLabels (g);
        drawSlot (g, 0);
        drawStrip (g);
    }

    // valeur de chaque potard, au-dessus (serigraphie blanche comme la maquette)
    juce::String knobValueText (int i) const
    {
        auto& prm = knobs[i]->param;
        const float v = prm.convertFrom0to1 (prm.getValue());
        auto num = [] (float x, bool sign)
        {
            const float r1 = std::round (x * 10.0f) / 10.0f;
            juce::String t = std::abs (r1 - std::round (r1)) < 0.05f ? juce::String ((int) std::lround (r1)) : juce::String (r1, 1);
            if (sign && r1 > 0.04f) t = "+" + t;
            return t;
        };
        if (i == 0 || i == 4) return num (v, true);
        if (i < 4) return num (v, false);
        const int n = numSizes();
        if (n == 0) return "-";
        if (n == 2) return sizeStep() == 1 ? "FULL" : "LITE";
        return juce::String (sizeStep() + 1) + " / " + juce::String (n);
    }
    // ---- serigraphie de la facade : TOUTES les inscriptions (titres, valeurs, graduations, POWER / ON / OFF) sont
    //      dessinees ici avec le meme rendu (police, blanc, ombre) ; positions et hauteurs de capitale relevees sur la maquette
    static juce::Font silkFont (float capDesign, float kerning, bool medium = false)
    {
        const float h = D (capDesign) / 0.727f * 1.15f;
        return (medium ? fMedium (h) : fSemi (h)).withExtraKerningFactor (kerning);
    }
    static void silkText (juce::Graphics& g, const juce::String& t, float cxDesign, float baselineDesign, const juce::Font& f, float alpha = 1.0f)
    {
        g.setFont (f);
        const auto r = juce::Rectangle<float> (D (cxDesign) - D (110.0f), D (baselineDesign) - f.getAscent(), D (220.0f), f.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
        g.drawText (t, r.translated (0.0f, 0.6f), juce::Justification::centredTop, false);
        g.setColour (kSilk.withAlpha (alpha));
        g.drawText (t, r, juce::Justification::centredTop, false);
    }
    static constexpr float kTitleX[6] = { 336.9f, 548.6f, 761.0f, 972.6f, 1185.8f, 1400.4f };
    static constexpr float kScaleX[6][2] = { { 268.2f, 401.8f }, { 485.4f, 614.1f }, { 698.0f, 828.8f },
                                             { 910.7f, 1038.7f }, { 1120.6f, 1250.3f }, { 1338.0f, 1469.6f } };
    juce::Rectangle<int> valueArea (int i) const { return toInt ({ D (kTitleX[i]) - D (70.0f), D (385.0f), D (140.0f), D (32.0f) }); }
    void drawKnobValues (juce::Graphics& g)
    {
        static const juce::Font f = silkFont (19.4f, 0.0f);
        for (int i = 0; i < 6; ++i)
            silkText (g, knobValueText (i), kTitleX[i], 412.2f, f, (i == 5 && numSizes() < 2) ? 0.40f : 1.0f);
    }
    void drawFaceLabels (juce::Graphics& g)
    {
        static const juce::Font title = silkFont (20.5f, 0.07f), scale = silkFont (16.8f, 0.03f, true), sw = silkFont (19.0f, 0.05f);
        const char* names[5] = { "INPUT", "BASS", "MIDDLE", "TREBLE", "OUTPUT" };
        const char* scales[5][2] = { { "-20", "+20" }, { "0", "10" }, { "0", "10" }, { "0", "10" }, { "-40", "+40" } };
        for (int i = 0; i < 5; ++i)
        {
            silkText (g, names[i], kTitleX[i], 363.3f, title);
            silkText (g, scales[i][0], kScaleX[i][0], 640.5f, scale);
            silkText (g, scales[i][1], kScaleX[i][1], 640.5f, scale);
        }
        silkText (g, "POWER", 1715.8f, 363.3f, title);
        silkText (g, "ON", 1716.9f, 435.0f, sw);
        silkText (g, "OFF", 1720.3f, 620.0f, sw);
    }

    // voyant rubis (allume quand Ampire est en marche)
    // potard de taille A2 : libelles selon la capture chargee
    //  - A2 a 2 tailles          : « A2 SIZE »   LITE ... FULL
    //  - conteneur a 3+ modeles  : « GAIN STEP » LOW ... HIGH (captures parametriques en paliers, ex. Janus)
    //  - sans conteneur          : « A2 SIZE » grise
    int numSizes() const { return info[0].loaded && info[0].meta.slimmable ? (int) info[0].meta.breakpoints.size() + 1 : 0; }
    int sizeStep() const
    {
        const auto& bp = info[0].meta.breakpoints;
        const double v = proc.apvts.getRawParameterValue ("size")->load();
        for (size_t i = 0; i < bp.size(); ++i) if (v < bp[i]) return (int) i;
        return (int) bp.size();
    }
    void drawSizeLabels (juce::Graphics& g)
    {
        static const juce::Font title = silkFont (20.5f, 0.07f), scale = silkFont (16.8f, 0.03f, true);
        const int n = numSizes();
        const bool ladder = n > 2;
        const float a = n >= 2 ? 1.0f : 0.40f;
        silkText (g, ladder ? "GAIN STEP" : "A2 SIZE", kTitleX[5], 363.3f, title, a);
        silkText (g, ladder ? "LOW" : "LITE", kScaleX[5][0], 640.5f, scale, a);
        silkText (g, ladder ? "HIGH" : "FULL", kScaleX[5][1], 640.5f, scale, a);
    }
    juce::Rectangle<int> sizeLabelArea() const { return toInt (R (1270, 330, 1530, 650)); }

    void drawJewel (juce::Graphics& g)
    {
        // voyant POWER : allume = image de la maquette (recoloree selon le reglage) posee sur le voyant eteint du fond
        if (power.norm() > 0.5f) drawSpriteIn (g, ledImage.isValid() ? ledImage : Art::get().ledOn, rLedSprite);
    }
public:
    // couleur et halo du voyant : l'image de la maquette est recoloree une fois (thread de messages, hors paint)
    juce::Colour ledColour { AmpireSettings::ledColour (JucePlugin_Name) };
    float ledGlow = AmpireSettings::glowAttr (JucePlugin_Name, "ledGlow", 1.0f);
    float screenGlow = 0.0f;                         // pas de halo sur l'ecran (texte blanc fixe)
    void rebuildLedImage()
    {
        const auto& src = Art::get().ledOn;
        if (ledColour.getARGB() == AmpireSettings::kDefaultLed && std::abs (ledGlow - 1.0f) < 0.01f) { ledImage = {}; askRepaint (toInt (rLedSprite)); return; }
        juce::Image img (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        const float cx = src.getWidth() * (1618.4f - 1570.0f) / 97.0f, cy = src.getHeight() * (522.5f - 474.0f) / 97.0f;
        const float lensR = src.getWidth() * 15.0f / 97.0f;
        const float cr = ledColour.getFloatRed(), cg = ledColour.getFloatGreen(), cb = ledColour.getFloatBlue();
        for (int y = 0; y < src.getHeight(); ++y)
            for (int x = 0; x < src.getWidth(); ++x)
            {
                const auto p = src.getPixelAt (x, y);
                const float a = p.getFloatAlpha();
                if (a <= 0.0f) continue;
                const float r = p.getFloatRed(), gg = p.getFloatGreen(), b = p.getFloatBlue();
                const float v = juce::jmax (r, gg, b), w = v > 0.0f ? juce::jmin (r, gg, b) / v : 0.0f;   // luminosite / blancheur
                const float d = std::hypot ((float) x - cx, (float) y - cy);
                // halo (hors de la lentille) : seule la lumiere orange est remplacee et dosee ; bague et plaque gardees
                const float glowZone = juce::jlimit (0.0f, 1.0f, (d - lensR) / (src.getWidth() * 3.0f / 97.0f));
                const float hue = juce::jlimit (0.0f, 1.0f, (p.getSaturation() - 0.25f) / 0.35f);      // part coloree du pixel
                const float k = 1.0f + (ledGlow - 1.0f) * glowZone * hue;
                const float nr = juce::jlimit (0.0f, 1.0f, (cr * (1.0f - w) + w) * v * k);
                const float ng = juce::jlimit (0.0f, 1.0f, (cg * (1.0f - w) + w) * v * k);
                const float nb = juce::jlimit (0.0f, 1.0f, (cb * (1.0f - w) + w) * v * k);
                const float mix = juce::jmax (hue, 1.0f - glowZone);    // pixels neutres du halo (plaque) : inchanges
                img.setPixelAt (x, y, juce::Colour::fromFloatRGBA (r + (nr - r) * mix, gg + (ng - gg) * mix, b + (nb - b) * mix,
                                                                    juce::jlimit (0.0f, 1.0f, a * (1.0f + (juce::jmin (ledGlow, 1.0f) - 1.0f) * glowZone * hue))));
            }
        ledImage = img;
        askRepaint (toInt (rLedSprite));
    }
private:
    juce::Image ledImage;
    // texte de l'ecran : couleur du nuancier « Screen colour » et halo optionnel (lueur d'afficheur)
    void screenText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, bool fitted = false)
    {
        const auto c = textCol;
        if (screenGlow > 0.01f)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                const float rad = pass == 0 ? 0.9f : 2.0f, al = (pass == 0 ? 0.075f : 0.04f) * screenGlow * c.getFloatAlpha();
                g.setColour (c.withAlpha (juce::jmin (1.0f, al)));
                for (int i = 0; i < 8; ++i)
                {
                    const float an = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                    const auto rr = r.translated (rad * std::cos (an), rad * std::sin (an));
                    if (fitted) g.drawFittedText (t, rr.toNearestInt(), j, 3, 0.8f); else g.drawText (t, rr, j, true);
                }
            }
            g.setColour (c);
        }
        if (fitted) g.drawFittedText (t, r.toNearestInt(), j, 3, 0.8f); else g.drawText (t, r, j, true);
    }
    juce::Colour textCol;
    void setTextCol (juce::Graphics& g, juce::Colour c) { textCol = c; g.setColour (c); }
public:

    void drawSlot (juce::Graphics& g, int s)
    {
        const auto& inf = info[s];
        const auto& m = inf.meta;
        const auto lcd = rLcdInfo;
        const bool on = true;
        const float a = on ? 1.0f : 0.4f;
        auto amber = [a] (float k) { return (k > 0.95f ? kSilk : k > 0.6f ? kSilk.withAlpha (0.92f) : kSilk.withAlpha (0.85f)).withMultipliedAlpha (a); };
        const auto tr = lcd.withTrimmedLeft (D (233.3f) - lcd.getX()).withTrimmedRight (6.0f).withY (D (140.0f)).withHeight (D (140.0f));
        const float xL = D (233.3f), xS = D (240.0f), w = lcd.getRight() - xL - 6.0f;
        const bool showErr = inf.error.isNotEmpty() && juce::Time::getMillisecondCounter() < errUntil[s];
        if (showErr) { g.setColour (col::err); g.setFont (fName (12.0f)); g.drawFittedText (inf.error, juce::Rectangle<float> (xL, D (145.0f), w, D (135.0f)).toNearestInt(), juce::Justification::centredLeft, 3, 0.8f); }
        else if (inf.loaded)
        {
            const auto title = m.title.isNotEmpty() ? m.title : m.fileName;
            setTextCol (g, amber (1.0f));
            g.setFont (fName (16.0f));
            screenText (g, title, juce::Rectangle<float> (xL, D (182.8f) - 14.5f, w, 18.0f), juce::Justification::bottomLeft);
            const auto gear = (m.gearMake + " " + m.gearModel).trim();
            juce::StringArray l2;
            if (gear.isNotEmpty()) l2.add (gear);
            if (m.gearType.isNotEmpty()) l2.add (m.gearType.replace ("_", " "));
            if (m.toneType.isNotEmpty()) l2.add (m.toneType.replace ("_", " "));
            setTextCol (g, amber (0.7f));
            g.setFont (fMedium (9.6f));
            screenText (g, l2.isEmpty() ? juce::String ("no gear information") : l2.joinIntoString (dotSep()), juce::Rectangle<float> (xS, D (221.0f) - 9.0f, w, 12.0f), juce::Justification::bottomLeft);
            juce::String fmt = m.family;
            if (m.slimmable && ! m.breakpoints.empty())
            {
                const double v = proc.apvts.getRawParameterValue ("size")->load();
                size_t idx = m.breakpoints.size();
                for (size_t i = 0; i < m.breakpoints.size(); ++i) if (v < m.breakpoints[i]) { idx = i; break; }
                const size_t nSizes = m.breakpoints.size() + 1;
                fmt << (nSizes == 2 ? (idx + 1 == nSizes ? " FULL" : " LITE") : " STEP " + juce::String ((int) idx + 1) + "/" + juce::String ((int) nSizes));
            }
            else if (m.variant.isNotEmpty()) fmt << " " << m.variant.toUpperCase();
            juce::String k = juce::String (m.sampleRate / 1000.0, 1); if (k.endsWith (".0")) k = k.dropLastCharacters (2);
            fmt << dotSep() << k << " kHz";
            if (inf.osFactor > 1) fmt << dotSep() << "AA " << inf.osFactor << "x";
            if (m.hasInputLevel) fmt << dotSep() << "cal " << juce::String (m.inputLevel, 1) << " dBu";
            if (m.esr >= 0.0) fmt << dotSep() << "ESR " << juce::String (m.esr, 4);
            if (inf.fromEmbedded) fmt << dotSep() << "project copy";
            setTextCol (g, amber (0.5f));
            g.setFont (fMedium (9.3f));
            screenText (g, fmt, juce::Rectangle<float> (xS, D (263.9f) - 8.5f, w, 11.0f), juce::Justification::bottomLeft);
        }
        else if (inf.loading) { setTextCol (g, amber (0.8f)); g.setFont (fName (14.0f)); screenText (g, "LOADING...", juce::Rectangle<float> (xL, D (182.8f) - 14.5f, w, 18.0f), juce::Justification::bottomLeft); }
        else
        {
            setTextCol (g, kSilk.withAlpha (0.45f));
            g.setFont (fName (14.0f));
            screenText (g, "EMPTY", juce::Rectangle<float> (xL, D (182.8f) - 14.5f, w, 18.0f), juce::Justification::bottomLeft);
            g.setFont (fMedium (9.6f));
            g.drawFittedText ("Click here or on the library button, use the arrows, or drop a .nam file (NAM A1 / A2)",
                              juce::Rectangle<float> (xS, D (200.0f), w, 26.0f).toNearestInt(), juce::Justification::topLeft, 2);
        }
        if (dropHover == s) { g.setColour (kSilk.withAlpha (0.8f)); g.drawRoundedRectangle (rLcd[s].reduced (2.0f), 4.0f, 1.5f); }
    }

    void drawStrip (juce::Graphics& g)
    {
        // vu-metres a LEDs : 48 LEDs ; les 3 du haut a la couleur de saturation, les 4 d'avant en fondu vers elle.
        // Crete maintenue 2 s puis effacee en 0,5 s (trait blanc incandescent). Reflet de chaque LED allumee sur le bord
        // du verre (visibilite irreguliere relevee sur la maquette), reflet de la crete plus fort et diffus.
        constexpr int N = 48;
        auto ledColour = [this] (int i)
        {
            const float t = i >= N - 3 ? 1.0f : (i < N - 7 ? 0.0f : (float) (i - (N - 8)) / 5.0f);
            return meterMain.interpolatedWith (meterSat, t);
        };
        auto meter = [&] (juce::Rectangle<float> r, float reflX, float level, float peak, float peakAlpha, bool clip, juce::Point<float> clipLed)
        {
            const int lit = juce::jlimit (0, N, (int) std::lround (meterPos (juce::Decibels::gainToDecibels (level, -100.0f)) * N));
            const int pk = juce::jlimit (-1, N - 1, (int) std::lround (meterPos (juce::Decibels::gainToDecibels (peak, -100.0f)) * N) - 1);
            const bool showPk = pk >= lit && peakAlpha > 0.01f && peak > 0.0f;
            const float h = r.getHeight() / N, gap = D (1.6f);
            auto ledRect = [&] (int i) { return juce::Rectangle<float> (r.getX(), r.getBottom() - (i + 1) * h + gap, r.getWidth(), h - gap); };
            // reflets dans le verre (sous les LEDs)
            for (int i = 0; i < lit; ++i)
            {
                float acc = 0.0f, wsum = 0.0f;
                for (int j = juce::jmax (0, i - 4); j <= juce::jmin (N - 1, i + 4); ++j)
                {
                    const float w = std::exp (-0.5f * (float) ((j - i) * (j - i)) / 1.0f);
                    wsum += w;
                    if (j < lit) acc += w * kMeterVis[j];
                }
                const float v = juce::jmin (1.0f, 7.0f * 0.9f * acc / wsum) * 0.4877f;
                if (v < 0.01f) continue;
                const auto c = ledColour (i);
                const float cy = ledRect (i).getCentreY();
                juce::ColourGradient gr (c.withAlpha (v), reflX, cy, c.withAlpha (0.0f), reflX + D (4.0f), cy, true);
                g.setGradientFill (gr);
                g.fillEllipse (reflX - D (4.0f), cy - h * 1.15f, D (8.0f), h * 2.3f);
            }
            if (showPk)
            {
                const auto c = ledColour (pk);
                const float cy = ledRect (pk).getCentreY();
                juce::ColourGradient gr (c.withAlpha (0.4877f * 0.9f * peakAlpha), reflX, cy, c.withAlpha (0.0f), reflX + D (6.0f), cy, true);
                gr.addColour (0.45, c.withAlpha (0.25f * peakAlpha));
                g.setGradientFill (gr);
                g.fillEllipse (reflX - D (6.0f), cy - D (15.0f), D (12.0f), D (30.0f));
            }
            // LEDs eteintes
            for (int i = 0; i < N; ++i)
            {
                if (i < lit || (showPk && i == pk)) continue;
                g.setColour (ledColour (i).withMultipliedBrightness (0.10f).withAlpha (0.9f));
                g.fillRect (ledRect (i));
            }
            // halo des LEDs allumees
            if (lit > 0)
            {
                const auto top = ledRect (lit - 1).getY();
                for (int pass = 0; pass < 2; ++pass)
                {
                    const float e = pass == 0 ? D (4.0f) : D (2.0f);
                    g.setColour (ledColour (lit / 2).withAlpha (0.12f));
                    g.fillRoundedRectangle (r.getX() - e, top - e, r.getWidth() + 2 * e, r.getBottom() - top + 2 * e, e);
                }
            }
            auto drawLed = [&] (int i, float alpha, bool isPeak)
            {
                const auto lr = ledRect (i);
                const auto c = ledColour (i);
                g.setColour (c.withAlpha (alpha));
                g.fillRect (lr);
                juce::ColourGradient hl (juce::Colour (0xffffe6b3).withAlpha (0.38f * alpha), lr.getCentreX(), lr.getCentreY(),
                                         juce::Colour (0xffffe6b3).withAlpha (0.0f), lr.getRight(), lr.getCentreY(), true);
                g.setGradientFill (hl);
                g.fillRect (lr);
                if (isPeak)
                {
                    g.setColour (juce::Colour (0xfffff8e6).withAlpha (alpha));
                    g.fillRect (lr.withSizeKeepingCentre (lr.getWidth(), juce::jmax (0.6f, lr.getHeight() * 0.38f)));
                }
            };
            for (int i = 0; i < lit; ++i) drawLed (i, 1.0f, false);
            if (showPk) drawLed (pk, peakAlpha, true);
            // voyant d'ecretage
            const auto led = juce::Rectangle<float> (clipLed.x - D (7.5f), clipLed.y - D (7.5f), D (15.0f), D (15.0f));
            if (clip)
            {
                g.setColour (juce::Colour (0xffff2a1a).withAlpha (0.35f)); g.fillEllipse (led.expanded (D (5.0f)));
                juce::ColourGradient lens (juce::Colour (0xffffb0a0), led.getCentreX() - D (2.0f), led.getCentreY() - D (2.5f),
                                           juce::Colour (0xffc01008), led.getRight(), led.getBottom(), true);
                g.setGradientFill (lens); g.fillEllipse (led);
            }
        };
        meter (rMeterIn, kReflXIn, shownIn, peakIn, peakAlphaIn, clipIn, kClipIn);
        meter (rMeterOut, kReflXOut, shownOut, peakOut, peakAlphaOut, clipOut, kClipOut);

        // afficheur d'etat : message, sinon CPU / latence
        const auto r = rStatusText.reduced (8, 5);
        const auto st = statusText();
        if (st.isNotEmpty())
        {
            setTextCol (g, kSilk);
            g.setFont (fSemi (10.5f));
            screenText (g, st, r, juce::Justification::centred, true);
        }
        else
        {
            setTextCol (g, kSilk.withAlpha (0.92f));
            g.setFont (fMedium (9.3f));
            const int lat = proc.getReportedLatency();
            const double ms = 1000.0 * lat / juce::jmax (1.0, proc.getHostRate());
            juce::String k = juce::String (proc.getHostRate() / 1000.0, 1); if (k.endsWith (".0")) k = k.dropLastCharacters (2);
            const float cx = D (1467.0f);
            screenText (g, "CPU " + juce::String (shownCpu, 1) + " %" + dotSep() + k + " kHz" + dotSep() + "AA " + juce::String (osShown) + "x",
                        juce::Rectangle<float> (cx - 90.0f, D (172.2f) - 8.5f, 180.0f, 11.0f), juce::Justification::centredBottom);
            screenText (g, "LATENCY " + (lat == 0 ? juce::String ("0 (none added)") : juce::String (lat) + " smp  " + juce::String (ms, 2) + " ms"),
                        juce::Rectangle<float> (cx - 90.0f, D (246.0f) - 8.5f, 180.0f, 11.0f), juce::Justification::centredBottom);
        }
    }

    // ---------------------------------------------------------------- fond (graphite, plaque anodisee, baie)
    void drawStatic (juce::Graphics& g, bool)
    {
        const auto& bg = Art::get().bg;
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        if (bg.isValid()) g.drawImage (bg, juce::Rectangle<float> (0, 0, (float) kW, (float) kH));
        else g.fillAll (col::outer);
    }

    // ---------------------------------------------------------------- souris / fichiers
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        if (e.mods.isPopupMenu())
        {
            showGearMenu (*this);
            return;
        }
        if (rLcd[0].contains (p)) { openLibrary (0); return; }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (juce::File (f).hasFileExtension ("nam") || juce::File (f).isDirectory()) return true;
        return false;
    }
    void fileDragMove (const juce::StringArray&, int x, int y) override
    {
        const int s = slotAt ({ (float) x, (float) y });
        if (s != dropHover) { dropHover = s; repaint(); }
    }
    void fileDragExit (const juce::StringArray&) override { dropHover = -1; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        int s = slotAt ({ (float) x, (float) y });
        dropHover = -1;
        juce::Array<juce::File> nams;
        for (auto& f : files)
        {
            const juce::File file (f);
            if (file.isDirectory()) { for (auto& c : file.findChildFiles (juce::File::findFiles, false, "*.nam")) nams.add (c); }
            else if (file.hasFileExtension ("nam")) nams.add (file);
        }
        auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
        std::sort (nams.begin(), nams.end(), byName);
        if (! nams.isEmpty())
        {
            juce::ignoreUnused (s, y);
            proc.loadCapture (0, nams[0]);
        }
        repaint();
    }

    void editorResized() { resizeSettle = 4; }
    void useAccent() const { col::accent = juce::Colour (proc.accentColour.load()); }
    void showStatus (const juce::String& s) { statusMsg = s; statusUntil = juce::Time::getMillisecondCounter() + 1800; askRepaint (toInt (rStatus)); }

private:
    struct SlotUI
    {
        std::unique_ptr<IconBtn> lib, prev, next;
    };

    void updateKnobSet()
    {
        for (int i = 0; i < 6; ++i) knobs[i]->setVisible (true);
        bgCache = {};                                   // gravures (PRESENCE / DI LOW) : fond recalcule par le timer
        resizeSettle = juce::jmax (resizeSettle, 1);
        askRepaint (getLocalBounds());
    }

    int slotAt (juce::Point<float> p) const
    {
        if (rRow[0].contains (p) || rMeterWinIn.contains (p) || rMeterWinOut.contains (p)) return 0;
        return -1;
    }

    void chooseFile (int s)
    {
        chooser = std::make_unique<juce::FileChooser> (juce::String ("Load NAM capture ") + (s == 0 ? "A" : "B"), proc.startFolder (s), "*.nam");
        juce::Component::SafePointer<AmpireContent> sp (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [sp, s] (const juce::FileChooser& fc)
        {
            if (sp == nullptr) return;
            const auto f = fc.getResult();
            if (f.existsAsFile()) sp->proc.loadCapture (s, f);
        });
    }

    void slotMenu (int s)
    {
        juce::PopupMenu m;
        const auto inf = proc.getSlotInfo (s);
        m.addSectionHeader (juce::String ("Capture ") + (s == 0 ? "A" : "B"));
        m.addItem (1, "Open library...");
        m.addItem (2, "Load file...");
        m.addItem (3, "Previous capture in folder", inf.loaded);
        m.addItem (4, "Next capture in folder", inf.loaded);
        m.addItem (5, "Show in folder", inf.loaded && juce::File (inf.meta.path).existsAsFile());
        m.addItem (6, "Favourite", inf.loaded, proc.library.isFavourite (inf.meta.path));
        m.addItem (7, "Clear", inf.loaded);
        if (inf.loaded) { m.addSeparator(); m.addItem (8, inf.meta.formatLine(), false); }
        juce::Component::SafePointer<AmpireContent> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [sp, s, path = inf.meta.path] (int r)
        {
            if (sp == nullptr) return;
            if (r == 1) sp->openLibrary (s);
            else if (r == 2) sp->chooseFile (s);
            else if (r == 3) sp->proc.stepCapture (s, -1);
            else if (r == 4) sp->proc.stepCapture (s, +1);
            else if (r == 5) juce::File (path).revealToUser();
            else if (r == 6) sp->proc.library.toggleFavourite (path);
            else if (r == 7) sp->proc.clearCapture (s);
        });
    }

    void presetMenuAt (juce::Component* near)
    {
        juce::PopupMenu m;
        const auto names = proc.getPresetNames();
        const auto cur = proc.getPresetName();
        m.addItem (1, "Init (empty)");
        m.addSeparator();
        if (names.isEmpty()) m.addItem (2, "No preset saved yet", false);
        for (int i = 0; i < names.size(); ++i) m.addItem (100 + i, names[i], true, names[i] == cur);
        m.addSeparator();
        m.addItem (3, "Save preset as...");
        m.addItem (4, "Save \"" + cur + "\"", cur.isNotEmpty());
        m.addItem (5, "Delete \"" + cur + "\"", cur.isNotEmpty());
        m.addItem (6, "Open presets folder");
        juce::Component::SafePointer<AmpireContent> sp (this);
        m.showMenuAsync (near != nullptr ? juce::PopupMenu::Options().withTargetComponent (near) : juce::PopupMenu::Options(), [sp, names, cur] (int r)
        {
            if (sp == nullptr) return;
            auto& p = sp->proc;
            if (r == 1) p.initPreset();
            else if (r == 3) sp->askPresetName();
            else if (r == 4) p.savePreset (cur);
            else if (r == 5) p.deletePreset (cur);
            else if (r == 6) { AmpireProcessor::presetFolder().createDirectory(); AmpireProcessor::presetFolder().startAsProcess(); }
            else if (r >= 100 && r - 100 < names.size()) p.loadPreset (names[r - 100]);
            sp->repaint();
        });
    }

    void askPresetName()
    {
        auto* aw = new juce::AlertWindow ("Save preset", "Preset name (captures and IR are saved inside the preset):", juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", proc.getPresetName().isNotEmpty() ? proc.getPresetName() : juce::String ("My rig"));
        aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<AmpireContent> sp (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([sp, aw] (int r)
        {
            if (r == 1 && sp != nullptr)
            {
                sp->proc.savePreset (aw->getTextEditorContents ("name"));
                sp->showStatus ("Preset saved: " + sp->proc.getPresetName());
            }
        }), true);
    }

    juce::String statusText() const
    {
        const int lt = proc.learnTarget.load();
        if (lt >= 0) return juce::String ("MIDI LEARN: move a control for ") + AmpireProcessor::learnDef (lt).name;
        const auto now = juce::Time::getMillisecondCounter();
        if (learnMsg.isNotEmpty() && now < learnMsgUntil) return learnMsg;
        if (statusMsg.isNotEmpty() && now < statusUntil) return statusMsg;
        return {};
    }

    void refreshInfos()
    {
        for (int s = 0; s < 1; ++s)
        {
            const auto old = info[s].error;
            info[s] = proc.getSlotInfo (s);
            if (info[s].error.isNotEmpty() && info[s].error != old) errUntil[s] = juce::Time::getMillisecondCounter() + 5000;
            const bool sizable = info[s].loaded && info[s].meta.slimmable;
            knobs[5]->dimmed = ! sizable;
            knobs[5]->repaint();
            askRepaint (sizeLabelArea());
        }
        drawnInfoVersion = proc.getInfoVersion();
    }

    void askRepaint (juce::Rectangle<int> r)
    {
        repaint (r);
        if (repaintAskedMs == 0) repaintAskedMs = juce::Time::getMillisecondCounter();
    }

    void updateBackgroundCache (AmpireEditor& ed, juce::uint32 now)
    {
        if (now - lastScaleCheck >= 250)
        {
            lastScaleCheck = now;
            const float sc = juce::Component::getApproximateScaleFactorForComponent (&ed);
            if (std::abs (sc - displayScale) > 0.001f) { displayScale = sc; resizeSettle = juce::jmax (resizeSettle, 1); }
        }
        if (resizeSettle > 0 && --resizeSettle == 0)
        {
            const float px = (float) ed.getWidth() / kVisW * displayScale;
            const int tw = juce::jlimit (64, kW * 4, (int) std::lround (kW * px));
            const int th = juce::jlimit (32, kH * 4, (int) std::lround (kH * px));
            if (! bgCache.isValid() || bgCache.getWidth() != tw || bgCache.getHeight() != th)
            {
                juce::Image img (juce::Image::RGB, tw, th, true);
                {
                    juce::Graphics g (img);
                    g.addTransform (juce::AffineTransform::scale ((float) tw / (float) kW, (float) th / (float) kH));
                    drawStatic (g, true);
                }
                bgCache = img;
                askRepaint (getLocalBounds());
            }
        }
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (auto* ed = findParentComponentOfClass<AmpireEditor>())
        {
            ed->checkRenderer();
            updateBackgroundCache (*ed, now);
            if (repaintAskedMs != 0 && now - repaintAskedMs > 1500 && isShowing())
            {
                repaintAskedMs = 0;
                ed->resetRenderer();
                repaint();
            }
        }

        if (proc.getInfoVersion() != drawnInfoVersion)
        {
            refreshInfos();
            askRepaint (toInt (rRow[0]));
            for (int i = 0; i < 6; ++i) askRepaint (valueArea (i));
        }
        for (int s = 0; s < 1; ++s)
            if (errUntil[s] != 0 && now > errUntil[s]) { errUntil[s] = 0; askRepaint (toInt (rRow[s])); }

        const auto pv = proc.getPresetVersion();
        if (pv != drawnPresetVersion)
        {
            drawnPresetVersion = pv;
            const auto pn = proc.getPresetName();
            if (pn.isNotEmpty()) showStatus ("PRESET   " + pn);
        }

        // vu-metres, CPU
        auto decay = [] (float& shown, float peak) { shown = juce::jmax (peak, shown * 0.80f); };
        const float pin = proc.meterIn.exchange (0.0f), pout = proc.meterOut.exchange (0.0f);
        const float oi = shownIn, oo = shownOut;
        decay (shownIn, pin); decay (shownOut, pout);
        // memoire de crete : maintenue 2 s puis effacee en fondu (0,5 s) ; un nouveau maximum la remplace
        auto hold = [now] (float& pk, float& alpha, juce::uint32& until, float v)
        {
            if (v >= pk || alpha <= 0.0f) { if (v > 1e-5f) { pk = v; until = now + 2000; alpha = 1.0f; } return; }
            if (now > until) { alpha = juce::jmax (0.0f, 1.0f - (float) (now - until) / 500.0f); if (alpha <= 0.0f) pk = 0.0f; }
        };
        const float opi = peakIn, opo = peakOut, oai = peakAlphaIn, oao = peakAlphaOut;
        hold (peakIn, peakAlphaIn, peakInUntil, shownIn); hold (peakOut, peakAlphaOut, peakOutUntil, shownOut);
        if (proc.clipIn.exchange (false)) { clipIn = true; clipInUntil = now + 1500; }
        if (proc.clipOut.exchange (false)) { clipOut = true; clipOutUntil = now + 1500; }
        if (clipIn && now > clipInUntil) clipIn = false;
        if (clipOut && now > clipOutUntil) clipOut = false;
        if (std::abs (oi - shownIn) > 1e-4f || std::abs (oo - shownOut) > 1e-4f || std::abs (opi - peakIn) > 1e-4f || std::abs (opo - peakOut) > 1e-4f
            || oai != peakAlphaIn || oao != peakAlphaOut || clipIn || clipOut || clipDrawn)
        {
            askRepaint (toInt (rMeterWinIn));
            askRepaint (toInt (rMeterWinOut));
            clipDrawn = clipIn || clipOut;
        }
        if (now - lastCpuMs > 500)
        {
            lastCpuMs = now;
            const float c = juce::jlimit (0.0f, 999.0f, proc.cpuLoad.load() * 100.0f);
            const int os = (int) std::lround (proc.apvts.getRawParameterValue ("oversample")->load());
            const int osf = os == 2 ? 4 : os == 1 ? 2 : 1;
            const int lat = proc.getReportedLatency();
            if (std::abs (c - shownCpu) > 0.05f || osf != osShown || lat != shownLat)
            {
                shownCpu = c; osShown = osf; shownLat = lat;
                askRepaint (toInt (rStatus));
            }
        }

        const int learned = proc.lastLearned.exchange (-1);
        if (learned >= 0)
        {
            learnMsg = juce::String (AmpireProcessor::learnDef (learned).name) + " = " + proc.learnAssignText (learned);
            learnMsgUntil = now + 2000;
        }
        const int lt = proc.learnTarget.load();
        const bool blink = lt >= 0 && (now / 250) % 2 == 0;
        if (lt != drawnLearn || blink != drawnBlink)
        {
            for (auto* c : getChildren()) if (c != &library) c->repaint();
            drawnLearn = lt; drawnBlink = blink;
        }

        const auto st = statusText();
        if (st != drawnStatus) { drawnStatus = st; askRepaint (toInt (rStatus)); }
    }

    AmpireProcessor& proc;
    Lever power;
    IconBtn gear;
    std::unique_ptr<Knob> knobs[6];
    SlotUI slot[1];
    LibraryPanel library;
    std::unique_ptr<juce::FileChooser> chooser;
    SlotInfo info[1];
    juce::uint32 errUntil[1] {};
    juce::uint32 drawnInfoVersion = 0, drawnPresetVersion = 0;
    juce::String drawnPresetName, drawnStatus, statusMsg, learnMsg;
    juce::uint32 statusUntil = 0, learnMsgUntil = 0, clipInUntil = 0, clipOutUntil = 0, lastCpuMs = 0;
    int drawnLearn = -1, dropHover = -1, osShown = 1, shownLat = -1;
    bool drawnBlink = false, clipIn = false, clipOut = false;
    float shownIn = 0.0f, shownOut = 0.0f, shownCpu = 0.0f, peakIn = 0.0f, peakOut = 0.0f;
    juce::uint32 peakInUntil = 0, peakOutUntil = 0;
    float peakAlphaIn = 0.0f, peakAlphaOut = 0.0f;
    bool clipDrawn = false;
public:
    juce::Colour meterMain, meterSat;                  // couleurs des LEDs (reglage commun, nuancier)
    void presetMenu (juce::Component* near) { presetMenuAt (near); }
    void askPresetNamePublic() { askPresetName(); }
private:

    // anti-gel v2
    juce::Image bgCache;
    int resizeSettle = 2;
    float displayScale = 1.0f;
    juce::uint32 lastScaleCheck = 0, lastPaintMs = 0, repaintAskedMs = 0;
};

// ============================================================================ editeur
AmpireEditor::AmpireEditor (AmpireProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setOpaque (true);
    content = std::make_unique<AmpireContent> (proc);
    addAndMakeVisible (*content);
    setResizable (true, true);
    // jamais plus petit que la taille par defaut (« 60 % »)
    setResizeLimits (kWin, kWinH, (int) std::lround (kWin * 2.2f), (int) std::lround (kWinH * 2.2f));
    getConstrainer()->setFixedAspectRatio ((double) kVisW / (double) kVisH);
    const int saved = juce::jlimit (-1, 2, proc.uiScaleIdx.load());
    if (saved >= 0) setScaleIndex (saved);
    else            applyScaleIndex (WigaWindowSize::defaultIndex (JucePlugin_Name, numSizes));
}

AmpireEditor::~AmpireEditor() = default;

void AmpireEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if (peer == nullptr) return;
   #if JUCE_WINDOWS
    int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
    if (sw < 0) sw = 0;
    if (peer->getCurrentRenderingEngine() != sw)
        peer->setCurrentRenderingEngine (sw);
   #endif
    if ((void*) peer != lastPeer) { lastPeer = (void*) peer; repaint(); }
    lastRendererCheck = juce::Time::getMillisecondCounter();
}

void AmpireEditor::checkRenderer()
{
    if ((void*) getPeer() != lastPeer || juce::Time::getMillisecondCounter() - lastRendererCheck >= 1000)
        useSoftwareRenderer();
}

void AmpireEditor::resetRenderer()
{
    lastPeer = nullptr;
    useSoftwareRenderer();
    repaint();
}

void AmpireEditor::paint (juce::Graphics& g) { g.fillAll (col::outer); }

void AmpireEditor::openLibrary (int slot) { content->openLibrary (slot); }
void AmpireEditor::showStatus (const juce::String& msg) { content->showStatus (msg); }

void AmpireEditor::chooseFolder (bool lib)
{
    const auto cur = lib ? proc.library.getRoot() : AmpireSettings::irFolder (JucePlugin_Name);
    folderChooser = std::make_unique<juce::FileChooser> (lib ? "Default capture folder (.nam files, subfolders included)" : "Default cab IR folder",
                                                         cur.isDirectory() ? cur : proc.lastFolder);
    juce::Component::SafePointer<AmpireEditor> sp (this);
    folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [sp, lib] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.isDirectory() || sp == nullptr) return;
        if (lib) { sp->proc.library.setRoot (f); sp->content->showStatus ("Default capture folder: " + f.getFileName()); }
        else { AmpireSettings::setIrFolder (JucePlugin_Name, f); sp->content->showStatus ("IR folder: " + f.getFileName()); }
    });
}

void AmpireEditor::openPresetMenu (juce::Component* near) { content->presetMenu (near); }
float AmpireEditor::getScreenGlow() const { return content->screenGlow; }
float AmpireEditor::getLedGlow() const { return content->ledGlow; }
void AmpireEditor::setScreenGlow (float v)
{
    content->screenGlow = v;
    AmpireSettings::setGlowAttr (JucePlugin_Name, "screenGlow", v);
    content->repaint();
}
void AmpireEditor::setLedGlow (float v)
{
    content->ledGlow = v;
    AmpireSettings::setGlowAttr (JucePlugin_Name, "ledGlow", v);
    content->rebuildLedImage();
}
void AmpireEditor::askPresetName() { content->askPresetNamePublic(); }

void AmpireEditor::openColourPicker (juce::Component* near, int which)
{
    const float s = (float) getWidth() / (float) kWin;
    const juce::Colour start = which == 1 ? content->meterMain : which == 2 ? content->meterSat : which == 3 ? content->ledColour : juce::Colour (proc.accentColour.load());
    const char* title = which == 1 ? "Meter LED colour" : which == 2 ? "Saturation LED colour (top 3)" : which == 3 ? "POWER LED colour" : "Screen text colour";
    auto picker = std::make_unique<WigaColourPicker> (title, start, juce::jlimit (0.85f, 1.6f, s), JucePlugin_Name);
    juce::Component::SafePointer<AmpireEditor> sp (this);
    auto apply = [sp, which] (juce::Colour c)
    {
        if (sp == nullptr) return;
        if (which == 1) sp->content->meterMain = c;
        else if (which == 2) sp->content->meterSat = c;
        else if (which == 3) { sp->content->ledColour = c; sp->content->rebuildLedImage(); }
        else sp->proc.accentColour = c.getARGB();
        sp->content->repaint();
    };
    picker->onChange = apply;
    picker->onCommit = [which] (juce::Colour c)
    {
        if (which == 1) AmpireSettings::setColourAttr (JucePlugin_Name, "meterColour", c.getARGB());
        else if (which == 2) AmpireSettings::setColourAttr (JucePlugin_Name, "saturationColour", c.getARGB());
        else if (which == 3) AmpireSettings::setColourAttr (JucePlugin_Name, "ledColour", c.getARGB());
        else AmpireSettings::setAccent (JucePlugin_Name, c.getARGB());
    };
    const juce::uint32 def = which == 1 ? AmpireSettings::kDefaultMeter : which == 2 ? AmpireSettings::kDefaultSaturation
                           : which == 3 ? AmpireSettings::kDefaultLed : AmpireSettings::kDefaultAccent;
    picker->onDefault = [apply, which, def]
    {
        apply (juce::Colour (def));
        if (which == 1) AmpireSettings::setColourAttr (JucePlugin_Name, "meterColour", def);
        else if (which == 2) AmpireSettings::setColourAttr (JucePlugin_Name, "saturationColour", def);
        else if (which == 3) AmpireSettings::setColourAttr (JucePlugin_Name, "ledColour", def);
    };
    auto area = near != nullptr ? getLocalArea (near, near->getLocalBounds()) : getLocalBounds().withSizeKeepingCentre (10, 10);
    auto* layer = new WigaPickerLayer (std::move (picker), area, getLocalBounds());
    addAndMakeVisible (layer);
    layer->toFront (true);
}
void AmpireEditor::parentHierarchyChanged() { useSoftwareRenderer(); }
void AmpireEditor::visibilityChanged() { useSoftwareRenderer(); if (isShowing()) repaint(); }

void AmpireEditor::resized()
{
    const float s = (float) getWidth() / kVisW;
    content->setTransform (juce::AffineTransform::translation (-kVisX, -kVisY).scaled (s));
    content->setBounds (0, 0, kW, kH);
    content->editorResized();            // jamais de setSize ici (boucle avec l'hote a 125 / 150 %)
}

int AmpireEditor::getScaleIndex() const
{
    const float s = (float) getWidth() / (float) kWin;
    for (int i = 0; i < numSizes; ++i) if (std::abs (s - kScales[i]) < 0.02f) return i;
    return -1;
}

void AmpireEditor::setScaleIndex (int idx)
{
    idx = juce::jlimit (-1, numSizes - 1, idx);
    proc.uiScaleIdx = idx;
    applyScaleIndex (idx < 0 ? WigaWindowSize::defaultIndex (JucePlugin_Name, numSizes) : idx);
}

void AmpireEditor::applyScaleIndex (int idx)
{
    idx = juce::jlimit (0, numSizes - 1, idx);
    const int w = (int) std::lround (kWin * kScales[idx]);
    setSize (w, (int) std::lround ((double) w * kVisH / kVisW));
}

// ============================================================================ menus clic droit
static AmpireProcessor* procFor (juce::Component* c)
{
    if (c == nullptr) return nullptr;
    if (auto* ed = dynamic_cast<AmpireEditor*> (c)) return &ed->getProc();
    if (auto* ed = c->findParentComponentOfClass<AmpireEditor>()) return &ed->getProc();
    return nullptr;
}

static void addSizeItems (juce::PopupMenu& m, juce::Component& c)
{
    auto* ed = c.findParentComponentOfClass<AmpireEditor>();
    if (ed == nullptr) ed = dynamic_cast<AmpireEditor*> (&c);
    const int cur = ed != nullptr ? ed->getScaleIndex() : -1;
    const int def = WigaWindowSize::defaultIndex (JucePlugin_Name, AmpireEditor::numSizes);
    const char* names[AmpireEditor::numSizes] = { "60 %", "75 %", "100 %" };
    juce::PopupMenu defSizes;
    for (int i = 0; i < AmpireEditor::numSizes; ++i)
    {
        m.addItem (101 + i, juce::String (names[i]) + (i == def ? "  (default)" : ""), true, cur == i);
        defSizes.addItem (120 + i, names[i], true, i == def);
    }
    m.addSeparator();
    m.addSubMenu ("Default size (new windows)", defSizes);
}

static bool handleSizeChoice (int r, juce::Component* c)
{
    if (c == nullptr) return false;
    auto* ed = c->findParentComponentOfClass<AmpireEditor>();
    if (ed == nullptr) ed = dynamic_cast<AmpireEditor*> (c);
    if (ed == nullptr) return false;
    if (r >= 101 && r <= 103) { ed->setScaleIndex (r - 101); return true; }
    if (r >= 120 && r < 120 + AmpireEditor::numSizes)
    {
        WigaWindowSize::setDefaultIndex (JucePlugin_Name, r - 120);
        ed->setScaleIndex (-1);
        return true;
    }
    return false;
}

static void addMidiItems (juce::PopupMenu& midi, AmpireProcessor& p)
{
    for (int i = 0; i < AmpireProcessor::numLearn; ++i)
        midi.addItem (1000 + i, juce::String ("Learn: ") + AmpireProcessor::learnDef (i).name + " (" + p.learnAssignText (i) + ")", true, p.learnTarget.load() == i);
    midi.addSeparator();
    if (p.learnTarget.load() >= 0) midi.addItem (1200, "Cancel MIDI learn");
    midi.addItem (1202, "Footswitch momentary (each press toggles)", true, p.toggleMomentary.load());
    midi.addItem (1201, "Clear all MIDI assignments");
}

static bool handleLearnChoice (int r, AmpireProcessor* p)
{
    if (r < 1000 || r > 1299 || p == nullptr) return false;
    if (r < 1000 + AmpireProcessor::numLearn) p->learnTarget.store (p->learnTarget.load() == r - 1000 ? -1 : r - 1000);
    else if (r >= 1100 && r < 1100 + AmpireProcessor::numLearn) p->clearLearn (r - 1100);
    else if (r == 1200) p->learnTarget.store (-1);
    else if (r == 1201) p->resetMidiMap();
    else if (r == 1202) p->toggleMomentary = ! p->toggleMomentary.load();
    return true;
}

static void showContextMenu (juce::Component& c, int learnIdx)
{
    // un seul menu dans tout le plugin : clic droit n'importe ou = menu des reglages (MIDI Learn de chaque commande
    // dans son sous-menu MIDI)
    juce::ignoreUnused (learnIdx);
    showGearMenu (c);
    return;
    auto* p = procFor (&c);
    if (p == nullptr) return;
    juce::PopupMenu m;
    if (learnIdx >= 0)
    {
        const juce::String name = AmpireProcessor::learnDef (learnIdx).name;
        m.addSectionHeader (name);
        m.addItem (1000 + learnIdx, "MIDI Learn: " + name + " (" + p->learnAssignText (learnIdx) + ")", true, p->learnTarget.load() == learnIdx);
        m.addItem (1100 + learnIdx, "Clear MIDI: " + name, p->learnAssignText (learnIdx) != "none");
        if (p->learnTarget.load() >= 0) m.addItem (1200, "Cancel MIDI learn");
        m.addSeparator();
    }
    m.addSectionHeader ("Plugin size");
    addSizeItems (m, c);
    if (learnIdx < 0)
    {
        m.addSeparator();
        juce::PopupMenu midi;
        addMidiItems (midi, *p);
        m.addSubMenu ("MIDI", midi);
    }
    juce::Component::SafePointer<juce::Component> sp (&c);
    m.showMenuAsync (juce::PopupMenu::Options(), [sp, p] (int r)
    {
        if (sp == nullptr) return;
        if (r == 2) { if (auto* ed = sp->findParentComponentOfClass<AmpireEditor>()) ed->openColourPicker (sp.getComponent()); return; }
        if (! handleLearnChoice (r, p)) handleSizeChoice (r, sp.getComponent());
    });
}

static void showGearMenu (juce::Component& c)
{
    auto* p = procFor (&c);
    if (p == nullptr) return;
    auto& ap = p->apvts;
    auto val = [&ap] (const char* id) { return ap.getRawParameterValue (id)->load(); };
    const int lows[] = { 20, 40, 60, 80, 100, 120, 150 };
    const int highs[] = { 20000, 12000, 10000, 8000, 7000, 6000, 5000 };

    juce::PopupMenu m, aaM, outM, inM, calM, lowM, highM, sizeM, midi, folderM;
    m.addSectionHeader ("Ampire " JucePlugin_VersionString " - NINE HANDS | DSP");
    {
        juce::PopupMenu presetM;
        const auto names = p->getPresetNames();
        const auto cur = p->getPresetName();
        presetM.addItem (800, "Init (empty)");
        presetM.addSeparator();
        if (names.isEmpty()) presetM.addItem (899, "No preset saved yet", false);
        for (int i = 0; i < juce::jmin (names.size(), 99); ++i) presetM.addItem (900 + i, names[i], true, names[i] == cur);
        presetM.addSeparator();
        presetM.addItem (801, "Save preset as...");
        presetM.addItem (802, "Save \"" + cur + "\"", cur.isNotEmpty());
        presetM.addItem (803, "Delete \"" + cur + "\"", cur.isNotEmpty());
        presetM.addItem (804, "Open presets folder");
        m.addSubMenu ("Presets" + (cur.isNotEmpty() ? juce::String (" (") + cur + (p->isPresetDirty() ? " *" : "") + ")" : juce::String()), presetM);
        m.addSeparator();
    }
    // anti-repliement
    const int os = (int) std::lround (val ("oversample"));
    aaM.addItem (30, "Off (lowest CPU)", true, os == 0);
    aaM.addItem (31, "2x (recommended)", true, os == 1);
    aaM.addItem (32, "4x (best, high gain)", true, os == 2);
    m.addSubMenu ("Anti-aliasing (oversampling, 0 latency)", aaM);
    // niveaux
    const int om = (int) std::lround (val ("outmode"));
    outM.addItem (33, "Raw (capture as recorded)", true, om == 0);
    outM.addItem (34, "Normalized (all captures at the same loudness)", true, om == 1);
    outM.addItem (35, "Calibrated (dBu levels of the capture)", true, om == 2);
    m.addSubMenu ("Output level", outM);
    calM.addItem (36, "Calibrate input to each capture", true, val ("calin") > 0.5f);
    calM.addSeparator();
    const float cl = val ("callevel");
    const float levels[] = { 0.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f, 20.0f, 24.0f };
    for (int i = 0; i < 8; ++i) calM.addItem (40 + i, "Interface: 0 dBFS = +" + juce::String ((int) levels[i]) + " dBu", true, std::abs (cl - levels[i]) < 0.05f);
    m.addSubMenu ("Input calibration", calM);
    const int is = (int) std::lround (val ("inputsel"));
    inM.addItem (50, "Left", true, is == 0); inM.addItem (51, "Right", true, is == 1); inM.addItem (52, "L + R (sum)", true, is == 2);
    m.addSubMenu ("Input channel", inM);
    m.addItem (38, "Tone stack (BASS / MIDDLE / TREBLE)", true, val ("tonestack") > 0.5f);
    m.addSeparator();
    {
        auto* edp = c.findParentComponentOfClass<AmpireEditor>();
        const float lg = edp != nullptr ? edp->getLedGlow() : 1.0f;
        const char* glowNames[4] = { "Off", "Low", "Medium (design)", "High" };
        const float glowVals[4] = { 0.0f, 0.5f, 1.0f, 1.6f };
        juce::PopupMenu colM, lgM;
        colM.addItem (76, "POWER LED colour...");
        for (int i = 0; i < 4; ++i) lgM.addItem (77 + i, glowNames[i], true, std::abs (lg - glowVals[i]) < 0.01f);
        colM.addSubMenu ("POWER LED glow", lgM);
        colM.addSeparator();
        colM.addItem (70, "Meter LED colour...");
        colM.addItem (71, "Saturation LED colour (top 3)...");
        m.addSubMenu ("Colours & glow", colM);
    }
    {
        const auto root = p->library.getRoot();
        folderM.addSectionHeader (root.isDirectory() ? root.getFullPathName() : juce::String ("No library folder"));
        const auto info = p->getSlotInfo (0);
        const juce::File cur (info.meta.path);
        const bool canUse = info.loaded && ! info.fromEmbedded && juce::File::isAbsolutePath (info.meta.path)
                            && cur.getParentDirectory().isDirectory() && cur.getParentDirectory() != root;
        folderM.addItem (60, "Choose default capture folder...");
        folderM.addItem (62, "Use this capture's folder as default", canUse);
        folderM.addItem (61, "Rescan library");
        m.addSubMenu ("Capture folder", folderM);
    }
    for (int i = 0; i < 7; ++i) lowM.addItem (10 + i, i == 0 ? juce::String ("Off") : juce::String (lows[i]) + " Hz", true, std::abs (val ("lowcut") - (float) lows[i]) < 1.0f);
    for (int i = 0; i < 7; ++i) highM.addItem (20 + i, i == 0 ? juce::String ("Off") : juce::String (highs[i] / 1000.0, 1).replace (".0", "") + " kHz", true, std::abs (val ("highcut") - (float) highs[i]) < 10.0f);
    m.addSubMenu ("Low cut", lowM);
    m.addSubMenu ("High cut", highM);
    m.addSeparator();
    addMidiItems (midi, *p);
    m.addSubMenu ("MIDI", midi);
    addSizeItems (sizeM, c);
    m.addSubMenu ("Plugin size", sizeM);
    juce::Component::SafePointer<juce::Component> sp (&c);
    m.showMenuAsync (juce::PopupMenu::Options(), [sp, p, lows, highs, levels] (int r)
    {
        if (sp == nullptr) return;
        auto set = [p] (const char* id, float v) { if (auto* prm = p->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        auto flip = [p, &set] (const char* id) { set (id, p->apvts.getRawParameterValue (id)->load() > 0.5f ? 0.0f : 1.0f); };
        auto* ed = sp->findParentComponentOfClass<AmpireEditor>();
        if (r == 70 || r == 71) { if (ed) ed->openColourPicker (sp.getComponent(), r == 70 ? 1 : 2); }
        else if (r == 76) { if (ed) ed->openColourPicker (sp.getComponent(), 3); }
        else if (r >= 77 && r <= 80) { const float v[4] = { 0.0f, 0.5f, 1.0f, 1.6f }; if (ed) ed->setLedGlow (v[r - 77]); }
        else if (r == 800) p->initPreset();
        else if (r == 801) { if (ed) ed->askPresetName(); }
        else if (r == 802) p->savePreset (p->getPresetName());
        else if (r == 803) p->deletePreset (p->getPresetName());
        else if (r == 804) { AmpireProcessor::presetFolder().createDirectory(); AmpireProcessor::presetFolder().startAsProcess(); }
        else if (r >= 900 && r < 999) { const auto names = p->getPresetNames(); if (r - 900 < names.size()) p->loadPreset (names[r - 900]); }
        else if (r >= 30 && r <= 32) set ("oversample", (float) (r - 30));
        else if (r >= 33 && r <= 35) set ("outmode", (float) (r - 33));
        else if (r == 36) flip ("calin");
        else if (r == 38) flip ("tonestack");
        else if (r >= 40 && r < 48) set ("callevel", levels[r - 40]);
        else if (r >= 50 && r <= 52) set ("inputsel", (float) (r - 50));
        else if (r == 60) { if (ed) ed->chooseFolder (true); }
        else if (r == 61) p->library.rescan();
        else if (r == 62)
        {
            const auto dir = juce::File (p->getSlotInfo (0).meta.path).getParentDirectory();
            if (dir.isDirectory()) { p->library.setRoot (dir); if (ed) ed->showStatus ("Default capture folder: " + dir.getFileName()); }
        }
        else if (r >= 10 && r < 17) set ("lowcut", (float) lows[r - 10]);
        else if (r >= 20 && r < 27) set ("highcut", (float) highs[r - 20]);
        else if (! handleLearnChoice (r, p)) handleSizeChoice (r, sp.getComponent());
    });
}
