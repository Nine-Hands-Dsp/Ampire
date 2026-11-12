#include "PluginEditor.h"
#include "NhdStack.h"
#include <BinaryData.h>
#include "WigaWindowSize.h"
#include "ColourPicker.h"
#include "MenuStyle.h"
#include <complex>

// ============================================================================
// AMPIRE - facade v005 (Ampire_Design_Mockup_v005.svg, repere 3600 x 1440). Le fond (assets/bg.png, calcule par
// tools/design_v005.py) porte le boitier, l'ecran et les fenetres vides, les boutons, les centres fixes des potards et
// les logos ; le plugin dessine par-dessus : toutes les inscriptions et les points, potards (couronne qui tourne, reflet
// fixe, repere), valeurs, ecran de la capture, vu-metres a LEDs (48 LEDs, memoire de crete, reflet dans le verre),
// diodes et inverseurs STACK / POWER.
// Repere logique 750 x 300 points (taille « 60 % ») ; anti-gel v2 (fond mis en cache par le timer).
// ============================================================================
namespace
{
    constexpr int kW = 750, kH = 300;
    constexpr float kVisX = 0.0f, kVisY = 0.0f, kVisW = (float) kW, kVisH = (float) kH;
    constexpr int kWin = 747, kWinH = 299;      // fenetre « 60 % » (meme largeur qu'Impulse)
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
    juce::Font emSemi (float px)   { return juce::Font (juce::FontOptions (Fonts::get().semi).withPointHeight (px)); }
    juce::Font emMedium (float px) { return juce::Font (juce::FontOptions (Fonts::get().medium).withPointHeight (px)); }
    juce::Font fName (float h)
    {
        auto f = fSemi (h);
        f.setFeatureSetting ({ juce::FontFeatureTag ("calt"), juce::FontFeatureSetting::featureDisabled });
        return f;
    }
    juce::String dotSep() { return juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")); }

    // ---- geometrie : points logiques = unites de la maquette v005 (3600 de large) x 750 / 3600
    constexpr float kDs = (float) kW / 3600.0f;
    constexpr float D (float v) { return v * kDs; }
    const juce::Rectangle<float> R (float x0, float y0, float x1, float y1) { return { D (x0), D (y0), D (x1 - x0), D (y1 - y0) }; }
    constexpr float M (float oldUnits) { return D (oldUnits * 50.0f / 21.0f); }   // mesures des LEDs de l'ancienne maquette (barre de 21)

    constexpr float kKnobXd[6] = { 600.0f, 1000.0f, 1400.0f, 1800.0f, 2200.0f, 2600.0f };
    const float kKnobX[6] = { D (600.0f), D (1000.0f), D (1400.0f), D (1800.0f), D (2200.0f), D (2600.0f) };
    constexpr float kKnobYd = 950.0f;
    const float kKnobY = D (kKnobYd);
    constexpr float kKnobBox = 300.0f;                 // composant du potard (couronne de 280 au centre)
    constexpr float kSweep = 120.0f;   // course du repere : -120 .. +120 degres, centre de la course en haut
    constexpr float kDotR = 162.0f, kDotSize = 5.5f;   // graduation : 9 points tous les 30 degres
    // titres, valeurs et graduations des potards (lignes de base)
    constexpr float kTitleBase = 703.52f, kValueBase = 757.0f, kScaleBase = 1088.0f;
    // diodes et inverseurs (STACK a gauche, POWER a droite)
    const juce::Point<float> kDiodeStack (D (2954.0f), D (783.43f)), kDiodePower (D (3174.0f), D (783.43f));
    const float kDiodeSprite = D (280.0f * 0.75f);   // diodes STACK / POWER -25 % (le logement de la facade garde sa taille)
    const juce::Point<float> kSwStack (D (2954.0f), D (1029.71f)), kSwPower (D (3174.0f), D (1029.71f));
    const float kToggleSprite = D (252.0f);
    juce::Rectangle<float> diodeRect (juce::Point<float> c) { return { c.x - kDiodeSprite * 0.5f, c.y - kDiodeSprite * 0.5f, kDiodeSprite, kDiodeSprite }; }
    // proportions d'Impulse v1.5 (design v005, repere 3600 x 1440, 2,5 : 1) pour le mode stack
    constexpr float kImpulseAspect = 3600.0f / 1440.0f;
    // ecran : infos de la capture a gauche (alignees sur le bord droit de la fenetre IN), etat a droite du filet
    // (filet dans l'axe du potard OUTPUT)
    const juce::Rectangle<float> rLcd[1] = { R (58, 212, 3080, 537) };
    const juce::Rectangle<float> rRow[1] = { R (50, 204, 3088, 545) };
    constexpr float kInfoX = 212.0f, kDividerX = 2200.0f, kStatusCx = 2640.0f;
    const juce::Rectangle<float> rLcdInfo = R (212, 212, 2160, 537);
    const juce::Rectangle<float> rStatus = R (2210, 214, 3076, 535), rStatusText = R (2230, 214, 3056, 535);
    const juce::Rectangle<float> rLib = R (3114, 312.5f, 3234, 432.5f), rPrev = R (3268, 312.5f, 3388, 432.5f), rNext = R (3422, 312.5f, 3542, 432.5f);
    // vu-metres : 48 LEDs, reflet sur le bord exterieur du verre ; ecretage = 3 LEDs du haut en rouge vif
    const juce::Rectangle<float> rMeterWinIn = R (58, 571, 212, 1348), rMeterWinOut = R (3388, 571, 3542, 1348);
    const juce::Rectangle<float> rMeterIn = R (92, 671, 142, 1280), rMeterOut = R (3458, 671, 3508, 1280);
    const float kReflXIn = D (76.0f), kReflXOut = D (3524.0f);
    // visibilite du reflet de chaque LED (bas -> haut), relevee sur l'ancienne maquette
    const float kMeterVis[48] = { 0.048f, 0.182f, 0.064f, 0.046f, 0.110f, 0.308f, 0.352f, 0.217f, 0.127f, 0.472f, 0.533f, 0.224f, 0.216f, 0.337f, 0.395f, 0.327f, 0.225f, 0.242f, 0.396f, 0.695f, 0.114f, 0.136f, 0.689f, 0.371f, 0.691f, 0.403f, 0.297f, 0.709f, 0.244f, 0.484f, 0.375f, 0.110f, 0.115f, 0.436f, 0.202f, 0.243f, 0.145f, 0.278f, 0.205f, 0.053f, 0.012f, 0.033f, 0.012f, 0.011f, 0.009f, 0.003f, 0.008f, 0.002f };
    // echelle gravee (dBFS -> position 0..1 sur la hauteur de la barre)
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

    // visuels photo-realistes calcules par tools/design_v005.py
    struct Art
    {
        juce::Image bg, crown, reflect, indicator, toggleUp, toggleDown, diodeOn, diodeOff;
        Art()
        {
            auto ld = [] (const void* d, int n) { return juce::ImageCache::getFromMemory (d, n); };
            bg = ld (BinaryData::bg_png, BinaryData::bg_pngSize);
            crown = ld (BinaryData::knob_crown_png, BinaryData::knob_crown_pngSize);
            reflect = ld (BinaryData::knob_reflect_png, BinaryData::knob_reflect_pngSize);
            indicator = ld (BinaryData::knob_indicator_png, BinaryData::knob_indicator_pngSize);
            toggleUp = ld (BinaryData::switch_up_png, BinaryData::switch_up_pngSize);
            toggleDown = ld (BinaryData::switch_down_png, BinaryData::switch_down_pngSize);
            diodeOn = ld (BinaryData::diode_on_png, BinaryData::diode_on_pngSize);
            diodeOff = ld (BinaryData::diode_off_png, BinaryData::diode_off_pngSize);
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
static juce::LookAndFeel* menuLookFor (juce::Component* c);
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

// ---- potard : centre fixe (dans le fond), couronne crantee qui tourne, reflet fixe par-dessus, repere blanc qui tourne
//      image composee mise en cache (recalculee seulement quand la valeur, l'etat ou la resolution changent).
//      La couronne a 96 crans (un tous les 3,75 degres) : tournee par a-coups de 2 a 3,7 degres (glisser la souris), les
//      crans semblaient reculer (effet stroboscopique, « roue de diligence »). Comme une vraie couronne qui tourne, elle
//      est dessinee avec un flou de mouvement sur l'angle parcouru depuis l'image precedente, puis redevient nette a l'arret.
class Knob : public ParamControl, private juce::Timer
{
public:
    Knob (AmpireProcessor& p, const char* id, int learn) : ParamControl (p, id, learn) {}
    std::function<juce::String (float)> format;   // texte de la ligne d'etat
    bool dimmed = false;                           // SIZE sans capture A2 : repere grise

    void paint (juce::Graphics& g) override
    {
        const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int w = juce::jmax (1, (int) std::ceil ((float) getWidth() * ps)), h = juce::jmax (1, (int) std::ceil ((float) getHeight() * ps));
        const float n = norm();
        if (! cache.isValid() || cache.getWidth() != w || cache.getHeight() != h || n != cacheNorm || dimmed != cacheDim || needSharp)
        {
            const auto& art = Art::get();
            const float ang = juce::degreesToRadians (-kSweep + 2.0f * kSweep * n);
            const bool sameSize = cache.isValid() && cache.getWidth() == w && cache.getHeight() == h;
            const float from = (sameSize && ! needSharp && std::isfinite (cacheAng)) ? cacheAng : ang;
            needSharp = false;
            cache = juce::Image (juce::Image::ARGB, w, h, true);
            juce::Graphics cg (cache);
            cg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            const float k = (float) w * (280.0f / kKnobBox) / (float) art.crown.getWidth();
            auto place = [&] (const juce::Image& im, float a)
            {
                return juce::AffineTransform::translation (-0.5f * (float) im.getWidth(), -0.5f * (float) im.getHeight())
                           .rotated (a).scaled (k).translated (0.5f * (float) w, 0.5f * (float) h);
            };
            // couronne : nette, ou moyenne de positions reparties sur l'angle parcouru (flou de mouvement)
            const float step = juce::degreesToRadians (0.6f);
            const int sub = juce::jlimit (1, 16, (int) std::ceil (std::abs (ang - from) / step));
            if (sub <= 1) cg.drawImageTransformed (art.crown, place (art.crown, ang));
            else
            {
                std::vector<float> acc ((size_t) w * (size_t) h * 4, 0.0f);
                juce::Image tmp (juce::Image::ARGB, w, h, true);
                for (int i = 0; i < sub; ++i)
                {
                    tmp.clear (tmp.getBounds());
                    {
                        juce::Graphics tg (tmp);
                        tg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                        tg.drawImageTransformed (art.crown, place (art.crown, from + (ang - from) * ((float) i + 1.0f) / (float) sub));
                    }
                    const juce::Image::BitmapData bd (tmp, juce::Image::BitmapData::readOnly);
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x)
                        {
                            const auto* px = reinterpret_cast<const juce::PixelARGB*> (bd.getPixelPointer (x, y));
                            float* a4 = &acc[((size_t) y * (size_t) w + (size_t) x) * 4];
                            a4[0] += px->getAlpha(); a4[1] += px->getRed(); a4[2] += px->getGreen(); a4[3] += px->getBlue();
                        }
                }
                juce::Image blurred (juce::Image::ARGB, w, h, true);
                {
                    const juce::Image::BitmapData bd (blurred, juce::Image::BitmapData::writeOnly);
                    const float inv = 1.0f / (float) sub;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x)
                        {
                            const float* a4 = &acc[((size_t) y * (size_t) w + (size_t) x) * 4];
                            auto* px = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
                            px->setARGB ((juce::uint8) std::lround (a4[0] * inv), (juce::uint8) std::lround (a4[1] * inv),
                                         (juce::uint8) std::lround (a4[2] * inv), (juce::uint8) std::lround (a4[3] * inv));   // premultiplie
                        }
                }
                cg.drawImageAt (blurred, 0, 0);
                startTimer (70);                   // redevient nette quand le potard s'arrete
            }
            cg.drawImageTransformed (art.reflect, place (art.reflect, 0.0f));
            cg.setOpacity (dimmed ? 0.35f : 1.0f);
            cg.drawImageTransformed (art.indicator, place (art.indicator, ang));
            cacheNorm = n; cacheDim = dimmed; cacheAng = ang;
        }
        g.drawImageTransformed (cache, juce::AffineTransform::scale ((float) getWidth() / (float) w, (float) getHeight() / (float) h));
        if (LearnBlink::shown (proc, learnIdx)) LearnBlink::draw (g, getLocalBounds().toFloat().reduced (2.0f), getWidth() * 0.5f);
    }
    bool hitTest (int x, int y) override
    {
        const float r = (float) getWidth() * 0.5f;
        return juce::Point<float> ((float) x, (float) y).getDistanceFrom ({ r, r }) <= r * (290.0f / kKnobBox);
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
    juce::Image cache;
    float cacheNorm = -1.0f, cacheAng = std::numeric_limits<float>::quiet_NaN();
    bool cacheDim = false, needSharp = false;
    void timerCallback() override { stopTimer(); needSharp = true; repaint(); }
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

#include "LibraryView.h"

// ============================================================================ contenu de la fenetre
// ---- inverseur STACK (mode stack : l'Impulse de la meme piste REAPER colle sous Ampire) ; levier haut = actif.
//      La diode STACK (dessinee par la facade) s'allume quand le mode est actif.
class StackBtn : public juce::Component
{
public:
    std::function<void()> onClick;
    bool available = false, on = false;
    juce::Colour led { 0xffff2a14 };
    void paint (juce::Graphics& g) override
    {
        drawSpriteIn (g, on ? Art::get().toggleUp : Art::get().toggleDown, getLocalBounds().toFloat());
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showContextMenu (*this, -1); return; }
        if (onClick) onClick();
    }
};

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
            knobs[i]->setBounds (toInt ({ kKnobX[col] - D (kKnobBox) * 0.5f, kKnobY - D (kKnobBox) * 0.5f, D (kKnobBox), D (kKnobBox) }));
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
        power.setBounds (toInt ({ kSwPower.x - kToggleSprite * 0.5f, kSwPower.y - kToggleSprite * 0.5f, kToggleSprite, kToggleSprite }));
        power.onValue = [this] { askRepaint (toInt (diodeRect (kDiodePower))); };

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

        // ---- mode stack : inverseur STACK (a gauche de POWER)
        addAndMakeVisible (stackBtn);
        stackBtn.setBounds (toInt ({ kSwStack.x - kToggleSprite * 0.5f, kSwStack.y - kToggleSprite * 0.5f, kToggleSprite, kToggleSprite }));
        stackBtn.onClick = [this] { if (auto* ed = findParentComponentOfClass<AmpireEditor>()) ed->toggleStack(); };

        addChildComponent (library);
        library.setBounds (0, 0, kW, kH);
        library.onClose = [this] { askRepaint (getLocalBounds()); };

        updateKnobSet();
        refreshInfos();
        rebuildLedImage();
        startTimerHz (30);
    }

    ~AmpireContent() override = default;
    void openLibrary (int s) { library.open (s); }
   #if AMPIRE_TOOL
    juce::Component* previewExpanded (bool scrolling) { library.previewExpanded (scrolling); return &library; }
    void previewLibrary (int tab, bool signedIn) { library.previewDemo (signedIn); library.open (0); library.setTab (tab % 10); if (tab >= 10) library.previewDialog (tab / 10); }
   #endif

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
        drawFaceLabels (g);          // inscriptions dessinees a chaque fois (meme rendu net que les valeurs, a toutes les tailles)
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
    // ---- serigraphie de la facade : TOUTES les inscriptions (titres, valeurs, graduations, STACK / POWER / ON / OFF,
    //      IN / OUT, echelles des vu-metres) sont dessinees ici avec le meme rendu (Inter Medium, blanc, ombre legere) ;
    //      corps et lignes de base en pixels de la maquette v005 (corps Inter = capitale du Myriad Pro 48 px de la maquette)
    static juce::Font silkFont (float sizeDesign, float kerning = 0.0f) { return emMedium (D (sizeDesign)).withExtraKerningFactor (kerning); }
    static void silkText (juce::Graphics& g, const juce::String& t, float xDesign, float baselineDesign, const juce::Font& f, float alpha = 1.0f,
                          juce::Justification j = juce::Justification::horizontallyCentred)
    {
        g.setFont (f);
        const float x = D (xDesign), y = D (baselineDesign);
        const auto jj = j.getOnlyHorizontalFlags();
        auto at = [&] (float dx, float dy)
        {
            if (jj == juce::Justification::left) g.drawSingleLineText (t, juce::roundToInt (x + dx), juce::roundToInt (y + dy), juce::Justification::left);
            else
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText (f, t, 0.0f, 0.0f);
                const float w = ga.getBoundingBox (0, -1, true).getRight();
                const float x0 = jj == juce::Justification::right ? x - w : x - w * 0.5f;
                ga.moveRangeOfGlyphs (0, -1, x0 + dx, y + dy);
                ga.draw (g);
            }
        };
        g.setColour (juce::Colours::black.withAlpha (0.45f * alpha));
        at (D (1.6f), D (1.6f));
        g.setColour (kSilk.withAlpha (0.97f * alpha));
        at (0.0f, 0.0f);
    }
    juce::Rectangle<int> valueArea (int i) const { return toInt (R (kKnobXd[i] - 190.0f, kValueBase - 45.0f, kKnobXd[i] + 190.0f, kValueBase + 14.0f)); }
    void drawKnobValues (juce::Graphics& g)
    {
        static const juce::Font f = silkFont (36.0f);
        for (int i = 0; i < 6; ++i)
            silkText (g, knobValueText (i), kKnobXd[i], kValueBase, f, (i == 5 && numSizes() < 2) ? 0.40f : 1.0f);
    }
    static juce::String minusSign (juce::String t) { return t.replace ("-", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))); }
    static float scaleX (int i, int side) { return kKnobXd[i] + (side == 0 ? -1.0f : 1.0f) * kDotR * std::sin (juce::degreesToRadians (120.0f)); }
    // inscriptions fixes
    void drawFaceLabels (juce::Graphics& g)
    {
        static const juce::Font title = silkFont (44.5f, 0.02f), small = silkFont (36.0f), io = silkFont (40.0f, 0.02f), grad = silkFont (23.0f);
        const char* names[5] = { "INPUT", "BASS", "MIDDLE", "TREBLE", "OUTPUT" };
        const char* scales[5][2] = { { "-20", "+20" }, { "0", "10" }, { "0", "10" }, { "0", "10" }, { "-40", "+40" } };
        for (int i = 0; i < 5; ++i)
        {
            silkText (g, names[i], kKnobXd[i], kTitleBase, title);
            silkText (g, minusSign (scales[i][0]), scaleX (i, 0), kScaleBase, small);
            silkText (g, minusSign (scales[i][1]), scaleX (i, 1), kScaleBase, small);
        }
        silkText (g, "STACK", 2954.0f, kTitleBase, title);
        silkText (g, "POWER", 3174.0f, kTitleBase, title);
        for (float x : { 2954.0f, 3174.0f })
        {
            silkText (g, "ON", x, 900.37f, small);
            silkText (g, "OFF", x, 1182.78f, small);
        }
        silkText (g, "IN", 135.0f, 640.0f, io);
        silkText (g, "OUT", 3465.0f, 640.0f, io);
        // echelles des vu-metres (graduation a droite de IN, a gauche de OUT)
        const float ticks[7] = { 0.0f, -6.0f, -12.0f, -20.0f, -30.0f, -40.0f, -50.0f };
        for (float db : ticks)
        {
            const float y = 1280.0f - meterPos (db) * 609.0f;
            const juce::String lab = minusSign (juce::String ((int) db));
            for (int side = 0; side < 2; ++side)
            {
                const auto tick = side == 0 ? R (145.0f, y - 1.3f, 153.0f, y + 1.3f) : R (3447.0f, y - 1.3f, 3455.0f, y + 1.3f);
                g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRect (tick.translated (D (1.6f), D (1.6f)));
                g.setColour (kSilk.withAlpha (0.97f)); g.fillRect (tick);
                if (side == 0) silkText (g, lab, 161.0f, y + 8.2f, grad, 1.0f, juce::Justification::left);
                else           silkText (g, lab, 3439.0f, y + 8.2f, grad, 1.0f, juce::Justification::right);
            }
        }
        // filet de l'ecran (dans l'axe du potard OUTPUT)
        g.setColour (juce::Colour (0xff8a8b8d));
        g.fillRect (R (kDividerX - 1.25f, 262.0f, kDividerX + 1.25f, 487.0f));
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
        static const juce::Font title = silkFont (44.5f, 0.02f), scale = silkFont (36.0f);
        const int n = numSizes();
        const bool ladder = n > 2;
        const float a = n >= 2 ? 1.0f : 0.40f;
        silkText (g, ladder ? "GAIN STEP" : "A2 SIZE", kKnobXd[5], kTitleBase, title, a);
        silkText (g, ladder ? "LOW" : "LITE", scaleX (5, 0), kScaleBase, scale, a);
        silkText (g, ladder ? "HIGH" : "FULL", scaleX (5, 1), kScaleBase, scale, a);
    }
    juce::Rectangle<int> sizeLabelArea() const { return toInt (R (2330, 640, 2870, 1110)); }

    void drawJewel (juce::Graphics& g)
    {
        // diodes STACK et POWER : image allumee (recoloree selon le reglage « LED colour ») ou eteinte
        const auto& on = ledImage.isValid() ? ledImage : Art::get().diodeOn;
        const bool stackLit = stackBtn.on && stackBtn.available;
        drawSpriteIn (g, stackLit ? on : Art::get().diodeOff, diodeRect (kDiodeStack));
        drawSpriteIn (g, power.norm() > 0.5f ? on : Art::get().diodeOff, diodeRect (kDiodePower));
    }
    void repaintDiodes() { askRepaint (toInt (diodeRect (kDiodeStack))); askRepaint (toInt (diodeRect (kDiodePower))); }
public:
    // couleur et halo des diodes : l'image fournie (diode rouge) est recoloree une fois (thread de messages, hors paint)
    juce::Colour ledColour { AmpireSettings::ledColour (JucePlugin_Name) };
    float ledGlow = AmpireSettings::glowAttr (JucePlugin_Name, "ledGlow", 1.0f);
    float screenGlow = 0.0f;                         // pas de halo sur l'ecran (texte blanc fixe)
    void rebuildLedImage()
    {
        const auto& src = Art::get().diodeOn;
        const bool recolour = ledColour.getARGB() != AmpireSettings::kDefaultLed;
        if (! recolour && std::abs (ledGlow - 1.0f) < 0.01f) { ledImage = {}; repaintDiodes(); return; }
        juce::Image img (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        const float c0 = 0.5f * (float) src.getWidth(), lensR = (float) src.getWidth() * 34.0f / 280.0f;
        const float cr = ledColour.getFloatRed(), cg = ledColour.getFloatGreen(), cb = ledColour.getFloatBlue();
        for (int y = 0; y < src.getHeight(); ++y)
            for (int x = 0; x < src.getWidth(); ++x)
            {
                const auto p = src.getPixelAt (x, y);
                float a = p.getFloatAlpha();
                if (a <= 0.0f) continue;
                float r = p.getFloatRed(), gg = p.getFloatGreen(), b = p.getFloatBlue();
                if (recolour)
                {
                    const float v = juce::jmax (r, gg, b), w = v > 0.0f ? juce::jmin (r, gg, b) / v : 0.0f;   // luminosite / blancheur
                    r = (cr * (1.0f - w) + w) * v; gg = (cg * (1.0f - w) + w) * v; b = (cb * (1.0f - w) + w) * v;
                }
                const float d = std::hypot ((float) x + 0.5f - c0, (float) y + 0.5f - c0);
                const float glowZone = juce::jlimit (0.0f, 1.0f, (d - lensR) / ((float) src.getWidth() * 3.0f / 280.0f));
                a = juce::jlimit (0.0f, 1.0f, a * (1.0f + (ledGlow - 1.0f) * glowZone));
                img.setPixelAt (x, y, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, r), juce::jlimit (0.0f, 1.0f, gg), juce::jlimit (0.0f, 1.0f, b), a));
            }
        ledImage = img;
        repaintDiodes();
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

    // texte de l'ecran pose sur sa ligne de base (repere de la maquette v005)
    void lcdLine (juce::Graphics& g, const juce::String& t, float xDesign, float baselineDesign, float wDesign, const juce::Font& f,
                  juce::Justification j = juce::Justification::topLeft)
    {
        g.setFont (f);
        const float x = j.testFlags (juce::Justification::horizontallyCentred) ? D (xDesign - wDesign * 0.5f) : D (xDesign);
        screenText (g, t, juce::Rectangle<float> (x, D (baselineDesign) - f.getAscent(), D (wDesign), f.getHeight()),
                    juce::Justification (j.getOnlyHorizontalFlags() | juce::Justification::top));
    }
    void drawSlot (juce::Graphics& g, int s)
    {
        const auto& inf = info[s];
        const auto& m = inf.meta;
        const float wInfo = kDividerX - 60.0f - kInfoX;
        static const juce::Font fTitle = emSemi (D (66.0f)), fL2 = emMedium (D (40.0f)), fL3 = emMedium (D (38.0f));
        const bool showErr = inf.error.isNotEmpty() && juce::Time::getMillisecondCounter() < errUntil[s];
        if (showErr)
        {
            g.setColour (col::err); g.setFont (emSemi (D (40.0f)));
            g.drawFittedText (inf.error, R (kInfoX, 260.0f, kInfoX + wInfo, 490.0f).toNearestInt(), juce::Justification::centredLeft, 3, 0.8f);
        }
        else if (inf.loaded)
        {
            const auto title = m.title.isNotEmpty() ? m.title : m.fileName;
            setTextCol (g, kSilk);
            lcdLine (g, title, kInfoX, 330.0f, wInfo, fTitle);
            const auto gear = (m.gearMake + " " + m.gearModel).trim();
            juce::StringArray l2;
            if (gear.isNotEmpty()) l2.add (gear);
            if (m.gearType.isNotEmpty()) l2.add (m.gearType.replace ("_", " "));
            if (m.toneType.isNotEmpty()) l2.add (m.toneType.replace ("_", " "));
            setTextCol (g, kSilk.withAlpha (0.92f));
            lcdLine (g, l2.isEmpty() ? juce::String ("no gear information") : l2.joinIntoString (dotSep()), kInfoX, 400.0f, wInfo, fL2);
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
            setTextCol (g, kSilk.withAlpha (0.85f));
            lcdLine (g, fmt, kInfoX, 468.0f, wInfo, fL3);
        }
        else if (inf.loading) { setTextCol (g, kSilk.withAlpha (0.92f)); lcdLine (g, "LOADING...", kInfoX, 330.0f, wInfo, fTitle); }
        else
        {
            setTextCol (g, kSilk.withAlpha (0.45f));
            lcdLine (g, "EMPTY", kInfoX, 330.0f, wInfo, fTitle);
            g.setFont (fL2);
            g.drawFittedText ("Click here or on the library button, use the arrows, or drop a .nam file (NAM A1 / A2)",
                              R (kInfoX, 365.0f, kInfoX + wInfo, 480.0f).toNearestInt(), juce::Justification::topLeft, 2);
        }
        if (dropHover == s) { g.setColour (kSilk.withAlpha (0.8f)); g.drawRoundedRectangle (rLcd[s].reduced (D (14.0f)), D (12.0f), 1.5f); }
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
        auto meter = [&] (juce::Rectangle<float> r, float reflX, float level, float peak, float peakAlpha, bool clip)
        {
            const int lit = juce::jlimit (0, N, (int) std::lround (meterPos (juce::Decibels::gainToDecibels (level, -100.0f)) * N));
            const int pk = juce::jlimit (-1, N - 1, (int) std::lround (meterPos (juce::Decibels::gainToDecibels (peak, -100.0f)) * N) - 1);
            const bool showPk = pk >= lit && peakAlpha > 0.01f && peak > 0.0f;
            const float h = r.getHeight() / N, gap = M (1.6f);
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
                const float v = juce::jmin (1.0f, 7.0f * 0.9f * acc / wsum) * 0.4877f * 0.7f;   // opacite des reflets -30 % (nov. 2026)
                if (v < 0.01f) continue;
                const auto c = ledColour (i);
                const float cy = ledRect (i).getCentreY();
                juce::ColourGradient gr (c.withAlpha (v), reflX, cy, c.withAlpha (0.0f), reflX + M (4.0f), cy, true);
                g.setGradientFill (gr);
                g.fillEllipse (reflX - M (4.0f), cy - h * 1.15f, M (8.0f), h * 2.3f);
            }
            if (showPk)
            {
                const auto c = ledColour (pk);
                const float cy = ledRect (pk).getCentreY();
                juce::ColourGradient gr (c.withAlpha (0.4877f * 0.7f * 0.9f * peakAlpha), reflX, cy, c.withAlpha (0.0f), reflX + M (6.0f), cy, true);
                gr.addColour (0.45, c.withAlpha (0.25f * peakAlpha));
                g.setGradientFill (gr);
                g.fillEllipse (reflX - M (6.0f), cy - M (15.0f), M (12.0f), M (30.0f));
            }
            // LEDs eteintes
            for (int i = 0; i < N; ++i)
            {
                if (i < lit || (showPk && i == pk) || (clip && i >= N - 3)) continue;
                g.setColour (ledColour (i).withMultipliedBrightness (0.10f).withAlpha (0.9f));
                g.fillRect (ledRect (i));
            }
            // halo des LEDs allumees
            if (lit > 0)
            {
                const auto top = ledRect (lit - 1).getY();
                for (int pass = 0; pass < 2; ++pass)
                {
                    const float e = pass == 0 ? M (4.0f) : M (2.0f);
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
            // ecretage : les 3 LEDs du haut en rouge vif
            if (clip)
                for (int i = N - 3; i < N; ++i)
                {
                    const auto lr = ledRect (i);
                    g.setColour (juce::Colour (0xffff2a1a).withAlpha (0.35f)); g.fillRect (lr.expanded (M (1.5f)));
                    g.setColour (juce::Colour (0xffff2a1a)); g.fillRect (lr);
                    g.setColour (juce::Colour (0xffffd0c0).withAlpha (0.55f)); g.fillRect (lr.withSizeKeepingCentre (lr.getWidth() * 0.6f, lr.getHeight() * 0.4f));
                }
        };
        meter (rMeterIn, kReflXIn, shownIn, peakIn, peakAlphaIn, clipIn);
        meter (rMeterOut, kReflXOut, shownOut, peakOut, peakAlphaOut, clipOut);

        // afficheur d'etat : message, sinon CPU / latence
        const auto st = statusText();
        if (st.isNotEmpty())
        {
            setTextCol (g, kSilk);
            g.setFont (emSemi (D (40.0f)));
            screenText (g, st, rStatusText, juce::Justification::centred, true);
        }
        else
        {
            static const juce::Font fs = emMedium (D (38.0f));
            setTextCol (g, kSilk.withAlpha (0.92f));
            const int lat = proc.getReportedLatency();
            const double ms = 1000.0 * lat / juce::jmax (1.0, proc.getHostRate());
            juce::String k = juce::String (proc.getHostRate() / 1000.0, 1); if (k.endsWith (".0")) k = k.dropLastCharacters (2);
            lcdLine (g, "CPU " + juce::String (shownCpu, 1) + " %" + dotSep() + k + " kHz" + dotSep() + "AA " + juce::String (osShown) + "x",
                     kStatusCx, 352.0f, 820.0f, fs, juce::Justification::horizontallyCentred);
            lcdLine (g, "LATENCY " + (lat == 0 ? juce::String ("0 (none added)") : juce::String (lat) + " smp  " + juce::String (ms, 2) + " ms"),
                     kStatusCx, 440.0f, 820.0f, fs, juce::Justification::horizontallyCentred);
        }
    }

    // ---------------------------------------------------------------- fond (graphite, plaque anodisee, baie)
    void drawStatic (juce::Graphics& g, bool)
    {
        drawStaticBg (g);
        // graduation : 9 points identiques autour de chaque potard, tous les 30 degres de -120 a +120, centres sur l'axe du
        // potard (le point du haut est exactement dans l'axe du repere au milieu de la course)
        for (int i = 0; i < 6; ++i)
            for (int d = -4; d <= 4; ++d)
            {
                const float a = juce::degreesToRadians (30.0f * (float) d);
                const float x = kKnobX[i] + D (kDotR) * std::sin (a), y = kKnobY - D (kDotR) * std::cos (a);
                const float r = D (kDotSize);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillEllipse (x - r + D (1.6f), y - r + D (1.6f), 2 * r, 2 * r);
                g.setColour (kSilk.withAlpha (0.97f));
                g.fillEllipse (x - r, y - r, 2 * r, 2 * r);
            }
    }
    void drawStaticBg (juce::Graphics& g)
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
        MenuStyle::styled (m, menuLookFor (this)).showMenuAsync (juce::PopupMenu::Options(), [sp, s, path = inf.meta.path] (int r)
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
        MenuStyle::styled (m, menuLookFor (this)).showMenuAsync (juce::PopupMenu::Options(), [sp, names, cur] (int r)
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
            if (++stackTick >= 15) { stackTick = 0; ed->updateStack(); }   // Impulse sur la piste ? (2 fois par seconde)
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
            repaintDiodes();
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
public:
    StackBtn stackBtn;
private:
    int stackTick = 0;
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
    menuLook = std::make_unique<MenuStyle::MenuLnf>();
    setLookAndFeel (menuLook.get());               // menus clic droit (et menus des champs de texte) au design d'Octopus
    content = std::make_unique<AmpireContent> (proc);
    addAndMakeVisible (*content);
    setResizable (true, true);
    // jamais plus petit que la taille par defaut (« 60 % »)
    setResizeLimits (kWin, kWinH, (int) std::lround (kWin * 2.5f), (int) std::lround (kWinH * 2.5f));   // jusqu'a 150 %
    hostLock.setLimits (kWin, kWinH, (int) std::lround (kWin * 2.5f), (int) std::lround (kWinH * 2.5f));
    getConstrainer()->setFixedAspectRatio ((double) kVisW / (double) kVisH);
    // taille choisie au menu pour ce projet, sinon celle de « Default size (new windows) »
    applyScaleIndex (NineHandsWindow::openIndex (JucePlugin_Name, proc.uiScaleIdx.load(), numSizes));
}

AmpireEditor::~AmpireEditor()
{
    juce::PopupMenu::dismissAllActiveMenus();
    setLookAndFeel (nullptr);
    if (stackView != 0)
        if (auto* api = proc.stackApi()) api->destroyView (stackView);
    stackView = 0;
}

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
    hostLock.track (*this);
}

void AmpireEditor::resetRenderer()
{
    lastPeer = nullptr;
    useSoftwareRenderer();
    repaint();
}

void AmpireEditor::paint (juce::Graphics& g) { g.fillAll (col::outer); }

void AmpireEditor::openLibrary (int slot) { content->openLibrary (slot); }
#if AMPIRE_TOOL
void AmpireEditor::previewLibrary (int tab, bool signedIn) { content->previewLibrary (tab, signedIn); }
juce::Component* AmpireEditor::previewExpanded (bool scrolling) { return content->previewExpanded (scrolling); }
#endif
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
    const char* title = which == 1 ? "Meter LED colour" : which == 2 ? "Saturation LED colour (top 3)" : which == 3 ? "LED colour (POWER / STACK)" : "Screen text colour";
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
    // en mode stack, la vue d'Impulse (fenetre enfant native, toujours au-dessus) occupe le bas de la fenetre :
    // le nuancier et son calque restent dans la partie Ampire, sinon ils seraient caches dessous
    const auto zone = stackView != 0 ? getLocalBounds().withHeight (juce::jmin (getHeight(), ampireHeight (getWidth())))
                                     : getLocalBounds();
    auto area = near != nullptr ? getLocalArea (near, near->getLocalBounds()) : zone.withSizeKeepingCentre (10, 10);
    area = area.getIntersection (zone);
    if (area.isEmpty()) area = zone.withSizeKeepingCentre (10, 10);
    auto* layer = new WigaPickerLayer (std::move (picker), area, zone);
    addAndMakeVisible (layer);
    layer->toFront (true);
}
void AmpireEditor::parentHierarchyChanged() { useSoftwareRenderer(); hostLock.track (*this); }
void AmpireEditor::visibilityChanged() { useSoftwareRenderer(); if (isShowing()) repaint(); }

void AmpireEditor::resized()
{
    const float s = (float) getWidth() / kVisW;
    content->setTransform (juce::AffineTransform::translation (-kVisX, -kVisY).scaled (s));
    content->setBounds (0, 0, kW, kH);
    content->editorResized();            // jamais de setSize ici (boucle avec l'hote a 125 / 150 %)
    placeStackView();
}

// ============================================================================ mode « stack » (NhdStack.h)
int AmpireEditor::ampireHeight (int w) const { return (int) std::lround ((double) w * kVisH / kVisW); }
int AmpireEditor::impulseHeight (int w) const { return (int) std::lround ((double) w / kImpulseAspect); }

void AmpireEditor::setStackConstraints (bool stacked)
{
    const double aspect = stacked ? (double) kVisW / ((double) kVisH + (double) kVisW / kImpulseAspect) : (double) kVisW / (double) kVisH;
    const int minH = stacked ? kWinH + impulseHeight (kWin) : kWinH;
    setResizeLimits (kWin, minH, (int) std::lround (kWin * 2.5f), (int) std::lround (minH * 2.5f));
    hostLock.setLimits (kWin, minH, (int) std::lround (kWin * 2.5f), (int) std::lround (minH * 2.5f));
    getConstrainer()->setFixedAspectRatio (aspect);
    // pas de poignee d'agrandissement en mode stack (elle tomberait entre Ampire et Impulse) ;
    // la fenetre reste redimensionnable par ses bords et par Plugin size
    setResizable (true, ! stacked);
}

void AmpireEditor::placeStackView()
{
    if (stackView == 0) return;
    if (auto* api = proc.stackApi())
    {
        const int w = getWidth(), hA = ampireHeight (w);
        api->setViewBounds (stackView, 0, hA, w, juce::jmax (1, getHeight() - hA),
                            (double) juce::Component::getApproximateScaleFactorForComponent (this));
    }
}

void AmpireEditor::toggleStack()
{
    if (! proc.isReaper()) { content->showStatus ("IMPULSE STACK   REAPER only"); return; }
    if (stackInst == 0) updateStack();
    if (stackInst == 0) { content->showStatus ("IMPULSE STACK   load Impulse on this track"); return; }
    proc.stackOn = ! proc.stackOn.load();
    applyStack();
    content->showStatus (proc.stackOn ? "IMPULSE STACK   ON" : "IMPULSE STACK   OFF");
}

void AmpireEditor::updateStack()
{
    if (stackPreview) return;
    auto* api = proc.stackApi();
    void* track = proc.reaperTrack();
    const int inst = api != nullptr && track != nullptr ? api->findOnTrack (track) : 0;
    // vue perimee (Impulse retire, autre Impulse, module decharge) : on la lache
    if (stackView != 0 && (api == nullptr || api->viewAlive (stackView) == 0 || inst != stackInst))
    {
        if (api != nullptr && api->viewAlive (stackView) != 0) api->destroyView (stackView);
        stackView = 0;
    }
    stackInst = inst;
    applyStack();
}

void AmpireEditor::applyStack()
{
    auto& b = content->stackBtn;
    const bool avail = stackInst != 0;
    const bool want = avail && proc.stackOn.load() && getWindowHandle() != nullptr;
    if (b.available != avail || b.on != proc.stackOn.load() || b.led != content->ledColour)
    {
        b.available = avail; b.on = proc.stackOn.load(); b.led = content->ledColour;
        b.repaint();
        content->repaintDiodes();
    }
    const int w = getWidth();
    if (want && stackView == 0)
    {
        auto* api = proc.stackApi();
        if (api == nullptr) return;
        stackShown = true;
        setStackConstraints (true);
        setSize (w, ampireHeight (w) + impulseHeight (w));
        stackView = api->createView (stackInst, getWindowHandle(), 0, ampireHeight (w), w, impulseHeight (w),
                                     (double) juce::Component::getApproximateScaleFactorForComponent (this));
        if (stackView == 0) { stackShown = false; setStackConstraints (false); setSize (w, ampireHeight (w)); }
        else placeStackView();
    }
    else if (! want && stackShown)
    {
        if (stackView != 0)
            if (auto* api = proc.stackApi()) api->destroyView (stackView);
        stackView = 0;
        stackShown = false;
        setStackConstraints (false);
        setSize (w, ampireHeight (w));
    }
}

void AmpireEditor::previewStackButton (bool available, bool on)
{
    stackPreview = true;
    content->stackBtn.available = available; content->stackBtn.on = on; content->stackBtn.led = content->ledColour;
    content->stackBtn.repaint();
    content->repaintDiodes();
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
    setSize (w, ampireHeight (w) + (stackShown ? impulseHeight (w) : 0));
    hostLock.enforce();
}

// ============================================================================ menus clic droit
static juce::LookAndFeel* menuLookFor (juce::Component* c)
{
    if (c == nullptr) return nullptr;
    if (auto* ed = dynamic_cast<AmpireEditor*> (c)) return ed->getMenuLook();
    if (auto* ed = c->findParentComponentOfClass<AmpireEditor>()) return ed->getMenuLook();
    return nullptr;
}

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
        m.addItem (NineHandsWindow::idSize + i, juce::String (names[i]) + (i == def ? "  (default)" : ""), true, cur == i);
        defSizes.addItem (NineHandsWindow::idDefault + i, names[i], true, i == def);
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
    int inst = ed->getProc().uiScaleIdx.load();
    const int sz = NineHandsWindow::handleSize (r, JucePlugin_Name, AmpireEditor::numSizes, inst);
    if (sz < 0) return false;
    ed->setScaleIndex (inst);                  // -1 = suit la taille par defaut (ne fige pas l'instance)
    return true;
}

static void addMidiItems (juce::PopupMenu& midi, AmpireProcessor& p)
{
    auto assign = [&p] (int i) { const auto a = p.learnAssignText (i); return a == "none" ? juce::String ("not assigned") : a; };
    midi.addSectionHeader ("MIDI Learn");
    for (int i = 0; i < AmpireProcessor::numLearn; ++i)
        midi.addItem (1000 + i, juce::String ("Learn: ") + AmpireProcessor::learnDef (i).name + "  (" + assign (i) + ")", true, p.learnTarget.load() == i);
    midi.addSeparator();
    midi.addItem (1200, "Cancel MIDI learn", p.learnTarget.load() >= 0);
    midi.addItem (1201, "Clear all MIDI assignments");
    midi.addSeparator();
    midi.addItem (1203, "MIDI footswitch latching (127 = on / 0 = off)", true, ! p.toggleMomentary.load());
    midi.addItem (1202, "MIDI footswitch momentary (toggle each press)", true, p.toggleMomentary.load());
}

static bool handleLearnChoice (int r, AmpireProcessor* p)
{
    if (r < 1000 || r > 1299 || p == nullptr) return false;
    if (r < 1000 + AmpireProcessor::numLearn) p->learnTarget.store (p->learnTarget.load() == r - 1000 ? -1 : r - 1000);
    else if (r >= 1100 && r < 1100 + AmpireProcessor::numLearn) p->clearLearn (r - 1100);
    else if (r == 1200) p->learnTarget.store (-1);
    else if (r == 1201) p->resetMidiMap();
    else if (r == 1202) p->toggleMomentary = true;
    else if (r == 1203) p->toggleMomentary = false;
    return true;
}

// menu d'une commande (tronc commun) : en-tete, MIDI Learn, Clear MIDI, Cancel MIDI learn
static juce::PopupMenu buildControlMenu (AmpireProcessor& p, int learnIdx)
{
    juce::PopupMenu m;
    const juce::String name = AmpireProcessor::learnDef (learnIdx).name;
    const auto a = p.learnAssignText (learnIdx);
    MenuStyle::header (m, MenuStyle::icoParams, name, "MIDI: " + (a == "none" ? juce::String ("not assigned") : a));
    m.addItem (1000 + learnIdx, "MIDI Learn: " + name, true, p.learnTarget.load() == learnIdx);
    m.addItem (1100 + learnIdx, "Clear MIDI: " + name, a != "none");
    m.addItem (1200, "Cancel MIDI learn", p.learnTarget.load() >= 0);
    return m;
}

static juce::PopupMenu buildGearMenu (juce::Component& c, AmpireProcessor* p);

static void showContextMenu (juce::Component& c, int learnIdx)
{
    // un seul menu dans tout le plugin : clic droit n'importe ou = menu des reglages (MIDI Learn de chaque commande
    // dans son sous-menu MIDI)
    if (learnIdx < 0) { showGearMenu (c); return; }
    auto* p = procFor (&c);
    if (p == nullptr) return;
    auto m = buildControlMenu (*p, learnIdx);
    juce::Component::SafePointer<juce::Component> sp (&c);
    MenuStyle::styled (m, menuLookFor (&c)).showMenuAsync (juce::PopupMenu::Options(), [sp, p] (int r)
    {
        if (sp == nullptr) return;
        if (r == 2) { if (auto* ed = sp->findParentComponentOfClass<AmpireEditor>()) ed->openColourPicker (sp.getComponent()); return; }
        if (! handleLearnChoice (r, p)) handleSizeChoice (r, sp.getComponent());
    });
}

static juce::PopupMenu buildGearMenu (juce::Component& c, AmpireProcessor* p)
{
    auto& ap = p->apvts;
    auto val = [&ap] (const char* id) { return ap.getRawParameterValue (id)->load(); };
    const int lows[] = { 20, 40, 60, 80, 100, 120, 150 };
    const int highs[] = { 20000, 12000, 10000, 8000, 7000, 6000, 5000 };

    juce::PopupMenu m, engine, aaM, outM, inM, calM, lowM, highM, sizeM, midi;
    MenuStyle::header (m, MenuStyle::icoParams, "Ampire", "NAM loader  v" JucePlugin_VersionString);
    {
        // presets utilisateur (nommes) : Load user preset / Save user preset
        juce::PopupMenu loadM, saveM;
        const auto names = p->getPresetNames();
        const auto cur = p->getPresetName();
        loadM.addItem (800, "Init (empty)");
        loadM.addSeparator();
        if (names.isEmpty()) loadM.addItem (899, "No preset saved yet", false);
        for (int i = 0; i < juce::jmin (names.size(), 99); ++i) loadM.addItem (900 + i, names[i], true, names[i] == cur);
        saveM.addItem (801, "Save preset as...");
        saveM.addItem (802, "Save \"" + cur + "\"" + (p->isPresetDirty() ? "  *" : ""), cur.isNotEmpty());
        saveM.addItem (803, "Delete \"" + cur + "\"", cur.isNotEmpty());
        saveM.addSeparator();
        saveM.addItem (804, "Open presets folder");
        m.addSubMenu ("Load user preset", loadM);
        m.addSubMenu ("Save user preset", saveM);
        m.addSeparator();
    }
    // Engine : anti-repliement, niveaux, entree, filtres, tone stack
    const int os = (int) std::lround (val ("oversample"));
    aaM.addItem (30, "Off (lowest CPU)", true, os == 0);
    aaM.addItem (31, "2x (recommended)", true, os == 1);
    aaM.addItem (32, "4x (best, high gain)", true, os == 2);
    engine.addSubMenu ("Anti-aliasing (oversampling, 0 latency)", aaM);
    const int om = (int) std::lround (val ("outmode"));
    outM.addItem (33, "Raw (capture as recorded)", true, om == 0);
    outM.addItem (34, "Normalized (all captures at the same loudness)", true, om == 1);
    outM.addItem (35, "Calibrated (dBu levels of the capture)", true, om == 2);
    engine.addSubMenu ("Output level", outM);
    calM.addItem (36, "Calibrate input to each capture", true, val ("calin") > 0.5f);
    calM.addSeparator();
    const float cl = val ("callevel");
    const float levels[] = { 0.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f, 20.0f, 24.0f };
    for (int i = 0; i < 8; ++i) calM.addItem (40 + i, "Interface: 0 dBFS = +" + juce::String ((int) levels[i]) + " dBu", true, std::abs (cl - levels[i]) < 0.05f);
    engine.addSubMenu ("Input calibration", calM);
    const int is = (int) std::lround (val ("inputsel"));
    inM.addItem (50, "Left", true, is == 0); inM.addItem (51, "Right", true, is == 1); inM.addItem (52, "L + R (sum)", true, is == 2);
    engine.addSubMenu ("Input channel", inM);
    engine.addSeparator();
    for (int i = 0; i < 7; ++i) lowM.addItem (10 + i, i == 0 ? juce::String ("Off") : juce::String (lows[i]) + " Hz", true, std::abs (val ("lowcut") - (float) lows[i]) < 1.0f);
    for (int i = 0; i < 7; ++i) highM.addItem (20 + i, i == 0 ? juce::String ("Off") : juce::String (highs[i] / 1000.0, 1).replace (".0", "") + " kHz", true, std::abs (val ("highcut") - (float) highs[i]) < 10.0f);
    engine.addSubMenu ("High cut", highM);
    engine.addSubMenu ("Low cut", lowM);
    engine.addSeparator();
    engine.addItem (38, "Tone stack (Bass / Middle / Treble)", true, val ("tonestack") > 0.5f);
    m.addSubMenu ("Engine", engine);
    m.addSeparator();
    addMidiItems (midi, *p);
    m.addSubMenu ("MIDI", midi);
    m.addSeparator();
    addSizeItems (sizeM, c);
    m.addSubMenu ("Plugin size", sizeM);
    {
        auto* edp = c.findParentComponentOfClass<AmpireEditor>();
        if (edp == nullptr) edp = dynamic_cast<AmpireEditor*> (&c);
        const float lg = edp != nullptr ? edp->getLedGlow() : 1.0f;
        const char* glowNames[4] = { "Off", "Low", "Medium (design)", "High" };
        const float glowVals[4] = { 0.0f, 0.5f, 1.0f, 1.6f };
        juce::PopupMenu colM, lgM;
        colM.addItem (76, "LED colour (POWER / STACK)...");
        for (int i = 0; i < 4; ++i) lgM.addItem (77 + i, glowNames[i], true, std::abs (lg - glowVals[i]) < 0.01f);
        colM.addSubMenu ("LED glow", lgM);
        colM.addSeparator();
        colM.addItem (70, "Meter LED colour...");
        colM.addItem (71, "Saturation LED colour (top 3)...");
        m.addSubMenu ("Glow intensity & Colours", colM);
    }
    return m;
}

static void showGearMenu (juce::Component& c)
{
    auto* p = procFor (&c);
    if (p == nullptr) return;
    const int lows[] = { 20, 40, 60, 80, 100, 120, 150 };
    const int highs[] = { 20000, 12000, 10000, 8000, 7000, 6000, 5000 };
    const float levels[] = { 0.0f, 6.0f, 9.0f, 12.0f, 15.0f, 18.0f, 20.0f, 24.0f };
    auto m = buildGearMenu (c, p);
    juce::Component::SafePointer<juce::Component> sp (&c);
    MenuStyle::styled (m, menuLookFor (&c)).showMenuAsync (juce::PopupMenu::Options(), [sp, p, lows, highs, levels] (int r)
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

juce::PopupMenu AmpireEditor::buildContextMenu (int learnIdx)
{
    return learnIdx >= 0 ? buildControlMenu (proc, learnIdx) : buildGearMenu (*this, &proc);
}
