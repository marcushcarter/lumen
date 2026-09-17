if(NOT LUMEN_BUILD_EDITOR)
    return()
endif()

file(GLOB_RECURSE LUMEN_EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(lumen_editor STATIC ${LUMEN_EDITOR_SOURCES})

target_include_directories(lumen_editor
PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(lumen_editor
PUBLIC
    lumen_compiler_options
    lumen_core
    bc7enc
    assimp
    meshoptimizer
)