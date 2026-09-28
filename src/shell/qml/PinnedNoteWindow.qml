import QtQuick
import QtQuick.Window
import QtWebEngine
import QtWebChannel

/**
 * One pinned note in its own window, showing the SAME card as the fan (NoteCard.qml):
 * title spine, editable title, pin and close, the whole footer and its panels, and the
 * same editor page with its toolbar.
 *
 * This window is the card's host. Every card action here names `documentId`, never the
 * fan's selection, so nothing pressed in this window can reach another note.
 *
 * Several can be open at once, they stay open when they lose focus, and they appear in
 * the window list like anything else. They are kept above other windows: the flag covers
 * X11, and ShellControl.keepAbove asks KWin directly, because Wayland gives a client no
 * way to raise itself (see main.cpp).
 *
 * Closing one returns the note to the fan. It never deletes anything and never discards
 * the buffer: the editor is flushed through the engine on the way out, and the engine's
 * 250 ms autosave and recovery journal have been live the whole time the window was open.
 *
 * Position is deliberately never assigned. An ordinary Wayland client does not own its
 * own x/y. Only the settled SIZE is persisted, which a client does own.
 */
Window {
    id: pinnedWindow

    required property string documentId
    required property var record            // the engine's Document object
    /** The fan's shell: shared palette, appearance, manifest and the Library panel. */
    required property var fan
    property string status: "Saved"
    property bool noteDirty: false
    property bool editorEnabled: true
    /** What a successful close gate does next: return the note, or file it. */
    property string closeIntent: "unpin"

    signal closeRequested(string id)

    objectName: "pinned-window-" + documentId
    title: (record ? record.title : "Note") + " · Fan Fold"
    minimumWidth: 400
    minimumHeight: 260
    // A first pin opens at the fan card's own size, so the two read as the same card.
    width: Math.max(minimumWidth, record && record.pinnedWindowWidth > 0 ? record.pinnedWindowWidth : fan.cardWidth)
    height: Math.max(minimumHeight, record && record.pinnedWindowHeight > 0 ? record.pinnedWindowHeight : fan.cardHeight)
    // Opaque paper rather than transparent: this window requests no alpha buffer, and an
    // opaque window with a transparent clear colour paints its corners black.
    color: pinnedWindow.paperColor
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    visible: true

    // ---- The card's host interface (read by NoteCard, SwatchPopup, IconPanel) ----------
    property var own: notesStore.colourOf(documentId)
    function refreshOwn() { pinnedWindow.own = notesStore.colourOf(pinnedWindow.documentId) }
    readonly property var appearance: fan.appearance
    readonly property real cardWidth: pinnedWindow.width
    readonly property real cardHeight: pinnedWindow.height
    readonly property bool expanded: true
    readonly property string selectedId: documentId
    readonly property bool selectedIsPinned: true
    readonly property string cardTitle: record ? record.title : ""
    readonly property string closeExplanation: "Close · returns this note to the fan"
    readonly property string saveStatus: status
    readonly property color paperColor: own && own.paper ? own.paper : "#f5f0e6"
    readonly property color ink: own && own.ink ? own.ink : "#1b1b1f"
    readonly property string noteFont: fan.noteFont
    readonly property color neutralWhite: fan.neutralWhite
    readonly property var activePalette: fan.activePalette
    readonly property var palettes: fan.palettes
    readonly property string palette: fan.palette
    // One-note views of the fan's per-note maps: a pinned note is off the fan, so the
    // fan's maps never carry it.
    readonly property var papers: ({[documentId]: own ? own.paper : undefined})
    readonly property var inkModes: ({[documentId]: own ? own.inkMode : "auto"})
    readonly property var inkStored: ({[documentId]: own ? own.inkStored : "auto"})
    readonly property var manifest: ({folderCount: fan.manifest.folderCount,
                                      inkInPalette: {[documentId]: own ? own.inkInPalette : false}})
    function inkOf(id) { return pinnedWindow.ink }
    function paperInPalette(id) { return own ? own.inPalette === true : false }
    function iconOf(id) { return own && own.icon ? String(own.icon) : "" }
    function derivedTone(base, amount) { return fan.derivedTone(base, amount) }

    property bool paletteOpen: false
    property string paletteMode: "paper"
    property bool swatchKeyboardCue: false
    property bool confirmFolderColour: false
    property bool showInfo: false
    property var infoRows: []
    property bool iconPanelOpen: false
    property bool iconToTab: true
    property bool iconToNote: false
    property bool iconInline: false
    property string confirmArchiveId: ""
    property string confirmTrashId: ""
    property bool confirmQuit: false

    onPaletteOpenChanged: if (!paletteOpen) confirmFolderColour = false
    onPaletteModeChanged: confirmFolderColour = false
    onNoteDirtyChanged: refreshInfo()

    function dismissPanels() { paletteOpen = false; showInfo = false; iconPanelOpen = false }
    /** The page-side disclosures (Settings, formatting) close whenever a native panel opens,
     *  so the card never stacks two panels — the same rule as the fan. */
    function closePagePanels() {
        pinnedEditor.runJavaScript("if(window.appearance&&appearance.open) appearance.toggle(false); if(window.fan&&fan.formattingVisible&&fan.formattingVisible()) fan.toggleFormatting()")
    }
    function togglePalette(mode) {
        var wanted = mode ? mode : "paper"
        var opening = !paletteOpen || paletteMode !== wanted
        if (opening) { showInfo = false; iconPanelOpen = false; closePagePanels() }
        paletteMode = wanted
        paletteOpen = opening
        swatchKeyboardCue = false
        if (opening) card.focusCurrentSwatch()
    }
    function moveSwatchFocus(step) { card.moveSwatchFocus(step) }
    function syncColours() { refreshOwn(); pinnedEditor.runJavaScript("window.fan && fan.recolour && fan.recolour()") }
    function chooseSwatch(color) {
        var result = paletteMode === "ink" ? notesStore.setInk(documentId, color) : notesStore.setPaper(documentId, color)
        if (result.ok) { fan.applyManifest(result); syncColours() }
        else status = result.error
    }
    function chooseInk(value) {
        var result = notesStore.setInk(documentId, value)
        if (result.ok) { fan.applyManifest(result); syncColours() }
        else status = result.error
    }
    function choosePalette(key) { fan.choosePalette(key); refreshOwn() }
    function applyColourToFolder() {
        if (!confirmFolderColour) {
            confirmArchiveId = ""; confirmTrashId = ""; confirmQuit = false
            confirmFolderColour = true
            confirmLapse.restart()
            return
        }
        confirmFolderColour = false; confirmLapse.stop()
        var inkMode = paletteMode === "ink"
        var value = inkMode ? String(own.inkStored) : String(own.paper)
        var result = notesStore.applyColourToOpenFolder(inkMode ? "ink" : "paper", value)
        if (result.ok) { fan.applyManifest(result); fan.syncEditorColours(); syncColours() }
        else status = result.error
    }
    function toggleFormatting() {
        paletteOpen = false; showInfo = false
        pinnedEditor.runJavaScript("fan.toggleFormatting()")
    }
    function toggleSettings() {
        paletteOpen = false; showInfo = false
        pinnedEditor.runJavaScript("appearance.toggle()")
    }
    function reloadNote() { pinnedEditor.runJavaScript("fan.reload()") }
    /** The Library is one panel over the whole collection, so it opens in the fan. */
    function toggleLibrary() { dismissPanels(); fan.toggleLibrary(true); fan.requestActivate() }
    function refreshInfo() {
        if (!showInfo) { infoRows = []; return }
        infoRows = fan.infoRowsFor(store.info(documentId), noteDirty)
    }
    function toggleInfo(force) {
        var opening = typeof force === "boolean" ? force : !showInfo
        if (opening) { paletteOpen = false; iconPanelOpen = false; closePagePanels() }
        showInfo = opening
        refreshInfo()
    }
    function toggleIconPanel(force) {
        var opening = typeof force === "boolean" ? force : !iconPanelOpen
        if (opening) { paletteOpen = false; showInfo = false; closePagePanels() }
        iconPanelOpen = opening
    }
    function setIconDestination(which) { fan.setIconDestinationOn(pinnedWindow, which) }
    function setNoteIcon(relative) {
        collection.setIcon(documentId, relative)
        fan.applyManifest(notesStore.load())
        refreshOwn()
    }
    function applyIcon(entry) { fan.applyIconOn(pinnedWindow, pinnedEditor, entry) }
    function applyTitle() {
        pinnedEditor.runJavaScript("fan.renameActive(" + JSON.stringify(card.titleText) + ")")
    }
    function armArchive() {
        confirmTrashId = ""; confirmQuit = false; confirmFolderColour = false
        confirmArchiveId = confirmArchiveId === documentId ? "" : documentId
        if (confirmArchiveId) confirmLapse.restart()
    }
    function armTrash() {
        confirmArchiveId = ""; confirmQuit = false; confirmFolderColour = false
        confirmTrashId = confirmTrashId === documentId ? "" : documentId
        if (confirmTrashId) confirmLapse.restart()
    }
    function armQuit() {
        confirmArchiveId = ""; confirmTrashId = ""; confirmFolderColour = false
        confirmQuit = !confirmQuit
        if (confirmQuit) confirmLapse.restart()
    }
    // Filing moves the file, so it waits for the same gate as closing: the page must
    // acknowledge every push and the engine must commit before the file moves.
    function archiveSelected() { confirmArchiveId = ""; requestSafeClose("archive") }
    function trashSelected() { confirmTrashId = ""; requestSafeClose("trash") }
    function togglePinSelected() { requestSafeClose("unpin") }
    function collapse() { requestSafeClose("unpin") }
    function requestClose() { confirmQuit = false; fan.requestClose() }
    Timer {
        id: confirmLapse; interval: 4000
        onTriggered: { pinnedWindow.confirmArchiveId = ""; pinnedWindow.confirmTrashId = ""
                       pinnedWindow.confirmQuit = false; pinnedWindow.confirmFolderColour = false }
    }

    onWidthChanged: sizePersistence.restart()
    onHeightChanged: sizePersistence.restart()

    // Persist the SETTLED client size rather than every frame of an interactive resize.
    Timer {
        id: sizePersistence
        interval: 250; repeat: false
        onTriggered: collection.setPinnedWindowSize(pinnedWindow.documentId,
                                                    pinnedWindow.width, pinnedWindow.height)
    }
    // KWin maps the window asynchronously; ask for keep-above once it exists there.
    Timer {
        id: keepAboveRequest
        interval: 400; repeat: false
        onTriggered: shellControl.keepAbove(pinnedWindow.title)
    }
    Component.onCompleted: keepAboveRequest.start()
    onTitleChanged: keepAboveRequest.restart()

    property bool closeCheckPending: false
    property int closeAttempt: 0
    property string closeSaveError: ""
    Timer {
        id: closeDeadline
        interval: 5000; repeat: false
        onTriggered: pinnedWindow.abortSafeClose()
    }
    Timer {
        id: closePoll
        interval: 40; repeat: false
        onTriggered: {
            const attempt = pinnedWindow.closeAttempt
            pinnedEditor.runJavaScript("window.fan && fan.closeResult", function(result) {
                if (!pinnedWindow.closeCheckPending || attempt !== pinnedWindow.closeAttempt) return
                if (result === 0) { closePoll.start(); return }
                if (result === 1) {
                    // The bridge result predates this callback. Re-read the live value and
                    // every outstanding push immediately before committing native Markdown.
                    pinnedEditor.runJavaScript("window.fan && fan.closeReady() && fan.editors[0].getValue() === fan.state.baseline && !fan.state.dirty", function(ready) {
                        if (!pinnedWindow.closeCheckPending || attempt !== pinnedWindow.closeAttempt) return
                        if (ready !== true) {
                            pinnedWindow.closeSaveError = "Close refused · editor changed or push pending; retry"
                            pinnedWindow.status = pinnedWindow.closeSaveError
                            pinnedEditor.runJavaScript("window.fan && fan.cancelClose()")
                        } else if (!collection.saveNow(pinnedWindow.documentId)) {
                            pinnedWindow.closeSaveError = "Close refused · "
                                + (pinnedWindow.record && pinnedWindow.record.saveError
                                   ? pinnedWindow.record.saveError : "Unable to save this note; retry")
                            pinnedWindow.status = pinnedWindow.closeSaveError
                            pinnedEditor.runJavaScript("window.fan && fan.cancelClose()")
                        } else {
                            pinnedWindow.closeSaveError = ""
                            pinnedWindow.closeRequested(pinnedWindow.documentId)
                        }
                        closeDeadline.stop()
                        pinnedWindow.closeCheckPending = false
                    })
                    return
                }
                closeDeadline.stop()
                pinnedWindow.closeCheckPending = false
            })
        }
    }
    function abortSafeClose() {
        if (!pinnedWindow.closeCheckPending) return
        pinnedWindow.closeCheckPending = false
        pinnedWindow.closeAttempt++
        closePoll.stop()
        closeDeadline.stop()
        pinnedWindow.closeSaveError = "Close timed out · edits kept in memory; retry"
        pinnedWindow.status = pinnedWindow.closeSaveError
        pinnedEditor.runJavaScript("window.fan && fan.cancelClose()")
    }
    function appCloseReady(done) {
        if (!pinnedEditor || !pinnedEditor.url || pinnedEditor.loading) { done(false); return }
        pinnedEditor.runJavaScript("window.fan && fan.closeReady()", done)
    }
    function requestSafeClose(intent) {
        if (pinnedWindow.closeCheckPending) return
        pinnedWindow.closeIntent = intent || "unpin"
        pinnedWindow.closeCheckPending = true
        const attempt = ++pinnedWindow.closeAttempt
        closeDeadline.start()
        pinnedEditor.runJavaScript("window.fan && fan.beginClose()", function(started) {
            if (!pinnedWindow.closeCheckPending || attempt !== pinnedWindow.closeAttempt) return
            if (started === true) closePoll.start()
            else { closeDeadline.stop(); pinnedWindow.closeCheckPending = false }
        })
    }
    onClosing: function(close) {
        close.accepted = false
        requestSafeClose("unpin")
    }

    NoteCard {
        id: card
        anchors.fill: parent
        host: pinnedWindow
        bridge: pinnedBridgeObject
        cardTop: 0

        // The title spine and the strip above the title are the window's handle: it is
        // frameless, so the compositor moves it from there. Only those empty areas, so a
        // drag that selects text in the title or the note is never taken for a move.
        // startSystemMove is the native grab and is what works under Wayland.
        Repeater {
            model: [{x: 0, y: 0, w: 40, h: -1}, {x: 40, y: 0, w: -1, h: 9}]
            delegate: Item {
                required property var modelData
                z: 999
                x: modelData.x; y: modelData.y
                width: modelData.w < 0 ? card.width - modelData.x : modelData.w
                height: modelData.h < 0 ? card.height : modelData.h
                DragHandler {
                    id: moveGrab
                    target: null
                    onActiveChanged: if (moveGrab.active) pinnedWindow.startSystemMove()
                }
            }
        }
        // Escape with focus in the card (not the editor) closes a panel first, then the
        // window — the fan's order.
        Keys.onEscapePressed: pinnedBridgeObject.collapse()
        // The four edges and corners resize, since a frameless window has no border.
        Repeater {
            model: [{e: Qt.LeftEdge, x: 0, y: 8, w: 5, h: -16, c: Qt.SizeHorCursor},
                    {e: Qt.RightEdge, x: -5, y: 8, w: 5, h: -16, c: Qt.SizeHorCursor},
                    {e: Qt.BottomEdge, x: 8, y: -5, w: -16, h: 5, c: Qt.SizeVerCursor},
                    {e: Qt.TopEdge, x: 8, y: 0, w: -16, h: 5, c: Qt.SizeVerCursor},
                    {e: Qt.BottomEdge | Qt.RightEdge, x: -8, y: -8, w: 8, h: 8, c: Qt.SizeFDiagCursor},
                    {e: Qt.BottomEdge | Qt.LeftEdge, x: 0, y: -8, w: 8, h: 8, c: Qt.SizeBDiagCursor}]
            delegate: MouseArea {
                required property var modelData
                z: 1000
                x: modelData.x < 0 ? card.width + modelData.x : modelData.x
                y: modelData.y < 0 ? card.height + modelData.y : modelData.y
                width: modelData.w <= 0 ? card.width + modelData.w : modelData.w
                height: modelData.h <= 0 ? card.height + modelData.h : modelData.h
                cursorShape: modelData.c
                onPressed: pinnedWindow.startSystemResize(modelData.e)
            }
        }
    }

    WebEngineView {
        id: pinnedEditor
        enabled: pinnedWindow.editorEnabled
        objectName: "pinned-editor"
        parent: card.editorArea
        anchors.fill: parent
        backgroundColor: "transparent"
        webChannel: pinnedChannel
        profile: WebEngineProfile { offTheRecord: true; httpCacheType: WebEngineProfile.MemoryHttpCache }
        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: true
        settings.javascriptCanOpenWindows: false
        settings.pdfViewerEnabled: false
        url: Qt.resolvedUrl("pinned.html") + "?id=" + encodeURIComponent(pinnedWindow.documentId)
        // Same navigation lock as the deck's view: this window loads its own two local
        // documents and nothing else, ever.
        onNavigationRequested: function(request) {
            const target = request.url.toString().split("?")[0]
            const allowed = target === Qt.resolvedUrl("pinned.html").toString()
                || (!request.isMainFrame && target === Qt.resolvedUrl("editor.html").toString())
            if(allowed) return
            request.reject()
            // A link in a pinned note behaves exactly as it does in the card: the desktop
            // owns http(s)/mailto/tel, and a script link is refused for the same reason.
            // A pinned window has no deck to open a sibling note in, so a .md link is
            // handed to the desktop too rather than silently doing nothing.
            const href = request.url.toString()
            if(href.toLowerCase().indexOf("javascript:") === 0 || href.toLowerCase().indexOf("data:") === 0) {
                return
            }
            Qt.openUrlExternally(href)
        }
        onLoadingChanged: function(info) {
            if(info.status === WebEngineView.LoadFailedStatus) {
                console.warn("Fan Fold: pinned note failed to load:", info.errorString)
            }
        }
        onNewWindowRequested: function(request) {
            // target=_blank links land here, not in onNavigationRequested.
            const href = request.requestedUrl.toString()
            if(href.toLowerCase().indexOf("javascript:") === 0 || href.toLowerCase().indexOf("data:") === 0) {
                return
            }
            Qt.openUrlExternally(href)
        }
        /** Same grant as the deck view: Record needs the embedder to answer getUserMedia. */
        onPermissionRequested: function(permission) {
            // Compared numerically on purpose: QWebEnginePermission::MediaAudioCapture
            // is 1, but the QML enum name (WebEnginePermission.MediaAudioCapture)
            // resolves to undefined in this Qt version, so a named comparison never
            // matches and audio capture is silently denied.
            if (permission.permissionType === 1)
                permission.grant()
            else
                permission.deny()
        }
        // Without this the pinned window's web layer is silent: a JS exception or a CSP
        // refusal inside it produces no output at all, making a blank editor
        // indistinguishable from a working one.
        onJavaScriptConsoleMessage: function(level, message, line, source) {
            console.warn("Fan Fold [pinned]:", source + ":" + line, message)
        }
    }

    /** The same bridge shape `pinned.js` expects; it is a strict subset of the deck's.
     *
     *  The signatures carry NO callback parameter, exactly as the deck's bridge does.
     *  QWebChannel appends the result callback itself, so declaring one makes the arity
     *  wrong and the call is rejected with "No candidates found for <method> with N
     *  arguments". */
    property QtObject pinnedBridge: QtObject {
        id: pinnedBridgeObject
        WebChannel.id: "pinned"
        function loadNote(id) { return store.load(id) }
        function probeNote(id) { return store.probe(id) }
        function saveNote(id, text, revision) { return store.save(id, text, revision) }
        function noteEdited(id, text) { return store.updateContent(id, text) }
        /** This note's own colours and typography override, by id. A pinned note is off
         *  the fan, so the manifest's per-note maps do not carry it; appearance.js turns
         *  this record into the same CSS the deck paints. */
        function colours(id) { return notesStore.colourOf(id) }
        /** `self` is accepted and ignored: this page has one note, so the aggregate IS it.
         *  Declared because the shared page scripts pass three arguments, and the channel
         *  refuses a call with more arguments than the method declares. */
        function status(text, dirty, self) { pinnedWindow.status = pinnedWindow.closeSaveError || text; pinnedWindow.noteDirty = dirty }
        function closeWindow() { pinnedWindow.requestSafeClose() }
        /** editor.js calls this on Escape: a panel over the card closes first, exactly as
         *  in the fan; with none open the window closes and the note returns to the fan. */
        function collapse() {
            if (pinnedWindow.paletteOpen || pinnedWindow.showInfo || pinnedWindow.iconPanelOpen) pinnedWindow.dismissPanels()
            else pinnedWindow.requestSafeClose()
        }
        function renameNote(id, title, revision) {
            var result = store.rename(id, title, revision)
            if (result.ok) { pinnedWindow.fan.applyManifest(notesStore.load()); pinnedWindow.refreshInfo() }
            return result
        }
        function manifest() { return notesStore.load() }
        function libraryPath() { return shellControl.rootPath }
        function importAsset(name, base64, kind) { return shellControl.importAsset(name, base64, kind || "") }
        function setNoteFont(id, family, size) {
            var result = notesStore.setNoteFont(id, family, size)
            if (result.ok) { pinnedWindow.fan.applyManifest(result); pinnedWindow.refreshOwn() }
            return result
        }
        // Settings is the same web panel as the fan's (appearance.js), so it needs the
        // same native calls. A saved change restyles every window through appearanceStore.
        function appIcon() { return String(appIconSource) }
        function appVersion() { return shellControl.appVersion() }
        function chooseFolder() { pinnedWindow.fan.chooseRootFolder() }
        function loadAppearance() { return appearanceStore.load() }
        function saveAppearance(value) { return appearanceStore.save(value) }
        function resetAppearance() { return appearanceStore.reset() }
        function previewAppearance(value) { pinnedWindow.fan.appearance = appearanceStore.preview(value) }
        function fontFamilies() { return fontCatalog.families() }
        function fontResolve(family) { return fontCatalog.resolveFamily(family) }
        function fontDescribe(family) { return fontCatalog.describe(family) }
        function noteInfo(id) { return store.info(id) }
        /** Recording-name answer, published as in the fan's bridge (see Main.qml). */
        property string recordingName: ""
        function beginRecordingName() { card.openRecordingPrompt("Name this recording") }
        function ready() {}
    }
    property WebChannel pinnedChannel: WebChannel { id: pinnedChannel; registeredObjects: [pinnedBridgeObject] }

    Shortcut {
        sequences: [StandardKey.Save]
        onActivated: pinnedEditor.runJavaScript("window.fan && fan.save()")
    }
    Shortcut {
        sequence: "Ctrl+W"
        onActivated: pinnedWindow.requestSafeClose("unpin")
    }

    /** Live restyle: global appearance changes and this note's own colour changes must
     *  reach an OPEN pinned window, not only at construction. */
    Connections {
        target: appearanceStore
        function onChanged() { pinnedEditor.runJavaScript("window.fan && fan.recolour && fan.recolour()") }
    }
    Connections {
        target: notesStore
        function onChanged() { pinnedWindow.syncColours() }
    }
}
