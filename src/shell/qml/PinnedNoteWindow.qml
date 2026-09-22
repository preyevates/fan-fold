import QtQuick
import QtQuick.Window
import QtWebEngine
import QtWebChannel

/**
 * One pinned note in its own ordinary window.
 *
 * These are plain Qt windows, not popups: several can be open at once, they stay open
 * when they lose focus, and they appear in the window list like anything else. That is
 * the one place in this application where a plain `Window` is CORRECT — a pinned note is
 * a normal top-level the compositor places, not an edge dock. The fan itself remains a
 * PlasmaCore.Dialog, which is what makes it sit flush at the screen edge; see main.cpp.
 *
 * Closing one returns the note to the fan. It never deletes anything and never discards
 * the buffer: the editor is flushed through the engine on the way out, and the engine's
 * 250 ms autosave and recovery journal have been live the whole time the window was open.
 *
 * Position is deliberately never assigned. An ordinary Wayland client does not own its
 * own x/y, and assigning it anyway yields a window the compositor places wherever it
 * likes. Only the settled SIZE is persisted, which a client does own.
 */
Window {
    id: pinnedWindow

    required property string documentId
    required property var record            // the engine's Document object
    property color paper: "#f5f0e6"
    property color ink: "#1b1b1f"
    property string noteFont: "Noto Sans"
    property real iconSize: 16
    property string status: "Saved"
    property bool noteDirty: false

    signal closeRequested(string id)

    objectName: "pinned-window-" + documentId
    title: (record ? record.title : "Note") + " · Fan Fold"
    minimumWidth: 280
    minimumHeight: 220
    width: Math.max(minimumWidth, record && record.pinnedWindowWidth > 0 ? record.pinnedWindowWidth : 420)
    height: Math.max(minimumHeight, record && record.pinnedWindowHeight > 0 ? record.pinnedWindowHeight : 340)
    color: pinnedWindow.paper
    // An ordinary window: no always-on-top, no tool-window flag, no focus stealing.
    flags: Qt.Window
    visible: true

    onWidthChanged: sizePersistence.restart()
    onHeightChanged: sizePersistence.restart()

    // Persist the SETTLED client size rather than every frame of an interactive resize.
    Timer {
        id: sizePersistence
        interval: 250; repeat: false
        onTriggered: collection.setPinnedWindowSize(pinnedWindow.documentId,
                                                    pinnedWindow.width, pinnedWindow.height)
    }

    // A pinned note's unsaved buffer must reach disk before the window goes away. The
    // engine's journal would recover it anyway, but a clean close should not depend on
    // crash recovery to keep the user's words.
    onClosing: function(close) {
        close.accepted = false
        collection.saveNow(pinnedWindow.documentId)
        pinnedWindow.closeRequested(pinnedWindow.documentId)
    }

    Rectangle {
        id: header
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 28
        color: Qt.darker(pinnedWindow.paper, 1.08)

        Text {
            anchors.left: parent.left; anchors.leftMargin: 10
            anchors.right: unpinButton.left; anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: pinnedWindow.record ? pinnedWindow.record.title : ""
            color: pinnedWindow.ink
            font.family: pinnedWindow.noteFont; font.pixelSize: 11; font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        QuietButton {
            id: unpinButton
            objectName: "pinned-unpin"
            anchors.right: parent.right; anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            size: pinnedWindow.iconSize + 12; iconSize: pinnedWindow.iconSize
            glyph: "window-unpin"; ink: pinnedWindow.ink
            explanation: "Return this note to the fan"
            onClicked: {
                collection.saveNow(pinnedWindow.documentId)
                pinnedWindow.closeRequested(pinnedWindow.documentId)
            }
        }

        // The native move grab: the compositor moves the window, nothing here tracks the
        // pointer. This is what makes dragging behave correctly under Wayland.
        DragHandler {
            id: moveGrab
            target: null
            onActiveChanged: if(moveGrab.active) pinnedWindow.startSystemMove()
        }
    }

    WebEngineView {
        id: pinnedEditor
        objectName: "pinned-editor"
        anchors.top: header.bottom; anchors.left: parent.left
        anchors.right: parent.right; anchors.bottom: pinnedFooter.top
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

    Item {
        id: pinnedFooter
        anchors.left: parent.left; anchors.leftMargin: 8
        anchors.right: parent.right; anchors.rightMargin: 8
        anchors.bottom: parent.bottom; anchors.bottomMargin: 4
        height: pinnedWindow.iconSize + 12

        // Notes autosave and Ctrl+S saves immediately (editor.js keydown handler), so
        // there is no Save button. Format is the first and only control here.
        QuietButton {
            objectName: "pinned-format"
            x: 0
            size: pinnedWindow.iconSize + 12; iconSize: pinnedWindow.iconSize
            glyph: "format-text-bold"; ink: pinnedWindow.ink
            explanation: "Formatting"
            onClicked: pinnedEditor.runJavaScript("window.fan && fan.toggleFormatting()")
        }
        Text {
            objectName: "pinned-status"
            x: (pinnedWindow.iconSize+12) + 12
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: pinnedWindow.status; color: pinnedWindow.ink
            font.family: pinnedWindow.noteFont; font.pixelSize: 10
            Accessible.role: Accessible.StaticText
            Accessible.name: pinnedWindow.status
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
        /** WCAG relative luminance of a QML colour, local so this window never leans on
         *  the deck's context-chain ids being resolvable from a separate file. */
        function lumOf(c) {
            function linear(v) { return v<=0.03928 ? v/12.92 : Math.pow((v+0.055)/1.055,2.4) }
            return 0.2126*linear(c.r)+0.7152*linear(c.g)+0.0722*linear(c.b)
        }
        /** The note's own literal paper/ink plus global typography, as CSS variables —
         *  exactly the keys editor-theme.css reads in the deck. */
        function colours(id) {
            // Ask the adapter for THIS note's colours by id rather than indexing the
            // manifest's per-note maps. Those maps are keyed by fan membership, and a
            // pinned note is by definition off the fan, so the manifest lookup yields
            // undefined and the editor receives "--paper: undefined; --ink: undefined":
            // text present, selectable, and invisible.
            var own = notesStore.colourOf(id)
            var settings = appearanceStore.load().settings
            var family = settings.fontFamilyName ? settings.fontFamilyName : "Noto Sans"
            var paper = own && own.paper ? String(own.paper) : "#f5f0e6"
            var ink = own && own.ink ? String(own.ink) : "#2b2b2b"
            // The KEYS here must be exactly the custom properties editor-theme.css reads:
            // --leading (line-height) and --tabs (tab-size), not near-synonyms like
            // "line" and "tab" — a mismatched key is ignored silently, so the Line Space
            // and Indent Size settings simply stop applying. --spine and --codeink drive
            // fenced-code blocks; without them a code block paints the vendor default
            // over the note's paper.
            var spineColor = Qt.darker(Qt.color(paper), 1.14)
            var spineLum = pinnedBridgeObject.lumOf(spineColor)
            var codeink = ((spineLum+0.05)/0.05) >= (1.05/(spineLum+0.05)) ? "#101010" : "#f4f4f4"
            return {paper: paper, ink: ink,
                    spine: String(spineColor),
                    codeink: codeink,
                    tokenboost: spineLum >= 0.42 ? "none"
                        : (spineLum < 0.12 ? "brightness(3.1) saturate(1.25)"
                                           : "brightness(2.2) saturate(1.15)"),
                    font: "\"" + family + "\", sans-serif",
                    size: settings.fontSize + "px",
                    leading: String(settings.lineSpacing),
                    padx: settings.padX + "px", pady: settings.padY + "px",
                    tabs: String(settings.tabSpacing), icon: settings.iconSize + "px"}
        }
        function status(text, dirty) { pinnedWindow.status = text; pinnedWindow.noteDirty = dirty }
        function closeWindow() {
            collection.saveNow(pinnedWindow.documentId)
            pinnedWindow.closeRequested(pinnedWindow.documentId)
        }
    }
    property WebChannel pinnedChannel: WebChannel { id: pinnedChannel; registeredObjects: [pinnedBridgeObject] }

    Shortcut {
        sequences: [StandardKey.Save]
        onActivated: pinnedEditor.runJavaScript("window.fan && fan.save()")
    }
    Shortcut {
        sequence: "Ctrl+W"
        onActivated: {
            collection.saveNow(pinnedWindow.documentId)
            pinnedWindow.closeRequested(pinnedWindow.documentId)
        }
    }

    /** Live restyle: global appearance changes and this note's own colour changes must
     *  reach an OPEN pinned window. Applying them only at construction would leave a
     *  pinned window ignoring Settings edits and paper/ink assignments until reopened. */
    Connections {
        target: appearanceStore
        function onChanged() { pinnedEditor.runJavaScript("window.fan && fan.recolour && fan.recolour()") }
    }
    Connections {
        target: notesStore
        function onChanged() { pinnedEditor.runJavaScript("window.fan && fan.recolour && fan.recolour()") }
    }
}
