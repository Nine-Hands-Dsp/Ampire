#pragma once
// ============================================================================
// NINE HANDS | DSP - mode « stack » (fichier identique dans Ampire et Impulse)
//
// Dans REAPER, Ampire peut afficher, colle sous sa propre facade, l'Impulse charge sur la MEME piste :
//  - Impulse reste un effet independant, a sa place dans la chaine, avec sa propre fenetre ;
//  - la vue affichee sous Ampire est un vrai editeur d'Impulse (cree par Impulse lui-meme, dans son module),
//    attache comme fenetre enfant de celle d'Ampire : tout ce qu'on y touche agit sur l'Impulse de la piste.
//
// Les deux plugins sont deux modules separes : ils se parlent uniquement par une petite table de fonctions C,
// publiee par Impulse aupres de REAPER (plugin_register « API_... ») et retrouvee par Ampire (plugin_getapi).
// Les instances et les vues sont designees par des numeros (jamais par des pointeurs) : un numero perime est
// simplement refuse. Tout s'appelle sur le thread de messages.
// ============================================================================
#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <pluginterfaces/base/funknown.h>       // SDK VST3 fourni avec JUCE
#if defined (__clang__) || defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

#define NHD_STACK_API_NAME "NineHandsDsp_ImpulseStack_v1"

extern "C"
{
    struct NhdStackApi
    {
        int version;                                                                    // 1
        int  (*findOnTrack)   (void* mediaTrack);                                      // instance Impulse (> 0) ou 0
        int  (*createView)    (int instance, void* parentNativeHandle,
                               int x, int y, int w, int h, double scale);              // vue (> 0) ou 0
        void (*setViewBounds) (int view, int x, int y, int w, int h, double scale);
        void (*destroyView)   (int view);
        int  (*viewAlive)     (int view);                                              // 0 si l'Impulse a ete retire
    };
}

// ---------------------------------------------------------------- acces a REAPER (SDK officiel : reaper_vst3_interfaces.h)
// Declaree a l'identique (meme ordre des fonctions virtuelles, meme identifiant), jamais dans un espace de noms anonyme.
namespace NhdReaperSdk
{
    class IReaperHostApplication : public Steinberg::FUnknown    // REAPER 5.02+
    {
    public:
        virtual void* PLUGIN_API getReaperApi (Steinberg::CStringA funcname) = 0;
        virtual void* PLUGIN_API getReaperParent (Steinberg::uint32 w) = 0;   // piste (1), prise (2), projet (3)
        virtual void* PLUGIN_API reaperExtended (Steinberg::uint32 call, void* p1, void* p2, void* p3) = 0;
    };
}

class NhdReaperHost
{
public:
    NhdReaperHost() = default;
    ~NhdReaperHost() { if (host != nullptr) host->release(); }

    // a appeler depuis VST3ClientExtensions::setIHostApplication
    bool attach (Steinberg::FUnknown* app)
    {
        if (app == nullptr) return false;
        const Steinberg::TUID iid = INLINE_UID (0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);
        void* obj = nullptr;
        if (app->queryInterface (iid, &obj) != Steinberg::kResultOk || obj == nullptr) return false;
        if (host != nullptr) host->release();
        host = static_cast<NhdReaperSdk::IReaperHostApplication*> (obj);   // queryInterface a ajoute une reference
        return true;
    }
    bool isReaper() const noexcept { return host != nullptr; }
    void* api (const char* name) const { return host != nullptr ? host->getReaperApi (name) : nullptr; }
    void* track() const { return host != nullptr ? host->getReaperParent (1) : nullptr; }
    void* take() const { return host != nullptr ? host->getReaperParent (2) : nullptr; }

private:
    NhdReaperSdk::IReaperHostApplication* host = nullptr;
    JUCE_DECLARE_NON_COPYABLE (NhdReaperHost)
};
