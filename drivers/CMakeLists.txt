file(GLOB_RECURSE LUMEN_DRIVERS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(lumen_drivers STATIC ${LUMEN_DRIVERS_SOURCES})

target_include_directories(lumen_drivers
PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
)

target_link_options(lumen_drivers PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/ignore:4099>)

target_link_libraries(lumen_drivers
PUBLIC
    lumen_compiler_options
    Vulkan::Vulkan
    Vulkan::shaderc_combined
    imgui
    IconFontCppHeaders
    toml
    dwmapi
    comctl32
)