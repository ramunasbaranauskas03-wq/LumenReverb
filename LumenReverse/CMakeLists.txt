cmake_minimum_required(VERSION 3.22)
project(LumenReverse VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt)
  add_subdirectory(JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(LumenReverse
    COMPANY_NAME "Lumen Audio"
    BUNDLE_ID com.lumenaudio.reverse
    PLUGIN_MANUFACTURER_CODE Lmna
    PLUGIN_CODE LmRv
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Lumen Reverse"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)

juce_generate_juce_header(LumenReverse)

target_sources(LumenReverse PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ReverseEngine.cpp)

target_compile_definitions(LumenReverse PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(LumenReverse
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(LUMEN_BUILD_TESTS "Build offline test" OFF)
if(LUMEN_BUILD_TESTS)
  juce_add_console_app(LumenReverseTest PRODUCT_NAME "LumenReverseTest")
  juce_generate_juce_header(LumenReverseTest)
  target_sources(LumenReverseTest PRIVATE Tests/TestMain.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/ReverseEngine.cpp)
  target_compile_definitions(LumenReverseTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(LumenReverseTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
