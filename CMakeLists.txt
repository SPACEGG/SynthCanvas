# SynthCanvas Root CMakeLists.txt

cmake_minimum_required(VERSION 3.16)
project(SynthCanvas LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

if(ANDROID)
    # Enable Android MIDI (AMIDI) support in RtMidi submodule
    set(RTMIDI_API_AMIDI ON CACHE BOOL "Force AMIDI support on Android" FORCE)
endif()

# Prevent external libraries from overriding global build settings
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Force static libraries by default" FORCE)

find_package(Threads REQUIRED)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-pthread)
    add_link_options(-pthread)
endif()

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

## Subdirectories

# External Libraries
add_subdirectory(libs/clap)
add_subdirectory(libs/clap-helpers)
add_subdirectory(libs/readerwriterqueue)
add_subdirectory(libs/rtmidi)

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
add_custom_target(build_godot
    DEPENDS synth_canvas_gdextension build_plugins
    COMMENT "Building godot-binding project (GDExtension + Core + Plugins)..."
)

## Clang-Format / Clang-Tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(GLOB_RECURSE ALL_SOURCE_FILES 
    "${CORE_DIR}/src/*.cpp" "${CORE_DIR}/src/*.h" "${CORE_DIR}/src/*.hpp"
    "${CORE_DIR}/bindings/*.cpp" "${CORE_DIR}/bindings/*.h" "${CORE_DIR}/bindings/*.hpp"
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

# Enable test suite
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

