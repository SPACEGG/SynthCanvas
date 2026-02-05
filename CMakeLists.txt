# SynthCanvas Root CMakeLists.txt

# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.16)

# Set the project name
project(SynthCanvas LANGUAGES CXX)

# Set the C++ standard for the whole project
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Save the core directory path for relative referencing
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Ensure the bin directory exists for compiled artifacts
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Define the source files for the GDExtension library
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
    # For Windows/Editor builds, we only need the GDExtension interface with dummy implementation
    set(GDE_SOURCES
        ${CORE_DIR}/src/gdextension/register_types.cpp
        ${CORE_DIR}/src/gdextension/synthcanvas_audio_system.cpp
        ${CORE_DIR}/src/gdextension/synthcanvas_audio_system.h
    )
endif()

# Add the subdirectories for the libraries
add_subdirectory(libs/clap)
add_subdirectory(libs/clap-helpers)
add_subdirectory(libs/readerwriterqueue)

# Treat godot-cpp headers as system headers to suppress warnings/tidy checks
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
add_subdirectory(src/gdextension/godot-cpp)
add_subdirectory(src/gdextension)
add_subdirectory(src/module)

# --- Code Quality Tools (Clang-Format & Clang-Tidy) ---

# 1. Generate compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 2. Find all source files in src/ (excluding godot-cpp)
file(GLOB_RECURSE ALL_SOURCE_FILES 
    "${CORE_DIR}/src/*.cpp" 
    "${CORE_DIR}/src/*.h" 
    "${CORE_DIR}/src/*.hpp" 
    "${CORE_DIR}/src/*.c" 
    "${CORE_DIR}/src/*.cc"
)

# Filter out godot-cpp and clap-saw-demo files from the analysis list
list(FILTER ALL_SOURCE_FILES EXCLUDE REGEX "/godot-cpp/|/clap-saw-demo/")

# 3. Add 'format' target (runs clang-format -i)
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

# 4. Add 'check' target (runs clang-tidy)
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