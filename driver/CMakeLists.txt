# Spacer.driver: CoreAudio AudioServerPlugIn bundle (SPEC §14), macOS only.
# Depends on nothing in-repo except the header-only protocol
# (spacer_vdev_protocol); no JUCE, no spacer_core.

add_library(SpacerDriver MODULE ${CMAKE_CURRENT_SOURCE_DIR}/src/SpacerDriver.cpp)
set_target_properties(SpacerDriver PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION driver
    OUTPUT_NAME Spacer
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER org.spacer.driver
    MACOSX_BUNDLE_EXECUTABLE_NAME Spacer
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    # Built into <build>/driver/Spacer.driver so the app can embed it.
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(SpacerDriver PRIVATE SPACER_DRIVER_VERSION="${PROJECT_VERSION}")
target_link_libraries(SpacerDriver PRIVATE
    spacer_vdev_protocol
    "-framework CoreAudio"
    "-framework CoreFoundation")
target_compile_options(SpacerDriver PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)

# Path of the bundle for the app (embedding) and the tests (in-process harness).
set(SPACER_DRIVER_BUNDLE_DIR ${CMAKE_CURRENT_BINARY_DIR}/Spacer.driver CACHE INTERNAL "")
