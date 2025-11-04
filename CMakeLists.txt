# SynthCanvas Root CMakeLists.txt

# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.16)

# Set the project name
project(SynthCanvas LANGUAGES CXX)

# Add the subdirectory containing the GDExtension library.
# This is where the main build logic resides.
add_subdirectory(src/gdextension)
