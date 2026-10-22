cmake_minimum_required(VERSION 3.22)

# macOS 11+. Builds for the machine you're on by default; for a universal
# binary pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64". Set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Fractone VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE ---------------------------------------------------------------------
# By default JUCE is downloaded on first configure. To use a local checkout:
#   cmake -B build -DFRACTONE_JUCE_DIR=/path/to/JUCE
set(FRACTONE_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")

if(FRACTONE_JUCE_DIR)
    add_subdirectory(${FRACTONE_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Plugin -------------------------------------------------------------------
# Copies the built plugin into the system plug-in folders after each build:
#   macOS   ~/Library/Audio/Plug-Ins/VST3 (and /Components for AU)
#   Linux   ~/.vst3
if(WIN32)
    set(_fractone_copy_default OFF)
else()
    set(_fractone_copy_default ON)
endif()
option(FRACTONE_COPY_PLUGIN "Install the plugin into the system plug-in folders after building" ${_fractone_copy_default})

set(_fractone_formats VST3 Standalone)
if(APPLE)
    list(APPEND _fractone_formats AU)
endif()

juce_add_plugin(Fractone
    COMPANY_NAME                "Taddei"
    BUNDLE_ID                   com.taddei.fractone
    PLUGIN_MANUFACTURER_CODE    Tadd
    PLUGIN_CODE                 Frct
    PRODUCT_NAME                "Fractone"
    FORMATS                     ${_fractone_formats}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD     ${FRACTONE_COPY_PLUGIN})

set(FRACTONE_SOURCES
    Source/Engine/Engine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PresetManager.cpp
    Source/UI/Widgets.cpp
    Source/UI/Patchbay.cpp)

target_sources(Fractone PRIVATE ${FRACTONE_SOURCES})
target_include_directories(Fractone PRIVATE Source)

target_compile_definitions(Fractone PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Fractone
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Engine tests (no JUCE needed) --------------------------------------------
enable_testing()
add_executable(FractoneEngineTests Tests/EngineTests.cpp Source/Engine/Engine.cpp)
target_include_directories(FractoneEngineTests PRIVATE Source)
add_test(NAME engine COMMAND FractoneEngineTests)

# --- Optional dev tool: render the panel to a PNG (headless) ------------------
option(FRACTONE_BUILD_TOOLS "Build the panel snapshot tool" OFF)
if(FRACTONE_BUILD_TOOLS)
    juce_add_console_app(FractoneSnapshot PRODUCT_NAME "FractoneSnapshot")
    target_sources(FractoneSnapshot PRIVATE Tools/Snapshot.cpp ${FRACTONE_SOURCES})
    target_include_directories(FractoneSnapshot PRIVATE Source)
    target_compile_definitions(FractoneSnapshot PRIVATE
        JucePlugin_Name="Fractone"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(FractoneSnapshot PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
