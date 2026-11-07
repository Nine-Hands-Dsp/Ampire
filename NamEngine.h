#pragma once
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>
#include "NAM/dsp.h"

// ============================================================================
// AMPIRE (NINE HANDS | DSP) : moteur de captures Neural Amp Modeler.
//
// Repose sur les references open source (licences MIT) :
//  - NeuralAmpModelerCore v0.6 (Steven Atkinson) : lecture des .nam A1 / A2, chemin rapide A2,
//    conteneurs « slimmable » (A2 Full / A2 Lite dans le meme fichier).
//  - AudioDSPTools : reechantillonnage Lanczos quand l'hote ne tourne pas a la frequence de la capture.
//  - TONE3000 plugin : surechantillonnage a phases entrelacees (filtres demi-bande HIIR a phase
//    minimale, aucune latence) et mesure de la latence / polarite propre a chaque capture.
//
// Une « CaptureVoice » est une instance prete a jouer d'une capture pour une configuration donnee
// (frequence de l'hote, taille de bloc, facteur de surechantillonnage, taille A2). Elle est construite
// et prechauffee hors du thread audio, puis echangee sans verrou ; le thread audio n'alloue jamais.
// ============================================================================

struct CaptureMeta
{
    juce::String path, fileName, title;          // title : metadata.name, sinon nom du fichier
    juce::String architecture;                   // WaveNet, LSTM, SlimmableContainer...
    juce::String family;                         // « A2 », « A1 », « LSTM », « ConvNet », « IR »
    juce::String variant;                        // Standard / Lite / Feather / Nano / Full...
    juce::String gearType, gearMake, gearModel, toneType, modeledBy, date;
    double sampleRate = 48000.0;
    bool   hasLoudness = false, hasInputLevel = false, hasOutputLevel = false;
    double loudness = 0.0, inputLevel = 0.0, outputLevel = 0.0, gain = -1.0, esr = -1.0;
    bool   isA2 = false, slimmable = false, phaseSafe = true;
    std::vector<double> breakpoints;             // tailles disponibles (conteneur slimmable)
    int    latency = 0;                          // retard « cuit » dans la capture (echantillons a sa frequence)
    bool   inverted = false, latencyMeasured = false;
    double fileKB = 0.0;

    juce::String gearLine() const;               // « Marshall JCM800 · amp · crunch »
    juce::String formatLine() const;             // « A2 · 48 kHz · calibrated »
};

class CaptureVoice
{
public:
    ~CaptureVoice();
    void process (const float* in, float* out, int n) noexcept;   // thread audio
    int  hostLatency() const noexcept { return resLatency; }      // latence du reechantillonneur (echantillons hote)
    int  modelDelayHost() const noexcept { return modelDelay; }   // retard propre de la capture, ramene a l'hote
    bool inverted() const noexcept { return inv; }
    double size = 1.0;
    int    osFactor = 1;

private:
    friend struct CaptureBuilder;
    CaptureVoice() = default;
    void runModel (float** in, float** out, int n) noexcept;      // a la frequence de la capture (x os)

    std::vector<std::unique_ptr<nam::DSP>> phases;                // une instance par phase entrelacee
    struct Impl;
    std::unique_ptr<Impl> impl;
    int resLatency = 0, modelDelay = 0, maxBlock = 0, maxInner = 0;
    bool inv = false;
};

// Capture lue sur le disque : configuration gardee en memoire pour reconstruire des voix
// (changement de frequence, de taille A2, de surechantillonnage) sans relire le fichier.
struct CaptureData
{
    CaptureMeta meta;
    std::shared_ptr<const void> config;          // nam::dspData (opaque ici)
};

struct CaptureBuilder
{
    // lit un .nam (ou un .wav : capture lineaire) ; mesure latence / polarite ; aucune voix construite
    static std::shared_ptr<CaptureData> load (const juce::File& f, juce::String& error);
    // metadonnees seules (bibliotheque) : lecture JSON sans construire le reseau
    static bool readMeta (const juce::File& f, CaptureMeta& out);
    // construit une voix prete a jouer (prechauffee) ; nullptr + message en cas d'echec
    static std::unique_ptr<CaptureVoice> build (const CaptureData& d, double hostRate, int maxBlock,
                                                int osFactor, double size, juce::String& error);
};
