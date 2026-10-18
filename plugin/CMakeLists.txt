# Selva1 · the plugin (VST3, AU, Standalone) with JUCE 8.
#   cmake -B build -S plugin -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release
# The panel (dist/plugin/index.html) is built first by tools/build.py; the engine is core/selva1.c.
# JUCE comes from GitHub, or from a local copy with -DSELVA1_JUCE_DIR=/path/to/JUCE.
# Copyright (C) 2026 DeAbrew. GPL-3.0-or-later.
cmake_minimum_required(VERSION 3.22)
project(Selva1 VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(SELVA1_JUCE_DIR "" CACHE PATH "a local JUCE (else fetched)")
if(SELVA1_JUCE_DIR)
    add_subdirectory(${SELVA1_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SELVA1_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SELVA1_FORMATS AU)
endif()

juce_add_plugin(Selva1
    PRODUCT_NAME "Selva1"
    COMPANY_NAME "DeAbrew"
    COMPANY_WEBSITE "https://deabrewmusic.com.br"
    BUNDLE_ID "br.com.deabrewmusic.selva1"
    PLUGIN_MANUFACTURER_CODE Deab
    PLUGIN_CODE Slv1
    DESCRIPTION "Selva1: urban synthesis system"
    FORMATS ${SELVA1_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

set(SELVA1_PANEL ${CMAKE_CURRENT_SOURCE_DIR}/../dist/plugin/index.html)
if(NOT EXISTS ${SELVA1_PANEL})
    message(FATAL_ERROR "No panel: run python3 tools/build.py first (it writes dist/plugin/index.html)")
endif()
juce_add_binary_data(Selva1Panel SOURCES ${SELVA1_PANEL})

target_sources(Selva1 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/engine.c)
set_source_files_properties(src/engine.c PROPERTIES COMPILE_DEFINITIONS "S1_HOST=1;S1_THREADS=1;S1_NO_DEFAULT=1")
if(MSVC)
    set_source_files_properties(src/engine.c PROPERTIES COMPILE_OPTIONS "/std:c11")
endif()

target_compile_definitions(Selva1 PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)

target_link_libraries(Selva1
    PRIVATE
        Selva1Panel
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
