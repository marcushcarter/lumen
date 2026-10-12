cmake_minimum_required(VERSION 3.28)
project(Lumen VERSION 2.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LUMEN_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release)

set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_SOURCE_DIR}\" /I\"${CMAKE_BINARY_DIR}/generated\"")

option(LUMEN_BUILD_EDITOR "Build lumen_engine (editor + runtime, dev)" ON)
option(LUMEN_BUILD_GAME "Build lumen_game (runtime only, ship)" ON)
option(LUMEN_BUILD_CONSOLE_WRAPPER "Build the console wrapper" ON)

configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/Version.gen.h.in
    ${CMAKE_BINARY_DIR}/generated/core/version_generated.gen.h
    @ONLY
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CompilerOptions)
include(Platform)
include(Dependencies)
include(EmbedAssets)

add_subdirectory(thirdparty)
add_subdirectory(drivers)
add_subdirectory(core)
add_subdirectory(game)
add_subdirectory(editor)

set(LUMEN_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${LUMEN_GENERATED_DIR})

lumen_generate_asset_rc(
    ${LUMEN_GENERATED_DIR}/embedded_assets_shared.rc
    ${CMAKE_SOURCE_DIR}/assets/shared/shaders
    ${CMAKE_SOURCE_DIR}/assets/shared/images
)

lumen_generate_asset_rc(
    ${LUMEN_GENERATED_DIR}/embedded_assets_editor.rc
    ${CMAKE_SOURCE_DIR}/assets/editor/logos
    ${CMAKE_SOURCE_DIR}/assets/editor/fonts
    FILES
        ${CMAKE_SOURCE_DIR}/LICENSE
        ${CMAKE_SOURCE_DIR}/EULA.md
)

set(LUMEN_SHARED_ASSETS ${LUMEN_GENERATED_DIR}/embedded_assets_shared.rc)
set(LUMEN_EDITOR_ASSETS ${LUMEN_GENERATED_DIR}/embedded_assets_editor.rc)

add_subdirectory(platform)