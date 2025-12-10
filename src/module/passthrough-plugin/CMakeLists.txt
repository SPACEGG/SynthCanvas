# Add the library target, named after the directory
add_library(passthrough-plugin MODULE
    passthrough_plugin.cpp
    clap-entry.cpp
)

# Set C++ standard to C++20
set_target_properties(passthrough-plugin PROPERTIES CXX_STANDARD 20)

# Link against clap and clap-helpers libraries
target_link_libraries(passthrough-plugin PRIVATE clap clap-helpers)

# Call the helper function from the parent to add the copy command
add_plugin_post_build_copy_command(passthrough-plugin)