# core/tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(SynthCanvasTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch Catch2 from GitHub
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.4.0
)

# Make Catch2 targets available
FetchContent_MakeAvailable(Catch2)

# Treat Catch2 headers as SYSTEM includes to prevent linter errors
set_target_properties(Catch2 PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:Catch2,INTERFACE_INCLUDE_DIRECTORIES>")
set_target_properties(Catch2WithMain PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:Catch2WithMain,INTERFACE_INCLUDE_DIRECTORIES>")

# Define test executable
add_executable(synth_canvas_tests
    sanity_test.cpp
    simple_fm_test.cpp
    fm_synth_test.cpp
    ../src/module/fm/simple_fm_plugin.cpp
    ../src/module/fm-synth/fm_synth_plugin.cpp
)

# Link Catch2 (using Catch2WithMain so we don't have to write main())
# Also link synth_canvas_core to test internal engine functions
target_link_libraries(synth_canvas_tests
    PRIVATE
    Catch2::Catch2WithMain
    synth_canvas_core
    clap
    clap-helpers
)

# Include directories relative to src/
target_include_directories(synth_canvas_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# Register with CTest
add_test(NAME synth_canvas_sanity_test COMMAND synth_canvas_tests)
