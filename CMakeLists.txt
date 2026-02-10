# SynthCanvas Root CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(SynthCanvas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

## Subdirectories

# External Libraries
add_subdirectory(libs/clap)
add_subdirectory(libs/clap-helpers)
add_subdirectory(libs/readerwriterqueue)

# Engine & Plugins
add_subdirectory(src)         # synth_canvas_core
add_subdirectory(src/module)  # plugins

# Godot Bindings
add_subdirectory(bindings/godot) # synth_canvas_gdextension

## Custom Build Targets

# Core only
add_custom_target(build_core
    DEPENDS synth_canvas_core
    COMMENT "Building only the static engine core library..."
)

# Plugins only
# Note: This target is defined in src/module/CMakeLists.txt but can be used here.

# Full build with godot binding
add_custom_target(build_full
    DEPENDS synth_canvas_gdextension build_plugins
    COMMENT "Building godot-binding project (GDExtension + Core + Plugins)..."
)

## Clang-Format / Clang-Tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(GLOB_RECURSE ALL_SOURCE_FILES 
    "${CORE_DIR}/src/*.cpp" "${CORE_DIR}/src/*.h" "${CORE_DIR}/src/*.hpp"
)
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/godot-cpp/|/clap-saw-demo/")

find_program(CLANG_FORMAT_EXE NAMES "clang-format")
if(CLANG_FORMAT_EXE)
    add_custom_target(format COMMAND "${CLANG_FORMAT_EXE}" -i -style=file ${ALL_SOURCE_FILES})
endif()

find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
if(CLANG_TIDY_EXE)
    add_custom_target(check COMMAND "${CLANG_TIDY_EXE}" -p ${CMAKE_BINARY_DIR} ${ALL_SOURCE_FILES})
endif()