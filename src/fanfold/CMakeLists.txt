cmake_minimum_required(VERSION 3.22)

include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui)

set(FANFOLD_LOCAL_THEME_CATALOG_DIR "" CACHE PATH
    "Opt-in LOCAL-ONLY theme bundle; never set for a public package")
set(FANFOLD_LOCAL_THEME_BUNDLE OFF)
if(NOT FANFOLD_LOCAL_THEME_CATALOG_DIR STREQUAL "")
    get_filename_component(FANFOLD_LOCAL_THEME_CATALOG_DIR
                           "${FANFOLD_LOCAL_THEME_CATALOG_DIR}" ABSOLUTE)
    foreach(FANFOLD_THEME_FILE catalog.json manifest.json NOTICES.md)
        if(NOT EXISTS "${FANFOLD_LOCAL_THEME_CATALOG_DIR}/${FANFOLD_THEME_FILE}"
           OR IS_DIRECTORY "${FANFOLD_LOCAL_THEME_CATALOG_DIR}/${FANFOLD_THEME_FILE}"
           OR IS_SYMLINK "${FANFOLD_LOCAL_THEME_CATALOG_DIR}/${FANFOLD_THEME_FILE}")
            message(FATAL_ERROR
                "FANFOLD_LOCAL_THEME_CATALOG_DIR requires a regular ${FANFOLD_THEME_FILE}")
        endif()
    endforeach()
    file(SHA256 "${FANFOLD_LOCAL_THEME_CATALOG_DIR}/catalog.json"
         FANFOLD_LOCAL_THEME_CATALOG_SHA256)
    file(READ "${FANFOLD_LOCAL_THEME_CATALOG_DIR}/manifest.json"
         FANFOLD_LOCAL_THEME_MANIFEST)
    string(JSON FANFOLD_LOCAL_THEME_MANIFEST_SHA256 ERROR_VARIABLE FANFOLD_THEME_JSON_ERROR
           GET "${FANFOLD_LOCAL_THEME_MANIFEST}" catalog_sha256)
    if(NOT FANFOLD_THEME_JSON_ERROR STREQUAL "NOTFOUND"
       OR NOT FANFOLD_LOCAL_THEME_MANIFEST_SHA256 STREQUAL FANFOLD_LOCAL_THEME_CATALOG_SHA256)
        message(FATAL_ERROR "Local theme catalog checksum does not match manifest.json")
    endif()
    set(FANFOLD_LOCAL_THEME_BUNDLE ON)
    message(STATUS
        "Fan Fold LOCAL-ONLY theme catalog enabled: ${FANFOLD_LOCAL_THEME_CATALOG_DIR}")
endif()
set(FANFOLD_LOCAL_THEME_BUNDLE "${FANFOLD_LOCAL_THEME_BUNDLE}" PARENT_SCOPE)

# Headless core: the document engine plus the read-only projections and session logic the
# QML shell drives. Nothing here needs a GUI, so the focused suites run under a plain
# QCoreApplication with synthetic note and state roots.
add_library(fanfold_document_engine STATIC
    appearancesettings.cpp
    appearancesettings.h
    documentcollection.cpp
    documentcollection.h
    editorleases.cpp
    editorleases.h
    librarymodel.cpp
    librarymodel.h
    searchmodel.cpp
    searchmodel.h
    sessioncontroller.cpp
    sessioncontroller.h
    themecatalog.cpp
    themecatalog.h
)
add_library(FanFold::DocumentEngine ALIAS fanfold_document_engine)

target_compile_features(fanfold_document_engine PUBLIC cxx_std_20)
target_include_directories(fanfold_document_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fanfold_document_engine PUBLIC Qt6::Core)

# The font catalog is the one piece that needs the platform font database, so it is a
# separate target rather than forcing Qt6::Gui on every headless consumer.
add_library(fanfold_font_catalog STATIC
    fontcatalog.cpp
    fontcatalog.h
)
add_library(FanFold::FontCatalog ALIAS fanfold_font_catalog)

target_compile_features(fanfold_font_catalog PUBLIC cxx_std_20)
target_include_directories(fanfold_font_catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fanfold_font_catalog PUBLIC Qt6::Core Qt6::Gui FanFold::DocumentEngine)

# The QML shell (src/shell) needs Qml/Quick to build and links the engine library defined
# above. QtWebEngine and QtWebChannel are used purely as QML imports resolved at runtime,
# so no WebEngine development package is required to configure or build it.
find_package(Qt6 6.8 QUIET COMPONENTS Qml Quick QuickControls2)
