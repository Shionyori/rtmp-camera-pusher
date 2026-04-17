cmake_minimum_required(VERSION 3.16)

set(TARGET_NAME "rtmp-camera-pusher")
project(${TARGET_NAME} VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_CLI "Build CLI application (primary runtime)" ON)
option(BUILD_GUI "Build GUI helper application" ON)


# If QT_DIR is defined, add it to CMAKE_PREFIX_PATH so find_package can locate Qt
if(DEFINED QT_DIR)
    list(APPEND CMAKE_PREFIX_PATH ${QT_DIR}) 
endif()

# Find Qt6 package. Widgets module is only required when GUI target is enabled.
set(QT_COMPONENTS
    Core
    Gui
    Multimedia
)
if(BUILD_GUI)
    list(APPEND QT_COMPONENTS Widgets)
endif()

find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil libswscale)

# Shared core library (no QWidget dependency)
add_library(${PROJECT_NAME}-core
    src/common/AppLocale.cpp
    src/common/AppLocale.h
    src/core/StreamSession.cpp
    src/core/StreamSession.h
    src/streaming/RtmpStreamer.cpp
    src/streaming/RtmpStreamer.h
)

target_include_directories(${PROJECT_NAME}-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME}-core PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Multimedia
)

# CLI executable (primary target)
if(BUILD_CLI)
    set(CLI_TARGET ${PROJECT_NAME})
    qt_add_executable(${CLI_TARGET}
        src/cli/main.cpp
    )

    target_include_directories(${CLI_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(${CLI_TARGET} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Multimedia
        ${PROJECT_NAME}-core
    )
endif()

# GUI executable (helper tool)
if(BUILD_GUI)
    set(GUI_TARGET ${PROJECT_NAME}-gui)
    qt_add_executable(${GUI_TARGET}
        src/gui/main.cpp
        src/gui/MainWindow.cpp
        src/gui/MainWindow.h
    )

    target_include_directories(${GUI_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(${GUI_TARGET} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Multimedia
        ${PROJECT_NAME}-core
    )
endif()

if(FFMPEG_FOUND)
    target_link_libraries(${PROJECT_NAME}-core PRIVATE PkgConfig::FFMPEG)
    target_compile_definitions(${PROJECT_NAME}-core PRIVATE HAS_FFMPEG=1)
    message(STATUS "FFmpeg found. RTMP streaming features enabled.")
else()
    message(WARNING "FFmpeg not found. RTMP streaming features are disabled.")
endif()

# Deploy Qt dependencies using windeployqt (Windows) or macdeployqt (macOS)
if(WIN32 AND BUILD_GUI)
    # Find windeployqt tool based on Qt6::qmake location
    if(TARGET Qt6::qmake)
        get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    else()
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/bin)
    endif()

    if(WINDEPLOYQT_EXECUTABLE)
        # Run windeployqt after build to copy necessary DLLs to the output directory
        # Use generator expressions to select --debug or --release based on configuration
        add_custom_command(TARGET ${GUI_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Deploying Qt dependencies via windeployqt..."
            COMMAND ${WINDEPLOYQT_EXECUTABLE} 
                $<$<CONFIG:Debug>:--debug>
                $<$<CONFIG:Release>:--release>
                --no-compiler-runtime # Avoid copying compiler runtime, only copy Qt libraries
                --no-opengl-sw        # Usually software rasterizer is not needed
                --no-translations     # Translation files are usually not needed during development
                --no-system-d3d-compiler
                --verbose 0
                $<TARGET_FILE:${GUI_TARGET}>
            COMMENT "Running windeployqt to copy DLLs..."
        )
    endif()
elseif(APPLE AND BUILD_GUI)
    # macOS Bundle configuration
    set_target_properties(${GUI_TARGET} PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
        MACOSX_BUNDLE_BUNDLE_NAME ${GUI_TARGET}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
    
    # Find macdeployqt tool (optional, for generating standalone .app)
    if(TARGET Qt6::qmake)
        get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
        find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")
    else()
        find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS ${Qt6_DIR}/bin)
    endif()

    if(MACDEPLOYQT_EXECUTABLE)
        # Run macdeployqt after build to create .app bundle and copy necessary Frameworks
        add_custom_command(TARGET ${GUI_TARGET} POST_BUILD
            COMMAND ${MACDEPLOYQT_EXECUTABLE} 
                $<TARGET_BUNDLE_DIR:${GUI_TARGET}>
                -verbose=0
            COMMENT "Running macdeployqt to bundle Frameworks..."
        )
    endif()
elseif(UNIX)
    # On Linux, extra deployment steps are usually not needed as Qt libraries should be installed on the system
    # However, you can add post-build steps if you need to copy specific resources or libraries
endif()

