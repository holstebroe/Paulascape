cmake_minimum_required(VERSION 3.20)
project(Paulascape VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

enable_testing()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/clap/include
)

# Core library sources
set(PAULASCAPE_CORE_SOURCES
    src/core/pt2/pt2_tables.cpp
    src/core/pt2/pt2_font.cpp
    src/core/mod_loader.cpp
    src/core/module_io.cpp
    src/core/paula_voice.cpp
    src/core/blep.cpp
    src/core/rc_filters.cpp
    src/core/voice_pool.cpp
    src/core/replayer.cpp
    src/import/wav_import.cpp
    src/export/midi_export.cpp
    src/export/song_unroll.cpp
    src/clap/paulascape_plugin.cpp
)

# GUI library sources
set(PAULASCAPE_GUI_SOURCES
    src/gui/framebuffer.cpp
    src/gui/font_renderer.cpp
    src/gui/scopes.cpp
    src/gui/ui_app.cpp
    src/gui/file_dialog.cpp
    src/gui/gui_window.cpp
)

# CLAP plugin entry point sources
set(PAULASCAPE_CLAP_SOURCES
    src/clap/clap_entry.cpp
)

# Create core static library
add_library(paulascape_core STATIC ${PAULASCAPE_CORE_SOURCES})

# Create GUI static library
add_library(paulascape_gui STATIC ${PAULASCAPE_GUI_SOURCES})
target_link_libraries(paulascape_gui PUBLIC paulascape_core)

# Create CLAP Plugin MODULE library
add_library(Paulascape MODULE ${PAULASCAPE_CLAP_SOURCES})
target_link_libraries(Paulascape PRIVATE paulascape_gui paulascape_core)
if(WIN32)
    target_link_libraries(paulascape_gui PUBLIC ole32 shell32 comdlg32 gdi32 uuid)
    target_link_libraries(Paulascape PRIVATE ole32 shell32 comdlg32 gdi32 uuid)
endif()
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(Paulascape PRIVATE X11::X11 pthread)
    target_link_libraries(paulascape_gui PUBLIC X11::X11 pthread)
endif()
set_target_properties(Paulascape PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Helper function to add test executables
# Third argument NO_CTEST builds the executable only (tests that need a wrapper command).
function(add_paulascape_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE paulascape_core paulascape_gui)
    # Tests use assert(); keep it active in Release builds too.
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/enable_asserts.h)
    else()
        target_compile_options(${TEST_NAME} PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/enable_asserts.h)
    endif()
    if(NOT "${ARGV2}" STREQUAL "NO_CTEST")
        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endif()
endfunction()

# Unit tests
add_paulascape_test(mod_loader_test src/mod_loader_test_main.cpp)
add_paulascape_test(paula_voice_test src/paula_voice_test_main.cpp)
add_paulascape_test(paula_reference_test src/paula_reference_test_main.cpp)
target_compile_definitions(paula_reference_test PRIVATE PAULASCAPE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")
add_paulascape_test(voice_pool_test src/voice_pool_test_main.cpp)
add_paulascape_test(replayer_test src/replayer_test_main.cpp)
target_compile_definitions(replayer_test PRIVATE PAULASCAPE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")
add_paulascape_test(wav_import_test src/import/wav_import_test_main.cpp)
add_paulascape_test(midi_export_test src/export/midi_export_test_main.cpp)
target_compile_definitions(midi_export_test PRIVATE PAULASCAPE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")
add_paulascape_test(clap_plugin_test src/clap/clap_plugin_test_main.cpp)
target_compile_definitions(clap_plugin_test PRIVATE PAULASCAPE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")
add_paulascape_test(gui_render_test src/gui/gui_render_test_main.cpp)
add_paulascape_test(ui_app_test src/gui/ui_app_test_main.cpp)
target_compile_definitions(ui_app_test PRIVATE PAULASCAPE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")

if(UNIX AND NOT APPLE)
    # Window smoke test: needs a display, so it runs under Xvfb when that is installed.
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_paulascape_test(gui_window_x11_test src/gui/gui_window_x11_test_main.cpp NO_CTEST)
        target_compile_definitions(gui_window_x11_test PRIVATE PAULASCAPE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")
        add_test(NAME gui_window_x11_test_xvfb COMMAND ${XVFB_RUN} -a $<TARGET_FILE:gui_window_x11_test>)
    endif()
endif()

if(UNIX)
    # Loads the built .clap like a host would; GUI checks run when a display exists.
    add_paulascape_test(clap_host_test src/clap/clap_host_test_main.cpp NO_CTEST)
    target_link_libraries(clap_host_test PRIVATE ${CMAKE_DL_LIBS})
    if(NOT APPLE)
        target_link_libraries(clap_host_test PRIVATE X11::X11)
    endif()
    add_dependencies(clap_host_test Paulascape)
    add_test(NAME clap_host_test_plugin COMMAND clap_host_test $<TARGET_FILE:Paulascape> x)
    if(XVFB_RUN)
        add_test(NAME clap_host_test_xvfb COMMAND ${XVFB_RUN} -a $<TARGET_FILE:clap_host_test> $<TARGET_FILE:Paulascape> x)
    endif()
endif()

# Build the plugin and copy the .clap to the folder in the CLAPTEST environment
# variable. Not part of ALL: a host that has the plugin loaded locks the file,
# so a plain build should not fail on the copy.
add_custom_target(deploy_clap
    COMMAND ${CMAKE_COMMAND} -DCLAP_FILE=$<TARGET_FILE:Paulascape>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployClap.cmake
    COMMENT "Deploying Paulascape.clap to %CLAPTEST%"
    VERBATIM
)
add_dependencies(deploy_clap Paulascape)
