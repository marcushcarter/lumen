




function(lumen_common_target target)
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/generated
    )
    target_link_options(${target} PRIVATE /MANIFEST:NO)
endfunction()

set(LUMEN_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/lumen_main.cpp)
set(LUMEN_WRAPPER ${CMAKE_CURRENT_SOURCE_DIR}/lumen_console_wrapper.cpp)

set(LUMEN_RUNTIME_DIR ${CMAKE_BINARY_DIR}/runtime)
set(LUMEN_RUNTIME_EXE ${LUMEN_RUNTIME_DIR}/lumen_runtime.exe)
set(LUMEN_RUNTIME_RC ${CMAKE_BINARY_DIR}/generated/embedded_runtime.rc)

add_executable(lumen_runtime WIN32
    lumen_res_app.rc
    ${LUMEN_SHARED_ASSETS}
    ${LUMEN_MAIN}
)
target_link_libraries(lumen_runtime PRIVATE lumen_core lumen_game_code)
target_compile_definitions(lumen_runtime PRIVATE LUMEN_GAME)
set_target_properties(lumen_runtime PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${LUMEN_RUNTIME_DIR})
foreach(config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config_upper)
    set_target_properties(lumen_runtime PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${LUMEN_RUNTIME_DIR})
endforeach()
lumen_common_target(lumen_runtime)

if(LUMEN_BUILD_EDITOR)
    file(WRITE ${LUMEN_RUNTIME_RC} "LUMEN_RUNTIME RCDATA \"${LUMEN_RUNTIME_EXE}\"\n")
    set_source_files_properties(${LUMEN_RUNTIME_RC} PROPERTIES OBJECT_DEPENDS ${LUMEN_RUNTIME_EXE})

    add_executable(lumen_engine WIN32
        lumen_res.rc
        ${LUMEN_SHARED_ASSETS}
        ${LUMEN_EDITOR_ASSETS}
        ${LUMEN_RUNTIME_RC}
        ${LUMEN_MAIN}
    )
    target_link_libraries(lumen_engine PRIVATE lumen_core lumen_editor lumen_game_code)
    target_compile_definitions(lumen_engine PRIVATE LUMEN_EDITOR LUMEN_DEV_TOOLS)
    add_dependencies(lumen_engine lumen_runtime)
    lumen_common_target(lumen_engine)
endif()

if(LUMEN_BUILD_CONSOLE_WRAPPER)
    add_executable(lumen_console_wrapper
        lumen_res_console.rc
        ${LUMEN_WRAPPER}
    )
    target_compile_definitions(lumen_console_wrapper PRIVATE "LUMEN_WRAPPER_TARGET=L\"lumen_engine.exe\"")
    lumen_common_target(lumen_console_wrapper)
endif()
