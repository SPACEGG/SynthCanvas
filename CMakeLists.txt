# SynthCanvas Root CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(SynthCanvas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# --- Subdirectories ---

# 1. External Libraries
add_subdirectory(libs/clap)
add_subdirectory(libs/clap-helpers)
add_subdirectory(libs/readerwriterqueue)

# 2. Engine & Plugins
add_subdirectory(src)         # synth_canvas_core
add_subdirectory(src/module)  # plugins

# 3. GDExtension (Godot Bindings)
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
add_subdirectory(src/gdextension/godot-cpp)
add_subdirectory(src/gdextension) # synth_canvas_gdextension

# --- Custom Build Targets ---

# A. Build only the static engine core (No Godot, No Plugins)
add_custom_target(build_core
    DEPENDS synth_canvas_core
    COMMENT "Building only the static engine core library..."
)

# B. Build only the plugins (CLAP Modules)
# Note: This target is defined in src/module/CMakeLists.txt but can be used here.

# C. Build everything including GDExtension
add_custom_target(build_full
    DEPENDS synth_canvas_gdextension build_plugins
    COMMENT "Building full project (GDExtension + Core + Plugins)..."
)

## Development Tools (Clang-Format / Clang-Tidy)
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