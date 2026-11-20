cmake_minimum_required(VERSION 3.22)

# MBStudio91 ICX7 — sintetizzatore FM a 6 operatori (VST3 / AU / Standalone)
# Motore: dx7-core di Kenji Noguchi (Rust, Apache 2.0, https://github.com/knoguchi/dx7), collegato in C.
# Progetto distribuito sotto AGPL v3 (vedi LICENSE.md); il motore in engine/ resta Apache 2.0 (vedi engine/dx7-core/NOTICE).
project(MBStudio91ICX7 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    endif()
endif()

set(JUCE_DIR "" CACHE PATH "Percorso di una copia locale di JUCE")
if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

include(cmake/RustEngine.cmake)

set(ICX7_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ICX7_FORMATS AU)
endif()

juce_add_plugin(MBStudio91ICX7
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/icons/icx7-1024.png"
    COMPANY_NAME "MBStudio91"
    COMPANY_WEBSITE "https://mbstudio91.fun"
    BUNDLE_ID "fun.mbstudio91.icx7"
    PRODUCT_NAME "MBStudio91 ICX7"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Mbst
    PLUGIN_CODE Micx
    FORMATS ${ICX7_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    HARDENED_RUNTIME_ENABLED TRUE)

include(MBStudioUI/MBStudioUI.cmake)
mbstudio_ui_add(MBStudio91ICX7)

juce_add_binary_data(MBStudio91ICX7_Banks
    NAMESPACE ICX7Banks
    HEADER_NAME ICX7Banks.h
    SOURCES
        resources/banks/rom1a.syx resources/banks/rom1b.syx
        resources/banks/rom2a.syx resources/banks/rom2b.syx
        resources/banks/rom3a.syx resources/banks/rom3b.syx
        resources/banks/rom4a.syx resources/banks/rom4b.syx)

target_sources(MBStudio91ICX7 PRIVATE
    Source/Dx7Voice.cpp
    Source/EngineHost.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

if(TARGET MBStudio91ICX7_Standalone)
    target_sources(MBStudio91ICX7_Standalone PRIVATE Source/StandaloneApp.cpp)
    target_compile_definitions(MBStudio91ICX7_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
endif()

target_compile_definitions(MBStudio91ICX7 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(MBStudio91ICX7
    PRIVATE
        MBStudio91ICX7_Banks
        juce::juce_audio_utils
        juce::juce_dsp
        icx7_engine
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
add_dependencies(MBStudio91ICX7 icx7_engine_build)
