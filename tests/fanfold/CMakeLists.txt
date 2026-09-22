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
