cmake_minimum_required(VERSION 3.25)
project(PitchPaint VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW TRUE
    GIT_CONFIG core.longpaths=true) # JUCE's examples exceed Windows' 260-char path limit in deep build folders
FetchContent_MakeAvailable(JUCE)

# JUCE-free core: pattern model + MIDI renderer, shared by the plugin and the tests.
add_library(PitchPaintCore STATIC
    Source/Core/Pattern.cpp
    Source/Core/MidiRenderer.cpp
    Source/Core/PreviewSynth.cpp
    Source/Core/Tuning.cpp)
target_include_directories(PitchPaintCore PUBLIC Source)
if(MSVC)
    target_compile_options(PitchPaintCore PRIVATE /W4)
endif()

enable_testing()
add_executable(CoreTests Tests/CoreTests.cpp)
target_link_libraries(CoreTests PRIVATE PitchPaintCore)
add_test(NAME CoreTests COMMAND CoreTests)

juce_add_plugin(PitchPaint
    COMPANY_NAME "PitchPaint"
    PLUGIN_MANUFACTURER_CODE Ptpt
    PLUGIN_CODE Ptp1
    FORMATS VST3
    PRODUCT_NAME "PitchPaint"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    VST3_CATEGORIES Instrument Generator
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PitchPaint PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PitchCanvas.cpp
    Source/LookAndFeel.cpp
    Source/VibratoPanel.cpp
    Source/DynamicsLane.cpp)

target_compile_definitions(PitchPaint PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PitchPaint
    PRIVATE PitchPaintCore juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Offscreen editor test: the real processor + editor driven by synthetic mouse events, outside any host.
juce_add_console_app(EditorTests PRODUCT_NAME "EditorTests")
target_sources(EditorTests PRIVATE
    Tests/EditorTests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PitchCanvas.cpp
    Source/LookAndFeel.cpp
    Source/VibratoPanel.cpp
    Source/DynamicsLane.cpp)
target_compile_definitions(EditorTests PRIVATE
    JucePlugin_Name="PitchPaint"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(EditorTests PRIVATE
    PitchPaintCore juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME EditorTests COMMAND EditorTests)
