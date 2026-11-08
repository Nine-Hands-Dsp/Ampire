#include "PluginProcessor.h"
#include "NhdStack.h"
#include "PluginEditor.h"
#include "ColourPicker.h"

namespace
{
    constexpr double kFadeSeconds    = 0.030;   // fondu enchaine au changement de capture / taille / surechantillonnage
    constexpr juce::int64 kMaxEmbedBytes = 6 * 1024 * 1024;   // .nam / IR copies dans le projet si fichier <= 6 Mo
    constexpr float  kTargetLoudness = -18.0f;  // loi NAM / TONE3000
    constexpr int    kEqSub = 32;               // recalcul de l'egaliseur tous les 32 echantillons (pedale d'expression)

    float dbToGain (float db) { return db <= -99.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }


}

// ============================================================================ chargeur (thread dedie)
class AmpireProcessor::Loader : public juce::Thread
{
public:
    explicit Loader (AmpireProcessor& p) : juce::Thread ("Ampire loader"), proc (p) { startThread (juce::Thread::Priority::normal); }
    ~Loader() override { signalThreadShouldExit(); wake.signal(); stopThread (8000); }

    struct Job { int slot = 0; bool load = false; juce::File file; juce::uint32 gen = 0; };
    void add (Job j)
    {
        {
            const juce::ScopedLock sl (lock);
            // une reconstruction deja en file pour ce slot suffit
            if (! j.load)
                for (auto& q : jobs) if (! q.load && q.slot == j.slot) { q.gen = j.gen; return; }
            jobs.push_back (j);
            ++pending;
        }
        wake.signal();
    }
    bool idle() const { const juce::ScopedLock sl (lock); return pending == 0 && ! busy; }

    void run() override
    {
        while (! threadShouldExit())
        {
            Job j;
            bool have = false;
            {
                const juce::ScopedLock sl (lock);
                if (! jobs.empty()) { j = jobs.front(); jobs.erase (jobs.begin()); have = true; busy = true; --pending; }
            }
            if (! have) { wake.wait (200); continue; }
            if (j.load) proc.doLoad (j.slot, j.file, j.gen);
            else        proc.doBuild (j.slot, j.gen);
            { const juce::ScopedLock sl (lock); busy = false; }
        }
    }

private:
    AmpireProcessor& proc;
    juce::CriticalSection lock;
    std::vector<Job> jobs;
    int pending = 0;
    bool busy = false;
    juce::WaitableEvent wake;
};

// ============================================================================ MIDI Learn
const AmpireProcessor::LearnDef& AmpireProcessor::learnDef (int i)
{
    // aucun CC par defaut : tout passe par le MIDI Learn (clic droit sur la commande)
    static const LearnDef defs[numLearn] = {
        { "input",  "Input",            learnContinuous },
        { "bass",   "Bass",             learnContinuous },
        { "middle", "Middle",           learnContinuous },
        { "treble", "Treble",           learnContinuous },
        { "output", "Output",           learnContinuous },
        { "size",   "A2 size",          learnContinuous },
        { "active", "Power",            learnToggle },
        { "prev",   "Previous capture", learnTrigger },
        { "next",   "Next capture",     learnTrigger },
        { "prevP",  "Preset previous",  learnTrigger },
        { "nextP",  "Preset next",      learnTrigger },
    };
    return defs[juce::jlimit (0, numLearn - 1, i)];
}

void AmpireProcessor::resetMidiMap()
{
    learnTarget = -1;
    for (int i = 0; i < numLearn; ++i)
    {
        ccMap[i].store (-1); noteMap[i].store (-1);
        ccMin[i] = 127; ccMax[i] = 0; ccHeld[i] = false;
    }
}

void AmpireProcessor::clearLearn (int i)
{
    if (i < 0 || i >= numLearn) return;
    ccMap[i].store (-1); noteMap[i].store (-1);
    ccMin[i] = 127; ccMax[i] = 0;
    if (learnTarget.load() == i) learnTarget = -1;
}

juce::String AmpireProcessor::learnAssignText (int i) const
{
    i = juce::jlimit (0, numLearn - 1, i);
    if (ccMap[i].load() >= 0)   return "CC " + juce::String (ccMap[i].load());
    if (noteMap[i].load() >= 0) return "Note " + juce::String (noteMap[i].load());
    return "none";
}

// ============================================================================ parametres
juce::AudioProcessorValueTreeState::ParameterLayout AmpireProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1); });
    auto tenAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); });

    // memes commandes et memes plages que le plugin NAM officiel
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "active", 1 }, "Power", true));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "input", 1 }, "Input", NormalisableRange<float> (-20.0f, 20.0f), 0.0f, dbAttr));
    for (auto* id : { "bass", "middle", "treble" })
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, String (id).substring (0, 1).toUpperCase() + String (id).substring (1),
                                                            NormalisableRange<float> (0.0f, 10.0f), 5.0f, tenAttr));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-40.0f, 40.0f), 0.0f, dbAttr));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "size", 1 }, "A2 size", NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) std::lround (v * 100.0f)) + " %"; })));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "tonestack", 1 }, "Tone stack on", true));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "outmode", 1 }, "Output mode", StringArray { "Raw", "Normalized", "Calibrated" }, 1));
    p.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "calin", 1 }, "Calibrate input", false));
    p.push_back (std::make_unique<AudioParameterFloat>  (ParameterID { "callevel", 1 }, "Interface level", NormalisableRange<float> (-10.0f, 30.0f, 0.1f), 12.0f,
        AudioParameterFloatAttributes().withLabel ("dBu")));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "oversample", 1 }, "Anti-aliasing", StringArray { "Off", "2x", "4x" }, 1));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "inputsel", 1 }, "Input channel", StringArray { "Left", "Right", "L + R" }, 0));

    NormalisableRange<float> lowR (20.0f, 400.0f); lowR.setSkewForCentre (80.0f);
    NormalisableRange<float> highR (2000.0f, 20000.0f); highR.setSkewForCentre (7000.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "lowcut", 1 }, "Low cut", lowR, 20.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v <= 20.5f ? String ("Off") : String ((int) std::lround (v)); })));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "highcut", 1 }, "High cut", highR, 20000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v >= 19900.0f ? String ("Off") : String ((int) std::lround (v)); })));
    return { p.begin(), p.end() };
}

AmpireProcessor::AmpireProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& t : trash) t.store (nullptr);
    for (int s = 0; s < numSlots; ++s) { slotLatency[s].store (0); slotDelay[s].store (0); }

    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pActive = P ("active"); pInput = P ("input"); pBass = P ("bass"); pMiddle = P ("middle"); pTreble = P ("treble");
    pOutput = P ("output"); pSize = P ("size"); pTone = P ("tonestack");
    pNorm = P ("outmode"); pCalIn = P ("calin"); pCalLevel = P ("callevel"); pOversample = P ("oversample");
    pLowCut = P ("lowcut"); pHighCut = P ("highcut"); pInputSel = P ("inputsel");
    for (int i = 0; i < numParamLearn; ++i)
    {
        queuedPtr[i] = apvts.getRawParameterValue (learnDef (i).id);
        pendingValue[i].store (0.0f); pendingSet[i].store (false);
    }
    resetMidiMap();
    // dossier de depart : dossier de captures par defaut, sinon le dernier dossier ouvert (memorises), sinon Documents
    lastFolder = library.getLastFolder();
    if (! lastFolder.isDirectory()) lastFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    accentColour = AmpireSettings::accent (JucePlugin_Name);
    lastOs = osFactorParam();
    lastSizeIdx = sizeIndex();
    presetSnapshot = snapshotString();
    loader = std::make_unique<Loader> (*this);
    library.rescan();
    startTimerHz (60);   // anti-gel v2 : aucun appel a l'hote depuis le thread audio
}

AmpireProcessor::~AmpireProcessor()
{
    stopTimer();
    loader.reset();
    for (auto& L : live)
    {
        delete L.incoming.exchange (nullptr);
        delete L.cur; L.cur = nullptr;
        delete L.old; L.old = nullptr;
    }
    emptyTrash();
}

bool AmpireProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool okIn  = in  == juce::AudioChannelSet::mono() || in  == juce::AudioChannelSet::stereo();
    const bool okOut = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return okIn && okOut;
}

int AmpireProcessor::osFactorParam() const
{
    const int i = (int) std::lround (pOversample->load());
    return i == 2 ? 4 : i == 1 ? 2 : 1;
}

double AmpireProcessor::slotSize (int) const
{
    return juce::jlimit (0.0f, 1.0f, pSize->load());       // NAM : 0 = plus petit sous-modele, 1 = complet (Full)
}

int AmpireProcessor::sizeIndex() const
{
    // meme regle que NAM Core (ContainerModel::_get_index_for_slimmable_size)
    std::vector<double> bp;
    { const juce::ScopedLock sl (engineLock); bp = slots[0].info.meta.breakpoints; }
    const double v = slotSize (0);
    for (size_t i = 0; i < bp.size(); ++i) if (v < bp[i]) return (int) i;
    return (int) bp.size();
}

// tone stack du plugin NAM officiel (ToneStack.cpp) : grave 150 Hz +/-20 dB, medium 425 Hz +/-15 dB
// (Q 1.5 en coupe, 0.7 en accentuation), aigu 1800 Hz +/-10 dB ; lineaire autour de 5
void AmpireProcessor::toneStack (int band, float knob, double sr, float& b0, float& b1, float& b2, float& a1, float& a2)
{
    Biquad t;
    const double x = (double) knob - 5.0;
    if (band == 0) setShelf (t, 150.0, 4.0 * x, false, sr);
    else if (band == 1) { const double db = 3.0 * x; setPeak (t, 425.0, db < 0.0 ? 1.5 : 0.7, db, sr); }
    else setShelf (t, 1800.0, 2.0 * x, true, sr);
    b0 = t.b0; b1 = t.b1; b2 = t.b2; a1 = t.a1; a2 = t.a2;
}

// ============================================================================ preparation
void AmpireProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const double newRate = sampleRate > 0 ? sampleRate : 48000.0;
    const int newBlock = juce::jmax (32, samplesPerBlock);
    const bool changed = std::abs (newRate - hostRate) > 0.5 || newBlock > maxBlock || live[0].cur == nullptr;
    hostRate = newRate;
    maxBlock = juce::jmax (newBlock, changed ? newBlock : maxBlock);

    // voix reconstruites tout de suite (l'audio est arrete) : jamais de capture a la mauvaise frequence
    if (changed)
    {
        for (int s = 0; s < numSlots; ++s)
        {
            std::shared_ptr<CaptureData> data;
            juce::uint32 gen = 0;
            {
                const juce::ScopedLock sl (engineLock);
                data = slots[s].data;
                gen = ++slots[s].gen;
            }
            std::unique_ptr<CaptureVoice> v;
            if (data != nullptr)
            {
                juce::String err;
                v = CaptureBuilder::build (*data, hostRate, maxBlock, osFactorParam(), slotSize (s), err);
            }
            const juce::ScopedLock cl (getCallbackLock());
            auto& L = live[s];
            delete L.incoming.exchange (nullptr);
            delete L.old; L.old = nullptr;
            delete L.cur;
            L.cur = v.release();
            L.fade = 1.0f; L.fadingOut = false;
            L.outGain = L.cur != nullptr ? 1.0f : 0.0f;
            slotLatency[s].store (L.cur != nullptr ? L.cur->hostLatency() : 0);
            slotDelay[s].store (L.cur != nullptr ? L.cur->modelDelayHost() : 0);
            juce::ignoreUnused (gen);
        }
        lastOs = osFactorParam();
        lastSizeIdx = sizeIndex();
    }

    monoIn.setSize (1, maxBlock, false, true, true);
    capBuf.setSize (2, maxBlock, false, true, true);       // canal 1 : voix sortante (fondu)
    stereoBuf.setSize (2, maxBlock, false, true, true);
    bypassDry.setSize (2, maxBlock, false, true, true);
    const int ringLen = (int) std::ceil (0.03 * hostRate) + 2048;
    for (auto& r : bypassRing) r.setSize (ringLen);

    const float k = 1.0f - std::exp (-1.0f / (0.010f * (float) hostRate));   // 2 poles de 10 ms
    for (auto* s : { &smIn, &smOut, &smCap, &smActive, &smTone, &smEq[0], &smEq[1], &smEq[2] }) s->k = k;
    smIn.set (dbToGain (pInput->load())); smOut.set (dbToGain (pOutput->load())); smCap.set (1.0f);
    smActive.set (pActive->load() > 0.5f ? 1.0f : 0.0f);
    smTone.set (pTone->load() > 0.5f ? 1.0f : 0.0f);
    smEq[0].set (pBass->load()); smEq[1].set (pMiddle->load()); smEq[2].set (pTreble->load());
    fadeStep = (float) (1.0 / (kFadeSeconds * hostRate));
    for (auto* b : { &eqLow, &eqMid, &eqHigh, &cutHp, &cutLp }) b->reset();
    for (auto& e : eqState) e = -1.0f;
    lastLow = lastHigh = -1.0f;
    for (int c = 0; c < 2; ++c) dcX[c] = dcY[c] = 0.0f;
    updateLatency();
}

// ============================================================================ filtres (RBJ)
void AmpireProcessor::setPeak (Biquad& f, double fc, double q, double db, double sr)
{
    const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * juce::jmin (fc, sr * 0.45) / sr;
    const double al = std::sin (w) / (2.0 * q), cw = std::cos (w), a0 = 1.0 + al / A;
    f.b0 = (float) ((1.0 + al * A) / a0); f.b1 = (float) (-2.0 * cw / a0); f.b2 = (float) ((1.0 - al * A) / a0);
    f.a1 = (float) (-2.0 * cw / a0);      f.a2 = (float) ((1.0 - al / A) / a0);
}

void AmpireProcessor::setShelf (Biquad& f, double fc, double db, bool high, double sr)
{
    const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * juce::jmin (fc, sr * 0.45) / sr;
    const double cw = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), s = 2.0 * std::sqrt (A) * al;
    double b0, b1, b2, a0, a1, a2;
    if (high)
    {
        b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s;       a1 = 2 * ((A - 1) - (A + 1) * cw);      a2 = (A + 1) - (A - 1) * cw - s;
    }
    else
    {
        b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw);  b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s;       a1 = -2 * ((A - 1) + (A + 1) * cw);     a2 = (A + 1) + (A - 1) * cw - s;
    }
    f.b0 = (float) (b0 / a0); f.b1 = (float) (b1 / a0); f.b2 = (float) (b2 / a0); f.a1 = (float) (a1 / a0); f.a2 = (float) (a2 / a0);
}

void AmpireProcessor::setHighPass (Biquad& f, double fc, double q, double sr)
{
    const double w = juce::MathConstants<double>::twoPi * fc / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
    f.b0 = (float) ((1.0 + cw) * 0.5 / a0); f.b1 = (float) (-(1.0 + cw) / a0); f.b2 = f.b0;
    f.a1 = (float) (-2.0 * cw / a0);        f.a2 = (float) ((1.0 - al) / a0);
}

void AmpireProcessor::setLowPass (Biquad& f, double fc, double q, double sr)
{
    const double w = juce::MathConstants<double>::twoPi * juce::jmin (fc, sr * 0.45) / sr, cw = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
    f.b0 = (float) ((1.0 - cw) * 0.5 / a0); f.b1 = (float) ((1.0 - cw) / a0); f.b2 = f.b0;
    f.a1 = (float) (-2.0 * cw / a0);        f.a2 = (float) ((1.0 - al) / a0);
}

// ============================================================================ audio
void AmpireProcessor::retire (CaptureVoice* v)
{
    if (v == nullptr) return;
    const int w = trashW.load (std::memory_order_relaxed);
    const int next = (w + 1) % trashSize;
    if (next == trashR.load (std::memory_order_acquire)) return;   // corbeille pleine (jamais en pratique) : fuite plutot qu'une liberation ici
    trash[w].store (v, std::memory_order_relaxed);
    trashW.store (next, std::memory_order_release);
}

void AmpireProcessor::emptyTrash()
{
    int r = trashR.load (std::memory_order_relaxed);
    while (r != trashW.load (std::memory_order_acquire))
    {
        delete trash[r].exchange (nullptr);
        r = (r + 1) % trashSize;
        trashR.store (r, std::memory_order_release);
    }
}

void AmpireProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    handleMidi (midi);

    const int totalIn = getTotalNumInputChannels(), totalOut = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();
    if (total <= 0) return;
    for (int c = totalIn; c < totalOut; ++c) buffer.clear (c, 0, total);
    const int nOut = juce::jmin (2, totalOut, buffer.getNumChannels());
    const int nIn = juce::jmin (2, totalIn, buffer.getNumChannels());

    // ---- echange des voix (aucune allocation, aucun verrou)
    for (auto& L : live)
    {
        if (auto* v = L.incoming.exchange (nullptr, std::memory_order_acq_rel))
        {
            retire (L.old);
            L.old = L.cur;                         // l'ancienne voix sort en fondu
            L.cur = v;
            L.fade = 0.0f;
            L.fadingOut = false;
        }
        if (L.clearReq.exchange (false, std::memory_order_acq_rel))
        {
            retire (L.old);
            L.old = L.cur; L.cur = nullptr;
            L.fade = 0.0f; L.fadingOut = true;
        }
    }

    // ---- cibles (lues une fois par bloc)
    const int  inSel = (int) std::lround (pInputSel->load (std::memory_order_relaxed));
    auto& L = live[0];
    const bool haveCap = L.cur != nullptr || L.old != nullptr;

    // niveau de sortie de la capture (brut / normalise / calibre) et calibration d'entree, comme NAM
    const int outMode = (int) std::lround (pNorm->load (std::memory_order_relaxed));
    const bool calIn = pCalIn->load (std::memory_order_relaxed) > 0.5f;
    const float calLevel = pCalLevel->load (std::memory_order_relaxed);
    float normDb = 0.0f, inTrimDb = 0.0f;
    {
        const auto& m = slotMetaRt[0];
        const bool hl = m.hasLoudness.load (std::memory_order_relaxed), ho = m.hasOutputLevel.load (std::memory_order_relaxed);
        const float loud = m.loudness.load (std::memory_order_relaxed);
        if (outMode == 1 && hl) normDb = kTargetLoudness - loud;
        else if (outMode == 2) normDb = ho ? m.outputLevel.load (std::memory_order_relaxed) - calLevel : (hl ? kTargetLoudness - loud : 0.0f);
        if (calIn && m.hasInputLevel.load (std::memory_order_relaxed)) inTrimDb = calLevel - m.inputLevel.load (std::memory_order_relaxed);
    }
    const float tCap = dbToGain (normDb), trim = dbToGain (inTrimDb);
    const float tIn  = dbToGain (pInput->load (std::memory_order_relaxed));
    const float tOut = dbToGain (pOutput->load (std::memory_order_relaxed));
    const float tAct = pActive->load (std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f;
    const float tTone = pTone->load (std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f;
    const float tEq[3] = { pBass->load (std::memory_order_relaxed), pMiddle->load (std::memory_order_relaxed), pTreble->load (std::memory_order_relaxed) };

    // latence declaree : celle du reechantillonneur (0 quand l'hote tourne a la frequence de la capture)
    int reported = L.cur != nullptr ? L.cur->hostLatency() : (L.old != nullptr ? L.old->hostLatency() : 0);
    wantedLatency.store (reported, std::memory_order_relaxed);
    reported = juce::jlimit (0, (int) bypassRing[0].d.size() - 1, reported);

    // filtres de sortie
    const float lowT = pLowCut->load (std::memory_order_relaxed), highT = pHighCut->load (std::memory_order_relaxed);
    const float lowF  = lastLow  < 0 ? lowT  : lastLow  + 0.35f * (lowT - lastLow);
    const float highF = lastHigh < 0 ? highT : lastHigh + 0.35f * (highT - lastHigh);
    if (std::abs (lowF - lastLow) > 0.01f)  { if (lowF > 20.5f) setHighPass (cutHp, lowF, 0.7071, hostRate); lastLow = lowF; }
    if (std::abs (highF - lastHigh) > 0.5f) { if (highF < 19900.0f) setLowPass (cutLp, highF, 0.7071, hostRate); lastHigh = highF; }
    const bool useHp = lastLow > 20.5f, useLp = lastHigh < 19900.0f && lastHigh < hostRate * 0.45;
    const float dcR = 1.0f - juce::MathConstants<float>::twoPi * 6.0f / (float) hostRate;

    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        const float* inL = buffer.getReadPointer (0, start);
        const float* inR = nIn > 1 ? buffer.getReadPointer (1, start) : inL;
        float* x = monoIn.getWritePointer (0);
        // ---- entree mono + INPUT (+ calibration)
        for (int i = 0; i < n; ++i)
        {
            const float l = inL[i], r = inR[i];
            const float raw = inSel == 0 ? l : inSel == 1 ? r : 0.5f * (l + r);
            bypassDry.setSample (0, i, bypassRing[0].process (l, reported));
            bypassDry.setSample (1, i, bypassRing[1].process (nIn > 1 ? r : l, reported));
            const float v = raw * smIn.next (tIn);
            peakIn = juce::jmax (peakIn, std::abs (v));
            x[i] = v * trim;
        }

        // ---- capture (voix courante + voix sortante en fondu enchaine)
        float* y = stereoBuf.getWritePointer (0);
        if (haveCap)
        {
            float* tmp = capBuf.getWritePointer (1);
            if (L.cur != nullptr) L.cur->process (x, y, n); else std::fill (y, y + n, 0.0f);
            if (L.old != nullptr)
            {
                L.old->process (x, tmp, n);
                float f = L.fade;
                for (int i = 0; i < n; ++i) { f = juce::jmin (1.0f, f + fadeStep); y[i] = f * y[i] + (1.0f - f) * tmp[i]; }
                L.fade = f;
                if (f >= 1.0f) { retire (L.old); L.old = nullptr; L.fadingOut = false; }
            }
            else if (L.fade < 1.0f && L.cur != nullptr)
            {
                float f = L.fade;
                for (int i = 0; i < n; ++i) { f = juce::jmin (1.0f, f + fadeStep); y[i] *= f; }
                L.fade = f;
            }
            for (int i = 0; i < n; ++i) y[i] *= smCap.next (tCap);
        }
        else std::memcpy (y, x, sizeof (float) * (size_t) n);   // aucune capture : signal direct

        // ---- tone stack NAM (coefficients mis a jour tous les 32 echantillons, potards lisses)
        for (int i0 = 0; i0 < n; i0 += kEqSub)
        {
            const int i1 = juce::jmin (n, i0 + kEqSub);
            for (int k = 0; k < 3; ++k)
            {
                for (int i = i0; i < i1; ++i) smEq[k].next (tEq[k]);
                const float e = smEq[k].b;
                if (std::abs (e - eqState[k]) > 0.002f)
                {
                    eqState[k] = e;
                    auto& q = k == 0 ? eqLow : k == 1 ? eqMid : eqHigh;
                    toneStack (k, e, hostRate, q.b0, q.b1, q.b2, q.a1, q.a2);
                }
            }
            for (int i = i0; i < i1; ++i)
            {
                const float dry = y[i];
                const float wet = eqHigh.process (eqMid.process (eqLow.process (dry, 0), 0), 0);
                const float t = smTone.next (tTone);
                y[i] = dry + t * (wet - dry);
            }
        }

        // ---- sortie : coupe-bas / coupe-haut, anti-DC, OUTPUT, power (mono -> stereo)
        float* oL = buffer.getWritePointer (0, start);
        float* oR = nOut > 1 ? buffer.getWritePointer (1, start) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float g = smOut.next (tOut), act = smActive.next (tAct);
            float s = y[i];
            if (useHp) s = cutHp.process (s, 0);
            if (useLp) s = cutLp.process (s, 0);
            const float d = s - dcX[0] + dcR * dcY[0];
            dcX[0] = s; dcY[0] = d;
            const float v = d * g;
            const float l = bypassDry.getSample (0, i) + act * (v - bypassDry.getSample (0, i));
            const float r = bypassDry.getSample (1, i) + act * (v - bypassDry.getSample (1, i));
            if (oR != nullptr) { oL[i] = l; oR[i] = r; }
            else oL[i] = 0.5f * (l + r);
            peakOut = juce::jmax (peakOut, std::abs (l), std::abs (r));
        }
    }

    // ---- mesures pour l'interface
    if (peakIn > meterIn.load (std::memory_order_relaxed)) meterIn.store (peakIn, std::memory_order_relaxed);
    if (peakOut > meterOut.load (std::memory_order_relaxed)) meterOut.store (peakOut, std::memory_order_relaxed);
    if (peakIn >= 0.999f) clipIn.store (true, std::memory_order_relaxed);
    if (peakOut >= 0.999f) clipOut.store (true, std::memory_order_relaxed);
    peakIn = peakOut = 0.0f;
    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const double load = secs / ((double) total / hostRate);
    cpuSmooth += 0.05 * (load - cpuSmooth);
    cpuLoad.store ((float) cpuSmooth, std::memory_order_relaxed);
}

// ============================================================================ MIDI (thread audio : aucun appel a l'hote)
void AmpireProcessor::handleMidi (const juce::MidiBuffer& midiBuf)
{
    auto queueValue = [this] (int j, float norm)
    {
        pendingValue[j].store (juce::jlimit (0.0f, 1.0f, norm), std::memory_order_relaxed);
        pendingSet[j].store (true, std::memory_order_release);
    };
    auto effective = [this] (int j)
    {
        return pendingSet[j].load (std::memory_order_acquire) ? pendingValue[j].load (std::memory_order_relaxed)
                                                               : queuedPtr[j]->load (std::memory_order_relaxed);
    };
    auto fire = [this, &queueValue, &effective] (int j, bool on, bool edgeOnly)
    {
        const int type = learnDef (j).type;
        if (type == learnTrigger)
        {
            if (on && ! ccHeld[j]) pendingTrigger.fetch_or (1 << (j - lPrev));
        }
        else if (type == learnToggle)
        {
            if (edgeOnly || toggleMomentary.load()) { if (on && ! ccHeld[j]) queueValue (j, effective (j) > 0.5f ? 0.0f : 1.0f); }
            else queueValue (j, on ? 1.0f : 0.0f);
        }
        ccHeld[j] = on;
    };

    for (const auto meta : midiBuf)
    {
        const auto msg = meta.getMessage();
        const int lt = learnTarget.load();
        if (lt >= 0 && lt < numLearn)
        {
            if (msg.isController() && msg.getControllerNumber() < 120)
            {
                const int cc = msg.getControllerNumber();
                for (int j = 0; j < numLearn; ++j) if (j != lt && ccMap[j].load() == cc) ccMap[j].store (-1);
                ccMap[lt].store (cc); noteMap[lt].store (-1);
                ccMin[lt] = 127; ccMax[lt] = 0; ccHeld[lt] = msg.getControllerValue() >= 64;
                lastLearned.store (lt); learnTarget.store (-1);
                continue;
            }
            if (msg.isNoteOn() && learnDef (lt).type != learnContinuous)
            {
                const int note = msg.getNoteNumber();
                for (int j = 0; j < numLearn; ++j) if (j != lt && noteMap[j].load() == note) noteMap[j].store (-1);
                noteMap[lt].store (note); ccMap[lt].store (-1);
                lastLearned.store (lt); learnTarget.store (-1);
                continue;
            }
        }

        if (msg.isController() && msg.getControllerNumber() < 120)
        {
            const int c = msg.getControllerNumber(), v = msg.getControllerValue();
            if (c < 32) msb[c] = v;
            if (c >= 32 && c < 64)
            {
                bool used = false;
                for (int j = 0; j < numParamLearn; ++j)
                    if (learnDef (j).type == learnContinuous && ccMap[j].load (std::memory_order_relaxed) == c - 32)
                    {
                        queueValue (j, (float) (msb[c - 32] * 128 + v) / 16383.0f);
                        used = true;
                    }
                if (used) continue;
            }
            for (int j = 0; j < numLearn; ++j)
            {
                if (ccMap[j].load (std::memory_order_relaxed) != c) continue;
                if (learnDef (j).type == learnContinuous)
                {
                    ccMin[j] = juce::jmin (ccMin[j], v); ccMax[j] = juce::jmax (ccMax[j], v);
                    const int span = ccMax[j] - ccMin[j];
                    float xx = span >= 24 ? (float) (v - ccMin[j]) / (float) span : (float) v / 127.0f;
                    xx = juce::jlimit (0.0f, 1.0f, (xx - 0.015f) / 0.97f);
                    queueValue (j, xx);
                }
                else fire (j, v >= 64, false);
            }
        }
        else if (msg.isNoteOn() || msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            const bool on = msg.isNoteOn();
            for (int j = 0; j < numLearn; ++j)
                if (noteMap[j].load (std::memory_order_relaxed) == note) fire (j, on, true);
        }
    }
}

// ============================================================================ timer (thread de messages)
void AmpireProcessor::timerCallback()
{
    for (int i = 0; i < numParamLearn; ++i)
        if (pendingSet[i].exchange (false, std::memory_order_acq_rel))
            if (auto* p = apvts.getParameter (learnDef (i).id))
                p->setValueNotifyingHost (pendingValue[i].load (std::memory_order_relaxed));

    const int trig = pendingTrigger.exchange (0);
    if (trig & 1) stepCapture (0, -1);
    if (trig & 2) stepCapture (0, +1);
    if (trig & 4) stepPreset (-1);
    if (trig & 8) stepPreset (+1);

    emptyTrash();

    // taille A2 (sous-modele) / surechantillonnage changes : nouvelle voix en fondu enchaine
    const int os = osFactorParam();
    const int si = sizeIndex();
    if (os != lastOs || si != lastSizeIdx) { lastSizeIdx = si; requestBuild (0); }
    lastOs = os;
    updateLatency();
}

void AmpireProcessor::updateLatency()
{
    const int l = wantedLatency.load();
    if (l != getLatencySamples()) setLatencySamples (l);
}

// ============================================================================ captures
void AmpireProcessor::loadCapture (int s, const juce::File& file)
{
    if (s < 0 || s >= numSlots) return;
    juce::uint32 gen;
    {
        const juce::ScopedLock sl (engineLock);
        ++slots[s].gen;
        gen = ++slots[s].loadGen;
        slots[s].info.loading = true;
        slots[s].info.error.clear();
    }
    infoVersion++;
    lastFolder = file.getParentDirectory();
    library.setLastFolder (lastFolder);
    // premiere capture chargee sans dossier par defaut : son dossier devient le dossier de captures par defaut
    if (! library.getRoot().isDirectory() && lastFolder.isDirectory()) library.setRoot (lastFolder);
    loader->add ({ s, true, file, gen });
}

bool AmpireProcessor::loadCaptureNow (int s, const juce::File& file)
{
    if (s < 0 || s >= numSlots) return false;
    juce::uint32 gen;
    { const juce::ScopedLock sl (engineLock); ++slots[s].gen; gen = ++slots[s].loadGen; }
    doLoad (s, file, gen);
    return getSlotInfo (s).loaded;
}

void AmpireProcessor::doLoad (int s, const juce::File& file, juce::uint32 gen)
{
    juce::String err;
    auto data = CaptureBuilder::load (file, err);
    if (data == nullptr)
    {
        {
            const juce::ScopedLock sl (engineLock);
            if (gen != slots[s].loadGen) return;
            slots[s].info.loading = false;
            slots[s].info.error = err;
        }
        infoVersion++;
        return;
    }
    const auto packed = file.getSize() <= kMaxEmbedBytes ? packFile (file) : juce::String();
    {
        const juce::ScopedLock sl (engineLock);
        if (gen != slots[s].loadGen) return;           // une autre capture a ete demandee entre-temps
        // (une reconstruction demandee pendant le chargement - prepareToPlay, anti-repliement - n'annule plus le chargement :
        //  avant, l'ouverture d'un projet pouvait laisser la capture bloquee sur LOADING...)
        slots[s].data = data;
        slots[s].embeddedText = packed;
        slots[s].info.meta = data->meta;
        slots[s].info.loaded = true;
        slots[s].info.loading = false;
        slots[s].info.error.clear();
        slots[s].info.fromEmbedded = file.getParentDirectory() == CaptureLibrary::settingsDir().getChildFile ("Embedded");
        slots[s].info.osFactor = data->meta.phaseSafe ? osFactorParam() : 1;
        slotMetaRt[s].set (data->meta);
    }
    library.addRecent (file.getFullPathName());
    infoVersion++;
    juce::uint32 bgen;
    { const juce::ScopedLock sl (engineLock); bgen = ++slots[s].gen; }
    doBuild (s, bgen);
}

void AmpireProcessor::requestBuild (int s)
{
    juce::uint32 gen;
    {
        const juce::ScopedLock sl (engineLock);
        if (slots[s].data == nullptr) return;
        gen = ++slots[s].gen;
    }
    loader->add ({ s, false, {}, gen });
}

void AmpireProcessor::doBuild (int s, juce::uint32 gen)
{
    std::shared_ptr<CaptureData> data;
    double rate; int block;
    {
        const juce::ScopedLock sl (engineLock);
        if (gen != slots[s].gen || slots[s].data == nullptr) return;
        data = slots[s].data;
        rate = hostRate; block = maxBlock;
    }
    juce::String err;
    auto v = CaptureBuilder::build (*data, rate, block, osFactorParam(), slotSize (s), err);
    if (v == nullptr)
    {
        { const juce::ScopedLock sl (engineLock); slots[s].info.error = err; }
        infoVersion++;
        return;
    }
    publishVoice (s, v.release(), gen);
}

void AmpireProcessor::publishVoice (int s, CaptureVoice* v, juce::uint32 gen)
{
    {
        const juce::ScopedLock sl (engineLock);
        if (gen != slots[s].gen) { delete v; return; }
        slots[s].info.osFactor = v->osFactor;
        slotLatency[s].store (v->hostLatency());
        slotDelay[s].store (v->modelDelayHost());
        delete live[s].incoming.exchange (v, std::memory_order_acq_rel);   // jamais pris par l'audio : liberation sure
    }
    infoVersion++;
}

void AmpireProcessor::clearCapture (int s)
{
    if (s < 0 || s >= numSlots) return;
    {
        const juce::ScopedLock sl (engineLock);
        ++slots[s].gen;
        ++slots[s].loadGen;
        slots[s].data.reset();
        slots[s].embeddedText.clear();
        slots[s].info = SlotInfo();
        slotMetaRt[s].set (CaptureMeta());
        delete live[s].incoming.exchange (nullptr);
        live[s].clearReq.store (true);
    }
    infoVersion++;
}

void AmpireProcessor::stepCapture (int s, int dir)
{
    if (s < 0 || s >= numSlots) return;
    const auto info = getSlotInfo (s);
    const juce::File current (info.meta.path.isNotEmpty() && juce::File::isAbsolutePath (info.meta.path) ? info.meta.path : juce::String());
    const juce::File folder = startFolder (s);
    if (! folder.isDirectory()) return;
    juce::Array<juce::File> files;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.nam")) files.add (f);
    if (files.isEmpty()) return;
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    int idx = files.indexOf (current);
    idx = idx < 0 ? (dir > 0 ? 0 : files.size() - 1) : (idx + dir + files.size()) % files.size();
    loadCapture (s, files[idx]);
}

juce::File AmpireProcessor::startFolder (int s) const
{
    const auto info = getSlotInfo (juce::jlimit (0, numSlots - 1, s));
    const auto& path = info.meta.path;
    if (! info.fromEmbedded && path.isNotEmpty() && juce::File::isAbsolutePath (path) && juce::File (path).getParentDirectory().isDirectory())
        return juce::File (path).getParentDirectory();
    const auto root = library.getRoot();
    if (root.isDirectory()) return root;
    return lastFolder;
}

SlotInfo AmpireProcessor::getSlotInfo (int s) const
{
    const juce::ScopedLock sl (engineLock);
    return slots[juce::jlimit (0, numSlots - 1, s)].info;
}

void AmpireProcessor::waitForLoads (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    juce::Thread::sleep (20);
    while (! loader->idle() && juce::Time::getMillisecondCounter() < until) juce::Thread::sleep (10);
}

// ============================================================================ copies integrees (projets / presets)
juce::String AmpireProcessor::packFile (const juce::File& f)
{
    juce::MemoryBlock raw;
    if (! f.loadFileAsData (raw)) return {};
    juce::MemoryOutputStream mo;
    {
        juce::GZIPCompressorOutputStream gz (mo, 6);
        gz.write (raw.getData(), raw.getSize());
    }
    return mo.getMemoryBlock().toBase64Encoding();
}

juce::File AmpireProcessor::unpackToCache (const juce::String& packed, const juce::String& name, const juce::String& ext)
{
    juce::MemoryBlock gzData;
    if (packed.isEmpty() || ! gzData.fromBase64Encoding (packed)) return {};
    juce::MemoryInputStream mi (gzData, false);
    juce::GZIPDecompressorInputStream gz (mi);
    juce::MemoryOutputStream out;
    out.writeFromInputStream (gz, -1);
    if (out.getDataSize() == 0) return {};
    const auto dir = CaptureLibrary::settingsDir().getChildFile ("Embedded");
    dir.createDirectory();
    const auto hash = juce::String::toHexString ((juce::int64) juce::DefaultHashFunctions::generateHash (packed, 0x7fffffff));
    auto f = dir.getChildFile (juce::File::createLegalFileName (name) + "_" + hash + ext);
    if (! f.existsAsFile()) f.replaceWithData (out.getData(), out.getDataSize());
    return f;
}

// ============================================================================ etat (projet) et presets
std::unique_ptr<juce::XmlElement> AmpireProcessor::buildStateXml (bool embed) const
{
    auto xml = std::make_unique<juce::XmlElement> ("AMPIRE");
    xml->setAttribute ("version", JucePlugin_VersionString);
    if (auto st = const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState().createXml()) xml->addChildElement (st.release());
    const juce::ScopedLock sl (engineLock);
    for (int s = 0; s < numSlots; ++s)
    {
        const auto& slot = slots[s];
        if (! slot.info.loaded) continue;
        auto* e = xml->createNewChildElement ("SLOT");
        e->setAttribute ("index", s);
        e->setAttribute ("name", slot.info.meta.fileName);
        e->setAttribute ("path", slot.info.meta.path);
        if (embed && slot.embeddedText.isNotEmpty()) e->setAttribute ("data", slot.embeddedText);
    }
    auto* midiXml = xml->createNewChildElement ("MIDI");
    midiXml->setAttribute ("momentary", toggleMomentary.load() ? 1 : 0);
    for (int i = 0; i < numLearn; ++i)
        if (ccMap[i].load() >= 0 || noteMap[i].load() >= 0)
        {
            auto* l = midiXml->createNewChildElement ("L");
            l->setAttribute ("id", learnDef (i).id);
            l->setAttribute ("cc", ccMap[i].load());
            l->setAttribute ("note", noteMap[i].load());
        }
    return xml;
}

void AmpireProcessor::applyStateXml (const juce::XmlElement& xml)
{
    if (auto* st = xml.getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*st));
    lastOs = osFactorParam();
    lastSizeIdx = sizeIndex();

    for (int s = 0; s < numSlots; ++s)
    {
        const juce::XmlElement* e = nullptr;
        for (auto* c : xml.getChildWithTagNameIterator ("SLOT")) if (c->getIntAttribute ("index", -1) == s) e = c;
        if (e == nullptr) { clearCapture (s); continue; }
        const juce::File f (e->getStringAttribute ("path"));
        juce::File use;
        if (juce::File::isAbsolutePath (f.getFullPathName()) && f.existsAsFile()) use = f;
        else use = unpackToCache (e->getStringAttribute ("data"), e->getStringAttribute ("name"), ".nam");   // fichier deplace : copie du projet
        if (use.existsAsFile())
        {
            loadCapture (s, use);
            if (use != f) { const juce::ScopedLock sl (engineLock); slots[s].embeddedText = e->getStringAttribute ("data"); }
        }
        else
        {
            clearCapture (s);
            { const juce::ScopedLock sl (engineLock); slots[s].info.error = "Capture not found: " + e->getStringAttribute ("name"); }
            infoVersion++;
        }
    }

    if (auto* midiXml = xml.getChildByName ("MIDI"))
    {
        resetMidiMap();
        toggleMomentary = midiXml->getIntAttribute ("momentary") != 0;
        for (auto* l : midiXml->getChildWithTagNameIterator ("L"))
            for (int i = 0; i < numLearn; ++i)
                if (l->getStringAttribute ("id") == learnDef (i).id)
                {
                    ccMap[i].store (juce::jlimit (-1, 127, l->getIntAttribute ("cc", -1)));
                    noteMap[i].store (juce::jlimit (-1, 127, l->getIntAttribute ("note", -1)));
                }
    }
}

void AmpireProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto xml = buildStateXml (true);
    xml->setAttribute ("preset", presetName);
    xml->setAttribute ("snap", presetSnapshot);
    xml->setAttribute ("uiScale", uiScaleIdx.load());
    xml->setAttribute ("stack", stackOn.load() ? 1 : 0);
    xml->setAttribute ("accent", juce::String::toHexString ((juce::int64) accentColour.load()));
    xml->setAttribute ("lastFolder", lastFolder.getFullPathName());
    copyXmlToBinary (*xml, dest);
}

void AmpireProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("AMPIRE")) return;
    applyStateXml (*xml);
    presetName = xml->getStringAttribute ("preset");
    presetSnapshot = xml->hasAttribute ("snap") ? xml->getStringAttribute ("snap") : snapshotString();
    uiScaleIdx = juce::jlimit (-1, 2, xml->getIntAttribute ("uiScale", -1));
    stackOn = xml->getIntAttribute ("stack", 0) != 0;
    if (xml->hasAttribute ("accent")) accentColour = 0xff000000u | (juce::uint32) xml->getStringAttribute ("accent").getHexValue64();
    const juce::File lf (xml->getStringAttribute ("lastFolder"));
    if (lf.isDirectory()) lastFolder = lf;
    presetVersion++;
}

juce::String AmpireProcessor::snapshotString() const
{
    juce::String s;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            s << rp->getParameterID() << '=' << juce::String (rp->getValue(), 4) << ';';
    const juce::ScopedLock sl (engineLock);
    for (auto& slot : slots) s << '|' << slot.info.meta.fileName;
    return s;
}

bool AmpireProcessor::isPresetDirty() const { return presetName.isNotEmpty() && snapshotString() != presetSnapshot; }

juce::File AmpireProcessor::presetFolder() { return CaptureLibrary::settingsDir().getChildFile ("Presets"); }

juce::StringArray AmpireProcessor::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : presetFolder().findChildFiles (juce::File::findFiles, false, "*.ampire"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool AmpireProcessor::loadPreset (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (presetFolder().getChildFile (name + ".ampire"));
    if (xml == nullptr || ! xml->hasTagName ("AMPIRE")) return false;
    if (auto* m = xml->getChildByName ("MIDI")) xml->removeChildElement (m, true);   // le preset garde les affectations MIDI de l'instance
    applyStateXml (*xml);
    presetName = name;
    waitForLoads (3000);                 // le nom des captures fait partie de l'instantane
    presetSnapshot = snapshotString();
    presetVersion++;
    return true;
}

bool AmpireProcessor::savePreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    auto xml = buildStateXml (true);
    if (auto* m = xml->getChildByName ("MIDI")) xml->removeChildElement (m, true);
    presetFolder().createDirectory();
    if (! xml->writeTo (presetFolder().getChildFile (name + ".ampire"))) return false;
    presetName = name;
    presetSnapshot = snapshotString();
    presetVersion++;
    return true;
}

bool AmpireProcessor::deletePreset (const juce::String& name)
{
    const bool ok = presetFolder().getChildFile (name + ".ampire").deleteFile();
    if (ok && name == presetName) { presetName.clear(); presetVersion++; }
    return ok;
}

void AmpireProcessor::stepPreset (int dir)
{
    const auto names = getPresetNames();
    if (names.isEmpty()) return;
    int idx = names.indexOf (presetName);
    idx = idx < 0 ? (dir > 0 ? 0 : names.size() - 1) : (idx + dir + names.size()) % names.size();
    loadPreset (names[idx]);
}

void AmpireProcessor::initPreset()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (int s = 0; s < numSlots; ++s) clearCapture (s);
    presetName.clear();
    presetSnapshot = snapshotString();
    presetVersion++;
}

juce::AudioProcessorEditor* AmpireProcessor::createEditor() { return new AmpireEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmpireProcessor(); }

// ============================================================================ mode « stack » (NhdStack.h)
void AmpireProcessor::setIHostApplication (Steinberg::FUnknown* app)
{
    if (reaperHost == nullptr) reaperHost = std::make_unique<NhdReaperHost>();
    reaperHost->attach (app);
}

bool AmpireProcessor::isReaper() const { return reaperHost != nullptr && reaperHost->isReaper(); }

void* AmpireProcessor::reaperTrack() const
{
    return isReaper() && reaperHost->take() == nullptr ? reaperHost->track() : nullptr;
}

const NhdStackApi* AmpireProcessor::stackApi() const
{
    if (! isReaper()) return nullptr;
    using GetApiFn = void* (*) (const char*);
    auto getApi = reinterpret_cast<GetApiFn> (reaperHost->api ("plugin_getapi"));
    if (getApi == nullptr) return nullptr;
    auto* t = static_cast<const NhdStackApi*> (getApi (NHD_STACK_API_NAME));   // relu a chaque fois (Impulse peut etre decharge)
    return t != nullptr && t->version >= 1 ? t : nullptr;
}
