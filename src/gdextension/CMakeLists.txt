# GDExtension Library CMakeLists.txt

# Define our GDExtension library
add_library(synth_canvas_gdextension SHARED ${GDE_SOURCES})

# For Android, forcefully enable exceptions for our library to be compatible with clap-helpers.
# This overrides the `-fno-exceptions` flag inherited from the godot-cpp subdirectory.
if(ANDROID)
    target_compile_options(synth_canvas_gdextension PRIVATE -fexceptions)
endif()

# --- Configure Includes and Links ---

# Set the include directories required for our library.
target_include_directories(synth_canvas_gdextension PRIVATE
    # For our own host headers
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Mark external library headers as SYSTEM to suppress warnings and tidy checks
target_include_directories(synth_canvas_gdextension SYSTEM PRIVATE
    # For clap and clap-helpers headers
    ${CMAKE_SOURCE_DIR}/libs/clap/include
    ${CMAKE_SOURCE_DIR}/libs/clap-helpers/include
    # For readerwriterqueue
    ${CMAKE_SOURCE_DIR}/libs/readerwriterqueue
)

# Link the library against its dependencies.
target_link_libraries(synth_canvas_gdextension PRIVATE
    # Link against the compiled godot-cpp library
    godot-cpp

    # Link against clap and clap-helpers
    clap
    clap-helpers

    # Link against readerwriterqueue
    readerwriterqueue
)

# --- Platform Specific Configuration ---
if(ANDROID)
    # Add and link Oboe only for Android
    # For out-of-tree sources, an explicit binary directory must be specified.
    add_subdirectory(${CMAKE_SOURCE_DIR}/libs/oboe ${CMAKE_CURRENT_BINARY_DIR}/oboe)
    # Mark oboe headers as SYSTEM
    target_include_directories(synth_canvas_gdextension SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/libs/oboe/include)
    target_link_libraries(synth_canvas_gdextension PRIVATE oboe)

    # On Android, we also need to link against libdl for dlopen/dlsym.
    target_link_libraries(synth_canvas_gdextension PRIVATE dl)

    # Link logging library
    find_library(log-lib log)
    target_link_libraries(synth_canvas_gdextension PRIVATE ${log-lib})

    # Define that this is an Android build for our source code
    target_compile_definitions(synth_canvas_gdextension PRIVATE __ANDROID__)

elseif(WIN32)
    # Add standard Windows definitions to avoid library conflicts.
    target_compile_definitions(synth_canvas_gdextension PRIVATE WIN32 _WINDOWS NDEBUG _WIN32 NOMINMAX)
endif()


# --- Post-build command to copy the library for immediate use ---
add_custom_command(
    TARGET synth_canvas_gdextension
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:synth_canvas_gdextension>"
        "${CMAKE_SOURCE_DIR}/bin/"
    COMMENT "Copying synth_canvas_gdextension to root bin directory for Godot to use."
)

# --- Installation (Optional but good practice) ---
# This helps in packaging the final library.
# The destination can be adjusted based on your project structure.
install(TARGETS synth_canvas_gdextension
    DESTINATION ${CMAKE_SOURCE_DIR}/bin
)