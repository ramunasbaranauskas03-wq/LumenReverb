cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
project(LumenKick VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

set(LUMEN_FORMATS VST3)
if(APPLE)
    list(APPEND LUMEN_FORMATS AU)
endif()

juce_add_plugin(LumenKick
    COMPANY_NAME                "Lumen Audio"
    BUNDLE_ID                   com.lumenaudio.lumenkick
    PLUGIN_MANUFACTURER_CODE    Lmna
    PLUGIN_CODE                 Lmkd
    FORMATS                     ${LUMEN_FORMATS}
    PRODUCT_NAME                "Lumen Kick Designer"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Drum
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(LumenKick PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(LumenKick PRIVATE Source)

target_compile_definitions(LumenKick PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    _USE_MATH_DEFINES=1)

if (MSVC)
    target_compile_options(LumenKick PRIVATE /bigobj /utf-8)
endif()

target_link_libraries(LumenKick
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
