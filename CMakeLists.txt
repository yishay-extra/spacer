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
# Ableton Link is GPLv2-or-later (or a proprietary licence by agreement with
# Ableton). That is not compatible with shipping Spacer closed-source under a
# commercial JUCE licence (docs/DESIGN.md §11.5), so LinkClock is opt-in.
option(SPACER_WITH_LINK "Build LinkClock with Ableton Link (GPLv2+, see WP12 notes)" OFF)

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
# spacer_vdev_protocol: the Spacer.driver <-> app shared-memory ring (SPEC §14).
# Header-only, plain C++20 + POSIX, no JUCE, no spacer_core: used by the driver
# (driver/) and by the engine's VirtualDeviceReceiver, tested on every platform.
# ---------------------------------------------------------------------------
add_library(spacer_vdev_protocol INTERFACE)
target_include_directories(spacer_vdev_protocol INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/driver/protocol)
if(UNIX AND NOT APPLE)
    target_link_libraries(spacer_vdev_protocol INTERFACE rt)  # shm_open on older glibc
endif()

# Spacer.driver (CoreAudio HAL plug-in): macOS only.
if(APPLE)
    add_subdirectory(driver)
endif()

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

# ---------------------------------------------------------------------------
# Ableton Link (optional, header-only + asio): only its CMake config is used,
# Link's own examples / tests are not added to the build.
# ---------------------------------------------------------------------------
set(SPACER_LINK_SOURCES "")
if(SPACER_WITH_LINK)
    FetchContent_Declare(link
        GIT_REPOSITORY https://github.com/Ableton/link.git
        GIT_TAG        Link-3.1.5
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES modules/asio-standalone
        SOURCE_SUBDIR  spacer-do-not-add
        SYSTEM)
    FetchContent_MakeAvailable(link)
    include(${link_SOURCE_DIR}/AbletonLinkConfig.cmake)
    file(GLOB SPACER_LINK_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/link/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/control/link/*.h)
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
    # src/ui/logic is the separate spacer_ui_logic library; src/control/link
    # is only built with SPACER_WITH_LINK.
    list(FILTER SPACER_APP_SOURCES EXCLUDE REGEX "/src/ui/logic/")
    list(FILTER SPACER_APP_SOURCES EXCLUDE REGEX "/src/control/link/")
    target_sources(Spacer PRIVATE ${SPACER_APP_SOURCES} ${SPACER_LINK_SOURCES})
    if(SPACER_WITH_LINK)
        target_compile_definitions(Spacer PRIVATE SPACER_WITH_LINK=1)
        target_link_libraries(Spacer PRIVATE Ableton::Link)
    endif()

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
            spacer_vdev_protocol
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

    # Ship Spacer.driver inside the app for "Install virtual device..." (SPEC §14).
    add_dependencies(Spacer SpacerDriver)
    add_custom_command(TARGET Spacer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_BUNDLE_CONTENT_DIR:Spacer>/Resources/Spacer.driver"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:SpacerDriver>"
                "$<TARGET_BUNDLE_CONTENT_DIR:Spacer>/Resources/Spacer.driver"
        COMMENT "Embedding Spacer.driver in Spacer.app"
        VERBATIM)

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
            spacer_vdev_protocol
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
