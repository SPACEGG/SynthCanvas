# src/CMakeLists.txt

# --- Core Engine Library ---
# Pure C++ engine logic without any Godot dependencies.

set(CORE_SOURCES
    api/system.cpp
    host/audio_buffer_manager.cpp
    host/audio_engine.cpp
    host/logger.cpp
    host/module_router.cpp
    host/plugin_host.cpp
)

add_library(synth_canvas_core STATIC ${CORE_SOURCES})

# Include directories relative to src/
target_include_directories(synth_canvas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/api
    ${CORE_DIR}
)

# External libraries (CLAP, etc.)
target_include_directories(synth_canvas_core SYSTEM PRIVATE
    ${CORE_DIR}/libs/clap/include
    ${CORE_DIR}/libs/clap-helpers/include
    ${CORE_DIR}/libs/readerwriterqueue
)

target_link_libraries(synth_canvas_core PRIVATE
    clap
    clap-helpers
    readerwriterqueue
)

if(ANDROID)
    target_compile_options(synth_canvas_core PRIVATE -fexceptions)
    target_compile_definitions(synth_canvas_core PRIVATE __ANDROID__)
    
    if(NOT TARGET oboe)
        add_subdirectory(${CORE_DIR}/libs/oboe ${CMAKE_CURRENT_BINARY_DIR}/oboe)
    endif()
    target_include_directories(synth_canvas_core SYSTEM PRIVATE ${CORE_DIR}/libs/oboe/include)
    target_link_libraries(synth_canvas_core PRIVATE oboe dl)
    
    find_library(log-lib log)
    target_link_libraries(synth_canvas_core PRIVATE ${log-lib})
endif()
