
add_library(lumen_compiler_options INTERFACE)

target_compile_options(lumen_compiler_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>: /W4 /WX /permissive- /Zc:__cplusplus /MP /GT>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>: -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter>
)