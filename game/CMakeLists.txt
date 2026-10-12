file(GLOB_RECURSE LUMEN_GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(lumen_game_code STATIC ${LUMEN_GAME_SOURCES})

target_include_directories(lumen_game_code
PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(lumen_game_code
PUBLIC
    lumen_compiler_options
    lumen_core
)
