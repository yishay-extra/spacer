cmake_minimum_required(VERSION 3.25)
project(Spacer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

option(SPACER_BUILD_APP   "Build the JUCE desktop application (macOS only)" ON)
option(SPACER_BUILD_TESTS "Build unit tests" ON)
option(SPACER_BUILD_ENGINE_TESTS "Build the JUCE engine tests (fetches JUCE on every platform)" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# Third-party: nlohmann/json (core, header-only)
# ---------------------------------------------------------------------------
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# spacer_core: pure C++20, NO JUCE. Model, maths, spatializers, motion, clock,
# project file. Everything here must be testable on any platform.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SPACER_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h)

add_library(spacer_core STATIC ${SPACER_CORE_SOURCES})
target_include_directories(spacer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(spacer_core PUBLIC nlohmann_json::nlohmann_json)
include(cmake/EmbedLayoutPresets.cmake)
spacer_embed_layout_presets(spacer_core ${CMAKE_CURRENT_SOURCE_DIR}/resources/layouts)
target_compile_options(spacer_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion>)

# ---------------------------------------------------------------------------
# spacer_ui_logic: the testable part of the UI (src/ui/logic) — stage
# transforms, hit-testing, layout-editor operations, undo history, engine
# sync planning. Plain C++20, NO JUCE (it may include the JUCE-free engine
# headers Command.h / Feedback.h). Linked by the app and the core tests.
# ---------------------------------------------------------------------------
file(GLOB SPACER_UI_LOGIC_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/logic/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/logic/*.h)
add_library(spacer_ui_logic STATIC ${SPACER_UI_LOGIC_SOURCES})
target_link_libraries(spacer_ui_logic PUBLIC spacer_core)
target_compile_options(spacer_ui_logic PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion>)

# ---------------------------------------------------------------------------
# JUCE: needed by the app (macOS) and by the engine tests (any platform). Off
# macOS only the modules are configured (no juceaide, so no GUI deps needed).
# ---------------------------------------------------------------------------
set(SPACER_WITH_APP OFF)
if(SPACER_BUILD_APP AND APPLE)
    set(SPACER_WITH_APP ON)
endif()
set(SPACER_WITH_ENGINE_TESTS OFF)
if(SPACER_BUILD_TESTS AND SPACER_BUILD_ENGINE_TESTS)
    set(SPACER_WITH_ENGINE_TESTS ON)
endif()

if(SPACER_WITH_APP OR SPACER_WITH_ENGINE_TESTS)
    if(NOT SPACER_WITH_APP)
        set(JUCE_MODULES_ONLY ON CACHE BOOL "Only configure the JUCE modules" FORCE)
    endif()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE
        SYSTEM)
    FetchContent_MakeAvailable(JUCE)
endif()

# Engine sources. src/engine/*.cpp is portable JUCE code (no device I/O) and
# is also built into the engine tests; src/engine/device/ needs
# juce_audio_devices / CoreAudio and is built into the app only.
file(GLOB SPACER_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.h)
# Portable control code (cue engine, installation mode): no JUCE types, built
# into the engine tests too. The app globs src/control itself.
file(GLOB SPACER_CONTROL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/control/*.h)
set(SPACER_ENGINE_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# ---------------------------------------------------------------------------
# Spacer app: JUCE. macOS only for phase 1.
# ---------------------------------------------------------------------------
if(SPACER_WITH_APP)
    juce_add_gui_app(Spacer
        PRODUCT_NAME        "Spacer"
        COMPANY_NAME        "Spacer"
        BUNDLE_ID           "org.spacer.app"
        VERSION             ${PROJECT_VERSION}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Spacer needs audio input access to receive audio from devices.")

    juce_generate_juce_header(Spacer)

    file(GLOB_RECURSE SPACER_APP_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.h)
    # src/ui/logic is the separate spacer_ui_logic library.
    list(FILTER SPACER_APP_SOURCES EXCLUDE REGEX "/src/ui/logic/")
    target_sources(Spacer PRIVATE ${SPACER_APP_SOURCES})

    target_compile_definitions(Spacer PRIVATE
        ${SPACER_ENGINE_JUCE_DEFINITIONS}
        $<$<CONFIG:Debug>:SPACER_ALLOCATION_GUARD=1>
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Spacer,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Spacer,JUCE_VERSION>")

    target_link_libraries(Spacer
        PRIVATE
            spacer_ui_logic  # brings spacer_core (listing both makes Apple ld warn)
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_osc
            juce::juce_gui_extra
            "-framework CoreAudio"
            "-framework CoreFoundation"
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # spacer-engine-check: CLI that drives the real AudioEngine on a real
    # device (manual validation, docs/mac-validation/WP3.md).
    file(GLOB SPACER_ENGINE_DEVICE_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/device/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/device/*.h)
    juce_add_console_app(spacer_engine_check PRODUCT_NAME "spacer-engine-check")
    target_sources(spacer_engine_check PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/EngineCheck.cpp
        ${SPACER_ENGINE_SOURCES}
        ${SPACER_ENGINE_DEVICE_SOURCES})
    target_include_directories(spacer_engine_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(spacer_engine_check PRIVATE
        ${SPACER_ENGINE_JUCE_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1
        SPACER_ALLOCATION_GUARD=1)
    target_link_libraries(spacer_engine_check
        PRIVATE
            spacer_core
            juce::juce_audio_devices
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_events
            "-framework CoreAudio"
            "-framework CoreFoundation"
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
elseif(SPACER_BUILD_APP)
    message(STATUS "Spacer app is macOS-only; building spacer_core and tests only.")
endif()

# ---------------------------------------------------------------------------
# Tests (Catch2 v3), core only — run anywhere.
# ---------------------------------------------------------------------------
if(SPACER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
