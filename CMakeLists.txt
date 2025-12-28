# SynthCanvas Root CMakeLists.txt

# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.16)

# Set the project name
project(SynthCanvas LANGUAGES CXX)

# Set the C++ standard for the whole project
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure the bin directory exists for compiled artifacts
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Define the source files for the GDExtension library
set(GDE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gdextension/register_types.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gdextension/synthcanvas_audio_system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gdextension/synthcanvas_audio_system.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/audio_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/audio_engine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/logger.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/plugin_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/plugin_host.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/module_router.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/module_router.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/audio_buffer_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/audio_buffer_manager.h
)

# Add the subdirectories for the libraries
add_subdirectory(libs/clap)
add_subdirectory(libs/clap-helpers)
add_subdirectory(libs/readerwriterqueue)
add_subdirectory(src/gdextension/godot-cpp)
add_subdirectory(src/gdextension)
add_subdirectory(src/module)
