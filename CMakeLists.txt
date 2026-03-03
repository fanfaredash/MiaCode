cmake_minimum_required(VERSION 3.21)

project(maicode VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
find_package(Qt6 QUIET COMPONENTS Multimedia)

find_path(QSCINTILLA_INCLUDE_DIR NAMES Qsci/qsciscintilla.h)
find_library(QSCINTILLA_LIBRARY NAMES qscintilla2_qt6 qscintilla2-qt6 qscintilla2)

add_executable(maicode WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/PlainCodeEditor.h
    src/PlainCodeEditor.cpp
    src/PreviewCanvas.h
    src/PreviewCanvas.cpp
    src/PreviewGLRenderer.h
    src/PreviewGLRenderer.cpp
    src/PreviewMediaController.h
    src/PreviewMediaController.cpp
    src/PreviewIntegration.h
    src/PreviewIntegration.cpp
    src/PreviewAudioSettings.h
    src/PreviewAudioSettings.cpp
    src/QtPreviewSfxRuntime.h
    src/QtPreviewSfxRuntime.cpp
    src/SimaiNativeParser.h
    src/SimaiNativeParser.cpp
    src/TimelineView.h
    src/TimelineView.cpp
    resources/fonts.qrc
)

add_executable(simai_native_dump
    src/SimaiNativeDump.cpp
    src/SimaiNativeParser.h
    src/SimaiNativeParser.cpp
    src/TimelineView.h
    src/TimelineView.cpp
    resources/fonts.qrc
)

target_link_libraries(maicode PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::OpenGLWidgets
    Qt6::Widgets
)

target_link_libraries(simai_native_dump PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if (TARGET Qt6::Multimedia)
    target_link_libraries(maicode PRIVATE Qt6::Multimedia)
    target_compile_definitions(maicode PRIVATE HAVE_QT_MULTIMEDIA=1)
endif()

if (QSCINTILLA_INCLUDE_DIR AND QSCINTILLA_LIBRARY)
    target_include_directories(maicode PRIVATE ${QSCINTILLA_INCLUDE_DIR})
    target_link_libraries(maicode PRIVATE ${QSCINTILLA_LIBRARY})
    target_compile_definitions(maicode PRIVATE HAVE_QSCINTILLA=1)
endif()

if (WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${QT_BIN_DIR}")

    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET maicode POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                "$<$<CONFIG:Debug>:--debug>$<$<NOT:$<CONFIG:Debug>>:--release>"
                --compiler-runtime
                --no-translations
                "$<TARGET_FILE:maicode>"
            COMMENT "Deploying Qt runtime next to maicode.exe"
            VERBATIM
        )
    else()
        message(WARNING "windeployqt not found. Qt runtime DLLs will not be auto-deployed.")
    endif()
endif()
