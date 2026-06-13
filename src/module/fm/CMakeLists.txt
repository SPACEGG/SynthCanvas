# core/src/module/fm/CMakeLists.txt

add_library(simple-fm-plugin MODULE
    simple_fm_plugin.cpp
    clap-entry.cpp
)

set_target_properties(simple-fm-plugin PROPERTIES CXX_STANDARD 20)
target_link_libraries(simple-fm-plugin PRIVATE clap clap-helpers)
add_plugin_post_build_copy_command(simple-fm-plugin)
