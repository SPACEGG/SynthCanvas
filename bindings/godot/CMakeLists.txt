# bindings/godot/CMakeLists.txt

# godot-cpp
include(FetchContent)
FetchContent_Declare(
    godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG        godot-4.5-stable
)
set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(godot-cpp)

# godot binding
set(GDE_SOURCES
    register_types.cpp
    synthcanvas_audio_system.cpp
)

add_library(synth_canvas_gdextension SHARED ${GDE_SOURCES})

if(ANDROID)
    set_target_properties(synth_canvas_gdextension PROPERTIES
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
    )

    target_compile_options(synth_canvas_gdextension PRIVATE -fexceptions)
    target_include_directories(synth_canvas_gdextension PRIVATE "${CORE_DIR}/libs/rtmidi")
endif()

target_include_directories(synth_canvas_gdextension PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against godot-cpp and the core engine library
if(ANDROID)
    target_link_libraries(synth_canvas_gdextension PRIVATE
        godot-cpp
        synth_canvas_core
        -Wl,--whole-archive rtmidi -Wl,--no-whole-archive
        amidi
        log
        android
        nativehelper
    )
else()
    target_link_libraries(synth_canvas_gdextension PRIVATE
        godot-cpp
        synth_canvas_core
        rtmidi
    )
endif()

# copy the library
add_custom_command(
    TARGET synth_canvas_gdextension
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:synth_canvas_gdextension>"
        "${CORE_DIR}/bin/"
    COMMENT "Copying synth_canvas_gdextension to root bin directory for Godot to use."
)

if(ANDROID)
    set(RTMIDI_JAVA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/MidiDeviceOpenedListener.java")
    set(GODOT_JAVA_DEST_DIR "${CORE_DIR}/../app/android/build/src/com/yellowlab/rtmidi")
    
    add_custom_command(
        TARGET synth_canvas_gdextension
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GODOT_JAVA_DEST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RTMIDI_JAVA_SRC}"
            "${GODOT_JAVA_DEST_DIR}/MidiDeviceOpenedListener.java"
        COMMENT "Copying fixed RtMidi Java listener to Godot Android build directory."
    )
endif()

install(TARGETS synth_canvas_gdextension
    DESTINATION ${CORE_DIR}/bin
)
