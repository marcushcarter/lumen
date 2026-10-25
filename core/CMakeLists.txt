file(GLOB_RECURSE LUMEN_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(lumen_core STATIC ${LUMEN_CORE_SOURCES})

target_include_directories(lumen_core
PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(lumen_core
PUBLIC
    lumen_compiler_options
    lumen_drivers
    stb
    entt
)