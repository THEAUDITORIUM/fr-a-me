cmake_minimum_required(VERSION 3.22)

project(SYNC VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- JUCE ---------------------------------------------------------------------
# Use a local checkout if JUCE_DIR is given, otherwise fetch it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

option(SYNC_BUILD_TESTS "Build the offline engine test" OFF)
option(SYNC_LINUX_HEADLESS "Linux build without ALSA/Xrandr dev headers (CI/dev only)" OFF)

set(SYNC_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SYNC_FORMATS AU)
endif()

juce_add_plugin(SYNC
    PRODUCT_NAME                "SYNC"
    COMPANY_NAME                "Offkey555"
    BUNDLE_ID                   com.offkey555.sync
    PLUGIN_MANUFACTURER_CODE    Ofk5
    PLUGIN_CODE                 Sync
    FORMATS                     ${SYNC_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Instrument Sampler
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD     FALSE)

juce_add_binary_data(SyncData
    NAMESPACE BinaryData
    SOURCES
        Resources/Inter-Regular.ttf
        Resources/Inter-Medium.ttf)

target_sources(SYNC PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WarpEngine.cpp)

target_compile_definitions(SYNC PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1)

if(SYNC_LINUX_HEADLESS)
    target_compile_definitions(SYNC PUBLIC
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0)
endif()

target_link_libraries(SYNC
    PRIVATE
        SyncData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- offline engine test ------------------------------------------------------
if(SYNC_BUILD_TESTS)
    juce_add_console_app(SyncEngineTest PRODUCT_NAME "SyncEngineTest")
    target_sources(SyncEngineTest PRIVATE tests/EngineTest.cpp Source/WarpEngine.cpp)
    target_compile_definitions(SyncEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(SyncEngineTest PRIVATE
        juce::juce_audio_formats juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()

if(SYNC_BUILD_TESTS)
    juce_add_console_app(SyncSnapshot PRODUCT_NAME "SyncSnapshot")
    target_sources(SyncSnapshot PRIVATE tests/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/WarpEngine.cpp)
    target_compile_definitions(SyncSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0
        JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="SYNC")
    target_link_libraries(SyncSnapshot PRIVATE SyncData juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()
