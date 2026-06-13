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

# Define test executable
add_executable(synth_canvas_tests
    sanity_test.cpp
)

# Link Catch2 (using Catch2WithMain so we don't have to write main())
# Also link synth_canvas_core to test internal engine functions
target_link_libraries(synth_canvas_tests
    PRIVATE
    Catch2::Catch2WithMain
    synth_canvas_core
)

# Register with CTest
add_test(NAME synth_canvas_sanity_test COMMAND synth_canvas_tests)
