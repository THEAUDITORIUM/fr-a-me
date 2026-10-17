# ==============================================================================
#  fr(a)me — harmoniseur spectral
#  Projet CMake (JUCE 8). Formats : VST3, AU (macOS), Standalone.
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(FRAME VERSION 1.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS : binaire universel (Apple Silicon + Intel), macOS 11 minimum.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS minimum")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures" FORCE)
    endif()
endif()

# Windows : runtime statique (le plugin ne dépend pas des DLL du compilateur).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(FRAME_BUILD_TESTS "Compile le banc de test du moteur spectral" OFF)

# ------------------------------------------------------------------------------
# JUCE : utilise le dossier ./JUCE s'il existe, sinon le télécharge (tag 8.0.15).
# ------------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# ------------------------------------------------------------------------------
# Plugin
# ------------------------------------------------------------------------------
juce_add_plugin(Frame
    # Nom affiché dans le DAW : « fr(a)me ». Les fichiers s'appellent « frame »
    # car les parenthèses cassent certaines étapes de build (scripts shell).
    PLUGIN_NAME                  "fr(a)me"
    PRODUCT_NAME                 "frame"
    COMPANY_NAME                 "fr(a)me audio"
    BUNDLE_ID                    "com.frameaudio.frame"
    PLUGIN_MANUFACTURER_CODE     Frma
    PLUGIN_CODE                  Frm1
    FORMATS                      VST3 AU Standalone
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             FALSE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    VST3_CATEGORIES              Fx Pitch Shift Spectral
    AU_MAIN_TYPE                 kAudioUnitType_Effect
    DESCRIPTION                  "spectral harmoniser")

juce_generate_juce_header(Frame)

target_sources(Frame PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SpectralEngine.cpp
    Source/AmbientReverb.cpp
    Source/FrameLookAndFeel.cpp
    Source/Visualisers.cpp
    Source/Controls.cpp)

# Police embarquée (IBM Plex Mono, licence SIL OFL — voir Resources/fonts/OFL.txt)
juce_add_binary_data(FrameBinaryData SOURCES
    Resources/fonts/IBMPlexMono-Light.ttf
    Resources/fonts/IBMPlexMono-Regular.ttf)

target_compile_definitions(Frame PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Frame
    PRIVATE
        FrameBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Banc de test du moteur (optionnel) : cmake -DFRAME_BUILD_TESTS=ON
# ------------------------------------------------------------------------------
if(FRAME_BUILD_TESTS)
    juce_add_console_app(FrameEngineTest PRODUCT_NAME "frame_engine_test")
    target_sources(FrameEngineTest PRIVATE tests/EngineTest.cpp Source/SpectralEngine.cpp Source/AmbientReverb.cpp)
    target_compile_definitions(FrameEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(FrameEngineTest PRIVATE
        juce::juce_dsp juce::juce_audio_formats
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # Capture PNG de l'interface (utilise le code partagé du plugin)
    juce_add_console_app(FrameUiSnapshot PRODUCT_NAME "frame_ui_snapshot")
    target_sources(FrameUiSnapshot PRIVATE tests/UiSnapshot.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/SpectralEngine.cpp Source/AmbientReverb.cpp
        Source/FrameLookAndFeel.cpp Source/Visualisers.cpp Source/Controls.cpp)
    target_compile_definitions(FrameUiSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(FrameUiSnapshot PRIVATE FrameBinaryData
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
