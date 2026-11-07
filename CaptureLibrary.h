#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "NamEngine.h"

// ============================================================================
// Bibliotheque de captures : dossier racine (reglage commun a toutes les fenetres), parcouru
// recursivement en arriere-plan. Les metadonnees de chaque .nam sont gardees dans un cache
// (chemin + taille + date) : un dossier de milliers de captures se rouvre instantanement.
// Favoris et captures recentes sont communs a toutes les instances.
// ============================================================================
class CaptureLibrary : private juce::Thread
{
public:
    CaptureLibrary();
    ~CaptureLibrary() override;

    juce::File getRoot() const;
    void setRoot (const juce::File& dir);            // enregistre + relance le parcours
    // dossier de captures par defaut : celui de la bibliotheque, memorise pour toutes les instances et tous les projets
    juce::File getLastFolder() const;
    void setLastFolder (const juce::File& dir);      // dernier dossier ouvert (memorise aussi)
    void rescan();
    bool isScanning() const { return scanning.load(); }
    int  scannedCount() const { return scanCount.load(); }

    juce::Array<CaptureMeta> getEntries() const;     // copie (thread de messages)
    juce::uint32 getVersion() const { return version.load(); }

    bool isFavourite (const juce::String& path) const;
    void toggleFavourite (const juce::String& path);
    juce::StringArray getRecents() const;
    void addRecent (const juce::String& path);

    static juce::File settingsDir();

private:
    void run() override;
    void loadCache();
    void saveCache (const juce::Array<CaptureMeta>&);
    void loadLists();
    void saveLists();

    mutable juce::CriticalSection lock;
    juce::File root, lastFolder;
    juce::Array<CaptureMeta> entries;
    juce::HashMap<juce::String, CaptureMeta> cache;   // cle : chemin|taille|date
    juce::StringArray favourites, recents;
    std::atomic<bool> scanning { false };
    std::atomic<int> scanCount { 0 };
    std::atomic<juce::uint32> version { 1 };
};
