cmake_minimum_required(VERSION 3.21)

project(FanFold VERSION 0.2.0 LANGUAGES CXX)

# The revision a package actually carries (e.g. "0.1.0+r42.20260922…"), supplied by the
# packaging scripts. Without it a source build reports the project version, so the About
# panel never lies — it just says less.
if(NOT DEFINED FANFOLD_BUILD_VERSION)
  set(FANFOLD_BUILD_VERSION "${PROJECT_VERSION}")
endif()
add_compile_definitions(FANFOLD_BUILD_VERSION="${FANFOLD_BUILD_VERSION}")

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
find_package(Qt6 6.8 QUIET COMPONENTS Test)

qt_standard_project_setup(REQUIRES 6.8)

# The document engine: plain-Markdown storage, the library catalogue and the file
# lifecycle. Depends only on Qt Core/Gui/Qml/Quick.
add_subdirectory(src/fanfold)

# The shell: QML presentation and the adapters that bind it to the engine. QtWebEngine
# and QtWebChannel are QML imports resolved at runtime, not build-time dependencies.
add_subdirectory(src/shell)

if(BUILD_TESTING AND TARGET Qt6::Test)
    add_subdirectory(tests/fanfold)
endif()

# `cmake --build build --target uninstall` removes exactly what install_manifest.txt
# records, so a source install can be backed out without guessing at paths.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/fanfold/packaging/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    IMMEDIATE @ONLY)
add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
