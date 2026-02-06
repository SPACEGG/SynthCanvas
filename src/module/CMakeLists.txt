# Define a function to add the post-build copy command to a plugin target.
# This function can be called from subdirectories' CMakeLists.txt files.
function(add_plugin_post_build_copy_command target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/bin/module"
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:${target_name}>
            "${CMAKE_SOURCE_DIR}/bin/module/"
        COMMENT "Copying ${target_name} to bin/module"
    )
endfunction()

# Set the output directory for all plugin libraries built from this point onwards.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/module")

# Automatically find and add all plugin subdirectories
file(GLOB plugin_dirs LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)

foreach(dir ${plugin_dirs})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${dir}" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt")
        add_subdirectory(${dir})
    endif()
endforeach()
