cmake_minimum_required(VERSION 3.21)

set(MIACODE_VERSION_MAJOR "0" CACHE STRING "miacode major version")
set(MIACODE_VERSION_MINOR "1" CACHE STRING "miacode minor version")
set(MIACODE_VERSION_PATCH "0" CACHE STRING "miacode patch version")
set(MIACODE_VERSION "${MIACODE_VERSION_MAJOR}.${MIACODE_VERSION_MINOR}.${MIACODE_VERSION_PATCH}")

project(MiaCode VERSION ${MIACODE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum macOS deployment version" FORCE)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(MIACODE_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico" CACHE FILEPATH "Optional application icon (.ico) for Windows builds")
option(MIACODE_BUILD_DEV_TOOLS "Build developer helper tools" OFF)

configure_file(
    src/app/AppVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/AppVersion.h
    @ONLY
)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Concurrent Multimedia)

add_executable(MiaCode WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/UiText.h
    src/app/UiText.cpp
    src/editor/PlainCodeEditor.h
    src/editor/PlainCodeEditor.cpp
    src/preview/PreviewCanvas.h
    src/preview/PreviewCanvas.cpp
    src/preview/PreviewGLRenderer.h
    src/preview/PreviewGLRenderer.cpp
    src/preview/PreviewMediaController.h
    src/preview/PreviewMediaController.cpp
    src/preview/PreviewIntegration.h
    src/preview/PreviewIntegration.cpp
    src/preview/PreviewAudioSettings.h
    src/preview/PreviewAudioSettings.cpp
    src/preview/QtPreviewSfxRuntime.h
    src/preview/QtPreviewSfxRuntime.cpp
    src/simai/SimaiDocument.h
    src/simai/SimaiDocument.cpp
    src/simai/SimaiNativeParser.h
    src/simai/SimaiNativeParser.cpp
    src/timeline/TimelineView.h
    src/timeline/TimelineView.cpp
    resources/fonts.qrc
)
set_target_properties(MiaCode PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "MiaCode"
)

if (WIN32 AND EXISTS "${MIACODE_APP_ICON}")
    file(TO_NATIVE_PATH "${MIACODE_APP_ICON}" MIACODE_APP_ICON_NATIVE)
    set(MIACODE_APP_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/miacode_app_icon.rc")
    file(WRITE "${MIACODE_APP_RC}" "IDI_APP_ICON ICON \"${MIACODE_APP_ICON_NATIVE}\"\n")
    target_sources(MiaCode PRIVATE "${MIACODE_APP_RC}")
endif()

add_library(soundtouch STATIC
    third_party/soundtouch/source/SoundTouch/AAFilter.cpp
    third_party/soundtouch/source/SoundTouch/BPMDetect.cpp
    third_party/soundtouch/source/SoundTouch/cpu_detect_x86.cpp
    third_party/soundtouch/source/SoundTouch/FIFOSampleBuffer.cpp
    third_party/soundtouch/source/SoundTouch/FIRFilter.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateCubic.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateLinear.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateShannon.cpp
    third_party/soundtouch/source/SoundTouch/mmx_optimized.cpp
    third_party/soundtouch/source/SoundTouch/PeakFinder.cpp
    third_party/soundtouch/source/SoundTouch/RateTransposer.cpp
    third_party/soundtouch/source/SoundTouch/SoundTouch.cpp
    third_party/soundtouch/source/SoundTouch/sse_optimized.cpp
    third_party/soundtouch/source/SoundTouch/TDStretch.cpp
)
target_include_directories(soundtouch PUBLIC
    third_party/soundtouch/include
    third_party/soundtouch/source/SoundTouch
)
target_compile_definitions(soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)

target_link_libraries(MiaCode PRIVATE
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGLWidgets
    Qt6::Widgets
    soundtouch
)
target_include_directories(MiaCode PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    src
    src/app
    src/editor
    src/preview
    src/simai
    src/timeline
)

target_link_libraries(MiaCode PRIVATE Qt6::Multimedia)
target_compile_definitions(MiaCode PRIVATE HAVE_QT_MULTIMEDIA=1)

if (MIACODE_BUILD_DEV_TOOLS)
    add_executable(simai_native_dump
        src/simai/SimaiNativeDump.cpp
        src/simai/SimaiNativeParser.h
        src/simai/SimaiNativeParser.cpp
        src/timeline/TimelineView.h
        src/timeline/TimelineView.cpp
        resources/fonts.qrc
    )

    add_executable(soundtouch_probe
        src/tools/SoundtouchProbe.cpp
        src/preview/PreviewAudioSettings.h
        src/preview/PreviewAudioSettings.cpp
        src/preview/QtPreviewSfxRuntime.h
        src/preview/QtPreviewSfxRuntime.cpp
        src/timeline/TimelineView.h
        src/timeline/TimelineView.cpp
    )

    target_link_libraries(simai_native_dump PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(simai_native_dump PRIVATE
        src
        src/simai
        src/timeline
    )

    target_link_libraries(soundtouch_probe PRIVATE
        Qt6::Concurrent
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        soundtouch
    )
    target_include_directories(soundtouch_probe PRIVATE
        src
        src/preview
        src/timeline
        src/tools
    )
endif()

if (WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${QT_BIN_DIR}")

    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET MiaCode POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                "$<$<CONFIG:Debug>:--debug>$<$<NOT:$<CONFIG:Debug>>:--release>"
                --compiler-runtime
                --no-translations
                "$<TARGET_FILE:MiaCode>"
            COMMENT "Deploying Qt runtime next to MiaCode.exe"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found. Qt runtime DLLs will not be auto-deployed.")
    endif()
endif()

