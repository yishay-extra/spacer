FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE
    SYSTEM)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# ---------------------------------------------------------------------------
# Core tests: pure C++, no JUCE.
# ---------------------------------------------------------------------------
file(GLOB SPACER_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
# UI logic tests (src/ui/logic, no JUCE).
file(GLOB SPACER_UI_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp)

add_executable(spacer_tests ${SPACER_TEST_SOURCES} ${SPACER_UI_TEST_SOURCES})
target_include_directories(spacer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(spacer_tests PRIVATE SPACER_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(spacer_tests PRIVATE spacer_ui_logic Catch2::Catch2WithMain)  # + spacer_core
catch_discover_tests(spacer_tests)

# ---------------------------------------------------------------------------
# Engine tests: src/engine (portable part) + JUCE modules, offline only. The
# allocation guard is compiled in (counting mode is selected by the tests).
# ---------------------------------------------------------------------------
if(SPACER_WITH_ENGINE_TESTS)
    file(GLOB SPACER_ENGINE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp)

    add_executable(spacer_engine_tests ${SPACER_ENGINE_TEST_SOURCES} ${SPACER_ENGINE_SOURCES}
        ${SPACER_CONTROL_SOURCES} ${SPACER_LINK_SOURCES})
    if(SPACER_WITH_LINK)
        target_compile_definitions(spacer_engine_tests PRIVATE SPACER_WITH_LINK=1)
        target_link_libraries(spacer_engine_tests PRIVATE Ableton::Link)
    endif()
    target_include_directories(spacer_engine_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(spacer_engine_tests PRIVATE
        ${SPACER_ENGINE_JUCE_DEFINITIONS}
        JUCE_STANDALONE_APPLICATION=1
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        SPACER_ALLOCATION_GUARD=1)
    target_link_libraries(spacer_engine_tests
        PRIVATE
            spacer_core
            Catch2::Catch2WithMain
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_osc
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    # JUCE's warning set applies to the engine sources; tests compare floats
    # exactly on purpose.
    set_source_files_properties(${SPACER_ENGINE_TEST_SOURCES} PROPERTIES COMPILE_OPTIONS
        "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-float-equal>")
    catch_discover_tests(spacer_engine_tests)
endif()
