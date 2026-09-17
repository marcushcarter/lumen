add_library(stb INTERFACE)
target_include_directories(stb INTERFACE stb)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE glm)

file(GLOB IMGUI_CORE imgui/*.cpp)
file(GLOB IMPLOT_CORE implot/*.cpp)
set(IMGUI_SRC ${IMGUI_CORE} imgui/backends/imgui_impl_win32.cpp imgui/backends/imgui_impl_vulkan.cpp ${IMPLOT_CORE})
add_library(imgui STATIC ${IMGUI_SRC})
target_include_directories(imgui PUBLIC imgui implot ${Vulkan_INCLUDE_DIRS})

add_library(IconFontCppHeaders INTERFACE)
target_include_directories(IconFontCppHeaders INTERFACE IconFontCppHeaders)

add_library(toml INTERFACE)
target_include_directories(toml INTERFACE toml/include)

add_library(bc7enc STATIC
    bc7enc_rdo/bc7enc.cpp
    bc7enc_rdo/bc7decomp.cpp
    bc7enc_rdo/bc7decomp_ref.cpp
    bc7enc_rdo/rgbcx.cpp
    bc7enc_rdo/rdo_bc_encoder.cpp
    bc7enc_rdo/ert.cpp
    bc7enc_rdo/utils.cpp
    bc7enc_rdo/lodepng.cpp
)
target_include_directories(bc7enc PUBLIC bc7enc_rdo)
target_compile_options(bc7enc PRIVATE /W1)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
add_subdirectory(assimp)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(mesh_opt)