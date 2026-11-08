cmake_minimum_required(VERSION 3.22)
project(Stack5150 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Stack5150
    COMPANY_NAME "Stack 5150"
    PLUGIN_MANUFACTURER_CODE S5cc
    PLUGIN_CODE S5am
    FORMATS VST3 Standalone
    PRODUCT_NAME "Stack 5150"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Stack5150 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AmpFace.cpp)

target_compile_definitions(Stack5150 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Stack5150
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

add_executable(ModelTest Tests/ModelTest.cpp)
target_include_directories(ModelTest PRIVATE Source)

juce_add_console_app(IrTest PRODUCT_NAME "IrTest")
target_sources(IrTest PRIVATE Tests/IrTest.cpp)
target_include_directories(IrTest PRIVATE Source)
target_compile_definitions(IrTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(IrTest PRIVATE juce::juce_audio_formats juce::juce_dsp)

juce_add_console_app(PresetTest PRODUCT_NAME "PresetTest")
target_sources(PresetTest PRIVATE Tests/PresetTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/AmpFace.cpp)
target_include_directories(PresetTest PRIVATE Source)
target_compile_definitions(PresetTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(PresetTest PRIVATE juce::juce_audio_utils juce::juce_dsp)
