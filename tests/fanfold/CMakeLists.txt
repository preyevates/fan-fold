cmake_minimum_required(VERSION 3.22)
project(fanfold_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Test)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

enable_testing()

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME fanfold-pending-edits
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_pending_edits.js)
    add_test(NAME fanfold-navigation-gate
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_navigation_gate.js)
    add_test(NAME fanfold-markdown-fidelity
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_markdown_fidelity.js)
endif()

add_test(NAME fanfold-icon-export-security
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_icon_export_security.py)
add_test(NAME fanfold-asset-import-security
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_asset_import_security.py)

# A valid metainfo file alone is not sufficient: Discover's PackageKit backend
# discards components without a package name. Check the AppStream catalog output.
find_program(APPSTREAMCLI_EXECUTABLE appstreamcli)
if(APPSTREAMCLI_EXECUTABLE)
    add_test(NAME fanfold-appstream-package
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_appstream_metadata.py
                ${APPSTREAMCLI_EXECUTABLE})
    set_tests_properties(fanfold-appstream-package PROPERTIES TIMEOUT 30)
endif()

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

# Integrated editor-store and document-engine disk revision contract.
add_executable(tst_externalediting
    tst_externalediting.cpp
    ../../src/shell/storeadapter.cpp
    ../../src/shell/storeadapter.h)
target_include_directories(tst_externalediting PRIVATE ../../src/shell)
target_link_libraries(tst_externalediting PRIVATE FanFold::DocumentEngine Qt6::Core Qt6::Test)
add_test(NAME fanfold-externalediting COMMAND tst_externalediting)
set_tests_properties(fanfold-externalediting PROPERTIES TIMEOUT 120)

# The shell adapter keeps search transient: matches replace the visible deck without
# changing or persisting the open folder's own order.
add_executable(tst_notesadapter
    tst_notesadapter.cpp
    ../../src/shell/notesadapter.cpp
    ../../src/shell/notesadapter.h)
target_include_directories(tst_notesadapter PRIVATE ../../src/shell)
target_link_libraries(tst_notesadapter PRIVATE FanFold::DocumentEngine Qt6::Core Qt6::Test)
add_test(NAME fanfold-notesadapter COMMAND tst_notesadapter)
set_tests_properties(fanfold-notesadapter PROPERTIES TIMEOUT 120)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_fanscroll.qml
        ${FANFOLD_QML_TEST_STAGE}/tst_fanscroll.qml COPYONLY)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_libraryrowaction.qml
        ${FANFOLD_QML_TEST_STAGE}/tst_libraryrowaction.qml COPYONLY)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_panelreserve.qml
        ${FANFOLD_QML_TEST_STAGE}/tst_panelreserve.qml COPYONLY)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_noscreen.qml
        ${FANFOLD_QML_TEST_STAGE}/tst_noscreen.qml COPYONLY)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src/shell/qml/FanAutoHideReveal.qml
        ${FANFOLD_QML_TEST_STAGE}/FanAutoHideReveal.qml COPYONLY)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/qml/tst_autohidereveal.qml
        ${FANFOLD_QML_TEST_STAGE}/tst_autohidereveal.qml COPYONLY)

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
    find_package(Qt6 REQUIRED COMPONENTS Widgets Qml Quick DBus)
    find_package(KF6DBusAddons REQUIRED)
    add_executable(fanfold_shutdown_probe shutdown_probe.cpp
        ../../src/shell/main.cpp ../../src/shell/sessionexit.cpp ../../src/shell/appearanceadapter.cpp
        ../../src/shell/notesadapter.cpp ../../src/shell/storeadapter.cpp)
    target_compile_definitions(fanfold_shutdown_probe PRIVATE main=fanfoldApplicationMain)
    add_dependencies(fanfold_shutdown_probe fanfold_assets)
    target_include_directories(fanfold_shutdown_probe PRIVATE ../../src/shell)
    target_link_libraries(fanfold_shutdown_probe PRIVATE FanFold::DocumentEngine
        FanFold::FontCatalog Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Widgets
        Qt6::DBus KF6::DBusAddons)
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    if(XVFB_RUN_EXECUTABLE)
        add_test(NAME fanfold-shutdown COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_shutdown.py
            $<TARGET_FILE:fanfold_shutdown_probe> ${CMAKE_BINARY_DIR}/src/shell/qml)
        set_tests_properties(fanfold-shutdown PROPERTIES TIMEOUT 120)
    endif()
    add_test(
        NAME fanfold-package
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/package/run_package_test.sh
                ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${FANFOLD_LOCAL_THEME_BUNDLE}
    )
    set_tests_properties(fanfold-package PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77)

    # The package test's private D-Bus session must not share the host's runtime dir, or it
    # takes the host's xdg-document-portal down. Checked with a stand-in bus, never a real one.
    add_test(
        NAME fanfold-package-runtime-isolation
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/package/check_private_runtime_dir.sh
                ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR} ${FANFOLD_LOCAL_THEME_BUNDLE}
    )
    set_tests_properties(fanfold-package-runtime-isolation PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()
