cmake_minimum_required(VERSION 3.20)
project(TuningWizard VERSION 0.1.0 LANGUAGES CXX RC)

include(CTest)
if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch ImGui
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.8
)
FetchContent_MakeAvailable(imgui)

# Fetch FreeType (for high-quality hinted font rendering)
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
)
set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)

# ImGui library (core + DX11 + Win32 backend + FreeType font builder, no demo)
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/freetype
)
target_compile_definitions(imgui_lib PUBLIC IMGUI_ENABLE_FREETYPE)
target_link_libraries(imgui_lib PUBLIC d3d11 dxgi d3dcompiler freetype)

# Main application
add_executable(TuningWizard WIN32
    src/main.cpp
    src/app.cpp
    src/diagnostics_ui.cpp
    src/serial.cpp
    src/protocol.cpp
    src/protocol_codec.cpp
    src/firmware_flash.cpp
    src/cal_defs.cpp
    src/cal_validate_generated.cpp
    src/tune_transfer.cpp
    src/autotune.cpp
    src/diagnostics.cpp
    src/dtc_defs.cpp
    src/calibration.cpp
    src/calibration_safety.cpp
    src/storage.cpp
    src/theme.cpp
    src/dashboard.cpp
    src/logging.cpp
    src/logviewer.cpp
    src/editor.cpp
    src/grid_paste.cpp
    src/help.cpp
    src/undo.cpp
    src/tooltips.cpp
    src/resource.rc
)

target_include_directories(TuningWizard PRIVATE src)
target_compile_definitions(TuningWizard PRIVATE
    NOMINMAX
    TW_VERSION="${PROJECT_VERSION}"
    TW_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    TW_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    TW_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_link_libraries(TuningWizard PRIVATE imgui_lib)
set_target_properties(TuningWizard PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Embed application icon (optional, if icon.ico exists)
if(EXISTS "${CMAKE_SOURCE_DIR}/icon.ico")
    target_compile_definitions(TuningWizard PRIVATE HAS_APP_ICON)
endif()

# Use static CRT for standalone distribution (no MSVC redistributable needed)
if(MSVC)
    foreach(_tgt TuningWizard imgui_lib freetype)
        set_property(TARGET ${_tgt} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()
endif()

if(BUILD_TESTING)
    option(TW_BUILD_UI_PREVIEW "Build isolated diagnostic visual test fixture" OFF)
    if(TW_BUILD_UI_PREVIEW)
        get_target_property(TW_APP_SOURCES TuningWizard SOURCES)
        add_executable(TuningWizardPreview WIN32 ${TW_APP_SOURCES})
        target_include_directories(TuningWizardPreview PRIVATE src)
        target_compile_definitions(TuningWizardPreview PRIVATE NOMINMAX TW_UI_PREVIEW TW_VERSION="visual-test" TW_VERSION_MAJOR=0 TW_VERSION_MINOR=1 TW_VERSION_PATCH=0)
        target_link_libraries(TuningWizardPreview PRIVATE imgui_lib)
        set_target_properties(TuningWizardPreview PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endif()
    add_executable(TuningWizardTests
        tests/port_tests.cpp
        tests/diagnostic_tests.cpp
        src/cal_defs.cpp
    src/cal_validate_generated.cpp
    src/tune_transfer.cpp
    src/autotune.cpp
    src/diagnostics.cpp
    src/dtc_defs.cpp
    src/calibration.cpp
        src/calibration_safety.cpp
        src/protocol.cpp
        src/protocol_codec.cpp
        src/serial.cpp
        src/firmware_flash.cpp
        src/grid_paste.cpp
        src/undo.cpp
    )
    target_include_directories(TuningWizardTests PRIVATE src)
    target_compile_definitions(TuningWizardTests PRIVATE NOMINMAX TEST_TUNE_PATH="${CMAKE_SOURCE_DIR}/../build/oem/example-schema4.bin" TEST_DLL_PATH="${CMAKE_SOURCE_DIR}/../build/oem/oem.dll")
    add_test(NAME calibration_and_protocol_safety COMMAND TuningWizardTests)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME generated_definitions COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/wizard_definition.py --check)
    add_test(NAME diagnostic_definitions COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/diagnostic_definition.py --check)
    add_test(NAME neutral_interface COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_interface.py)
endif()

install(TARGETS TuningWizard RUNTIME DESTINATION .)
install(FILES OPERATOR_MANUAL.md DESTINATION docs)
set(CPACK_PACKAGE_NAME "Tuning Wizard")
set(CPACK_PACKAGE_VENDOR "Tuning Wizard project")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "TuningWizard-${PROJECT_VERSION}-Windows-x64")
set(CPACK_GENERATOR "ZIP")
find_program(TW_MAKENSIS_EXECUTABLE NAMES makensis)
if(TW_MAKENSIS_EXECUTABLE)
    list(APPEND CPACK_GENERATOR "NSIS")
endif()
set(CPACK_NSIS_DISPLAY_NAME "Tuning Wizard")
set(CPACK_NSIS_PACKAGE_NAME "Tuning Wizard")
include(CPack)
