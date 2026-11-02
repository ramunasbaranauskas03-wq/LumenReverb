cmake_minimum_required(VERSION 3.22)
project(LumenSynthXL VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LUMEN_BUILD_TESTS "Build offline render/screenshot test tool" OFF)

# JUCE: use a local copy if present, otherwise download it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(LUMEN_FORMATS VST3 AU Standalone)
else()
    set(LUMEN_FORMATS VST3 Standalone)
endif()

juce_add_plugin(LumenSynthXL
    COMPANY_NAME "Lumen Audio"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.lumenaudio.synthxl"
    PLUGIN_MANUFACTURER_CODE LumA
    PLUGIN_CODE LmXL
    FORMATS ${LUMEN_FORMATS}
    PRODUCT_NAME "Lumen Synth XL"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

file(GLOB LUMEN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/UI/*.cpp")

target_sources(LumenSynthXL PRIVATE ${LUMEN_SOURCES})

set(LUMEN_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(LumenSynthXL PUBLIC ${LUMEN_DEFS})

set(LUMEN_LIBS
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp)

target_link_libraries(LumenSynthXL
    PRIVATE ${LUMEN_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(LUMEN_BUILD_TESTS)
    juce_add_console_app(LumenXLTest PRODUCT_NAME "LumenXLTest")
    target_sources(LumenXLTest PRIVATE ${LUMEN_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/Tests/TestMain.cpp")
    target_compile_definitions(LumenXLTest PRIVATE ${LUMEN_DEFS} LUMEN_TEST_BUILD=1
        JucePlugin_Name="Lumen Synth XL" JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(LumenXLTest PRIVATE ${LUMEN_LIBS} juce::juce_recommended_config_flags)
endif()
