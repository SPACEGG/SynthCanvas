cmake_minimum_required(VERSION 3.15)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.11 CACHE STRING "Build for 10.1")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "CMAKE_BUILD_TYPE Type Unspecified; picking Release")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

project(clap-saw-demo VERSION 0.9.0 LANGUAGES C CXX)


set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)


# use asan as an option (currently mac only)
option(USE_SANITIZER "Build and link with ASAN" FALSE)
# SynthCanvas: Default to FALSE for Android/Headless build
option(CSD_INCLUDE_GUI "Include a GUI in ClapSawDemo" FALSE)

# Copy on mac (could expand to other platforms)
option(COPY_AFTER_BUILD "Copy the clap to ~/Library on MACOS, ~/.clap on linux" FALSE)

# SynthCanvas: Only add subdirectories if targets don't exist
if(NOT TARGET clap)
    add_subdirectory(libs/clap EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET clap-helpers)
    add_subdirectory(libs/clap-helpers EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET readerwriterqueue)
    add_subdirectory(libs/readerwriterqueue EXCLUDE_FROM_ALL)
endif()

if (${CSD_INCLUDE_GUI})
    message(STATUS "Including VSTGUI")
    add_subdirectory(libs/vstgui EXCLUDE_FROM_ALL)
else()
    message(STATUS "GUI Bypassed. headless CSD generated")
endif()
if (APPLE AND ${CSD_INCLUDE_GUI})
  target_compile_options(vstgui PRIVATE -Wno-deprecated-declarations)
endif()


add_library(${PROJECT_NAME} MODULE
        src/clap-saw-demo.cpp
        src/saw-voice.cpp
        src/clap-saw-demo-pluginentry.cpp
)
target_link_libraries(${PROJECT_NAME} clap-core clap-helpers readerwriterqueue)

# Disable clang-tidy for this target
set_target_properties(${PROJECT_NAME} PROPERTIES 
    C_CLANG_TIDY ""
    CXX_CLANG_TIDY ""
)

if (${CSD_INCLUDE_GUI})
    target_sources(${PROJECT_NAME} PRIVATE
    src/clap-saw-demo-editor.cpp
    src/linux-vstgui-adapter.cpp src/linux-vstgui-adapter.h)
    target_link_libraries(${PROJECT_NAME} vstgui)
else()
    message(STATUS "Skipping GUI generation")
endif()

if (${CSD_INCLUDE_GUI})
    # VSTGUI doesn't include the include paths and compile definitions in the library
    # targets like other CMake libraries do, so explicitly include them here
    target_include_directories(${PROJECT_NAME} PUBLIC libs/vstgui)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_GUI=1 ${VSTGUI_COMPILE_DEFINITIONS})
endif()

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
            BUNDLE True
            BUNDLE_EXTENSION clap
            MACOSX_BUNDLE_GUI_IDENTIFIER org.surge-synth-team.${PROJECT_NAME}
            MACOSX_BUNDLE_BUNDLE_NAME ${PROJECT_NAME}
            MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/${PROJECT_NAME}.plist.in
            )
    target_link_libraries(${PROJECT_NAME} "-framework CoreFoundation" "-framework AppKit" "-framework CoreGraphics")

    target_compile_definitions(${PROJECT_NAME} PRIVATE IS_MAC=1)
    target_compile_options(${PROJECT_NAME} PRIVATE
            -Werror
            $<$<BOOL:${USE_SANITIZER}>:-fsanitize=address>
            $<$<BOOL:${USE_SANITIZER}>:-fsanitize=undefined>
            )
    target_link_options(${PROJECT_NAME} PRIVATE
            $<$<BOOL:${USE_SANITIZER}>:-fsanitize=address>
            $<$<BOOL:${USE_SANITIZER}>:-fsanitize=undefined>
            )

    if (${COPY_AFTER_BUILD})
        message(STATUS "Will copy plugin after every build" )
        set(products_folder ${CMAKE_BINARY_DIR})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "Installing ${products_folder}/${PROJECT_NAME}.clap to ~/Library/Audio/Plug-Ins/CLAP/"
                COMMAND ${CMAKE_COMMAND} -E make_directory "~/Library/Audio/Plug-Ins/CLAP"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${products_folder}/${PROJECT_NAME}.clap" "~/Library/Audio/Plug-Ins/CLAP/${PROJECT_NAME}.clap"
                )
    endif()
elseif(UNIX)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IS_LINUX=1)
    if (${CSD_INCLUDE_GUI})
        target_sources(${PROJECT_NAME} PRIVATE src/linux-vstgui-adapter.cpp)
    endif()
    # SynthCanvas: Use .so suffix for Android builds, .clap for others
    if(NOT ANDROID)
        set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".clap" PREFIX "")
    else()
        # For Android, ensure it builds as a standard shared library (.so)
        set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".so" PREFIX "lib")
    endif()
    if (${COPY_AFTER_BUILD})
        message(STATUS "Will copy plugin after every build" )
        set(products_folder ${CMAKE_BINARY_DIR})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E echo "Installing ${products_folder}/${PROJECT_NAME}.clap to ~/.clap"
                COMMAND ${CMAKE_COMMAND} -E make_directory "~/.clap"
                COMMAND ${CMAKE_COMMAND} -E copy "${products_folder}/${PROJECT_NAME}.clap" "~/.clap"
                )
    endif()

else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE IS_WIN=1)
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".clap" PREFIX "")
endif()

# SynthCanvas: Copy artifact to bin/module
if(COMMAND add_plugin_post_build_copy_command)
    add_plugin_post_build_copy_command(${PROJECT_NAME})
endif()