# src/gdextension/CMakeLists.txt

# --- GDExtension Library ---
# Godot bindings that interface with the core engine.

set(GDE_SOURCES
    register_types.cpp
    synthcanvas_audio_system.cpp
)

add_library(synth_canvas_gdextension SHARED ${GDE_SOURCES})

if(ANDROID)
    target_compile_options(synth_canvas_gdextension PRIVATE -fexceptions)
    target_compile_definitions(synth_canvas_gdextension PRIVATE __ANDROID__)
endif()

target_include_directories(synth_canvas_gdextension PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against godot-cpp and the core engine library
target_link_libraries(synth_canvas_gdextension PRIVATE
    godot-cpp
    synth_canvas_core
)

# --- Post-build command to copy the library ---
add_custom_command(
    TARGET synth_canvas_gdextension
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:synth_canvas_gdextension>"
        "${CMAKE_SOURCE_DIR}/bin/"
    COMMENT "Copying synth_canvas_gdextension to root bin directory for Godot to use."
)

install(TARGETS synth_canvas_gdextension
    DESTINATION ${CMAKE_SOURCE_DIR}/bin
)
