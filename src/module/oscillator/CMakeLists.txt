# src/module/oscillator/CMakeLists.txt

# Add the library target
add_library(oscillator-plugin MODULE
    src/osc-voice.cpp
    src/oscillator-plugin.cpp
    src/plugin-entry.cpp
)

# Set C++ standard to C++20
set_target_properties(oscillator-plugin PROPERTIES CXX_STANDARD 20)

# Include directories
target_include_directories(oscillator-plugin PRIVATE src)

# Link against clap and clap-helpers libraries
target_link_libraries(oscillator-plugin PRIVATE clap clap-helpers)

# Call the helper function from the parent to add the copy command
add_plugin_post_build_copy_command(oscillator-plugin)
