cmake_minimum_required(VERSION 3.22)
project(fanfold_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Test)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

enable_testing()

if(NOT TARGET FanFold::DocumentEngine)
    add_subdirectory(../../src/fanfold fanfold-engine)
endif()

# Core-only suites: the document engine, the Library/search projections and the session
# controller all run headless with synthetic QTemporaryDir note and state roots.
foreach(suite documentcollection librarymodels session themecatalog)
    add_executable(tst_${suite} tst_${suite}.cpp)
    target_link_libraries(tst_${suite} PRIVATE FanFold::DocumentEngine Qt6::Core Qt6::Test)
    add_test(NAME fanfold-${suite} COMMAND tst_${suite})
    set_tests_properties(fanfold-${suite} PROPERTIES TIMEOUT 120)
endforeach()

# The optional network importer is source-only. Its complete safety contract is
# exercised with generated in-memory pages and temporary files; no real catalog,
# retained private snapshot, or network request is part of the public test tree.
add_test(
    NAME fanfold-terminalcolors-importer
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_terminalcolors.py
)
set_tests_properties(fanfold-terminalcolors-importer PROPERTIES
    TIMEOUT 120
    # 77 = the importer is absent. tools/terminalcolors.py is redistribution-blocked and
    # excluded from the tarball, so a published source build must SKIP this contract, not
    # fail it. A local checkout has the importer and runs it for real.
    SKIP_RETURN_CODE 77
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

if(FANFOLD_LOCAL_THEME_BUNDLE)
    set_tests_properties(fanfold-themecatalog PROPERTIES
        ENVIRONMENT "FANFOLD_TEST_THEME_BUNDLE=${FANFOLD_LOCAL_THEME_CATALOG_DIR}")
endif()

# The fan's geometry contract. LayoutContract.js is pure and carries no scene state, but
# what it decides is WHICH NOTE a click opens, so the suite rebuilds the stick delegate as
# Main.qml binds it and fires real mouse events through Qt's hit-testing rather than
# asserting returned numbers alone.
#
# Run by qmltestrunner, which needs no compiled target: the contract is JavaScript and the
# delegate under test is QML. Both files are staged into one build directory so the QML
# `import "LayoutContract.js"` resolves, and the staged copy is refreshed by configure_file
# whenever the real contract changes, so the suite can never run against a stale duplicate.
find_program(QMLTESTRUNNER_EXECUTABLE
    NAMES qmltestrunner qmltestrunner-qt6
    HINTS ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS} /usr/lib/qt6/bin)

if(QMLTESTRUNNER_EXECUTABLE)
    set(FANFOLD_QML_TEST_STAGE ${CMAKE_CURRENT_BINARY_DIR}/qml-contract)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell/qml/LayoutContract.js
        ${FANFOLD_QML_TEST_STAGE}/LayoutContract.js COPYONLY)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell/qml/LibraryRowAction.qml
        ${FANFOLD_QML_TEST_STAGE}/LibraryRowAction.qml COPYONLY)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_layoutcontract.qml
        ${FANFOLD_QML_TEST_STAGE}/tst_layoutcontract.qml COPYONLY)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_libraryrowaction.qml
        ${FANFOLD_QML_TEST_STAGE}/tst_libraryrowaction.qml COPYONLY)

    add_test(
        NAME fanfold-layoutcontract
        COMMAND ${QMLTESTRUNNER_EXECUTABLE} -input ${FANFOLD_QML_TEST_STAGE})
    # offscreen: the suite needs a real QQuickWindow for event delivery, but no display.
    set_tests_properties(fanfold-layoutcontract PROPERTIES
        TIMEOUT 120
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
else()
    message(WARNING
        "qmltestrunner not found; the LayoutContract geometry suite will not run.")
endif()

# Appearance needs a real QGuiApplication for the platform font database; it selects the
# offscreen platform plugin itself and opens no display.
add_executable(tst_appearance tst_appearance.cpp)
target_link_libraries(tst_appearance PRIVATE FanFold::FontCatalog Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME fanfold-appearance COMMAND tst_appearance)
set_tests_properties(fanfold-appearance PROPERTIES TIMEOUT 120)

# Install, launch, reinstall and remove a disposable DESTDIR package. This is the
# publication contract: no runtime asset may resolve through the source checkout, and
# removal must leave notes and XDG state untouched.
if(TARGET fanfold)
    add_test(
        NAME fanfold-package
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/package/run_package_test.sh
                ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${FANFOLD_LOCAL_THEME_BUNDLE}
    )
    set_tests_properties(fanfold-package PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)
endif()
