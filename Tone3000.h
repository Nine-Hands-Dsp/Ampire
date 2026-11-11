#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

// ============================================================================
// TONE3000 (API officielle v1, offre gratuite non commerciale) : connexion OAuth 2.0 + PKCE par le navigateur
// (redirection vers un petit serveur local 127.0.0.1), listes Favorites / Created / Trending / Latest, flux
// « Select » (Browse TONE3000) et synchronisation des tones dans le dossier de captures, rangement compatible NAMMAN :
//   <dossier>/<Categorie>/<nom du tone>/  + metadata.json ; versions remplacees deplacees dans _Archive (rien n'est efface).
// La cle publique (client_id) est la seule cle embarquee : aucune cle secrete dans le plugin.
// Un seul client partage par toutes les fenetres d'Ampire (SharedResourcePointer).
// ============================================================================
namespace t3k
{
    struct Tone
    {
        juce::String id, title, gear, format, license, user, avatar, image, url, updatedAt;
        int a1 = 0, a2 = 0, irs = 0, models = 0;
        static Tone fromVar (const juce::var& v);
        juce::String gearLabel() const;      // Amp + Cab, Amp, Pedal, Cabinet...
        juce::String formatBadge() const;    // A2, A1, IR
        juce::String category() const;       // dossier de rangement (NAMMAN)
    };

    struct Local                             // tone deja synchronise (metadata.json)
    {
        Tone tone;
        juce::File folder;
        juce::String syncedAt;
    };

    enum Tab { favorites, synced, created, trending, latest, numTabs };
    enum VersionMode { vBest, vA2, vA1, vCustom, vAll, numVersionModes };

    struct ListState
    {
        juce::Array<Tone> tones;
        int page = 0, totalPages = 1, total = 0;
        bool loading = false, loaded = false;
        juce::String error;
    };

    struct Job
    {
        int done = 0, total = 0;
        bool running = false, failed = false;
        juce::String error;
    };

    class Client : public juce::ChangeBroadcaster,
                   private juce::AsyncUpdater
    {
    public:
        Client();
        ~Client() override;

        // ---- compte
        bool isSignedIn() const;
        bool isConnecting() const;
        juce::String getUsername() const;
        juce::String getAvatarUrl() const;
        juce::String getAuthError() const;
        void signIn (bool selectTone);       // ouvre le navigateur (selectTone = Browse TONE3000)
        void cancelSignIn();
        void signOut();

        // ---- listes
        ListState getList (Tab t) const;
        void refresh (Tab t);
        void loadMore (Tab t);

        // ---- dossier de captures et tones synchronises
        void setRoot (const juce::File& dir);
        juce::File getRoot() const;
        bool findLocal (const juce::String& toneId, Local& out) const;
        juce::String remoteUpdatedAt (const juce::String& toneId) const;   // derniere date connue en ligne
        juce::uint32 getLibraryVersion() const { return libraryVersion.load(); }   // change apres chaque synchronisation
        juce::uint32 getSelectCount() const { return selectCount.load(); }          // change apres chaque tone choisi par Browse

        // ---- synchronisation
        void sync (const Tone& t);
        Job getJob (const juce::String& toneId) const;
        int getVersionMode() const;
        void setVersionMode (int m);
        static juce::String versionModeName (int m);

        // ---- images (vignettes, avatars) : chargees en arriere-plan, nulles en attendant
        juce::Image getImage (const juce::String& url);

        static juce::String safeName (const juce::String& s);
       #if AMPIRE_TOOL
        void previewDemo (bool signedIn);     // outil Linux : donnees fictives pour les captures d'ecran
       #endif

    private:
        class Login;
        friend class Login;
        void handleAsyncUpdate() override { sendChangeMessage(); }
        void changed() { triggerAsyncUpdate(); }

        juce::String accessToken();                          // renouvelle le jeton si besoin (thread de travail)
        juce::var apiGet (const juce::String& pathAndQuery, int* statusOut = nullptr);
        bool exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirect);
        bool refreshToken();
        void fetchUser();
        void loginFinished (const juce::String& error, const juce::String& selectedToneId);
        void fetchPage (Tab t, int page);
        void runSync (Tone t);
        void scanLocal();
        void checkUpdates();
        void loadSettings();
        void saveSettings();

        mutable juce::CriticalSection lock;
        juce::String access, refresh_, username, avatarUrl, authError;
        juce::int64 expiresAt = 0;
        int versionMode = vBest;
        juce::File root;
        ListState lists[numTabs];
        juce::HashMap<juce::String, Local> local;            // cle : id du tone
        juce::HashMap<juce::String, juce::String> remoteDates;
        juce::HashMap<juce::String, Job> jobs;
        juce::HashMap<juce::String, juce::Image> images;
        juce::StringArray pendingImages, failedImages;
        std::unique_ptr<Login> login;
        std::atomic<bool> connecting { false }, quitting { false }, demo { false };
        std::atomic<juce::uint32> libraryVersion { 1 }, selectCount { 0 };
        juce::ThreadPool pool { 3 };
        juce::ThreadPool imagePool { 2 };
        JUCE_DECLARE_NON_COPYABLE (Client)
    };
}
