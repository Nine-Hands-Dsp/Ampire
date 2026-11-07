#include "NamEngine.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"
#if defined(NAM_ENABLE_A2_FAST)
 #include "NAM/wavenet/a2_fast.h"
#endif
#ifndef DEFAULT_BLOCK_SIZE
namespace dsp { constexpr int DEFAULT_BLOCK_SIZE = 64; }
#endif
namespace iplug { constexpr double PI = 3.141592653589793238462643383279502884; }
#include "dsp/ResamplingContainer/ResamplingContainer.h"
#include "thirdparty/tone3000/ChainOversampler.h"
#include "thirdparty/tone3000/nam_latency.h"

// ============================================================================ textes
juce::String CaptureMeta::gearLine() const
{
    juce::StringArray parts;
    const auto gear = (gearMake + " " + gearModel).trim();
    if (gear.isNotEmpty()) parts.add (gear);
    auto pretty = [] (juce::String s) { return s.replace ("_", " ").trim(); };
    if (gearType.isNotEmpty()) parts.add (pretty (gearType));
    if (toneType.isNotEmpty()) parts.add (pretty (toneType));
    if (modeledBy.isNotEmpty()) parts.add ("by " + modeledBy);
    return parts.joinIntoString (juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")));
}

juce::String CaptureMeta::formatLine() const
{
    juce::StringArray parts;
    parts.add ((family + " " + variant).trim());
    juce::String k = juce::String (sampleRate / 1000.0, 1);
    if (k.endsWith (".0")) k = k.dropLastCharacters (2);
    parts.add (k + " kHz");
    if (hasInputLevel) parts.add ("cal. " + juce::String (inputLevel, 1) + " dBu");
    if (esr >= 0.0) parts.add ("ESR " + juce::String (esr, 4));
    if (latencyMeasured && latency > 0) parts.add ("delay " + juce::String (latency) + " smp");
    if (latencyMeasured && inverted) parts.add ("polarity -");
    return parts.joinIntoString ("  |  ");
}

// ============================================================================ voix
struct CaptureVoice::Impl
{
    explicit Impl (double modelRate) : resampler (modelRate) {}
    dsp::ResamplingContainer<float, 1, 12> resampler;   // Lanczos A = 12 (AudioDSPTools)
    bool resample = false;
    ChainOversampler os;                                  // demi-bandes HIIR, phase minimale, 0 latence
    std::vector<std::vector<float>> phaseIn, phaseOut;    // une piste par phase entrelacee
    std::vector<float> scratchIn, scratchOut;
    std::function<void (float**, float**, int)> innerFn;  // fixe une fois (aucune allocation a l'usage)
};

CaptureVoice::~CaptureVoice() = default;

void CaptureVoice::runModel (float** in, float** out, int n) noexcept
{
    // n echantillons a la frequence de la capture : surechantillonnage optionnel, phases entrelacees
    impl->os.processMono (in[0], out[0], n, [this] (float** a, float** b, int m)
    {
        const int P = (int) phases.size();
        if (P == 1) { phases[0]->process (a, b, m); return; }
        const int each = m / P;                         // m est toujours un multiple de P
        for (int p = 0; p < P; ++p)
        {
            float* pin = impl->phaseIn[(size_t) p].data();
            for (int i = 0; i < each; ++i) pin[i] = a[0][i * P + p];
        }
        for (int p = 0; p < P; ++p)
        {
            float* pi = impl->phaseIn[(size_t) p].data();
            float* po = impl->phaseOut[(size_t) p].data();
            phases[(size_t) p]->process (&pi, &po, each);
        }
        for (int p = 0; p < P; ++p)
        {
            const float* po = impl->phaseOut[(size_t) p].data();
            for (int i = 0; i < each; ++i) b[0][i * P + p] = po[i];
        }
    });
}

void CaptureVoice::process (const float* in, float* out, int n) noexcept
{
    if (n <= 0) return;
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        std::memcpy (impl->scratchIn.data(), in + start, sizeof (float) * (size_t) len);
        float* i0 = impl->scratchIn.data();
        float* o0 = out + start;
        if (impl->resample) impl->resampler.ProcessBlock (&i0, &o0, len, impl->innerFn);
        else runModel (&i0, &o0, len);
    }
}

// ============================================================================ lecture
namespace
{
    juce::String str (const nlohmann::json& j, const char* key)
    {
        auto it = j.find (key);
        if (it == j.end() || it->is_null()) return {};
        if (it->is_string()) return juce::String::fromUTF8 (it->get<std::string>().c_str());
        if (it->is_number()) return juce::String (it->get<double>());
        return {};
    }
    bool num (const nlohmann::json& j, const char* key, double& v)
    {
        auto it = j.find (key);
        if (it == j.end() || ! it->is_number()) return false;
        v = it->get<double>();
        return true;
    }

    // A2 : forme exacte reconnue par le chemin rapide de NAM Core ; A1 : nombre de canaux du WaveNet
    void classify (const juce::String& arch, const nlohmann::json& config, CaptureMeta& m)
    {
        m.architecture = arch;
        if (arch == "SlimmableContainer")
        {
            m.slimmable = true;
            m.family = "NAM";
            if (auto it = config.find ("submodels"); it != config.end() && it->is_array())
            {
                for (size_t i = 0; i < it->size(); ++i)
                {
                    const auto& sm = (*it)[i];
                    if (i + 1 < it->size()) m.breakpoints.push_back (sm.value ("max_value", 1.0));
                    if (sm.contains ("model"))
                    {
                        const auto& mo = sm["model"];
                        const auto a = juce::String (mo.value ("architecture", std::string()));
                        if (a == "LSTM") m.phaseSafe = false;
                       #if defined(NAM_ENABLE_A2_FAST)
                        int ch = 0;
                        if (a == "WaveNet" && mo.contains ("config") && nam::wavenet::a2_fast::is_a2_shape (mo["config"], &ch)) m.isA2 = true;
                       #endif
                    }
                }
            }
            if (m.isA2) { m.family = "A2"; m.variant = m.breakpoints.size() == 1 ? "Full / Lite" : "Slimmable"; }
            else m.variant = "Slimmable";
            return;
        }
        if (arch == "WaveNet")
        {
           #if defined(NAM_ENABLE_A2_FAST)
            int ch = 0;
            if (nam::wavenet::a2_fast::is_a2_shape (config, &ch)) { m.isA2 = true; m.family = "A2"; m.variant = ch >= 8 ? "Full" : "Lite"; return; }
           #endif
            m.family = "A1";
            int nch = 0;
            if (auto it = config.find ("layers"); it != config.end() && it->is_array() && ! it->empty())
                nch = (*it)[0].value ("channels", 0);
            m.variant = nch >= 16 ? "Standard" : nch >= 12 ? "Lite" : nch >= 8 ? "Feather" : nch > 0 ? "Nano" : "";
            return;
        }
        if (arch == "LSTM") { m.family = "LSTM"; m.phaseSafe = false; return; }
        if (arch == "Linear") { m.family = "IR"; return; }
        m.family = arch;
    }

    void readMetaJson (const nlohmann::json& md, CaptureMeta& m)
    {
        if (! md.is_object()) return;
        m.title = str (md, "name");
        m.modeledBy = str (md, "modeled_by");
        m.gearType = str (md, "gear_type");
        m.gearMake = str (md, "gear_make");
        m.gearModel = str (md, "gear_model");
        m.toneType = str (md, "tone_type");
        m.date = str (md, "date");
        if (md.contains ("date") && md["date"].is_object())
        {
            const auto& d = md["date"];
            m.date = juce::String (d.value ("year", 0)) + "-" + juce::String (d.value ("month", 0)).paddedLeft ('0', 2)
                     + "-" + juce::String (d.value ("day", 0)).paddedLeft ('0', 2);
        }
        m.hasLoudness = num (md, "loudness", m.loudness);
        m.hasInputLevel = num (md, "input_level_dbu", m.inputLevel);
        m.hasOutputLevel = num (md, "output_level_dbu", m.outputLevel);
        num (md, "gain", m.gain);
        if (md.contains ("training") && md["training"].is_object())
        {
            const auto& t = md["training"];
            if (t.contains ("validation_esr") && t["validation_esr"].is_number()) m.esr = t["validation_esr"].get<double>();
            else if (t.contains ("validation_esr_final") && t["validation_esr_final"].is_number()) m.esr = t["validation_esr_final"].get<double>();
        }
    }

    // retard « cuit » et polarite, mesures sur un balayage (methode TONE3000, analyse temporelle)
    void measureLatency (nam::DSP& dsp, double rate, CaptureMeta& m)
    {
        namlat::Config cfg;
        cfg.sampleRate = rate;
        cfg.blockSize = 256;
        std::vector<float> fi (256), fo (256);
        const auto r = namlat::measureTimeDomain ([&] (const double* in, double* out, int n)
        {
            for (int i = 0; i < n; ++i) fi[(size_t) i] = (float) in[i];
            float* pi = fi.data(); float* po = fo.data();
            dsp.process (&pi, &po, n);
            for (int i = 0; i < n; ++i) out[i] = std::isfinite (fo[(size_t) i]) ? (double) fo[(size_t) i] : 0.0;
        }, cfg);
        if (! r.ok) return;
        m.latencyMeasured = true;
        if (r.peakRatio >= 2.0 && ! r.ambiguous) { m.latency = r.latencySamples; m.inverted = r.inverted && r.confidence > 0.25; }
        else if (r.ambiguous) { m.latency = r.earlySamples; m.inverted = false; }     // lobes a egalite : on ne retourne pas la phase
        else { m.latency = r.onsetSamples; m.inverted = false; }
        m.latency = juce::jlimit (0, (int) (0.01 * rate), m.latency);
    }
}

bool CaptureBuilder::readMeta (const juce::File& f, CaptureMeta& m)
{
    // bibliotheque : on lit l'en-tete sans analyser les poids (fichiers A2 de plusieurs Mo)
    m = CaptureMeta();
    m.path = f.getFullPathName();
    m.fileName = f.getFileNameWithoutExtension();
    m.fileKB = (double) f.getSize() / 1024.0;
    const auto text = f.loadFileAsString();
    if (text.isEmpty()) return false;
    auto valueOf = [&text] (const char* key) -> juce::String
    {
        const int k = text.indexOf (juce::String ("\"") + key + "\"");
        if (k < 0) return {};
        int c = text.indexOfChar (k, ':');
        if (c < 0) return {};
        int i = c + 1;
        while (i < text.length() && juce::CharacterFunctions::isWhitespace (text[i])) ++i;
        if (text[i] == '"') { const int e = text.indexOfChar (i + 1, '"'); return e > i ? text.substring (i + 1, e) : juce::String(); }
        int e = i;
        while (e < text.length() && (juce::CharacterFunctions::isDigit (text[e]) || text[e] == '.' || text[e] == '-' || text[e] == 'e' || text[e] == '+')) ++e;
        return text.substring (i, e);
    };
    m.architecture = valueOf ("architecture");
    if (m.architecture.isEmpty()) return false;
    const auto sr = valueOf ("sample_rate");
    m.sampleRate = sr.isNotEmpty() ? sr.getDoubleValue() : 48000.0;
    // objet "metadata" : extrait par appariement des accolades puis analyse
    const int mk = text.indexOf ("\"metadata\"");
    if (mk >= 0)
    {
        const int b = text.indexOfChar (mk, '{');
        if (b > 0)
        {
            int depth = 0, e = b;
            bool inStr = false;
            for (; e < text.length(); ++e)
            {
                const auto ch = text[e];
                if (inStr) { if (ch == '\\') ++e; else if (ch == '"') inStr = false; continue; }
                if (ch == '"') inStr = true;
                else if (ch == '{') ++depth;
                else if (ch == '}' && --depth == 0) break;
            }
            try
            {
                const auto md = nlohmann::json::parse (text.substring (b, e + 1).toStdString());
                readMetaJson (md, m);
            }
            catch (...) {}
        }
    }
    if (m.architecture == "SlimmableContainer") { m.slimmable = true; m.family = text.contains ("LeakyReLU") ? "A2" : "NAM"; m.isA2 = m.family == "A2"; }
    else if (m.architecture == "WaveNet")
    {
        m.isA2 = text.contains ("LeakyReLU") && ! text.contains ("\"Tanh\"");
        m.family = m.isA2 ? "A2" : "A1";
    }
    else if (m.architecture == "LSTM") m.family = "LSTM";
    else m.family = m.architecture;
    return true;
}

std::shared_ptr<CaptureData> CaptureBuilder::load (const juce::File& f, juce::String& error)
{
    if (! f.existsAsFile()) { error = "File not found: " + f.getFileName(); return nullptr; }
    if (! f.hasFileExtension ("nam")) { error = "Not a .nam capture: " + f.getFileName(); return nullptr; }
    try
    {
        auto cfg = std::make_shared<nam::dspData>();
        nam::DspLoadOptions opt; opt.prewarm = false;
        auto dsp = nam::get_dsp (std::filesystem::u8path (f.getFullPathName().toStdString()), *cfg, opt);
        if (dsp == nullptr) { error = "Could not read " + f.getFileName(); return nullptr; }
        if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
        {
            error = "Capture must be mono in / mono out: " + f.getFileName();
            return nullptr;
        }
        auto data = std::make_shared<CaptureData>();
        auto& m = data->meta;
        m.path = f.getFullPathName();
        m.fileName = f.getFileNameWithoutExtension();
        m.fileKB = (double) f.getSize() / 1024.0;
        m.sampleRate = cfg->expected_sample_rate > 0 ? cfg->expected_sample_rate : 48000.0;
        classify (juce::String (cfg->architecture), cfg->config, m);
        readMetaJson (cfg->metadata, m);
        if (auto* s = dynamic_cast<nam::SlimmableModel*> (dsp.get()))
        {
            m.slimmable = true;
            const auto bp = s->GetSlimmableSizeBreakpoints();
            if (! bp.empty()) m.breakpoints = bp;
            s->SetSlimmableSize (1.0);               // mesure sur la plus grande taille
        }
        dsp->SetPrewarmOnReset (true);
        dsp->Reset (m.sampleRate, 256);
        measureLatency (*dsp, m.sampleRate, m);
        data->config = cfg;
        return data;
    }
    catch (const std::exception& e) { error = juce::String (e.what()); }
    catch (...) { error = "Unknown error"; }
    if (error.isEmpty()) error = "Could not read " + f.getFileName();
    else error = f.getFileName() + ": " + error;
    return nullptr;
}

std::unique_ptr<CaptureVoice> CaptureBuilder::build (const CaptureData& d, double hostRate, int maxBlock,
                                                     int osFactor, double size, juce::String& error)
{
    try
    {
        const auto& cfgIn = *static_cast<const nam::dspData*> (d.config.get());
        const auto& m = d.meta;
        const double modelRate = m.sampleRate > 0 ? m.sampleRate : 48000.0;
        osFactor = (osFactor == 2 || osFactor == 4) && m.phaseSafe ? osFactor : 1;

        std::unique_ptr<CaptureVoice> v (new CaptureVoice());
        v->impl = std::make_unique<CaptureVoice::Impl> (modelRate);
        v->osFactor = osFactor;
        v->size = size;
        v->maxBlock = juce::jmax (16, maxBlock);
        v->impl->resample = std::abs (hostRate - modelRate) > 0.5;
        // bloc maximal vu par la capture (a sa frequence), arrondi, avec marge pour le reechantillonneur
        const int inner = (int) std::ceil ((double) v->maxBlock * modelRate / hostRate) + 64;
        v->maxInner = inner;
        const int perPhase = inner;                     // chaque phase recoit inner echantillons (inner * os / os)

        for (int p = 0; p < osFactor; ++p)
        {
            nam::dspData copy = cfgIn;                  // get_dsp() consomme les poids
            nam::DspLoadOptions opt; opt.prewarm = false;
            auto dsp = nam::get_dsp (copy, opt);
            if (dsp == nullptr) { error = "Could not build the model"; return nullptr; }
            if (auto* s = dynamic_cast<nam::SlimmableModel*> (dsp.get())) s->SetSlimmableSize (size);
            dsp->SetPrewarmOnReset (true);
            dsp->Reset (modelRate, perPhase);           // prechauffage (etat du reseau stable, aucun clic)
            v->phases.push_back (std::move (dsp));
        }
        v->impl->os.prepare (osFactor, inner);
        if (osFactor > 1)
        {
            v->impl->phaseIn.assign ((size_t) osFactor, std::vector<float> ((size_t) perPhase, 0.0f));
            v->impl->phaseOut.assign ((size_t) osFactor, std::vector<float> ((size_t) perPhase, 0.0f));
            // le filtre demi-bande demarre a froid : on le fait tourner sur du silence avant usage
            std::vector<float> z ((size_t) inner, 0.0f), o ((size_t) inner, 0.0f);
            for (int k = 0; k < 4; ++k) v->runModel (std::array<float*, 1> { z.data() }.data(), std::array<float*, 1> { o.data() }.data(), juce::jmin (inner, 256));
        }
        v->impl->scratchIn.assign ((size_t) v->maxBlock, 0.0f);
        auto* raw = v.get();
        v->impl->innerFn = [raw] (float** a, float** b, int n) { raw->runModel (a, b, n); };
        if (v->impl->resample)
        {
            v->impl->resampler.Reset (hostRate, v->maxBlock);   // pre-remplit avec du silence via FallbackFunc
            v->resLatency = v->impl->resampler.GetLatency();
        }
        v->modelDelay = m.latencyMeasured ? (int) std::lround (m.latency * hostRate / modelRate) : 0;
        v->inv = m.latencyMeasured && m.inverted;
        return v;
    }
    catch (const std::exception& e) { error = juce::String (e.what()); }
    catch (...) { error = "Unknown error while building the model"; }
    return nullptr;
}
