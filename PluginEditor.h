#pragma once
#include "PluginProcessor.h"

class AmpireContent;

class AmpireEditor : public juce::AudioProcessorEditor
{
public:
    explicit AmpireEditor (AmpireProcessor&);
    ~AmpireEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    int  getScaleIndex() const;          // 0, 1, 2 = 60 % / 75 % / 100 %  (-1 = taille libre)
    void setScaleIndex (int idx);
    void applyScaleIndex (int idx);
    static constexpr int numSizes = 3;
    AmpireProcessor& getProc() { return proc; }
    void openColourPicker (juce::Component* near, int which = 0);   // 0 accent, 1 LEDs des vu-metres, 2 saturation
    void openPresetMenu (juce::Component* near);
    float getScreenGlow() const;
    float getLedGlow() const;
    void setScreenGlow (float v);    // halo des textes de l'ecran (0 = aucun)
    void setLedGlow (float v);       // halo du voyant POWER (1 = maquette)
    void askPresetName();
    void chooseFolder (bool library);    // dossier des captures (bibliotheque) ou des IR
    void openLibrary (int slot);         // outil : ouvre la bibliotheque
    void showStatus (const juce::String& msg);   // message temporaire dans l'ecran

private:
    friend class AmpireContent;
    void useSoftwareRenderer();
    void checkRenderer();
    void resetRenderer();
    void* lastPeer = nullptr;
    juce::uint32 lastRendererCheck = 0;
    AmpireProcessor& proc;
    std::unique_ptr<AmpireContent> content;
    std::unique_ptr<juce::FileChooser> folderChooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpireEditor)
};
