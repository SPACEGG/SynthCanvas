# src/module/filter/CMakeLists.txt

# Add the library target
add_library(filter-plugin MODULE
    src/svf-engine.cpp
    src/filter-plugin.cpp
    src/plugin-entry.cpp
)

# Set C++ standard to C++20
set_target_properties(filter-plugin PROPERTIES CXX_STANDARD 20)

# Include directories
target_include_directories(filter-plugin PRIVATE src)

# Link against clap and clap-helpers libraries
target_link_libraries(filter-plugin PRIVATE clap clap-helpers)

# Call the helper function from the parent to add the copy command
add_plugin_post_build_copy_command(filter-plugin)
