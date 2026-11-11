cmake_minimum_required(VERSION 3.22)

# macOS : binaire universel (Apple Silicon + Intel)
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)   # std::filesystem (NAM Core)
endif()

project(WigaAmpire VERSION 1.1.3)

option(AMPIRE_TOOLS "Build the Linux render / test tools" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
if(DEFINED JUCE_LOCAL_DIR)
  add_subdirectory(${JUCE_LOCAL_DIR} juce)
else()
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------- NeuralAmpModelerCore (MIT) + Eigen + AudioDSPTools
# Sous-modules inclus (Eigen, AudioDSPTools). Hors ligne : -DFETCHCONTENT_SOURCE_DIR_NAMCORE=<clone local>
FetchContent_Declare(NamCore
  GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG v0.6.0
  GIT_SHALLOW ON
  GIT_SUBMODULES_RECURSE ON
  SOURCE_SUBDIR "sources-only")       # aucun CMakeLists a cet endroit : sources seulement, outils NAM non construits
FetchContent_MakeAvailable(NamCore)

file(GLOB NAM_SOURCES "${namcore_SOURCE_DIR}/NAM/*.cpp" "${namcore_SOURCE_DIR}/NAM/*/*.cpp")
# bibliotheque OBJET : tous les fichiers sont lies (les architectures s'enregistrent par objets statiques)
# en-tetes et definitions NAM (sans les objets) : pour le code partage du plugin
add_library(AmpireNamHeaders INTERFACE)
target_include_directories(AmpireNamHeaders INTERFACE
  ${namcore_SOURCE_DIR}
  ${namcore_SOURCE_DIR}/NAM
  ${namcore_SOURCE_DIR}/Dependencies/eigen
  ${namcore_SOURCE_DIR}/Dependencies/nlohmann
  ${namcore_SOURCE_DIR}/Dependencies/AudioDSPTools
  ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(AmpireNamHeaders INTERFACE NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
if(MSVC)
  target_compile_definitions(AmpireNamHeaders INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
endif()
add_library(AmpireNam OBJECT ${NAM_SOURCES})
target_link_libraries(AmpireNam PUBLIC AmpireNamHeaders)
set_target_properties(AmpireNam PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(AmpireNam PRIVATE /bigobj /W0 "$<$<CONFIG:Release>:/O2>")
else()
  target_compile_options(AmpireNam PRIVATE -w "$<$<CONFIG:Release>:-O3>")
endif()

# polices Inter (SIL OFL) et logo NINE HANDS | DSP integres au plugin
juce_add_binary_data(AmpireAssets SOURCES
  assets/Inter-SemiBold.otf
  assets/Inter-Bold.otf
  assets/Inter-Medium.otf
  assets/bg.png
  assets/knob_crown.png
  assets/knob_reflect.png
  assets/knob_indicator.png
  assets/switch_up.png
  assets/switch_down.png
  assets/diode_on.png
  assets/diode_off.png
  assets/Poppins-Regular.ttf
  assets/Poppins-Medium.ttf
  assets/Poppins-Bold.ttf
  assets/t3k/T3K_logo.svg
  assets/t3k/TONE3000_logo.svg)

if(APPLE)
  set(AMPIRE_FORMATS VST3 AU)
else()
  set(AMPIRE_FORMATS VST3)
endif()

juce_add_plugin(Ampire
  COMPANY_NAME "NINE HANDS | DSP"
  BUNDLE_ID com.ninehandsdsp.ampire
  PLUGIN_MANUFACTURER_CODE Wiga          # code 4 lettres conserve (ne jamais changer)
  PLUGIN_CODE WgAm
  FORMATS ${AMPIRE_FORMATS}
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx Distortion
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  PRODUCT_NAME "Ampire"
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Ampire PRIVATE
  PluginProcessor.cpp
  PluginEditor.cpp
  NamEngine.cpp
  CaptureLibrary.cpp
  Tone3000.cpp)

# mode stack (NhdStack.h) : interface VST3 de REAPER (funknown.h du SDK VST3 fourni avec JUCE)
target_include_directories(Ampire PRIVATE
  ${JUCE_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK)

target_compile_definitions(Ampire PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Ampire
  PRIVATE
    AmpireNamHeaders
    AmpireAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_cryptography
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# Les architectures NAM (SlimmableContainer, LSTM, ConvNet, Linear, Sequential...) s'enregistrent par objets
# statiques que rien ne reference. Places dans la bibliotheque statique partagee, l'editeur de liens les jetterait
# ("No config parser registered for architecture") : les objets NAM sont donc lies directement dans chaque format.
foreach(fmt IN LISTS AMPIRE_FORMATS)
  target_link_libraries(Ampire_${fmt} PRIVATE AmpireNam)
endforeach()

if(AMPIRE_TOOLS)
  add_subdirectory(tools)
endif()
