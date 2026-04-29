# Add the library target, named after the directory
add_library(gain-plugin MODULE
    gain.cpp
    clap-entry.cpp
)

# Set C++ standard to C++20
set_target_properties(gain-plugin PROPERTIES CXX_STANDARD 20)

# Link against clap and clap-helpers libraries
target_link_libraries(gain-plugin PRIVATE clap clap-helpers)

# Call the helper function from the parent to add the copy command
add_plugin_post_build_copy_command(gain-plugin)
