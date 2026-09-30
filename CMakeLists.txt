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
    src/clap/paulascape_plugin.cpp
)

# GUI library sources
set(PAULASCAPE_GUI_SOURCES
    src/gui/framebuffer.cpp
    src/gui/font_renderer.cpp
    src/gui/scopes.cpp
    src/gui/front_page.cpp
    src/gui/settings_screen.cpp
)

# CLAP plugin entry point sources
set(PAULASCAPE_CLAP_SOURCES
    src/clap/clap_entry.cpp
)

# Create core static library
add_library(paulascape_core STATIC ${PAULASCAPE_CORE_SOURCES})

# Create GUI static library
add_library(paulascape_gui STATIC ${PAULASCAPE_GUI_SOURCES})
target_link_libraries(paulascape_gui PRIVATE paulascape_core)

# Create CLAP Plugin MODULE library
add_library(Paulascape MODULE ${PAULASCAPE_CLAP_SOURCES})
target_link_libraries(Paulascape PRIVATE paulascape_core paulascape_gui)
set_target_properties(Paulascape PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Helper function to add test executables
function(add_paulascape_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE paulascape_core paulascape_gui)
    # Tests use assert(); keep it active in Release builds too.
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${TEST_NAME} PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Unit tests
add_paulascape_test(mod_loader_test src/mod_loader_test_main.cpp)
add_paulascape_test(paula_voice_test src/paula_voice_test_main.cpp)
add_paulascape_test(voice_pool_test src/voice_pool_test_main.cpp)
add_paulascape_test(replayer_test src/replayer_test_main.cpp)
add_paulascape_test(wav_import_test src/import/wav_import_test_main.cpp)
add_paulascape_test(midi_export_test src/export/midi_export_test_main.cpp)
add_paulascape_test(clap_plugin_test src/clap/clap_plugin_test_main.cpp)
target_compile_definitions(clap_plugin_test PRIVATE PAULASCAPE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test")
add_paulascape_test(gui_render_test src/gui/gui_render_test_main.cpp)

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
