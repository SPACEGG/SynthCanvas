# src/module/soundfont/CMakeLists.txt

# Add the library target
add_library(soundfont-plugin MODULE
    src/soundfont-engine.cpp
    src/soundfont-plugin.cpp
    src/plugin-entry.cpp
)

# Set C++ standard to C++20
set_target_properties(soundfont-plugin PROPERTIES CXX_STANDARD 20)

# Include directories
target_include_directories(soundfont-plugin PRIVATE 
    libs
    src
)

# Link against clap and clap-helpers libraries
target_link_libraries(soundfont-plugin PRIVATE clap clap-helpers)

# Call the helper function from the parent to add the copy command
add_plugin_post_build_copy_command(soundfont-plugin)
