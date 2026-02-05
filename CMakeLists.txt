# SynthCanvas Root CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(SynthCanvas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

if(ANDROID)
    set(GDE_SOURCES
        ${CORE_DIR}/src/gdextension/register_types.cpp
        ${CORE_DIR}/src/gdextension/synthcanvas_audio_system.cpp
        ${CORE_DIR}/src/gdextension/synthcanvas_audio_system.h
        ${CORE_DIR}/src/host/audio_engine.cpp
        ${CORE_DIR}/src/host/audio_engine.h
        ${CORE_DIR}/src/host/logger.cpp
        ${CORE_DIR}/src/host/logger.h
        ${CORE_DIR}/src/host/plugin_host.cpp
        ${CORE_DIR}/src/host/plugin_host.h
        ${CORE_DIR}/src/host/module_router.cpp
        ${CORE_DIR}/src/host/module_router.h
        ${CORE_DIR}/src/host/audio_buffer_manager.cpp
        ${CORE_DIR}/src/host/audio_buffer_manager.h
    )
else()
    # GDExtension interface with dummy implementation
    set(GDE_SOURCES
        ${CORE_DIR}/src/gdextension/register_types.cpp
        ${CORE_DIR}/src/gdextension/synthcanvas_audio_system.cpp
        ${CORE_DIR}/src/gdextension/synthcanvas_audio_system.h
    )
endif()

# Libraries
add_subdirectory(libs/clap)
add_subdirectory(libs/clap-helpers)
add_subdirectory(libs/readerwriterqueue)

set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
add_subdirectory(src/gdextension/godot-cpp)
add_subdirectory(src/gdextension)
add_subdirectory(src/module)

## Clang-Format & Clang-Tidy

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(GLOB_RECURSE ALL_SOURCE_FILES 
    "${CORE_DIR}/src/*.cpp" 
    "${CORE_DIR}/src/*.h" 
    "${CORE_DIR}/src/*.hpp" 
    "${CORE_DIR}/src/*.c" 
    "${CORE_DIR}/src/*.cc"
)
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/godot-cpp/|/clap-saw-demo/")

# clang-format -i
find_program(CLANG_FORMAT_EXE NAMES "clang-format")
if(CLANG_FORMAT_EXE)
    add_custom_target(
        format
        COMMAND "${CLANG_FORMAT_EXE}" -i -style=file ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format on source files..."
        WORKING_DIRECTORY ${CORE_DIR}
    )
else()
    message(WARNING "clang-format not found. 'format' target will not be available.")
endif()

# clang-tidy
find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    add_custom_target(
        check
        COMMAND "${CLANG_TIDY_EXE}" -p ${CMAKE_BINARY_DIR} ${ALL_SOURCE_FILES}
        COMMENT "Running clang-tidy on source files..."
        WORKING_DIRECTORY ${CORE_DIR}
    )
else()
    message(WARNING "clang-tidy not found. 'check' target will not be available.")
endif()