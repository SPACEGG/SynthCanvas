# Add the library target, named after the directory
add_library(test-plugin MODULE "plugin.cpp")

# Link against clap library
target_link_libraries(test-plugin PRIVATE clap)

# Call the helper function from the parent to add the copy command
add_plugin_post_build_copy_command(test-plugin)
