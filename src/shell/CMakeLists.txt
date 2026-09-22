cmake_minimum_required(VERSION 3.22)

include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick Widgets)

# The shipped shell: the QML presentation layer over the Fan Fold document engine.
#
# It links the headless engine library rather than duplicating it, and adds only the
# adapters that translate the engine's vocabulary into the context-property names the
# QML binds to.
add_executable(fanfold
    main.cpp
    appearanceadapter.cpp
    appearanceadapter.h
    notesadapter.cpp
    notesadapter.h
    palette.h
    storeadapter.cpp
    storeadapter.h
)

target_compile_features(fanfold PRIVATE cxx_std_20)
target_include_directories(fanfold PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fanfold PRIVATE
    FanFold::DocumentEngine
    FanFold::FontCatalog
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Widgets
)

# ------------------------------------------------------------------ vendored editor --
# Keep only the published Vditor files Fan Fold actually exercises. This avoids shipping
# optional diagram/export/devtools bundles and their unrelated license surface.
#
# editor.html loads these from `vendor/package/dist/`, so that is the layout staged and
# installed; the path in the HTML is presentation and is not rewritten.
set(FANFOLD_VDITOR_DIST ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/vditor-3.11.2/dist)
set(FANFOLD_VDITOR_RUNTIME_FILES
    index.css
    index.min.js
    css/content-theme/light.css
    images/img-loading.svg
    js/highlight.js/LICENSE
    js/highlight.js/highlight.min.js
    js/highlight.js/third-languages.js
    js/highlight.js/styles/github.min.css
    js/i18n/en_US.js
    js/icons/ant.js
    js/lute/lute.min.js
)
set(FANFOLD_VDITOR_RUNTIME ${CMAKE_CURRENT_BINARY_DIR}/vditor-runtime/dist)
file(REMOVE_RECURSE ${FANFOLD_VDITOR_RUNTIME})
foreach(FANFOLD_VDITOR_FILE IN LISTS FANFOLD_VDITOR_RUNTIME_FILES)
    get_filename_component(FANFOLD_VDITOR_DIRECTORY ${FANFOLD_VDITOR_FILE} DIRECTORY)
    file(MAKE_DIRECTORY ${FANFOLD_VDITOR_RUNTIME}/${FANFOLD_VDITOR_DIRECTORY})
    configure_file(
        ${FANFOLD_VDITOR_DIST}/${FANFOLD_VDITOR_FILE}
        ${FANFOLD_VDITOR_RUNTIME}/${FANFOLD_VDITOR_FILE}
        COPYONLY
    )
    install(FILES ${FANFOLD_VDITOR_DIST}/${FANFOLD_VDITOR_FILE}
            DESTINATION
            ${CMAKE_INSTALL_DATADIR}/fanfold/qml/vendor/package/dist/${FANFOLD_VDITOR_DIRECTORY})
endforeach()

# Stage the QML and the vendored editor beside the binary, so the built tree runs without
# being installed.
set(FANFOLD_STAGE $<TARGET_FILE_DIR:fanfold>)
add_custom_target(fanfold_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/qml ${FANFOLD_STAGE}/qml
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${FANFOLD_VDITOR_RUNTIME} ${FANFOLD_STAGE}/qml/vendor/package/dist
    COMMENT "Staging the shell QML and vendored Vditor assets"
    VERBATIM
)
add_dependencies(fanfold_assets fanfold)

# ------------------------------------------------------------------------- install --
install(TARGETS fanfold RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The QML presentation, installed verbatim. Main.qml resolves LayoutContract.js and
# QuietButton.qml from this same directory and loads the web editor assets from it too,
# so they install as one unit.
install(FILES
    qml/Main.qml
    qml/PinnedNoteWindow.qml
    qml/QuietButton.qml
    qml/LayoutContract.js
    qml/index.html
    qml/app.js
    qml/editor.js
    qml/editor.html
    qml/editor-theme.css
    qml/appearance.js
    qml/appearance.css
    qml/pinned.html
    qml/pinned.js
    qml/appicon.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fanfold/qml
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../fanfold/packaging/io.github.preyevates.FanFold.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/../fanfold/packaging/io.github.preyevates.FanFold.svg
        ${CMAKE_CURRENT_SOURCE_DIR}/../fanfold/packaging/io.github.preyevates.FanFold-symbolic.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fanfold)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../fanfold/packaging/THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fanfold)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../fanfold/packaging/licenses/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/fanfold/licenses)
