cmake_minimum_required(VERSION 3.16)

project(Desktop VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Core)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/inc)
set(SRC_DIR     ${CMAKE_SOURCE_DIR}/src)
set(UI_DIR      ${CMAKE_SOURCE_DIR}/ui)

include_directories(${INCLUDE_DIR})

file(GLOB_RECURSE HEADERS
    ${INCLUDE_DIR}/*.h
    ${INCLUDE_DIR}/*.hpp
)

file(GLOB_RECURSE SOURCES
    ${SRC_DIR}/*.cpp
)

file(GLOB_RECURSE FORMS
    ${UI_DIR}/*.ui
)

set(ALL_PROJECT_FILES
    ${SOURCES}
    ${HEADERS}
    ${FORMS}
)


if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    # Qt6 标准创建方式
    qt_add_executable(Desktop
        MANUAL_FINALIZATION
        ${ALL_PROJECT_FILES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET Desktop APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(Desktop SHARED
            ${ALL_PROJECT_FILES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(Desktop
            ${ALL_PROJECT_FILES}
        )
    endif()
endif()

target_link_libraries(Desktop PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network)
set_target_properties(Desktop PROPERTIES
    AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/ui"
)
# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Desktop)
endif()
set_target_properties(Desktop PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS Desktop
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Desktop)
endif()
