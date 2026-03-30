cmake_minimum_required(VERSION 3.21)

set(MIACODE_VERSION_MAJOR "0")
set(MIACODE_VERSION_MINOR "3")
set(MIACODE_VERSION_PATCH "6")
set(MIACODE_VERSION_PRERELEASE "dev2")
set(MIACODE_VERSION "${MIACODE_VERSION_MAJOR}.${MIACODE_VERSION_MINOR}.${MIACODE_VERSION_PATCH}")
set(MIACODE_VERSION_STRING "${MIACODE_VERSION}")
if (NOT "${MIACODE_VERSION_PRERELEASE}" STREQUAL "")
    set(MIACODE_VERSION_STRING "${MIACODE_VERSION}-${MIACODE_VERSION_PRERELEASE}")
endif()
set(MIACODE_DISPLAY_PRERELEASE "dev2")
set(MIACODE_DISPLAY_VERSION_STRING "${MIACODE_VERSION}")
if (NOT "${MIACODE_DISPLAY_PRERELEASE}" STREQUAL "")
    set(MIACODE_DISPLAY_VERSION_STRING "${MIACODE_DISPLAY_VERSION_STRING}.${MIACODE_DISPLAY_PRERELEASE}")
endif()

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
set(MIACODE_MACOS_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.icns" CACHE FILEPATH "Optional application icon (.icns) for macOS bundles")
option(MIACODE_BUILD_DEV_TOOLS "Build developer helper tools" OFF)

configure_file(
    src/app/AppVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/AppVersion.h
    @ONLY
)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Concurrent Multimedia)

add_executable(MiaCode WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/mainwindow/MainWindow.h
    src/app/mainwindow/MainWindow.cpp
    src/app/ui/UiText.h
    src/app/ui/UiText.cpp
    src/app/ui/UiTheme.h
    src/app/ui/UiTheme.cpp
    src/common/MuriTypes.h
    src/common/MuriTypes.cpp
    src/common/MuriRenderOptions.h
    src/common/MuriConfig.h
    src/common/ChartAssetPaths.h
    src/common/PreviewSfxTimeline.h
    src/editor/PlainCodeEditor.h
    src/editor/PlainCodeEditor.cpp
    src/editor/BracketScopeHighlighter.h
    src/editor/BracketScopeHighlighter.cpp
    src/tools/latency/LatencyDetectorDialog.h
    src/tools/latency/LatencyDetectorDialog.cpp
    src/tools/muri/MuriAnalyzer.h
    src/tools/muri/MuriAnalyzer.cpp
    src/tools/muri/MuriStaticChecker.h
    src/tools/muri/MuriStaticChecker.cpp
    src/tools/video_export/VideoExportDialog.h
    src/tools/video_export/VideoExportDialog.cpp
    src/tools/video_export/BatchVideoExportDialog.h
    src/tools/video_export/BatchVideoExportDialog.cpp
    src/tools/video_export/VideoExportController.h
    src/tools/video_export/VideoExportController.cpp
    src/tools/video_export/RawVideoPipeTransport.h
    src/tools/video_export/RawVideoPipeTransport.cpp
    src/tools/video_export/VideoExportSnapshot.h
    src/tools/video_export/VideoExportSnapshot.cpp
    src/preview/video/PreviewCanvas.h
    src/preview/video/PreviewCanvas.cpp
    src/preview/video/PreviewGLRenderer.h
    src/preview/video/PreviewGLRenderer.cpp
    src/preview/video/PreviewMediaController.h
    src/preview/video/PreviewMediaController.cpp
    src/preview/audio/PreviewAudioSettings.h
    src/preview/audio/PreviewAudioSettings.cpp
    src/preview/audio/QtPreviewSfxRuntime.h
    src/preview/audio/QtPreviewSfxRuntime.cpp
    src/simai/transform/ChartBatchTransform.h
    src/simai/transform/ChartBatchTransform.cpp
    src/simai/document/SimaiDocument.h
    src/simai/document/SimaiDocument.cpp
    src/simai/parser/SimaiNativeParser.h
    src/simai/parser/SimaiNativeParser.cpp
    src/timeline/TimelineData.h
    src/timeline/TimelineRenderData.h
    src/timeline/TimelineQuickModel.h
    src/timeline/TimelineQuickModel.cpp
    src/timeline/TimelineSlowRefresh.h
    src/timeline/TimelineSlowRefresh.cpp
    src/timeline/TimelineView.h
    src/timeline/TimelineView.cpp
    resources/app_icons.qrc
    resources/fonts.qrc
)
set_target_properties(MiaCode PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "MiaCode"
)

if (WIN32 AND EXISTS "${MIACODE_APP_ICON}")
    file(TO_CMAKE_PATH "${MIACODE_APP_ICON}" MIACODE_APP_ICON_RC_PATH)
    set(MIACODE_APP_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/miacode_app_icon.rc")
    file(WRITE "${MIACODE_APP_RC}" "IDI_APP_ICON ICON \"${MIACODE_APP_ICON_RC_PATH}\"\n")
    target_sources(MiaCode PRIVATE "${MIACODE_APP_RC}")
endif()

if (APPLE AND EXISTS "${MIACODE_MACOS_APP_ICON}")
    set_source_files_properties("${MIACODE_MACOS_APP_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(MiaCode PRIVATE "${MIACODE_MACOS_APP_ICON}")
    set_target_properties(MiaCode PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "app.icns"
    )
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
    src/app/ui
    src/editor
    src/preview
    src/preview/audio
    src/preview/video
    src/simai
    src/simai/document
    src/simai/parser
    src/timeline
    src/tools/latency
    src/tools/video_export
)

target_link_libraries(MiaCode PRIVATE Qt6::Multimedia)
target_compile_definitions(MiaCode PRIVATE HAVE_QT_MULTIMEDIA=1)

if (MIACODE_BUILD_DEV_TOOLS)
    add_executable(miacode_muri_dump
        src/tools/muri/MuriDump.cpp
        src/common/MuriTypes.h
        src/common/MuriTypes.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        src/tools/muri/MuriAnalyzer.h
        src/tools/muri/MuriAnalyzer.cpp
        src/tools/muri/MuriStaticChecker.h
        src/tools/muri/MuriStaticChecker.cpp
        resources/fonts.qrc
    )

    add_executable(simai_native_dump
        src/simai/parser/SimaiNativeDump.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        resources/fonts.qrc
    )

    add_executable(soundtouch_probe
        src/tools/probe/SoundtouchProbe.cpp
        src/preview/audio/PreviewAudioSettings.h
        src/preview/audio/PreviewAudioSettings.cpp
        src/preview/audio/QtPreviewSfxRuntime.h
        src/preview/audio/QtPreviewSfxRuntime.cpp
    )

    add_executable(simai_parser_spec
        src/tools/simai_parser/SimaiParserSpec.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        resources/fonts.qrc
    )

    add_executable(chart_batch_transform_spec
        src/tools/chart_transform/ChartBatchTransformSpec.cpp
        src/simai/transform/ChartBatchTransform.h
        src/simai/transform/ChartBatchTransform.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        resources/fonts.qrc
    )

    target_link_libraries(miacode_muri_dump PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(miacode_muri_dump PRIVATE
        src
        src/app/ui
        src/common
        src/simai
        src/simai/document
        src/simai/parser
        src/timeline
        src/tools
    )

    target_link_libraries(simai_native_dump PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(simai_native_dump PRIVATE
        src
        src/simai
        src/simai/parser
        src/timeline
    )

    target_link_libraries(simai_parser_spec PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(simai_parser_spec PRIVATE
        src
        src/app/ui
        src/simai
        src/simai/parser
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
        src/preview/audio
        src/preview/video
        src/timeline
        src/tools
    )

    target_link_libraries(chart_batch_transform_spec PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(chart_batch_transform_spec PRIVATE
        src
        src/app/ui
        src/common
        src/simai
        src/simai/document
        src/simai/parser
        src/simai/transform
        src/timeline
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
