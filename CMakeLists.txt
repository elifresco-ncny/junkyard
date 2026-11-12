cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
include(VersionNumber)
project(Junkyard VERSION ${JUNKYARD_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /bigobj)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Version)
include(Dependencies)

# ---------------------------------------------------------------- DSP (no UI deps)
file(GLOB DSP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/dsp/*.cpp)
add_library(junkyard_dsp STATIC ${DSP_SOURCES})
target_include_directories(junkyard_dsp PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(junkyard_dsp PUBLIC
  juce::juce_dsp
  PRIVATE juce::juce_recommended_config_flags)
target_compile_definitions(junkyard_dsp PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
if(NOT MSVC)
  target_compile_options(junkyard_dsp PRIVATE -O3 -fno-math-errno)
endif()

# ---------------------------------------------------------------- Binary data
file(GLOB PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/presets/factory/*.xml)
file(GLOB FONT_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/fonts/*.ttf)
if(PRESET_FILES)
  set(JUNKYARD_HAVE_PRESETS 1)
else()
  set(JUNKYARD_HAVE_PRESETS 0)
endif()
if(FONT_FILES)
  set(JUNKYARD_HAVE_FONTS 1)
else()
  set(JUNKYARD_HAVE_FONTS 0)
endif()
if(PRESET_FILES OR FONT_FILES)
  juce_add_binary_data(junkyard_data SOURCES ${PRESET_FILES} ${FONT_FILES})
  set_target_properties(junkyard_data PROPERTIES POSITION_INDEPENDENT_CODE ON)
else()
  add_library(junkyard_data INTERFACE)
endif()

# ---------------------------------------------------------------- Shared plugin code (processor, state, UI)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/ui/*.cpp)
list(FILTER CORE_SOURCES EXCLUDE REGEX "PluginEntry\\.cpp$")
add_library(junkyard_core STATIC ${CORE_SOURCES})
target_include_directories(junkyard_core PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(junkyard_core PUBLIC
  junkyard_dsp junkyard_data
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
  PRIVATE juce::juce_recommended_config_flags)
target_compile_definitions(junkyard_core PUBLIC JUNKYARD_HAVE_FONTS=${JUNKYARD_HAVE_FONTS} JUNKYARD_HAVE_PRESETS=${JUNKYARD_HAVE_PRESETS})
set_target_properties(junkyard_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(junkyard_core PRIVATE -O3)
endif()

# ---------------------------------------------------------------- The plugin
juce_add_plugin(Junkyard
  COMPANY_NAME "Junkyard Audio"
  BUNDLE_ID com.junkyardaudio.junkyard
  PLUGIN_MANUFACTURER_CODE Jnky
  PLUGIN_CODE Jky1
  PRODUCT_NAME "Junkyard"
  FORMATS ${JUNKYARD_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  VST3_CATEGORIES Fx Distortion
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD FALSE
  VERSION ${JUNKYARD_VERSION})
target_sources(Junkyard PRIVATE src/PluginEntry.cpp)
target_compile_definitions(Junkyard PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(Junkyard PRIVATE junkyard_core
  PUBLIC juce::juce_recommended_config_flags)

# ---------------------------------------------------------------- Tests & tools
enable_testing()
if(NOT JUNKYARD_NO_TESTS)
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
  add_executable(junkyard_tests ${TEST_SOURCES})
  target_link_libraries(junkyard_tests PRIVATE junkyard_core Catch2::Catch2WithMain juce::juce_recommended_config_flags)
  target_compile_definitions(junkyard_tests PRIVATE JUNKYARD_FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  include(CTest)
  include(Catch)
  catch_discover_tests(junkyard_tests DISCOVERY_MODE PRE_TEST)
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/render/render.cpp)
    add_executable(junkyard_render tools/render/render.cpp)
    target_link_libraries(junkyard_render PRIVATE junkyard_core juce::juce_recommended_config_flags)
  endif()
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/shot/shot.cpp)
    add_executable(junkyard_shot tools/shot/shot.cpp)
    target_link_libraries(junkyard_shot PRIVATE junkyard_core juce::juce_recommended_config_flags)
  endif()
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/soak/soak.cpp AND APPLE)
    add_executable(junkyard_soak tools/soak/soak.cpp)
    target_link_libraries(junkyard_soak PRIVATE junkyard_core juce::juce_recommended_config_flags)
  endif()
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/calibrate/calibrate.cpp)
    add_executable(junkyard_calibrate tools/calibrate/calibrate.cpp)
    target_link_libraries(junkyard_calibrate PRIVATE junkyard_dsp juce::juce_recommended_config_flags)
    target_compile_definitions(junkyard_calibrate PRIVATE JUNKYARD_ROOT="${CMAKE_SOURCE_DIR}")
  endif()
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/bench/bench.cpp)
    add_executable(junkyard_bench tools/bench/bench.cpp)
    target_link_libraries(junkyard_bench PRIVATE junkyard_core juce::juce_recommended_config_flags)
  endif()
endif()
