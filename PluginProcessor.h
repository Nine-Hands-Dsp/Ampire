#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include "NamEngine.h"
#include "CaptureLibrary.h"

// ============================================================================
// AMPIRE : chargeur d'une capture Neural Amp Modeler (A1 et A2), guitare et basse.
// Commandes = celles du plugin NAM officiel : INPUT, BASS, MIDDLE, TREBLE, OUTPUT, plus SIZE
// (taille des captures A2 « slimmables », Full -> Lite). Chaine mono, sortie dupliquee en stereo :
//
//   entree -> INPUT (+ calibration dBu) -> capture NAM (A1 / A2, rechantillonnage si besoin,
//          surechantillonnage anti-repliement) -> niveau (brut / normalise / calibre)
//          -> tone stack NAM (grave 150 Hz, medium 425 Hz, aigu 1800 Hz) -> anti-DC -> OUTPUT
//
// Pas de noise gate (NGATE avant Ampire) ni d'IR de baffle (Impulse apres Ampire).
// Latence : 0 echantillon quand l'hote tourne a la frequence de la capture (48 kHz en general) ;
// sinon latence du reechantillonneur Lanczos, declaree a l'hote. Le surechantillonnage (filtres
// HIIR a phase minimale) n'ajoute aucune latence.
// ============================================================================

struct SlotInfo
{
    bool loaded = false, loading = false, fromEmbedded = false;
    CaptureMeta meta;
    juce::String error;
    int osFactor = 1;            // facteur reellement utilise (LSTM : 1)
};

class NhdReaperHost;
struct NhdStackApi;

class AmpireProcessor : public juce::AudioProcessor,
                        public juce::VST3ClientExtensions,
                        private juce::Timer
{
public:
    // ---- mode « stack » (NhdStack.h) : dans REAPER, l'Impulse de la meme piste s'affiche colle sous Ampire
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return this; }
    void setIHostApplication (Steinberg::FUnknown*) override;
    bool isReaper() const;
    void* reaperTrack() const;                 // MediaTrack* (nullptr hors REAPER)
    const NhdStackApi* stackApi() const;       // table publiee par Impulse (nullptr si aucun Impulse charge)
    std::atomic<bool> stackOn { false };       // sauvegarde avec le projet

    AmpireProcessor();
    ~AmpireProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ampire"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---------------------------------------------------------------- captures (jamais depuis le thread audio)
    static constexpr int numSlots = 1;
    void loadCapture (int slot, const juce::File& file);      // asynchrone (thread de chargement)
    bool loadCaptureNow (int slot, const juce::File& file);   // synchrone (outil hors ligne, restauration)
    void clearCapture (int slot);
    void stepCapture (int slot, int dir);                     // capture precedente / suivante du dossier
    SlotInfo getSlotInfo (int slot) const;
    juce::uint32 getInfoVersion() const { return infoVersion.load(); }
    juce::File startFolder (int slot) const;
    juce::File lastFolder;
    void waitForLoads (int timeoutMs);                         // outil hors ligne

    // ---------------------------------------------------------------- bibliotheque de captures
    CaptureLibrary library;

    // ---------------------------------------------------------------- presets (fichiers .ampire)
    static juce::File presetFolder();
    juce::StringArray getPresetNames() const;
    juce::String getPresetName() const { return presetName; }
    bool isPresetDirty() const;
    bool loadPreset (const juce::String& name);
    bool savePreset (const juce::String& name);
    bool deletePreset (const juce::String& name);
    void stepPreset (int dir);
    void initPreset();
    juce::uint32 getPresetVersion() const { return presetVersion.load(); }

    static void toneStack (int band, float knob, double sr, float& b0, float& b1, float& b2, float& a1, float& a2);   // tone stack NAM

    // ---------------------------------------------------------------- MIDI Learn (clic droit)
    enum LearnType { learnContinuous, learnToggle, learnTrigger };
    struct LearnDef { const char* id; const char* name; int type; };
    enum { lInput, lBass, lMiddle, lTreble, lOutput, lSize,   // continus
           lActive,                                         // bascule
           lPrev, lNext, lPrevPreset, lNextPreset,          // impulsions
           numLearn };
    static constexpr int numParamLearn = lActive + 1;
    static const LearnDef& learnDef (int i);
    std::atomic<int> ccMap[numLearn];
    std::atomic<int> noteMap[numLearn];
    std::atomic<int> learnTarget { -1 };
    std::atomic<int> lastLearned { -1 };
    std::atomic<bool> toggleMomentary { false };
    void resetMidiMap();
    void clearLearn (int i);
    juce::String learnAssignText (int i) const;

    // ---------------------------------------------------------------- interface
    std::atomic<juce::uint32> accentColour { 0xffe9e7e1 };
    std::atomic<int> uiScaleIdx { -1 };
    std::unique_ptr<NhdReaperHost> reaperHost;
    std::atomic<float> meterIn { 0.0f }, meterOut { 0.0f }, cpuLoad { 0.0f };
    std::atomic<bool> clipIn { false }, clipOut { false };
    double getHostRate() const { return hostRate; }
    int getReportedLatency() const { return wantedLatency.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    // ---- voix en jeu (thread audio) + echange sans verrou
    struct LiveSlot
    {
        std::atomic<CaptureVoice*> incoming { nullptr };   // publie par le chargeur, pris par l'audio
        std::atomic<bool> clearReq { false };
        CaptureVoice* cur = nullptr;                       // thread audio seulement
        CaptureVoice* old = nullptr;                       // voix en fondu sortant
        float fade = 1.0f;                                 // 0 -> 1 pendant le fondu enchaine
        bool  fadingOut = false;                           // vidage : fondu vers le silence
        float outGain = 0.0f;                              // presence (0 = silencieux), pour le fondu de vidage
    };
    struct Slot                                            // thread de messages / chargeur (verrou engineLock)
    {
        std::shared_ptr<CaptureData> data;
        SlotInfo info;
        juce::uint32 gen = 0;                              // generation : une construction perimee n'est pas publiee
        juce::String embeddedText;                         // copie gzip+base64 du .nam (projets / presets)
    };

    class Loader;
    friend class Loader;
    std::unique_ptr<Loader> loader;
    void requestBuild (int slot);
    void doLoad (int slot, const juce::File& file, juce::uint32 gen);     // thread de chargement
    void doBuild (int slot, juce::uint32 gen);
    // metadonnees utiles au thread audio (niveaux), en atomiques
    struct MetaRt
    {
        std::atomic<bool> hasLoudness { false }, hasInputLevel { false }, hasOutputLevel { false };
        std::atomic<float> loudness { 0.0f }, inputLevel { 0.0f }, outputLevel { 0.0f };
        void set (const CaptureMeta& m)
        {
            loudness = (float) m.loudness; inputLevel = (float) m.inputLevel; outputLevel = (float) m.outputLevel;
            hasLoudness = m.hasLoudness; hasInputLevel = m.hasInputLevel; hasOutputLevel = m.hasOutputLevel;
        }
    };
    MetaRt slotMetaRt[numSlots];                          // reconstruit la voix (frequence, taille, surechantillonnage)
    void publishVoice (int slot, CaptureVoice* v, juce::uint32 gen);
    bool setSlotData (int slot, std::shared_ptr<CaptureData> data, bool embedded);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void handleMidi (const juce::MidiBuffer&);
    void retire (CaptureVoice* v);                         // thread audio -> corbeille (detruite par le timer)
    void emptyTrash();
    void updateLatency();
    int  osFactorParam() const;
    double slotSize (int s) const;

    std::unique_ptr<juce::XmlElement> buildStateXml (bool embed) const;
    void applyStateXml (const juce::XmlElement&);
    juce::String snapshotString() const;
    static juce::String packFile (const juce::File& f);
    static juce::File unpackToCache (const juce::String& packed, const juce::String& name, const juce::String& ext);

    // ---- etat partage
    Slot slots[numSlots];
    LiveSlot live[numSlots];
    mutable juce::CriticalSection engineLock;              // jamais pris par le thread audio
    std::atomic<juce::uint32> infoVersion { 1 };
    std::atomic<int> wantedLatency { 0 };
    std::atomic<int> slotLatency[numSlots];                // reechantillonneur (hote)
    std::atomic<int> slotDelay[numSlots];                  // retard propre de la capture (hote)
    double hostRate = 48000.0;
    int maxBlock = 512;
    int lastOs = -1;
    int lastSizeIdx = -1;
    int sizeIndex() const;                                  // sous-modele A2 choisi par SIZE

    // corbeille : file SPSC audio -> timer
    static constexpr int trashSize = 64;
    std::atomic<CaptureVoice*> trash[trashSize];
    std::atomic<int> trashW { 0 }, trashR { 0 };

    // ---- DSP (thread audio)
    juce::AudioBuffer<float> monoIn, capBuf, stereoBuf, bypassDry;
    struct Ring
    {
        std::vector<float> d; int w = 0;
        void setSize (int n) { d.assign ((size_t) juce::nextPowerOfTwo (juce::jmax (8, n)), 0.0f); w = 0; }
        void clear() { std::fill (d.begin(), d.end(), 0.0f); }
        float process (float x, int delay) noexcept
        {
            const int mask = (int) d.size() - 1;
            d[(size_t) w] = x;
            const float y = d[(size_t) ((w - delay) & mask)];
            w = (w + 1) & mask;
            return y;
        }
    };
    Ring bypassRing[2];
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] = {}, z2[2] = {};
        inline float process (float x, int c) noexcept { const float y = b0 * x + z1[c]; z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y; return y; }
        void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    };
    static void setPeak (Biquad&, double fc, double q, double db, double sr);
    static void setShelf (Biquad&, double fc, double db, bool high, double sr);
    static void setHighPass (Biquad&, double fc, double q, double sr);
    static void setLowPass (Biquad&, double fc, double q, double sr);
    struct Smooth2 { float a = 0, b = 0, k = 0.01f; void set (float v) { a = b = v; } float next (float t) { a += k * (t - a); b += k * (a - b); return b; } };
    Biquad eqLow, eqMid, eqHigh, cutHp, cutLp;            // tone stack NAM + filtres de sortie
    float eqState[3] { -1.0f, -1.0f, -1.0f };
    float lastLow = -1.0f, lastHigh = -1.0f;
    float dcX[2] {}, dcY[2] {};
    Smooth2 smIn, smOut, smCap, smActive, smTone, smEq[3];
    float fadeStep = 0.001f;
    float peakIn = 0.0f, peakOut = 0.0f;
    double cpuSmooth = 0.0;

    std::atomic<float> *pActive, *pInput, *pBass, *pMiddle, *pTreble, *pOutput, *pSize, *pTone, *pNorm,
                       *pCalIn, *pCalLevel, *pOversample, *pLowCut, *pHighCut, *pInputSel;

    // ---- anti-gel : MIDI (thread audio) -> parametres (timer)
    std::atomic<float> pendingValue[numParamLearn];
    std::atomic<bool>  pendingSet[numParamLearn];
    std::atomic<float>* queuedPtr[numParamLearn] {};
    std::atomic<int> pendingTrigger { 0 };
    bool ccHeld[numLearn] {};
    int ccMin[numLearn], ccMax[numLearn];
    int msb[32] {};

    // ---- presets
    juce::String presetName, presetSnapshot;
    std::atomic<juce::uint32> presetVersion { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpireProcessor)
};
