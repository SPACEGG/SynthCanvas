cmake_minimum_required(VERSION 3.16)
project(TutorialClapPlugin)

# Add clap and clap-helpers libraries
# These should already be available from the root CMakeLists.txt

# Source file
add_library(tutorial_plugin MODULE "plugin.cpp")

# Set C++ standard to C++20
set_target_properties(tutorial_plugin PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

# Include directories
target_include_directories(tutorial_plugin PUBLIC
    "${CMAKE_SOURCE_DIR}/libs/clap/include"
)

# Set the output file extension to .clap
set_target_properties(tutorial_plugin PROPERTIES PREFIX "" SUFFIX ".clap")

# Symbol export settings for Windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/plugin.def "EXPORTS\n    clap_entry")
    target_link_options(tutorial_plugin PRIVATE "/DEF:${CMAKE_CURRENT_BINARY_DIR}/plugin.def")
endif()
# (You can add elseif for Darwin/Linux if needed)

# Copy the .clap file to the bin/module directory after build
add_custom_command(TARGET tutorial_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin/module"
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:tutorial_plugin>
        "${CMAKE_SOURCE_DIR}/bin/module/"
)
