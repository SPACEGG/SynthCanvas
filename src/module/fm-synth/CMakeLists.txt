# core/src/module/fm-synth/CMakeLists.txt

add_library(fm-synth-plugin MODULE
    fm_synth_plugin.cpp
    clap-entry.cpp
)

set_target_properties(fm-synth-plugin PROPERTIES CXX_STANDARD 20)
target_link_libraries(fm-synth-plugin PRIVATE clap clap-helpers)
add_plugin_post_build_copy_command(fm-synth-plugin)
