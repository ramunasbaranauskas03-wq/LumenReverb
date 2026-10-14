cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
project(LumenDelay VERSION 1.0.0 LANGUAGES C CXX)

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

juce_add_plugin(LumenDelay
    COMPANY_NAME                "Lumen Audio"
    BUNDLE_ID                   com.lumenaudio.lumendelay
    PLUGIN_MANUFACTURER_CODE    Lmna
    PLUGIN_CODE                 Lmdl
    FORMATS                     ${LUMEN_FORMATS}
    PRODUCT_NAME                "Lumen Delay"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Delay
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(LumenDelay PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(LumenDelay PRIVATE Source)

target_compile_definitions(LumenDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(LumenDelay
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
