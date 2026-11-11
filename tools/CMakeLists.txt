# Outil Linux (hors build Windows / macOS) : captures de l'interface et tests audio hors ligne.
juce_add_console_app(AmpireTool PRODUCT_NAME "AmpireTool")
target_sources(AmpireTool PRIVATE
  AmpireTool.cpp
  ../PluginProcessor.cpp
  ../PluginEditor.cpp
  ../NamEngine.cpp
  ../CaptureLibrary.cpp
  ../Tone3000.cpp)
target_include_directories(AmpireTool PRIVATE .. ${JUCE_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK)
target_compile_definitions(AmpireTool PRIVATE
  JucePlugin_Name="Ampire"
  JucePlugin_VersionString="${PROJECT_VERSION}"
  JUCE_STANDALONE_APPLICATION=1
  AMPIRE_TOOL=1
  JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_ALSA=0 JUCE_JACK=0
  JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0)
target_link_libraries(AmpireTool PRIVATE
  AmpireNam
  AmpireAssets
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_cryptography
  juce::juce_recommended_config_flags)
