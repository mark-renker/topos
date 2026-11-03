cmake_minimum_required(VERSION 3.22)

project(TOPOS VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE (already cloned next to this project; overridable)
# ---------------------------------------------------------------------------
set(TOPOS_JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../build_deps/JUCE"
    CACHE PATH "Path to the JUCE source tree")

if(NOT EXISTS "${TOPOS_JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${TOPOS_JUCE_PATH}")
endif()

add_subdirectory("${TOPOS_JUCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# plugin target
# ---------------------------------------------------------------------------
set(TOPOS_FORMATS VST3)
if(APPLE)
    list(APPEND TOPOS_FORMATS AU)   # AU can only be built on macOS
endif()

juce_add_plugin(TOPOS
    COMPANY_NAME                "Renky Denk Audio"
    PRODUCT_NAME                "TOPOS"
    PLUGIN_MANUFACTURER_CODE    Rdka
    PLUGIN_CODE                 Tpos
    BUNDLE_ID                   com.renkydenkaudio.topos
    DESCRIPTION                 "TOPOS - Modular Filter Matrix"
    FORMATS                     ${TOPOS_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Filter
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(TOPOS PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# the framework-free DSP core lives in dsp/ and is included as "dsp/..."
target_include_directories(TOPOS PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_compile_definitions(TOPOS PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(TOPOS PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
