# Offline JUCE-level smoke tests for the TOPOS plugin wrapper.
#
#   smoke    - loads every registered engine into every slot with randomised
#              parameters, checks finite output, exercises the 4x oversampling
#              switch, the mono bus layout, and an APVTS state round-trip.
#   guishot  - renders the editor headless at several sizes and writes PNGs, so
#              layout clipping and overlap can be checked without a DAW.
#
# Build (from this directory):
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DJUCE_PATH=../../../build_deps/JUCE -DTOPOS_ROOT=../..
#   cmake --build build -j2
#   ./build/smoke_artefacts/Release/smoke
#
cmake_minimum_required(VERSION 3.22)
project(ToposSmoke VERSION 1.0.0 LANGUAGES C CXX)
set(JUCE_PATH "../../../build_deps/JUCE" CACHE PATH "path to JUCE")
set(TOPOS_ROOT "../.." CACHE PATH "path to the TOPOS repository root")
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
juce_add_console_app(smoke PRODUCT_NAME smoke)
target_sources(smoke PRIVATE main.cpp
  ${TOPOS_ROOT}/Source/PluginProcessor.cpp
  ${TOPOS_ROOT}/Source/PluginEditor.cpp)
target_include_directories(smoke PRIVATE ${TOPOS_ROOT} ${TOPOS_ROOT}/Source)
target_compile_definitions(smoke PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
  JucePlugin_Name="TOPOS" JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(smoke PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags)
juce_add_console_app(guishot PRODUCT_NAME guishot)
target_sources(guishot PRIVATE gui.cpp
  ${TOPOS_ROOT}/Source/PluginProcessor.cpp
  ${TOPOS_ROOT}/Source/PluginEditor.cpp)
target_include_directories(guishot PRIVATE ${TOPOS_ROOT} ${TOPOS_ROOT}/Source)
target_compile_definitions(guishot PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
  JucePlugin_Name="TOPOS" JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(guishot PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics
  PUBLIC juce::juce_recommended_config_flags)

# fixcheck - regression checks for the knob-mapping / gain-staging fixes
juce_add_console_app(fixcheck PRODUCT_NAME fixcheck)
target_sources(fixcheck PRIVATE fixes.cpp
  ${TOPOS_ROOT}/Source/PluginProcessor.cpp
  ${TOPOS_ROOT}/Source/PluginEditor.cpp)
target_include_directories(fixcheck PRIVATE ${TOPOS_ROOT} ${TOPOS_ROOT}/Source)
target_compile_definitions(fixcheck PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
  JucePlugin_Name="TOPOS" JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(fixcheck PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics
  PUBLIC juce::juce_recommended_config_flags)
