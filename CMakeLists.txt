cmake_minimum_required(VERSION 3.22)

# Universal binary, set before project() so it applies to every target.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(SPXAmbience VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SPX_BUILD_PLUGIN "Build the AU/VST3/Standalone plugin (requires JUCE)" ON)
option(SPX_BUILD_TESTS  "Build the standalone DSP measurement harness" ON)
option(SPX_BUILD_PREVIEW "Build the offscreen editor screenshot tool" OFF)
option(SPX_BUILD_RENDER "Build the offline WAV renderer" ON)

#==============================================================================
# The reverb core has no JUCE dependency, so it can be built and measured on
# its own. The test target below does exactly that.
#==============================================================================
if(SPX_BUILD_TESTS)
    add_executable(SPXAmbienceDspTest
        tools/DspTest.cpp
        Source/dsp/AmbienceEngine.cpp)

    target_include_directories(SPXAmbienceDspTest PRIVATE Source/dsp)

    if(NOT MSVC)
        target_compile_options(SPXAmbienceDspTest PRIVATE -Wall -Wextra)
    endif()

    add_executable(SPXAmbienceDspBench
        tools/DspBench.cpp
        Source/dsp/AmbienceEngine.cpp)

    target_include_directories(SPXAmbienceDspBench PRIVATE Source/dsp)

    if(NOT MSVC)
        target_compile_options(SPXAmbienceDspBench PRIVATE -Wall -Wextra)
    endif()

    enable_testing()
    add_test(NAME dsp COMMAND SPXAmbienceDspTest)
endif()

#==============================================================================
# Plugin
#==============================================================================
if(NOT SPX_BUILD_PLUGIN)
    return()
endif()

# Point CMake at a local JUCE checkout with -DJUCE_PATH=/path/to/JUCE to skip
# the download.
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(SPXAmbience
    PRODUCT_NAME                "SPX Ambience"
    COMPANY_NAME                "Connor Corwin"
    BUNDLE_ID                   com.connorcorwin.spxambience
    PLUGIN_MANUFACTURER_CODE    Ccor
    PLUGIN_CODE                 Spxa
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Reverb
    MICROPHONE_PERMISSION_ENABLED TRUE)

target_sources(SPXAmbience PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/gui/AmbienceLookAndFeel.cpp
    Source/dsp/AmbienceEngine.cpp)

target_include_directories(SPXAmbience PRIVATE Source)

target_compile_definitions(SPXAmbience PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(SPXAmbience
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Offline WAV renderer. Needs JUCE only for file I/O - the reverb it runs is
# the same engine the plugin uses, driven from the same preset table.
#==============================================================================
if(SPX_BUILD_RENDER)
    juce_add_console_app(SPXAmbienceRender PRODUCT_NAME "SPXAmbienceRender")

    target_sources(SPXAmbienceRender PRIVATE
        tools/RenderFile.cpp
        Source/dsp/AmbienceEngine.cpp)

    target_include_directories(SPXAmbienceRender PRIVATE Source)

    target_compile_definitions(SPXAmbienceRender PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_OGGVORBIS=0
        JUCE_USE_FLAC=0)

    target_link_libraries(SPXAmbienceRender
        PRIVATE
            juce::juce_audio_formats
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

#==============================================================================
# Offscreen editor renderer, used to regenerate the README screenshot. Renders
# without a host or a display.
#==============================================================================
if(SPX_BUILD_PREVIEW)
    juce_add_console_app(SPXAmbiencePreview PRODUCT_NAME "SPX Ambience Preview")

    target_sources(SPXAmbiencePreview PRIVATE
        tools/PreviewRender.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/gui/AmbienceLookAndFeel.cpp
        Source/dsp/AmbienceEngine.cpp)

    target_include_directories(SPXAmbiencePreview PRIVATE Source)

    # The editor sources reference the JucePlugin_* definitions that
    # juce_add_plugin normally supplies, so borrow them from the plugin target.
    # JUCE_STANDALONE_APPLICATION is dropped: the console app sets its own.
    get_target_property(spx_plugin_defs SPXAmbience INTERFACE_COMPILE_DEFINITIONS)
    list(FILTER spx_plugin_defs EXCLUDE REGEX "^JUCE_STANDALONE_APPLICATION")
    target_compile_definitions(SPXAmbiencePreview PRIVATE ${spx_plugin_defs})

    target_link_libraries(SPXAmbiencePreview
        PRIVATE
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
