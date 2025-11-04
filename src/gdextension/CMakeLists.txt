# GDExtension Library CMakeLists.txt

# Set the C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Add Sub-libraries ---

# Add godot-cpp. It's a submodule in the current directory.
# We build it with GDExtension bindings enabled.
set(GODOT_CPP_BUILD_EXTENSIONS ON)
add_subdirectory(godot-cpp)

# Add clap and clap-helpers for the plugin host logic.
# For out-of-tree sources, an explicit binary directory must be specified.
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/clap ${CMAKE_CURRENT_BINARY_DIR}/clap)
add_subdirectory(${CMAKE_SOURCE_DIR}/libs/clap-helpers ${CMAKE_CURRENT_BINARY_DIR}/clap-helpers)

# --- Define the GDExtension Library ---

# Library name that Godot will look for.
set(LIBRARY_NAME "synth_canvas_gdextension")

# Define the source files for the library
set(GDE_SOURCES
    register_types.cpp
    clap_host_node.cpp
    clap_host_node.h
    ../host/audio_engine.cpp
    ../host/audio_engine.h
    ../host/plugin_host.h
)

# Only add the full plugin_host implementation for non-windows builds.
# Windows uses a header-only dummy class.
if(NOT WIN32)
    list(APPEND GDE_SOURCES ../host/plugin_host.cpp)
endif()

# Create the shared library from the source files.
add_library(${LIBRARY_NAME} SHARED
    ${GDE_SOURCES}
)

# --- Configure Includes and Links ---

# Set the include directories required for our library.
target_include_directories(${LIBRARY_NAME} PRIVATE
    # For godot-cpp headers
    ${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/godot-cpp/gen/include

    # For our own host headers
    ${CMAKE_CURRENT_SOURCE_DIR}/..

    # For clap and clap-helpers headers
    ${CMAKE_SOURCE_DIR}/libs/clap/include
    ${CMAKE_SOURCE_DIR}/libs/clap-helpers/include
)

# Link the library against its dependencies.
target_link_libraries(${LIBRARY_NAME} PRIVATE
    # Link against the compiled godot-cpp library
    godot-cpp

    # Link against clap and clap-helpers
    clap
    clap-helpers
)

# --- Platform Specific Configuration ---
if(ANDROID)
    # Add and link Oboe only for Android
    # For out-of-tree sources, an explicit binary directory must be specified.
    add_subdirectory(${CMAKE_SOURCE_DIR}/libs/oboe ${CMAKE_CURRENT_BINARY_DIR}/oboe)
    target_include_directories(${LIBRARY_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/libs/oboe/include)
    target_link_libraries(${LIBRARY_NAME} PRIVATE oboe)

    # On Android, we also need to link against libdl for dlopen/dlsym.
    target_link_libraries(${LIBRARY_NAME} PRIVATE dl)

    # Link logging library
    find_library(log-lib log)
    target_link_libraries(${LIBRARY_NAME} PRIVATE ${log-lib})

    # Define that this is an Android build for our source code
    target_compile_definitions(${LIBRARY_NAME} PRIVATE __ANDROID__)

elseif(WIN32)
    # Add standard Windows definitions to avoid library conflicts.
    target_compile_definitions(${LIBRARY_NAME} PRIVATE WIN32 _WINDOWS NDEBUG _WIN32)
endif()


# --- Installation (Optional but good practice) ---
# This helps in packaging the final library.
# The destination can be adjusted based on your project structure.
install(TARGETS ${LIBRARY_NAME}
    DESTINATION ${CMAKE_SOURCE_DIR}/bin
)
