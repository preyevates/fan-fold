import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import QtWebEngine
import QtWebChannel
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami
import org.kde.iconthemes as KIconThemes
import "LayoutContract.js" as LayoutContract

// Real Plasma Dialog in an isolated Qt host, not a PlasmoidItem or shell containment.
// Colors are per note and come from the library index; every other appearance value is global.
PlasmaCore.Dialog {
    id: dialog
    objectName: "fanDialog"
    visible: true
    flags: Qt.Tool | Qt.FramelessWindowHint
    type: PlasmaCore.Dialog.Dock
    backgroundHints: PlasmaCore.Types.NoBackground
    hideOnWindowDeactivate: false
    title: "Fan Fold"
    color: "transparent"

    property var appearance: appearanceStore.load().settings
    property var manifest: notesStore.load()
    property var ids: manifest.ids
    property var titles: manifest.titles
    // Literal resolved colours keyed by stable ID, plus the computed ink for each. The
    // palette below only CHOOSES which swatches are offered; it never writes here.
    property var papers: manifest.paper
    /** File NAME per id ("Other.md"), the manifest's `files` map. Bound here so a
     *  note-to-note link can resolve a href against the library without a round trip. */
    property var fileNames: manifest.files
    /** Per-note tab icon as an absolute file URL, or "" for none. */
    property var noteIcons: manifest.icons
    /** The icons currently OFFERED — a filtered view, not the whole corpus. Re-read when
     *  the panel opens or the search text settles, so a newly added file is offered. */
    property var iconChoices: []
    /** Whether the icon panel is up. One panel over the card at a time. */
    property bool iconPanelOpen: false
    /** Where a picked icon goes. Independent toggles, not radio buttons, so one visit can
     *  send the same icon to the tab AND the note body. At least one stays on: a picker
     *  with no destination is a button that does nothing. */
    property bool iconToTab: true
    property bool iconToNote: false
    /** Inline = the icon sits IN the paragraph with text flowing around it. Markdown has
     *  no wrap syntax, so this emits the `<img align>` HTML Markdown has always permitted;
     *  the file stays a portable .md with a relative link. */
    property bool iconInline: false
    /** Size of the searchable corpus, shown beside the search field. */
    property int iconTotal: 0
    /** Name of the theme supplying the icons, so their source is never a mystery. */
    property string iconThemeLabel: ""
    /** Capped rather than unbounded: a full theme is tens of thousands of names, and a
     *  delegate per name stalls the card. Search reaches past the cap. */
    readonly property int iconLimit: 400

    function toggleIconPanel(force) {
        var opening = typeof force === "boolean" ? force : !iconPanelOpen
        if (opening) {
            dialog.paletteOpen = false
            dialog.showInfo = false
            if (loader.item) loader.item.runJavaScript("if(window.appearance&&appearance.open) appearance.toggle(false); if(window.fan&&fan.formattingVisible()) fan.toggleFormatting()")
            dialog.refreshIconChoices()
        }
        iconPanelOpen = opening
    }
    function setIconDestination(which) {
        if (which === "inline") {
            // A layout modifier on "Into note", not a destination of its own: ticking it
            // implies the icon is going into the note at all.
            iconInline = !iconInline
            if (iconInline) iconToNote = true
            return
        }
        if (which === "tab") {
            iconToTab = !iconToTab
            if (!iconToTab && !iconToNote) iconToNote = true
        } else {
            iconToNote = !iconToNote
            if (!iconToNote) iconInline = false
            if (!iconToTab && !iconToNote) iconToTab = true
        }
    }
    /** Rebuild the offered icons: ONE searchable list, not segmented into group tabs —
     *  typing "brave" should find the Brave icon whichever drawer it lives in. Application
     *  icons rank first, then the user's own files, then the theme's symbol corpus. */
    function refreshIconChoices(query) {
        const q = (query === undefined) ? "" : String(query)
        const needle = q.toLowerCase()
        const match = function (e) { return String(e.name).toLowerCase().indexOf(needle) >= 0 }
        let list = shellControl.appIcons()
        if (needle !== "") list = list.filter(match)
        let own = shellControl.availableIcons()
        if (needle !== "") own = own.filter(match)
        list = list.concat(own)
        // Theme symbols fill the remainder, skipping names the app/own lists already
        // offered so no icon appears twice. The request must OVER-FETCH by the number
        // that can drop, or de-duplication silently under-fills the grid's own cap.
        if (list.length < dialog.iconLimit) {
            const seen = {}
            for (let i = 0; i < list.length; ++i) seen[String(list[i].relative)] = true
            const symbols = shellControl.searchIcons(q, dialog.iconLimit)
            for (let j = 0; j < symbols.length && list.length < dialog.iconLimit; ++j) {
                if (!seen[String(symbols[j].relative)]) list.push(symbols[j])
            }
        }
        if (list.length > dialog.iconLimit) list = list.slice(0, dialog.iconLimit)
        dialog.iconChoices = list
        dialog.iconTotal = shellControl.iconCount()
        dialog.iconThemeLabel = shellControl.iconThemeName()
    }
    /** Apply a picked icon to the chosen destinations. The tab icon is stored in the
     *  manifest; a note icon is rendered to a PNG under Assets/icons/ and appended as an
     *  image link, so the note stays a portable .md whose links resolve in the library. */
    function applyIcon(entry) {
        const rel = String(entry.relative || "")
        if (dialog.iconToTab) dialog.setNoteIcon(rel)
        if (dialog.iconToNote && loader.item) {
            const r = shellControl.exportIconForNote(rel, String(entry.name || "icon"))
            if (r && r.ok) {
                loader.item.runJavaScript(
                    "fan.appendImage(fan.active," + JSON.stringify(String(r.relative))
                    + "," + JSON.stringify(String(entry.name || "icon"))
                    + "," + (dialog.iconInline ? "true" : "false") + ")")
            }
        }
    }
    /** A theme icon as Kirigami should load it.
     *
     *  Two kinds of icon share one grid: theme icons carried as "theme:<name>", which the
     *  active theme resolves and restyles, and the user's own files carried as a library-
     *  relative path. Kirigami.Icon takes a bare name for the former and a URL for the
     *  latter, so the prefix decides. Falls back to the bare name — which Qt resolves
     *  against the desktop theme — so the result is never blank. */
    function themeIcon(name) {
        const path = shellControl.resolveThemeIcon(name)
        return path === "" ? name : "file://" + path
    }
    function iconSource(entry) {
        if (!entry) return ""
        const rel = String(entry.relative || "")
        if (rel.indexOf("theme:") === 0) return dialog.themeIcon(rel.substring(6))
        return String(entry.url || "")
    }
    /** The same resolution for a STORED value rather than a picker entry. A theme name
     *  goes to Kirigami bare; anything else is a file inside the library and must be made
     *  absolute, because a relative source resolves against the QML directory. */
    function iconSourceFor(value) {
        // The ADAPTER's contract, not the picker's: notesadapter.cpp has already turned a
        // stored "theme:<name>" into a bare name and a library path into an absolute file
        // URL. The "theme:" branch remains for direct calls that bypass the adapter.
        const v = String(value || "")
        if (v === "") return ""
        if (v.indexOf("theme:") === 0) return dialog.themeIcon(v.substring(6))
        if (v.indexOf("file:") === 0 || v.indexOf("/") === 0) return v
        return dialog.themeIcon(v)
    }
    function iconOf(id) { return (id && dialog.noteIcons && dialog.noteIcons[id]) ? String(dialog.noteIcons[id]) : "" }
    /** Assign or clear the selected note's tab icon. */
    function setNoteIcon(relative) {
        if(dialog.selectedId === "") return
        collection.setIcon(dialog.selectedId, relative)
        dialog.applyManifest(notesStore.load())
    }
    property var inks: manifest.ink
    // Per-note ink, keyed by the same stable ID as paper. `inkModes[id]` is "auto" or
    // "explicit"; `inkStored[id]` is the literal the user chose, or the "auto" sentinel.
    property var inkModes: manifest.inkMode
    property var inkStored: manifest.inkStored
    property string palette: manifest.palette
    property var palettes: manifest.palettes
    property var order: manifest.order
    // Bounded by the available work area, so the card and its fan deck never extend under
    // a panel; the fan pitch limit below is derived from this same clamped height.
    property real cardWidth: Math.min(appearance.width, dialog.availW-36)
    property real cardHeight: Math.min(appearance.height, dialog.availH-12)
    property int selected: 0
    // An empty library makes `ids[selected]` undefined, and an undefined id propagates
    // into every string/colour binding below as a QML type warning. Resolve to "" instead.
    property string selectedId: ids[selected] !== undefined ? ids[selected] : ""
    property bool expanded: false
    property bool loaded: false
    property bool dirty: false
    /** The footer says ONE routine thing: whether this note differs from its file. Errors
     *  and conflicts still take this line and override the routine state, so nothing about
     *  a refusal is quiet. */
    property string saveStatus: "Saved"
    /** True while the whole LIBRARY holds no notes at all — the first-run question.
     *
     *  Deliberately NOT `order.length === 0`. The fan is one FOLDER's notes, so an empty
     *  fan is an ordinary state (an empty folder, or every note archived or pinned) and
     *  binding the first-run welcome to it announces "no notes yet" over a full library. */
    readonly property bool libraryIsEmpty: (manifest.libraryCount !== undefined
                                            ? manifest.libraryCount : dialog.order.length) === 0
    /** Root-relative folder the fan is a window onto; "" is the library root. */
    property string openFolder: manifest.openFolder !== undefined ? manifest.openFolder : ""
    /** Leaf name of the open folder, for the one line that has to say where you are. */
    readonly property string openFolderLabel: dialog.openFolder === ""
        ? (libraryRoot ? String(libraryRoot).split("/").pop() : "Notes")
        : String(dialog.openFolder).split("/").pop()
    /** True while the OPEN FOLDER holds no live notes — a different question from the
     *  library being empty, and the one the scoped empty state asks. */
    readonly property bool folderIsEmpty: (manifest.folderCount !== undefined
                                           ? manifest.folderCount : dialog.order.length) === 0
    property bool closeRequested: false
    property bool allowDiscard: false
    property bool paletteOpen: false
    // Which surface the single compact panel is assigning: "paper" or "ink".
    property string paletteMode: "paper"
    property bool showInfo: false
    /** Library panel (browse + restore from Archive) visibility. */
    property bool libraryOpen: false
    /** Notes currently held in their own pinned window, by stable id. */
    property var pinnedIds: []
    readonly property bool selectedIsPinned: dialog.pinnedIds.indexOf(dialog.selectedId) >= 0
    /** The note a destructive action is ARMED for, and nothing else.
     *
     *  Archive and Trash move the user's file, so each arms on the first press and acts
     *  only on a second press of a control that has visibly changed. A dense icon row
     *  makes a mis-aimed click easy; the interlock makes it harmless. */
    property string confirmArchiveId: ""
    property string confirmTrashId: ""
    /** The archived note a RESTORE is armed for. Restoring also moves the user's file —
     *  out of Archive, back to its original folder — so a Library row arms on the first
     *  press and says so; only a second press on the changed row moves anything. */
    property string confirmRestoreId: ""
    /** Quit is armed the same way. With the 250 ms autosave `dirty` is almost always
     *  false, so an unguarded quit exits instantly and the application vanishes under a
     *  stray click on the power glyph while the footer is in use. */
    property bool confirmQuit: false
    function armArchive() {
        dialog.confirmTrashId = ""; dialog.confirmQuit = false
        dialog.confirmArchiveId = dialog.confirmArchiveId === dialog.selectedId ? "" : dialog.selectedId
        if(dialog.confirmArchiveId) confirmLapse.restart()
    }
    function armTrash() {
        dialog.confirmArchiveId = ""; dialog.confirmQuit = false
        dialog.confirmTrashId = dialog.confirmTrashId === dialog.selectedId ? "" : dialog.selectedId
        if(dialog.confirmTrashId) confirmLapse.restart()
    }
    function armQuit() {
        dialog.confirmArchiveId = ""; dialog.confirmTrashId = ""
        dialog.confirmQuit = !dialog.confirmQuit
        if(dialog.confirmQuit) confirmLapse.restart()
    }
    /** An armed destructive control disarms itself; leaving it armed means a click minutes
     *  later, aimed at whatever the user thinks is there now, files a note. 12 s rather
     *  than the footer's 4 s, because a Library restore is a READING decision: a shorter
     *  window expires mid-decision and silently turns the confirming press into an arming
     *  press. */
    property Timer restoreLapseTimer: Timer {
        id: restoreLapse; interval: 12000
        onTriggered: dialog.confirmRestoreId = ""
    }
    /** An armed destructive control disarms itself. Leaving it armed would mean a click
     *  minutes later, aimed at whatever the user thinks is there now, files a note. */
    property Timer confirmLapseTimer: Timer {
        id: confirmLapse; interval: 4000
        onTriggered: { dialog.confirmArchiveId = ""; dialog.confirmTrashId = ""; dialog.confirmQuit = false }
    }
    /** True when the SELECTED note's own buffer differs from its file. `dirty` above is
     *  the aggregate across every note, which is the right input for the close guard and
     *  the wrong one for "does this file match what I am looking at". */
    property bool selectedDirty: false
    /** Read-only file facts for the selected note, and the rows the panel paints. */
    property var infoData: ({ok:false})
    property var infoRows: []
    /** Re-read the native filestore for the SELECTED ID. Nothing is cached across a
     *  selection change or a save: a stale mtime would be worse than no panel. */
    function refreshInfo() {
        if(!showInfo) { infoRows = []; return }
        var r = store.info(dialog.selectedId)
        infoData = r
        var rows = []
        if(r && r.ok) {
            rows.push({label:"Name", value:r.filename})
            rows.push({label:"Path", value:r.path})
            rows.push({label:"On disk", value:r.bytes + " bytes"})
            rows.push({label:"Modified", value:r.modified})
            // Derived from the FILE, not the editor: `matchesLoaded` compares the current
            // on-disk digest with the revision the store last handed this note.
            rows.push({label:"Status", value: dialog.selectedDirty
                ? "Unsaved · buffer differs from this file"
                : (r.matchesLoaded ? "Saved · matches this file"
                                   : "External change · this file differs from the copy loaded")})
        } else {
            rows.push({label:"Status", value: (r && r.error) ? String(r.error) : "File details unavailable"})
        }
        // A safety statement rather than routine chatter. It must stay accurate: edits
        // autosave after a 250 ms quiet period and a killed process recovers its buffer
        // from the journal. This is the one panel consulted to find out what is safe.
        rows.push({label:"Saving", value:"Autosaves after a brief pause · Ctrl+S saves immediately · recovers after a crash"})
        infoRows = rows
    }
    /** Toggle File details. Mutually exclusive with Settings, the colour panel and the
     *  formatting disclosure, exactly like those are with each other. */
    function toggleInfo(force) {
        var opening = typeof force === "boolean" ? force : !showInfo
        if(opening) {
            paletteOpen = false
            iconPanelOpen = false
            if(loader.item) loader.item.runJavaScript("if(window.appearance&&appearance.open) appearance.toggle(false); if(window.fan&&fan.formattingVisible()) fan.toggleFormatting()")
        }
        showInfo = opening
        refreshInfo()
        if(opening) refreshIconChoices()
    }
    /** Toggle the Library. Mutually exclusive with the other in-card panels, exactly
     *  like they are with each other, so the card never stacks two. */
    function toggleLibrary(force) {
        var opening = typeof force === "boolean" ? force : !libraryOpen
        if(opening) {
            paletteOpen = false
            showInfo = false
            iconPanelOpen = false
            if(loader.item) loader.item.runJavaScript("if(window.appearance&&appearance.open) appearance.toggle(false); if(window.fan&&fan.formattingVisible()) fan.toggleFormatting()")
            libraryModel.showArchive = true   // the panel's whole purpose is restoring
            // Folders come from the directory walk, which nothing watches, so a folder
            // created since the last build stays invisible until restart. The library is
            // small and the walk costs microseconds, so re-walk on every open.
            libraryModel.rebuild()
        }
        libraryOpen = opening
        // Closing the panel drops any armed restore: reopening it later must not present
        // a row that is still one click from moving a file.
        if(!opening) dialog.confirmRestoreId = ""
    }
    // Selection change: disarm any destructive control (see confirmArchiveId) and re-read
    // the file facts. One handler, because QML allows only one per signal.
    onSelectedIdChanged: { dialog.confirmArchiveId = ""; dialog.confirmTrashId = ""; refreshInfo() }
    onSelectedDirtyChanged: refreshInfo()
    /** The family the OLD enum meant, used only when no family has been chosen yet: the
     *  in-memory half of the migration, so an appearance.json written before the font
     *  control existed keeps its look without being rewritten. */
    readonly property string legacyFamily: appearance.fontFamily === "serif" ? "Noto Serif"
        : (appearance.fontFamily === "mono" ? "DejaVu Sans Mono" : "Noto Sans")
    /** The family actually stored, or "" while the legacy enum is still deciding. */
    readonly property string chosenFamily: appearance.fontFamilyName ? String(appearance.fontFamilyName) : ""
    /** What is actually PAINTED: the chosen family when installed, else the legacy family
     *  when installed, else the platform default. The stored preference is never
     *  rewritten by this resolution. */
    property string noteFont: fontCatalog.resolveFamily(
        dialog.chosenFamily.length ? dialog.chosenFamily : dialog.legacyFamily)

    /** The palette currently offering choices, with its swatches. `palettes` is the whole
     *  named CHOICE list and carries labels only, so the selected one's swatches arrive
     *  ready-built from the native side. */
    property var activePalette: manifest.activePalette
    function paletteRecord(key) {
        for(var i=0;i<palettes.length;i++) if(palettes[i].key===key) return palettes[i]
        return palettes[0]
    }
    /** A note's own stored literal colour; independent of the selected palette.
     *
     *  Falls back to the default paper for an id the manifest no longer carries, which
     *  happens for one frame whenever a note LEAVES the fan (pinned, archived, trashed):
     *  the delegate outlives the manifest entry, and undefined is not a QColor. */
    function paperOf(id) { return papers[id] !== undefined ? papers[id] : "#f5f0e6" }
    /** Automatic contrast ink computed natively from that note's own paper; same
     *  leaving-the-fan fallback as paperOf. */
    function inkOf(id) { return inks[id] !== undefined ? inks[id] : "#1b1b1f" }
    /** True when this note's literal colour happens to be one the selected palette
     *  offers. False is normal: the panel then shows no ring and the current-colour
     *  indicator instead of pretending some swatch is selected. */
    function paperInPalette(id) {
        // activePalette is absent until the first manifest lands, and `swatches` is
        // undefined for an id mid-removal; both yield "Value is undefined and could not
        // be converted to an object" on every pin/archive.
        var list = activePalette && activePalette.swatches ? activePalette.swatches : []
        for(var j=0;j<list.length;j++) if(String(list[j].paper)===String(papers[id])) return true
        return false
    }
    // An empty library has no selected note, so these fall back to the first curated paper
    // and its computed ink. Undefined instead makes the empty state paint transparent.
    property color paperColor: dialog.libraryIsEmpty || paperOf(selectedId) === undefined
        ? "#f5f0e6" : paperOf(selectedId)
    property color ink: dialog.libraryIsEmpty || inkOf(selectedId) === undefined
        ? "#1b1b1f" : inkOf(selectedId)

    // --- Derived chrome tones -------------------------------------------------------
    // Nothing below stamps a flat black over a colour the user chose, and nothing borrows
    // the note's ink: an explicit ink may be deliberately low-contrast, which is the user's
    // call for their own text but never for a control a keyboard must find.
    function relLuminance(c) {
        function linear(v) { return v<=0.03928 ? v/12.92 : Math.pow((v+0.055)/1.055,2.4) }
        return 0.2126*linear(c.r)+0.7152*linear(c.g)+0.0722*linear(c.b)
    }
    /** WCAG contrast ratio between two colours, 1.0 (identical) to 21.0. */
    function contrastOf(a,b) {
        var x=relLuminance(a), y=relLuminance(b)
        return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05)
    }
    /** A subtle tone taken from a colour's OWN hue: lifted on a dark colour, deepened on
     *  a light one, so an outline reads as belonging to the surface it edges. */
    function derivedTone(base,amount) {
        return relLuminance(base) < 0.42 ? Qt.lighter(base,1+amount) : Qt.darker(base,1+amount)
    }
    /** The black/white endpoint with the better ACTUAL contrast against `base`. Used only
     *  where a control must stay findable — keyboard focus — never as decoration. */
    function controlAccent(base) {
        var l=relLuminance(base)
        return ((l+0.05)/0.05) >= (1.05/(l+0.05)) ? Qt.rgba(0,0,0,1) : Qt.rgba(1,1,1,1)
    }
    // --- Fixed neutral chrome ---------------------------------------------------------
    // Settings and the close question are DESKTOP chrome: one fixed charcoal whatever paper
    // and ink the selected note carries, so a cream note and a near-black one produce the
    // same dialog. Kept local — no system colour scheme is read.
    readonly property color neutralBg: "#15171b"
    readonly property color neutralOutline: "#242c3b"
    readonly property color neutralScroll: "#2a2c2f"
    readonly property color neutralText: "#d8dfe6"
    readonly property color neutralWhite: "#ffffff"
    /** The one accent: keyboard focus. Blue, so it is never mistaken for a note colour. */
    readonly property color neutralAccent: "#4c8dff"
    /** Fixed chrome type as well as fixed chrome colour: the global note font is a
     *  writing choice and must not restyle the question that guards unsaved work. */
    readonly property string neutralFont: "Noto Sans"
    /** The keyboard focus ring: the SURFACE's own tone pushed until plainly visible, rather
     *  than a flat black ring stamped over a chosen colour. Drawn from the surface while a
     *  selection halo is drawn from the swatch, so the two never read alike on one dot. */
    function focusTone(surface) {
        var steps=[0.55,0.9,1.3,1.8,2.4]
        for(var i=0;i<steps.length;i++) {
            var t=derivedTone(surface,steps[i])
            if(contrastOf(t,surface) >= 2.4) return t
        }
        return derivedTone(surface,2.4)
    }

    /** Adopt a fresh native manifest snapshot; never rebuilds editors. */
    function applyManifest(value) {
        if(!value || !value.ok) return value
        dialog.manifest = value
        dialog.ids = value.ids; dialog.titles = value.titles
        dialog.papers = value.paper; dialog.inks = value.ink
        dialog.palette = value.palette; dialog.palettes = value.palettes; dialog.order = value.order
        if(value.openFolder !== undefined) dialog.openFolder = value.openFolder
        dialog.activePalette = value.activePalette
        dialog.inkModes = value.inkMode; dialog.inkStored = value.inkStored
        titleField.reset()
        return value
    }
    // Keep the same dock and editor objects alive through collapse, reorder and rename.
    /** @param activate false when the caller has no real user activation to spend — a tray
     *  verb on Wayland has no xdg-activation token, and requesting focus without one makes
     *  KWin paint a "demands attention" frame around the dock instead. */
    function openNote(index, activate) {
        selected = index; expanded = true; loaded = true
        titleField.reset()
        if(activate !== false) dialog.requestActivate()
        if(loader.item) loader.item.resume(index)
        alignment.restart()
    }
    function openNoteId(id, activate) { openNote(ids.indexOf(id), activate) }

    /** Follow a link the user clicked inside a note.
     *
     *  The card is an editor, never a browser: no link navigates the web view. A note in
     *  THIS library opens in the card, http(s) goes to the default browser, and
     *  mailto/tel/anything else is handed to the desktop. `javascript:` is refused — it
     *  would execute in the editor's own context, which holds the live WebChannel to the
     *  shell (save, rename, change folder). */
    function followLink(href) {
        if(!href) return
        var lower = href.toLowerCase()
        if(lower.indexOf("javascript:") === 0 || lower.indexOf("data:") === 0) {
            dialog.saveStatus = "Blocked a script link — notes cannot run code"
            return
        }
        // A link into this library, by file name: "Other.md", "./Other.md", "notes/Other.md",
        // or the file:// form the web view resolves a relative href into.
        var root = shellControl.rootPath || ""
        var path = ""
        if(lower.indexOf("file://") === 0) path = decodeURIComponent(href.substring(7))
        else if(href.indexOf("://") < 0) path = href
        if(path !== "") {
            var leaf = path.split("#")[0].split("?")[0]
            if(leaf.toLowerCase().lastIndexOf(".md") === leaf.length-3) {
                var wanted = leaf.split("/").pop()
                for(var i=0;i<dialog.order.length;i++) {
                    var id = dialog.order[i]
                    if(String(dialog.fileNames[id]) === wanted) { dialog.openNoteId(id, true); return }
                }
                dialog.saveStatus = "No note named " + wanted + " in this folder"
                return
            }
        }
        // Everything else belongs to the desktop: default browser for http(s), mail client
        // for mailto:, dialler for tel:, registered handler otherwise. A RELATIVE href must
        // be made absolute against the LIBRARY first: assets are stored as
        // "Assets/audio/foo.wav" to keep the notes folder portable, but a bare relative path
        // handed to the desktop resolves against the APPLICATION's directory instead.
        if(path !== "" && href.indexOf("://") < 0 && root !== "") {
            var abs = path.charAt(0) === "/" ? path
                    : root.replace(/\/+$/, "") + "/" + path.replace(/^\.\//, "")
            Qt.openUrlExternally("file://" + encodeURI(abs))
            return
        }
        Qt.openUrlExternally(href)
    }
    /** Point the fan at `folder` (root-relative; "" is the library root).
     *
     *  THE scoping gesture. Opening a folder in the Library, opening a note from the
     *  Library, creating a note and restoring one all route through here, so none of them
     *  can drift into a different idea of what the fan is showing.
     *
     *  The open card is collapsed first: it is showing a note from the folder being left,
     *  and leaving it open would paint a card whose tab is no longer on the edge.
     *  @return true when the scope moved (or was already there). */
    function scopeToFolder(folder) {
        var wanted = folder === undefined || folder === null ? "" : String(folder)
        if(wanted === dialog.openFolder) return true
        var result = notesStore.openFolder(wanted)
        if(!result || !result.ok) {
            dialog.saveStatus = "Folder refused · " + (result && result.error ? result.error : "unknown")
            return false
        }
        dialog.collapse()
        applyManifest(result)
        dialog.selected = 0
        return true
    }
    /** Create a note, put it on the fan, and open it. THE creation path — the fan's "+",
     *  the empty state's button and Ctrl+N all run this one body, so none can drift into
     *  reporting a different refusal than the others.
     *
     * @param activate false when the caller has no real user activation to spend. See
     *   openNote(): a token-less request makes KWin flag demands-attention, and a keystroke
     *   arrives precisely because the window already holds focus.
     * @return the new note's id, or "" on refusal; the refusal text goes to saveStatus. */
    function createNoteOnFan(activate) {
        // "Whatever folder is open, that's where the note goes" — the Principal's rule.
        // There is no joinFan afterwards: the note is in the open folder, so the engine's
        // derivation already has it on the fan.
        var id = collection.createNote(dialog.openFolder)
        if(!id) { dialog.saveStatus = "New note refused · " + collection.lastError; return "" }
        applyManifest(notesStore.load())
        // Guarded by identity, not by count: a manifest that does not hold the new id
        // would make openNoteId() select index -1 and paint an empty card.
        if(dialog.ids.indexOf(id) >= 0) openNoteId(id, activate)
        return id
    }
    function collapse() {
        if (!expanded) return
        if(loader.item) loader.item.suspend()
        expanded = false; paletteOpen = false
        surface.forceActiveFocus()
        alignment.restart()
    }
    function requestClose() {
        if(!dialog.dirty) Qt.quit()
        else { dialog.closeRequested=true; dialog.openNote(dialog.selected) }
    }
    onClosing: function(close) { if(dialog.dirty && !dialog.allowDiscard) { close.accepted=false; dialog.requestClose() } }
    onActiveChanged: if(!active && expanded && !closePrompt.visible) collapse()

    // ---- Screen rectangles ---------------------------------------------------------
    // Screen reports the full screen; the AVAILABLE work area (panels and struts removed) is
    // read back natively from the QScreen this window is on, and re-read on screen or
    // work-area change. The dock is placed against the available rectangle, never the full
    // one, so a panel on the same edge is never painted over.
    property real screenX: Screen.virtualX
    property real screenY: Screen.virtualY
    property real screenW: Screen.width
    property real screenH: Screen.height
    property rect available: screenGeometry.available
    property real availX: dialog.available.width>0 ? dialog.available.x : dialog.screenX
    property real availY: dialog.available.height>0 ? dialog.available.y : dialog.screenY
    property real availW: dialog.available.width>0 ? dialog.available.width : dialog.screenW
    property real availH: dialog.available.height>0 ? dialog.available.height : dialog.screenH

    // ---- Fan geometry and the compact edge hover spread ------------------------------
    // ONE global spacing control: the fan stick PITCH in pixels. Not tabSpacing, which is the
    // tab-character width inside the editor. Visual placement, the shingled hit length,
    // drag-slot rounding all read this same live pitch, so a
    // reordered drag lands on an exact identity target in either deck state.
    property int fanTabLength: Math.max(60, Math.min(240, Math.round(dialog.appearance.fanTabLength || 114)))
    /** Tab width, settable like the length. Every stick, hit target, trigger strip and
     * the collapsed dock derive from THIS, so the geometry stays one fact. */
    property int fanTabWidth: Math.max(24, Math.min(72, Math.round(dialog.appearance.fanTabWidth || 36)))
    /** Tab-label typography. GLOBAL, and separate from the editor text: these three touch
     * the painted stick labels only, and the defaults reproduce what the deck rendered
     * before the settings existed.
     *
     * The 7..18 bound is practical: a stick is 36 px wide and a label line measures 13 px
     * tall at the default 9 px, so 18 px is the largest size whose line (~26 px) still sits
     * inside the tab width with margin. Tab geometry and hit zones never derive from it. */
    property int fanLabelSize: Math.max(7, Math.min(18, Math.round(dialog.appearance.fanLabelFontSize)))
    property bool fanLabelBold: dialog.appearance.fanLabelBold !== false
    /** A tab label is that note's own ink, always. The retired global fanLabelColor is not
     * read here, so a value left in an old appearance.json cannot reach the label. */
    function labelInkOf(id) { return dialog.inkOf(id) }
    property int fanTopInset: 42
    /** Clearance between the lane's bottom and the SCREEN's edge. On Wayland a client is
     *  never told the work area — availH is the full screen height — so this inset must
     *  clear the task manager on its own. 64 px clears Plasma's default 44 px panel with
     *  margin to spare; 18 px parks the "+" on top of it. */
    property int fanBottomInset: 64
    /** The full-edge surface: the window spans the WORK AREA's height rather than the card's,
     * so the deck may spread the whole screen edge. The card floats centred inside it; fan
     * geometry anchors to the BOTTOM, nearest the panel and task manager.
     *
     * Clamped to the work area, never merely floored to it. A surface taller than the
     * available height makes the centring offset in `alignmentTimer` negative; the
     * `Math.max(0, …)` guard there pins it to zero and the whole deck jumps to the top of the
     * screen. Written as a plain expression because with both bounds equal to `availH - 12`
     * a min-of-max collapses to exactly that. */
    property int fanSurfaceH: Math.max(0, dialog.availH - 12)
    /** Zone reserved at the bottom of the lane for the "+": a 30 px button plus breathing.
     *
     * A CONSTANT, deliberately. Deriving it from `fanHitOffset` (which follows the live
     * pitch) makes `fanBaseY` — and therefore every stick and the "+" — jump the moment the
     * deck spreads on hover. The "+" anchors to slot 0's real bottom edge instead, so the
     * gap is pitch-independent. */
    property int fanPlusZone: 56
    /** Top edge of SLOT 0's stick: the bottom-most stick, directly above the "+".
     * Note 1 sits nearest the "+" and the fan grows upward, away from it. */
    property int fanBaseY: dialog.fanSurfaceH - dialog.fanBottomInset - dialog.fanPlusZone - dialog.fanTabLength
    /** Top of the clickable strip: just above where the highest stick CAN reach, or just
     * above the "+" when the library is empty.
     *
     * Measured against `fanSpreadPitch`, never the live pitch. Sizing the window to the
     * COLLAPSED deck while `fanTrigger` computes its top from the SPREAD pitch leaves most of
     * the hover strip outside the window, where the compositor clips it, so only the bottom
     * sliver of the screen edge answers hover. Reserving the spread extent also keeps the
     * window from resizing mid-hover, which is what makes the deck jitter. */
    property int fanMaskTop: dialog.order.length > 0
        ? Math.max(0, dialog.fanBaseY - (dialog.order.length - 1)*dialog.fanSpreadPitch - 10)
        : Math.max(0, dialog.fanSurfaceH - dialog.fanBottomInset - dialog.fanPlusZone - 12)
    /** The WINDOW is only as tall as the part of the lane actually in use.
     *
     * `fanSurfaceH` is the full work-area height and must stay the coordinate space the fan
     * geometry is expressed in. But a window of that height is a 48 px column running the
     * whole screen edge, and every pixel of it eats input — including the close button of
     * whatever is maximised underneath. PlasmaCore.Dialog exposes no input mask (`margins`,
     * `outputOnly` and `flags` exist; nothing region-shaped), so the window itself has to end
     * where the deck ends. Expanded it is the full surface, because the card floats centred
     * in it and the outside-click catcher needs the whole area. */
    /** With an EMPTY fan the welcome panel is on screen, and it is a full card-sized surface
     *  rather than a strip. Masking down to a nonexistent deck puts its buttons below the
     *  fold, which strands an archived-everything library with no route back. */
    property int fanVisibleTop: (dialog.expanded || dialog.order.length === 0)
        ? 0 : dialog.fanMaskTop
    /** The outward hover lift, READ from the shared contract rather than copied, and the lane
     * the window reserves for it.
     *
     * The lift moves the stick FACE toward the desktop, which in a right-docked window is
     * negative x, so a window exactly as wide as a stick paints the lifted face partly
     * outside itself and the compositor clips that strip: a hover renders as a stick with a
     * slice missing. The reserve sits on the INWARD side, keeping every right-edge coordinate
     * unchanged, and is a constant — never added or removed mid-hover. */
    property int fanLiftDistance: Math.abs(LayoutContract.stickLift("right", false, true).x)
    property int fanLiftReserve: dialog.fanLiftDistance + 4
    /** One stick plus that reserve: the width of the collapsed edge dock. */
    property int fanCollapsedWidth: dialog.fanTabWidth + dialog.fanLiftReserve
    /** Largest pitch that still keeps the last stick inside the currently available height. */
    property int fanPitchLimit: dialog.order.length > 1
        ? Math.max(12, Math.floor((dialog.fanBaseY - 6)/(dialog.order.length-1)))
        : Math.round(dialog.appearance.fanSpacing)
    /** The pitch the global fanSpacing setting asks for, clamped to the available height.
     * An existing configuration keeps exactly the fan it had, now reached by hovering. */
    property int fanSpreadPitch: Math.max(12, Math.min(Math.round(dialog.appearance.fanSpacing), dialog.fanPitchLimit))
    /** Resting pitch of the untouched deck: a tight stack whose sticks only peek past one
     * another, so the quiet edge stays quiet. A design constant rather than a second
     * setting, and never wider than the spread pitch it has to fit inside. */
    property int fanCompactPitch: Math.min(14, dialog.fanSpreadPitch)
    /** The ONE live pitch every consumer reads: layout, shingled hit length, drag rounding. */
    property int fanPitch: dialog.fanSpread ? dialog.fanSpreadPitch : dialog.fanCompactPitch
    /** A vertical offset that is inside every shingled stick's own hit area at any pitch. */
    property int fanHitOffset: Math.max(4, Math.min(20, Math.round(dialog.fanPitch/2)))
    /** Slot Y in FULL-SURFACE coordinates — the space fan geometry is defined in. */
    function fanSlotY(slot) { return dialog.fanBaseY - slot*dialog.fanPitch + dialog.fanHitOffset }
    /** Slot Y inside the WINDOW, which collapsed is only the occupied strip. Everything
     *  that paints or hit-tests a stick uses this; fanSlotY stays the geometric truth so
     *  fanMaskTop can be derived from it without a circular binding. */
    function fanSlotWinY(slot) { return dialog.fanSlotY(slot) - dialog.fanVisibleTop }

    // ---- Hover state ----------------------------------------------------------------
    // Hovered and dragged sticks are tracked by note IDENTITY, not slot index: a slot index
    // moves under a reorder and a note ID does not, so a highlight cannot jump to a different
    // note when the deck rearranges itself under the pointer.
    property string hoveredNoteId: ""
    property string draggingNoteId: ""
    /** Hover is reported by the lane trigger OR by a stick. The two overlap, and Qt is not
     * required to deliver one hover position to both, so either source is enough. */
    property bool fanPointerInside: fanTrigger.containsMouse || dialog.hoveredNoteId !== ""
    /** Latched open by hover, released only by the hysteresis timer below. */
    property bool fanHoverOpen: false
    /** Reasons the deck must stay spread wherever the pointer currently is: an open note
     * card, an open swatch menu, or a drag in progress. */
    property bool fanHoldsOpen: dialog.expanded || dialog.paletteOpen || dialog.draggingNoteId !== ""
    property bool fanSpread: dialog.fanHoverOpen || dialog.fanHoldsOpen
    /** Auto-hide: with the setting on and nothing using the deck, the sticks fade out and only
     * a small reveal glyph stays in the corner. The EDGE remains live throughout — the trigger
     * strip still catches hover, which sets fanSpread, which unhides — so revealing costs a
     * hover, never a click. */
    property bool fanHiddenIdle: dialog.appearance.fanAutoHide === true
        && !dialog.fanSpread && !dialog.expanded && !libraryPanel.visible
    /** Hysteresis before a hovered deck falls closed again.
     *
     * 200 ms is a design choice, not a platform standard: the KDE HIG publishes no hover-close
     * delay. It sits between Kirigami's shortDuration (100 ms) and its humanMoment (2000 ms)
     * — long enough to cross the gap between two sticks or slip from a stick onto the card,
     * short enough that a deck left behind does not feel stuck open. An input affordance
     * rather than an animation, so deliberately NOT scaled away when animations are off. */
    property int fanCloseDelay: 200
    /** Transitions use the framework's own durations, NOT a locally rescaled copy.
     *
     * Kirigami's Units already multiply every duration it publishes by the desktop's
     * KDE/AnimationDurationFactor, from the same kdeglobals AnimationSettings watches, so
     * multiplying again here would square it.
     *
     * `animationFactor` is still read for two things: metrics report which config file the run
     * honoured, and it forces an explicit zero. That zero is not redundant with Kirigami
     * everywhere — a style publishing a floor instead of 0 would still animate — and
     * "animations globally disabled means instant" is an accessibility requirement
     * (https://develop.kde.org/hig/accessibility/), not a scaling preference. */
    property real animationFactor: animationSettings.factor
    property int fanSpreadDuration: dialog.animationFactor === 0 ? 0 : Kirigami.Units.longDuration
    property int fanLiftDuration: dialog.animationFactor === 0 ? 0 : Kirigami.Units.shortDuration
    onFanPointerInsideChanged: {
        if(dialog.fanPointerInside) { fanHold.stop(); dialog.fanHoverOpen = true }
        else fanHold.restart()
    }
    onFanHoldsOpenChanged: if(!dialog.fanHoldsOpen && !dialog.fanPointerInside) fanHold.restart()
    property Timer fanHoldTimer: Timer {
        id: fanHold; interval: dialog.fanCloseDelay
        onTriggered: if(!dialog.fanPointerInside && !dialog.fanHoldsOpen) dialog.fanHoverOpen = false
    }

    /** Move one note to a fan slot by identity; content, caret and history stay with the ID. */
    function moveNote(id,slot) {
        var next = order.slice()
        var from = next.indexOf(id)
        if(from<0) return
        next.splice(from,1)
        next.splice(Math.max(0,Math.min(next.length,slot)),0,id)
        if(next.join(",")===order.join(",")) return
        // Selection is an INDEX into ids, and the manifest may renumber ids on a reorder.
        // Keep it by IDENTITY across the move, or the open card adopts the note that landed
        // on its old index: paper repainted to that neighbour's colour, editor unchanged.
        var keep = dialog.selectedId
        var result = notesStore.setOrder(next)
        if(result.ok) {
            applyManifest(result)
            var at = dialog.ids.indexOf(keep)
            if(at >= 0) dialog.selected = at
        }
        else dialog.saveStatus = result.error
    }
    /** Whether the swatch panel should be painting a KEYBOARD cue at all.
     *
     * Binding the cue to `activeFocus` alone latches a focus ring onto a dot after a
     * pointer-only interaction, because togglePalette() below always moves focus
     * programmatically on open — including when a mouse press opened the panel. Focus is still
     * moved on open (Escape has to reach QML rather than the WebEngine child), but the visible
     * cue is gated on the focus REASON: it appears only once the keyboard has moved between
     * dots, and any pointer press clears it again. */
    property bool swatchKeyboardCue: false
    /** Move swatch focus by `step` and say so with the keyboard cue. Order matches the
     *  painted Flow: the Auto chip first in ink mode, then every palette dot. */
    function moveSwatchFocus(step) {
        var chain = []
        if(autoDot.visible) chain.push(autoDot)
        for(var i=0;i<swatchRepeater.count;i++) { var d=swatchRepeater.itemAt(i); if(d) chain.push(d) }
        if(!chain.length) return
        var at = -1
        for(var j=0;j<chain.length;j++) if(chain[j].activeFocus) { at = j; break }
        var next = at < 0 ? 0 : (at + step + chain.length) % chain.length
        dialog.swatchKeyboardCue = true
        chain[next].forceActiveFocus(Qt.TabFocusReason)
    }
    /** Toggle the in-card colour panel. Opening it closes the formatting disclosure so
     *  the two never occupy the same strip above the footer. */
    function togglePalette(mode) {
        var wanted = mode ? mode : "paper"
        // Paper and Ink are two buttons onto ONE panel: the other button switches its mode
        // in place, the same button again closes it, exactly like a single toggle.
        var opening = !paletteOpen || paletteMode !== wanted
        if(opening) { dialog.showInfo = false; dialog.iconPanelOpen = false }   // one panel over the card at a time
        if(opening && loader.item) loader.item.runJavaScript("if(appearance.open) appearance.toggle(false); if(fan.formattingVisible()) fan.toggleFormatting()")
        paletteMode = wanted
        paletteOpen = opening
        // The WebEngine child otherwise retains keyboard focus and consumes Escape before
        // QML's ancestor key handler sees it. Focusing a real swatch also makes keyboard
        // navigation visible without changing editor state.
        //
        // Focus lands on the swatch this note ALREADY has whenever the panel offers it, so the
        // focus ring and selection mark start on the same dot; two lit dots read as two active
        // choices. The scan covers the Auto chip as well as the palette dots, in the order the
        // Flow paints them, so the fallback is the FIRST painted circle.
        dialog.swatchKeyboardCue = false
        if(opening) {
            var chain = []
            if(paletteMode === "ink") chain.push(autoDot)
            for(var i=0;i<swatchRepeater.count;i++) { var dot = swatchRepeater.itemAt(i); if(dot) chain.push(dot) }
            var target = null
            for(var j=0;j<chain.length;j++) if(chain[j].selected) { target = chain[j]; break }
            if(!target && chain.length) target = chain[0]
            if(target) target.forceActiveFocus(Qt.PopupFocusReason)
        }
    }
    /** Re-read the native colours into the resident editor CSS. Appearance only: it never
     *  replaces editor text, undo history or caret. One place, so every colour entry
     *  point (panel, keyboard, bridge) repaints the same way. */
    function syncEditorColours() {
        if(loader.item) loader.item.runJavaScript("window.fan && fan.refresh()")
    }
    /** Route a swatch press to whichever surface the panel is currently assigning. */
    function chooseSwatch(color) {
        if(paletteMode === "ink") chooseInk(color); else choosePaper(color)
    }
    /** Store one literal ink, or the "auto" sentinel, against the selected note. An explicit
     *  low-contrast choice is the user's own and is applied as given.
     *
     *  The panel STAYS OPEN after a selection: colour choice is comparative, and a panel that
     *  dismisses itself on every pick turns comparing two colours into a fight. */
    function chooseInk(value) {
        var result = notesStore.setInk(selectedId,value)
        if(result.ok) {
            applyManifest(result)
            // Same native path as paper: the resident editor re-reads the colours it was
            // given. Appearance only — never text or history.
            syncEditorColours()
            // No routine chatter: the note changed colour under the panel, which says it
            // better than a sentence. Only a REFUSAL writes to the status line.
        }
        else dialog.saveStatus = result.error
    }
    /** Store one literal colour against the selected note. Its paper and its fan tab
     *  change together because both read the same stored value; no other note moves.
     *  Stays open for the same reason chooseInk does: colour choice is comparative. */
    function choosePaper(color) {
        var result = notesStore.setPaper(selectedId,color)
        if(result.ok) {
            applyManifest(result)
            // The native panel owns this action, so the editor CSS is synchronised on the
            // real click path rather than only when a later settings action occurs.
            // Appearance only: never editor text or history.
            syncEditorColours()
        }
        else dialog.saveStatus = result.error
    }
    /** Select the palette offering the CHOICES. No note is recoloured, no editor is
     *  rebuilt and no Markdown is written; only the swatches on offer change. */
    function choosePalette(key) {
        var result = notesStore.setPalette(key)
        if(result.ok) applyManifest(result)   // the offered swatches changing says it
        else dialog.saveStatus = result.error
    }

    /** Filing verbs. Each refreshes the manifest and re-selects, because removing the
     *  selected note from the fan leaves `selected` past the end of a shorter deck. */
    function reselectAfterFiling(previousId) {
        applyManifest(notesStore.load())
        if(dialog.order.length === 0) { dialog.collapse(); dialog.selected = 0; return }
        var at = dialog.order.indexOf(previousId)
        if(at >= 0) { dialog.selected = at; return }
        // The filed note is gone from the deck: fall back to the neighbour that took its
        // slot, clamped, rather than to an index that no longer exists.
        dialog.selected = Math.max(0, Math.min(dialog.selected, dialog.order.length-1))
        if(dialog.expanded) dialog.openNote(dialog.selected)
    }
    /** Move the selected note into the library's Archive folder. Reversible from the
     *  Library panel; the Markdown file is moved, never deleted. */
    function archiveSelected() {
        var id = dialog.selectedId
        if(!id) return
        // Flush first: archiving moves the file, and an unsaved buffer whose file moves
        // out from under it is how edits get lost.
        collection.saveNow(id)
        if(!collection.archive(id)) { dialog.saveStatus = "Archive refused · " + collection.lastError; return }
        dialog.confirmArchiveId = ""
        reselectAfterFiling(id)
    }
    /** Move the selected note to the DESKTOP trash — reversible in the file manager,
     *  never an unlink. */
    function trashSelected() {
        var id = dialog.selectedId
        if(!id) return
        collection.saveNow(id)
        if(!collection.moveToTrash(id)) { dialog.saveStatus = "Trash refused · " + collection.lastError; return }
        dialog.confirmTrashId = ""
        reselectAfterFiling(id)
    }
    /** Return an archived note to the folder it came from and put it back on the fan. */
    function restoreNote(id) {
        if(!collection.restoreArchive(id)) { dialog.saveStatus = "Restore refused · " + collection.lastError; return }
        // The note went back to the folder it came from, which may not be the open one.
        // Follow it: a restore that leaves the note invisible reads as a failed restore.
        var document = collection.documentObject(id)
        dialog.scopeToFolder(document ? document.folder : "")
        applyManifest(notesStore.load())
        dialog.libraryOpen = false
        var at = dialog.order.indexOf(id)
        if(at >= 0) openNote(at)
    }
    /** Open an archived-or-fanned note from the Library. An archived note is NOT restored on
     *  the first press: restoring moves the file, so it arms like Archive and Trash. An
     *  ordinary note opens on one press — nothing moves, so there is nothing to confirm. */
    function openFromLibrary(id) {
        var document = collection.documentObject(id)
        if(document && document.archived) {
            if(dialog.confirmRestoreId === id) { dialog.confirmRestoreId = ""; restoreNote(id) }
            else { dialog.confirmRestoreId = id; restoreLapse.restart() }
            return
        }
        dialog.confirmRestoreId = ""
        // Opening a note from the Library scopes the fan to ITS folder — the same gesture
        // as opening that folder. Nothing "joins": membership is derived from the scope.
        var target = collection.documentObject(id)
        dialog.scopeToFolder(target ? target.folder : "")
        applyManifest(notesStore.load())
        dialog.libraryOpen = false
        var at = dialog.order.indexOf(id)
        if(at >= 0) openNote(at)
    }
    /** Pin the selected note into its own ordinary window, or close that window again. A
     *  pinned note LEAVES the fan while its window is open, so the same note is never
     *  presented twice; closing the window returns it. Nothing is deleted either way. */
    function togglePinSelected() {
        var id = dialog.selectedId
        if(!id) return
        var document = collection.documentObject(id)
        if(!document) return
        if(document.pinned) { unpinNote(id); return }
        collection.saveNow(id)
        if(!collection.setPinned(id, true)) { dialog.saveStatus = "Pin refused · " + collection.lastError; return }
        // No leaveFan: pinned is one of the three exclusions the derivation applies, so
        // setting the pin is what takes the tab off the edge.
        dialog.pinnedIds = dialog.pinnedIds.concat([id])
        reselectAfterFiling(id)
    }
    function unpinNote(id) {
        // Clearing the pin is enough — the note returns to the fan whenever its folder is
        // the open one, in the slot this folder's persisted order kept for it.
        collection.setPinned(id, false)
        var next = dialog.pinnedIds.slice()
        var at = next.indexOf(id)
        if(at >= 0) next.splice(at,1)
        dialog.pinnedIds = next
        applyManifest(notesStore.load())
    }
    /** Every note the engine has recorded as pinned; read at startup so a pinned window
     *  survives a restart the way the card requires ("restored membership"). */
    function restorePersistedPins() {
        var restored = []
        var catalog = collection.catalogIds()
        for(var i=0;i<catalog.length;i++) {
            var document = collection.documentObject(catalog[i])
            if(document && document.pinned && !document.archived && !document.trashed && !document.missing) {
                // The engine already keeps a pinned note off the fan; this only rebuilds
                // the window register.
                restored.push(catalog[i])
            }
        }
        if(restored.length) { dialog.pinnedIds = restored; applyManifest(notesStore.load()) }
    }

    property Timer alignmentTimer: Timer {
        id: alignment; interval: 80
        onTriggered: {
            // Docked against the available work area, not the raw screen: a panel on the
            // right edge would otherwise sit on top of the fan tabs.
            dialog.x = LayoutContract.dialogEdgeAxisPosition(dialog.availX, dialog.availW, dialog.width, "right")
            // Collapsed, the window is only the occupied strip of the lane, so it anchors to
            // the BOTTOM of the work area — the deck's own anchor; centring a short window
            // would float the fan into the middle of the screen. Expanded, it is the full
            // surface again and centring keeps the card where it belongs.
            dialog.y = dialog.expanded || dialog.order.length === 0
                ? dialog.availY + Math.max(0, Math.round((dialog.availH-dialog.height)/2))
                : dialog.availY + Math.max(0, dialog.availH - dialog.height)
        }
    }
    Component.onCompleted: { alignment.start(); restorePersistedPins() }
    // Deliberately never started: a notes application must not interrupt someone to ask
    // whether they are still using it. The prompt it would drive is kept, and still guards
    // a genuine close with unsaved buffers.
    property Timer interactiveLimit: Timer {
        interval: 900000; running: false
        onTriggered: { dialog.closeRequested=true; dialog.openNote(dialog.selected) }
    }

    mainItem: Item {
        id: surface
        // An empty library takes the card's own width so the empty state has room to speak.
        // Otherwise the window is one 48 px fan lane over a transparent background —
        // nothing to see, which reads as a failed application.
        width: (dialog.expanded || dialog.order.length === 0) ? dialog.cardWidth+32 : dialog.fanCollapsedWidth
        // Only the occupied strip, so the empty column above the deck is not part of the
        // window and cannot swallow clicks meant for the app underneath. Children keep
        // using fanSurfaceH coordinates and are shifted up by the same offset below.
        height: dialog.fanSurfaceH - dialog.fanVisibleTop
        /** The card's top inside the edge-tall surface: centred, like the old window was. */
        property int cardTop: Math.max(0, Math.round((dialog.fanSurfaceH - dialog.cardHeight)/2) - dialog.fanVisibleTop)
        onWidthChanged: alignment.restart()
        onHeightChanged: alignment.restart()
        /** Only the OCCUPIED part of the lane accepts clicks.
         *
         * The window spans the whole work-area height so the deck can use the full screen
         * edge. Without a mask the empty region above the topmost tab is still ours, and a
         * 48 px column swallows every press for the height of the screen — including the close
         * buttons of whatever is maximised underneath. The mask is the union of the fan's own
         * strip and, while a card is open, the card itself; expanded state masks everything,
         * because the outside-click catcher needs those presses. */
        focus: true
        property bool titleDirty: titleField.text !== titleField.committed
        /** The one button-box rule, shared by BOTH command palettes.
         *
         * `iconSize + 12` is 6 px of padding on every side of the glyph; `actionGap` is the
         * row's spacing. The editor's format toolbar reads these same two numbers as CSS vars
         * (`--actionsize` / `--actiongap` in appearance.js), so Buttons → Size moves both
         * strips identically rather than only the glyph inside one of them. */
        property real actionSize: dialog.appearance.iconSize + 12
        property real actionGap: 2
        // Escape dismisses whichever panel is over the card first — colour, then File
        // details — and collapses the note only when none is open, so closing a panel never
        // also closes the note. It never touches the caret, the buffer or the undo stack.
        Keys.onEscapePressed: {
            if(dialog.libraryOpen) dialog.libraryOpen=false
            else if(dialog.paletteOpen) dialog.paletteOpen=false
            else if(dialog.showInfo) dialog.toggleInfo(false)
            else if(dialog.iconPanelOpen) dialog.toggleIconPanel(false)
            else dialog.collapse()
        }
        /** Ctrl+N — the keystroke the fan's "+" tooltip advertises.
         *
         * WINDOW-SCOPED, deliberately. `Shortcut` defaults to Qt.WindowShortcut and
         * QQuickShortcut resolves "the window" by walking this object's parents — from here
         * `surface` → its window → the dock — so it fires only while this shell holds keyboard
         * focus. A global binding would mean KGlobalAccel, a registered component and a service
         * outliving the app. Pinned notes and the close prompt are separate windows, so the key
         * correctly does nothing while they are focused.
         *
         * The literal "Ctrl+N" rather than StandardKey.New keeps the tooltip's promise and the
         * binding one string: on this platform StandardKey.New IS Ctrl+N, and listing both
         * registers one sequence twice, which Qt reports as an ambiguous activation.
         *
         * activate=false: a keystroke arrives BECAUSE this window already has focus, and a
         * token-less activation request makes KWin flag demands-attention instead. */
        property Shortcut newNoteShortcut: Shortcut {
            sequence: "Ctrl+N"
            onActivated: dialog.createNoteOnFan(false)
        }
        // The WebEngine rectangle stays inside the paper rounded perimeter at every bound.
        Rectangle {
            id: paper
            y: surface.cardTop
            width: dialog.cardWidth; height: dialog.cardHeight
            radius: dialog.appearance.radius; color: dialog.paperColor; visible: dialog.expanded
            Item {
                id: titleSpine; width: 40; height: parent.height
                Accessible.role: Accessible.StaticText
                Accessible.name: "Note spine, left edge · " + dialog.titles[dialog.selectedId]
                Rectangle { width: 80; height: parent.height; radius: paper.radius; color: Qt.darker(paper.color,1.14) }
            }
            Rectangle { x:40; width:40; height:parent.height; color:paper.color }
            Text { x:20-width/2; y:(parent.height-height)/2; width:parent.height-40; rotation:-90; horizontalAlignment:Text.AlignHCenter; elide:Text.ElideRight; text:(dialog.titles[dialog.selectedId] || "").toUpperCase(); color:dialog.ink; font.family:dialog.noteFont; font.pixelSize:10; font.weight:Font.DemiBold }
            Repeater { model:Math.max(0,Math.floor((paper.height-28)/11)); Rectangle { required property int index; x:40; y:14+index*11; width:2; height:6; radius:1; color:Qt.darker(paper.color,1.34) } }

            // Editable title with explicit apply and cancel: nothing is renamed while typing.
            TextField {
                id: titleField
                objectName: "note-title"
                property string committed: ""
                function reset() { committed = dialog.titles[dialog.selectedId] || ""; text = committed }
                x: 56; y: 10; height: 28
                width: Math.max(60, applyTitle.x - 64)
                color: dialog.ink
                font.family: dialog.noteFont; font.pixelSize: 15; font.weight: Font.DemiBold
                selectByMouse: true
                leftPadding: 4; rightPadding: 4; topPadding: 2; bottomPadding: 2
                Accessible.name: "Note title; apply or cancel explicitly"
                background: Rectangle { color: "transparent"; radius: 4; border.width: titleField.activeFocus ? 1 : 0; border.color: Qt.darker(dialog.paperColor,1.35) }
                onAccepted: dialog.applyTitle()
                Keys.onEscapePressed: { titleField.reset(); surface.forceActiveFocus() }
                Component.onCompleted: reset()
            }
            QuietButton {
                id: applyTitle; objectName: "apply-title"
                x: cancelTitle.x - surface.actionSize - 2; y: 10
                size: surface.actionSize; iconSize: dialog.appearance.iconSize
                visible: surface.titleDirty; enabled: visible
                glyph: "dialog-ok"; ink: dialog.ink
                explanation: "Rename file"
                onClicked: dialog.applyTitle()
            }
            QuietButton {
                id: cancelTitle; objectName: "cancel-title"
                x: closeButton.x - surface.actionSize - 6; y: 10
                size: surface.actionSize; iconSize: dialog.appearance.iconSize
                visible: surface.titleDirty; enabled: visible
                glyph: "dialog-cancel"; ink: dialog.ink
                explanation: "Cancel title edit"
                onClicked: { titleField.reset(); surface.forceActiveFocus() }
            }
            // Pin sits beside the close control: both answer "where does this note LIVE",
            // which is a different question from the footer's editing verbs.
            QuietButton {
                objectName: "pin"
                x: paper.width - 22 - 12 - 30; y: 10
                size: 22; iconSize: 14
                glyph: dialog.selectedIsPinned ? "window-unpin" : "window-pin"
                ink: dialog.ink
                explanation: dialog.selectedIsPinned ? "Return this note to the fan" : "Pin this note to its own window"
                onClicked: dialog.togglePinSelected()
            }
            QuietButton {
                id: closeButton; objectName: "collapse"
                // Fixed 22 px: the retired closeSize setting drove nothing worth tuning,
                // and a stored legacy value is simply ignored.
                x: paper.width - 22 - 12; y: 10
                size: 22; iconSize: 14
                glyph: "window-close"; ink: dialog.ink
                explanation: "Collapse · Esc"
                onClicked: dialog.collapse()
            }

            // Every action and the status live in the footer; only title and collapse are on top.
            Item {
                id: footer
                x: 50; y: paper.height - surface.actionSize - 10
                width: paper.width - 62; height: surface.actionSize
                Row {
                    id: footerActions; spacing: surface.actionGap
                    // The two disclosures are mutually exclusive, so the card never stacks
                    // two panels above the footer.
                    QuietButton { objectName:"format"; size:surface.actionSize; iconSize:dialog.appearance.iconSize; glyph:"format-text-bold"; ink:dialog.ink; explanation:"Formatting"; onClicked: { dialog.paletteOpen=false; dialog.showInfo=false; if(loader.item) loader.item.runJavaScript("fan.toggleFormatting()") } }
                    // Paper and Ink are SEPARATE controls onto the SAME compact panel: the
                    // same button again dismisses it, the other switches its mode in place.
                    QuietButton { id: colorButton; objectName:"swatch"; size:surface.actionSize; iconSize:dialog.appearance.iconSize; swatch:dialog.paperColor; ink:dialog.ink; explanation:"Note paper"; onClicked: dialog.togglePalette("paper") }
                    QuietButton { id: inkButton; objectName:"ink"; size:surface.actionSize; iconSize:dialog.appearance.iconSize; swatch:dialog.ink; ink:dialog.ink; explanation:"Note ink"; onClicked: dialog.togglePalette("ink") }
                    QuietButton { objectName:"reload"; size:surface.actionSize; iconSize:dialog.appearance.iconSize; glyph:"view-refresh"; ink:dialog.ink; explanation:"Reload · saved notes only"; onClicked: if(loader.item) loader.item.runJavaScript("fan.reload(fan.active)") }
                    QuietButton { objectName:"library"; size:surface.actionSize; iconSize:dialog.appearance.iconSize; glyph:"view-list-tree"; ink:dialog.ink; explanation:"Library · browse and restore from Archive"; onClicked: dialog.toggleLibrary() }
                    QuietButton { id: iconButton; objectName:"icon"; size:surface.actionSize; iconSize:dialog.appearance.iconSize; glyph:"preferences-desktop-emoticons-symbolic"; ink:dialog.ink; explanation:"Note icon · tab and/or note body"; onClicked: dialog.toggleIconPanel() }
                    QuietButton { objectName:"settings"; size:surface.actionSize; iconSize:dialog.appearance.iconSize; glyph:"configure"; ink:dialog.ink; explanation:"Settings"; onClicked: { dialog.paletteOpen=false; dialog.showInfo=false; if(loader.item) loader.item.runJavaScript("appearance.toggle()") } }
                    QuietButton { id: infoButton; objectName:"trial"; size:surface.actionSize; iconSize:dialog.appearance.iconSize; glyph:"help-about"; ink:dialog.ink; explanation:"File details"; onClicked: dialog.toggleInfo() }
                }
                // ---- Filing group ------------------------------------------------------
                // Archive and Trash are the only two controls that move the user's file. The
                // protection is the interlock, not the layout: each ARMS on the first press
                // and acts only on the second (see confirmArchiveId / confirmTrashId).
                Row {
                    id: filingActions
                    objectName: "filing-actions"
                    x: footerActions.width + surface.actionGap
                    spacing: surface.actionGap
                    QuietButton {
                        objectName: "archive"
                        size: surface.actionSize; iconSize: dialog.appearance.iconSize
                        glyph: dialog.confirmArchiveId === dialog.selectedId && dialog.selectedId !== ""
                            ? "dialog-ok" : "archive-insert"
                        ink: dialog.ink
                        explanation: dialog.confirmArchiveId === dialog.selectedId && dialog.selectedId !== ""
                            ? "Confirm · move this note to Archive"
                            : "Archive this note · press twice"
                        onClicked: {
                            if(dialog.confirmArchiveId === dialog.selectedId && dialog.selectedId !== "") dialog.archiveSelected()
                            else dialog.armArchive()
                        }
                    }
                    QuietButton {
                        objectName: "trash"
                        size: surface.actionSize; iconSize: dialog.appearance.iconSize
                        glyph: dialog.confirmTrashId === dialog.selectedId && dialog.selectedId !== ""
                            ? "dialog-ok" : "user-trash"
                        ink: dialog.ink
                        explanation: dialog.confirmTrashId === dialog.selectedId && dialog.selectedId !== ""
                            ? "Confirm · move this note to the desktop Trash"
                            : "Move this note to the desktop Trash · press twice"
                        onClicked: {
                            if(dialog.confirmTrashId === dialog.selectedId && dialog.selectedId !== "") dialog.trashSelected()
                            else dialog.armTrash()
                        }
                    }
                }
                Text {
                    id: statusText
                    x: filingActions.x + filingActions.width + 10; width: Math.max(40, quitButton.x - x - 8)
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight; text: dialog.saveStatus; color: dialog.ink
                    font.family: dialog.noteFont; font.pixelSize: 10
                    // The status line can elide in a narrow card; the accessible name is
                    // always the whole sentence.
                    Accessible.role: Accessible.StaticText
                    Accessible.name: dialog.saveStatus
                }
                QuietButton {
                    id: quitButton; objectName: "quit"
                    x: footer.width - surface.actionSize; size: surface.actionSize; iconSize: dialog.appearance.iconSize
                    glyph: dialog.confirmQuit ? "dialog-ok" : "system-shutdown"; ink: dialog.ink
                    explanation: dialog.confirmQuit ? "Confirm · quit Fan Fold"
                                                    : "Quit Fan Fold · press twice"
                    onClicked: {
                        if(dialog.confirmQuit) dialog.requestClose()
                        else dialog.armQuit()
                    }
                }
            }

        }
        Loader {
            id: loader; x: 42; y: surface.cardTop+46; width: dialog.cardWidth-58; height: dialog.cardHeight-46-surface.actionSize-16
            active: dialog.loaded; visible: dialog.expanded; sourceComponent: editorComponent
        }
        /** "Name this recording" — shown every time a recording stops, before it is written.
         *  It covers the editor because the answer decides whether the clip is kept at all:
         *  Save writes it, Discard throws the audio away, no third outcome is left around. */
        Rectangle {
            id: recordingPrompt
            function open(purpose) {
                bridge.recordingName = ""
                headline.text = purpose && purpose.length ? purpose : "Name this recording"
                nameField.text = ""
                visible = true
                nameField.forceActiveFocus()
            }
            function finish(name) {
                visible = false
                bridge.recordingName = name === "" ? "\u0000" : name
            }
            x: 42; y: surface.cardTop+46
            width: dialog.cardWidth-58; height: dialog.cardHeight-46-surface.actionSize-16
            visible: false; z: 60
            color: dialog.paperColor
            radius: dialog.appearance.radius
            // Swallow clicks so nothing reaches the editor behind the prompt.
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
            Column {
                anchors.centerIn: parent
                width: Math.min(360, parent.width-48)
                spacing: 12
                Text {
                    id: headline
                    text: "Name this recording"
                    color: dialog.ink
                    font.family: dialog.noteFont; font.pixelSize: 15; font.weight: Font.DemiBold
                }
                TextField {
                    id: nameField
                    objectName: "recording-name"
                    width: parent.width
                    color: dialog.ink
                    font.family: dialog.noteFont; font.pixelSize: 14
                    selectByMouse: true
                    placeholderText: "what it is about"
                    placeholderTextColor: Qt.rgba(dialog.ink.r, dialog.ink.g, dialog.ink.b, 0.45)
                    leftPadding: 6; rightPadding: 6; topPadding: 4; bottomPadding: 4
                    Accessible.name: "Recording name; Enter to save, Escape to discard"
                    background: Rectangle {
                        color: "transparent"; radius: 3
                        border.width: 1
                        border.color: Qt.rgba(dialog.ink.r, dialog.ink.g, dialog.ink.b, 0.30)
                    }
                    // Enter saves; Escape discards. Both work without the mouse because the
                    // field already holds focus when the prompt opens.
                    Keys.onReturnPressed: if(nameField.text.trim() !== "") recordingPrompt.finish(nameField.text.trim())
                    Keys.onEnterPressed: if(nameField.text.trim() !== "") recordingPrompt.finish(nameField.text.trim())
                    Keys.onEscapePressed: recordingPrompt.finish("")
                }
                Row {
                    spacing: 8
                    QuietButton {
                        glyph: "document-save"; explanation: "Save recording"
                        ink: dialog.ink; size: 28
                        enabled: nameField.text.trim() !== ""
                        opacity: enabled ? 1 : 0.4
                        onClicked: recordingPrompt.finish(nameField.text.trim())
                    }
                    QuietButton {
                        glyph: "edit-delete"; explanation: "Discard recording"
                        ink: dialog.ink; size: 28
                        onClicked: recordingPrompt.finish("")
                    }
                }
            }
        }
        // Outside-click dismissal. It sits just under the panel and over everything else,
        // including the WebEngine view, because a press inside the web content is consumed
        // there and would never reach QML. The WHOLE footer strip is excluded: excluding only
        // the routine row silently swallows every press on Archive, Trash and Quit.
        MouseArea {
            id: paletteDismiss; objectName: "palette-dismiss"
            anchors.fill: parent; z: 299
            visible: (dialog.paletteOpen || dialog.showInfo || dialog.iconPanelOpen) && dialog.expanded; enabled: visible
            acceptedButtons: Qt.AllButtons
            onPressed: function(mouse) {
                var p = mapToItem(footer, mouse.x, mouse.y)
                if(p.x>=0 && p.y>=0 && p.x<=footer.width && p.y<=footer.height) {
                    // Every footer control keeps its own press. The three panel toggles keep
                    // their toggle semantics; everything else acts normally and ALSO
                    // dismisses, because acting while an obsolete panel hangs open reads as
                    // broken.
                    var c = mapToItem(colorButton, mouse.x, mouse.y)
                    var k = mapToItem(inkButton, mouse.x, mouse.y)
                    var f = mapToItem(infoButton, mouse.x, mouse.y)
                    var g = mapToItem(iconButton, mouse.x, mouse.y)
                    var onPanelButton = (c.x>=0 && c.y>=0 && c.x<=colorButton.width && c.y<=colorButton.height)
                                     || (k.x>=0 && k.y>=0 && k.x<=inkButton.width && k.y<=inkButton.height)
                                     || (f.x>=0 && f.y>=0 && f.x<=infoButton.width && f.y<=infoButton.height)
                                     || (g.x>=0 && g.y>=0 && g.x<=iconButton.width && g.y<=iconButton.height)
                    if(!onPanelButton) { dialog.paletteOpen=false; dialog.showInfo=false; dialog.iconPanelOpen=false }
                    mouse.accepted=false; return
                }
                dialog.paletteOpen=false; dialog.showInfo=false; dialog.iconPanelOpen=false; mouse.accepted=true
            }
        }
        // Compact circular swatches of the palette currently offering choices, in-card and
        // immediately above the footer. A swatch assigns its literal colour to the SELECTED
        // note only, and carries an accessible name rather than a tooltip. A sibling of the
        // WebEngine Loader (not nested under paper) and declared after it, so its z-order is
        // compared against the loader.
        Rectangle {
            id: swatchPop; objectName: "swatch-popup"
            visible: dialog.paletteOpen && dialog.expanded
            // Clamped into the card so a small window cannot push the row past either
            // border; the dots keep their size and the panel keeps its margin.
            readonly property int dotSize: 22
            // The gap clears two neighbouring focus rings (4 px outside each dot) instead of
            // letting them collide, so an out-of-row ring can never be mistaken for a border.
            readonly property int dotGap: 9
            // Every ring is drawn OUTSIDE its dot, so the scrolled content is inset by that
            // overhang and a ring on the first dot or the top row stays inside the clip.
            readonly property int ringInset: 5
            // The mode label gets a band of its own; the first row starts below it rather
            // than against it.
            readonly property int headerHeight: 21
            // An imported variant offers every token it publishes — around two dozen dots —
            // so the row WRAPS and, past the compact height bound, scrolls. The panel never
            // grows to fit its content, and the bound follows the footer so on a short window
            // the rows still end above it.
            readonly property bool inkMode: dialog.paletteMode === "ink"
            readonly property int maxBodyHeight:
                Math.max(dotSize+2*ringInset, Math.min(92, footer.y - headerHeight - 18))
            readonly property real fullWidth: 12 + 2*ringInset + 8*(dotSize+dotGap) - dotGap
            width: Math.min(fullWidth, paper.width-24)
            x: Math.max(8, Math.min(50, paper.width-width-8))
            // The theme list adds its own band between the header and the dots when open.
            height: headerHeight + 6 + (themeList.open ? themeList.height + 6 : 0)
                    + Math.min(maxBodyHeight, swatchFlow.implicitHeight + 2*ringInset)
            y: Math.max(surface.cardTop+6, surface.cardTop + footer.y - height - 6)
            radius: 8; color: paper.color; border.width: 1; border.color: Qt.darker(paper.color,1.3); z: 300
            Accessible.role: Accessible.Grouping
            Accessible.name: (inkMode ? "Note ink · " : "Note paper · ") + dialog.activePalette.label
            // The mode label says which surface a press will assign. The palette name is a
            // CONTROL: clicking the header toggles the theme list, so the chooser lives
            // beside the swatches it repopulates rather than in Settings.
            Text {
                id: modeLabel; objectName: "swatch-mode-label"
                x: 8; y: 4; font.family: dialog.noteFont; font.pixelSize: 9; color: dialog.ink
                text: (swatchPop.inkMode ? "Ink" : "Paper") + " · " + dialog.activePalette.label
                     + (swatchPop.inkMode && dialog.inkModes[dialog.selectedId]==="auto" ? " · Auto" : "")
                     + (themeList.open ? "  ▴" : "  ▾")
                width: swatchPop.width-16; elide: Text.ElideRight
                MouseArea {
                    anchors.fill: parent; anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: themeList.open = !themeList.open
                    Accessible.role: Accessible.Button
                    Accessible.name: "Choose a colour theme · current " + dialog.activePalette.label
                }
            }
            /** The theme chooser: every palette as name + its real colours. Mid grey ground,
             *  DELIBERATELY not the note's paper: a neutral mid tone is the only background
             *  that judges light and dark palettes fairly, the same reason photographic grey
             *  cards are 18% grey. */
            Rectangle {
                id: themeList; objectName: "theme-list"
                property bool open: false
                visible: open
                x: 6; y: swatchPop.headerHeight
                width: swatchPop.width-12
                height: Math.min(200, themeCol.implicitHeight + 10)
                radius: 6
                color: "#808080"
                border.width: 1; border.color: "#33ffffff"
                Flickable {
                    anchors.fill: parent; anchors.margins: 5
                    contentHeight: themeCol.implicitHeight; clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: themeCol
                        width: parent.width
                        Repeater {
                            model: {
                                // palettes with group separators, in catalog order
                                var rows = [], lastGroup = ""
                                var all = dialog.palettes || []
                                for (var i = 0; i < all.length; i++) {
                                    if (all[i].group !== lastGroup) {
                                        lastGroup = all[i].group
                                        rows.push({header: true, label: lastGroup})
                                    }
                                    rows.push({header: false, palette: all[i]})
                                }
                                return rows
                            }
                            delegate: Item {
                                required property var modelData
                                required property int index
                                width: themeCol.width
                                // Group captions breathe: a blank line above each later
                                // group so the two catalogs read as separate lists.
                                height: modelData.header ? (index === 0 ? 16 : 26) : 17
                                // group caption, seated at the BOTTOM of its row so the
                                // extra height of a later group reads as a gap above it
                                Text {
                                    visible: modelData.header
                                    x: 5; anchors.bottom: parent.bottom; anchors.bottomMargin: 2
                                    font.family: dialog.noteFont; font.pixelSize: 8
                                    font.capitalization: Font.AllUppercase; font.letterSpacing: 0.8
                                    color: "#f0f0f0"
                                    text: modelData.header ? modelData.label : ""
                                }
                                Rectangle {
                                    visible: !modelData.header
                                    anchors.fill: parent; radius: 4
                                    color: rowArea.containsMouse ? "#8d8d8d"
                                         : (!modelData.header && modelData.palette.key === dialog.palette
                                            ? "#747474" : "transparent")
                                }
                                Text {
                                    visible: !modelData.header
                                    x: 5; anchors.verticalCenter: parent.verticalCenter
                                    width: 66; elide: Text.ElideRight
                                    font.family: dialog.noteFont; font.pixelSize: 10
                                    color: "#ffffff"
                                    text: modelData.header ? "" : modelData.palette.label
                                }
                                Row {
                                    visible: !modelData.header
                                    x: 76; anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Repeater {
                                        model: modelData.header ? [] : modelData.palette.colors
                                        delegate: Rectangle {
                                            required property string modelData
                                            width: 10; height: 10; radius: 5
                                            color: modelData
                                            border.width: 1; border.color: "#38000000"
                                        }
                                    }
                                }
                                MouseArea {
                                    id: rowArea
                                    visible: !modelData.header
                                    anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        dialog.choosePalette(modelData.palette.key)
                                        themeList.open = false
                                    }
                                    Accessible.role: Accessible.Button
                                    Accessible.name: modelData.header ? "" : ("Theme " + modelData.palette.label)
                                }
                            }
                        }
                    }
                }
            }
            Flickable {
                id: swatchScroll
                x: 6; y: swatchPop.headerHeight + (themeList.open ? themeList.height + 6 : 0)
                width: swatchPop.width-12
                height: swatchPop.height - y - 6
                contentHeight: swatchFlow.implicitHeight + 2*swatchPop.ringInset; clip: true
                boundsBehavior: Flickable.StopAtBounds
                Flow {
                    id: swatchFlow
                    x: swatchPop.ringInset; y: swatchPop.ringInset
                    width: swatchScroll.width - 2*swatchPop.ringInset; spacing: swatchPop.dotGap
                    // Ink mode leads with Auto, so restoring the derived ink is one press.
                    Rectangle {
                        id: autoDot; objectName: "swatch-auto"
                        visible: swatchPop.inkMode
                        readonly property bool selected: dialog.inkModes[dialog.selectedId]==="auto"
                        width: swatchPop.dotSize; height: swatchPop.dotSize; radius: width/2
                        // An empty library has no selected note, so the ink lookup is
                        // undefined and unassignable to a colour; the card's ink stands in.
                        color: dialog.inkOf(dialog.selectedId) !== undefined
                               ? dialog.inkOf(dialog.selectedId) : dialog.ink
                        border.width: 1; border.color: dialog.neutralWhite
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: "Automatic ink" + (selected ? " · selected" : "")
                        Accessible.onPressAction: dialog.chooseInk("auto")
                        Keys.onPressed: function(event) {
                            if(event.key===Qt.Key_Space || event.key===Qt.Key_Return || event.key===Qt.Key_Enter) { dialog.chooseInk("auto"); event.accepted=true }
                            else if(event.key===Qt.Key_Right || event.key===Qt.Key_Down || event.key===Qt.Key_Tab) { dialog.moveSwatchFocus(1); event.accepted=true }
                            else if(event.key===Qt.Key_Left || event.key===Qt.Key_Up || event.key===Qt.Key_Backtab) { dialog.moveSwatchFocus(-1); event.accepted=true }
                        }
                        // No letter: the chip is simply the first dot, painted in the ink
                        // Auto would derive. Selection is the same centre mark as any dot.
                        Rectangle { objectName: "swatch-mark"
                                    anchors.centerIn: parent; visible: autoDot.selected
                                    width: 7; height: 7; radius: width/2; color: dialog.paperColor }
                        Rectangle { objectName: "swatch-focus"
                                    readonly property int dashCount: 3
                                    readonly property color cueColor: dialog.neutralWhite
                                    anchors.centerIn: parent; width: parent.width+8; height: parent.height+8
                                    color: "transparent"; border.width: 0
                                    visible: autoDot.activeFocus && dialog.swatchKeyboardCue
                                    Row { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                                          spacing: 3
                                          Repeater { model: 3; delegate: Rectangle { width: 4; height: 2; radius: 1; color: dialog.neutralWhite } } } }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onPressed: dialog.swatchKeyboardCue=false
                                    onClicked: dialog.chooseInk("auto") }
                    }
                    Repeater {
                        id: swatchRepeater
                        // Ink mode leads with the NEUTRALS — white, light grey, dark grey,
                        // black — in every palette: most text wants a neutral, and no theme
                        // should take white or black away. Paper mode stays as published.
                        model: swatchPop.inkMode
                            ? [{paper:"#ffffff",ink:"#000000",group:"neutral",role:"#ffffff",label:"White"},
                               {paper:"#bfbfbf",ink:"#000000",group:"neutral",role:"#bfbfbf",label:"Light grey"},
                               {paper:"#595959",ink:"#ffffff",group:"neutral",role:"#595959",label:"Dark grey"},
                               {paper:"#000000",ink:"#ffffff",group:"neutral",role:"#000000",label:"Black"}
                              ].concat(dialog.activePalette.swatches)
                            : dialog.activePalette.swatches
                        delegate: Rectangle {
                            id: swatchDot
                            required property var modelData
                            // A literal string compare against the note's stored value. When
                            // that value is not in this theme nothing is marked, which is
                            // the honest answer rather than a nearest-match guess.
                            readonly property string current: swatchPop.inkMode
                                ? String(dialog.inkStored[dialog.selectedId])
                                : String(dialog.papers[dialog.selectedId])
                            readonly property bool selected: current===String(modelData.paper)
                            objectName: "swatch-dot"
                            width: swatchPop.dotSize; height: swatchPop.dotSize; radius: width/2
                            color: modelData.paper
                            // ONE ring rule for every dot: a single white hairline. Not a
                            // per-swatch derived tone, not a thicker ring when selected, and
                            // nothing black stamped over a chosen colour. Selection is said
                            // once, by the centre dot below.
                            border.width: 1
                            border.color: dialog.neutralWhite
                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: (swatchPop.inkMode ? "Note ink " : "Note paper ")
                                + modelData.label + " " + modelData.paper + (selected ? " · selected" : "")
                            Accessible.onPressAction: dialog.chooseSwatch(swatchDot.modelData.paper)
                            Keys.onPressed: function(event) {
                                if(event.key===Qt.Key_Space || event.key===Qt.Key_Return || event.key===Qt.Key_Enter) {
                                    dialog.chooseSwatch(swatchDot.modelData.paper); event.accepted=true
                                }
                                else if(event.key===Qt.Key_Right || event.key===Qt.Key_Down || event.key===Qt.Key_Tab) { dialog.moveSwatchFocus(1); event.accepted=true }
                                else if(event.key===Qt.Key_Left || event.key===Qt.Key_Up || event.key===Qt.Key_Backtab) { dialog.moveSwatchFocus(-1); event.accepted=true }
                            }
                            // Selection is a centre DOT and nothing else — no halo, no second
                            // ring, no checkmark — so at rest the row reads as colours
                            // rather than as decoration.
                            Rectangle {
                                objectName: "swatch-mark"
                                anchors.centerIn: parent; visible: swatchDot.selected
                                width: 7; height: 7; radius: width/2
                                color: swatchDot.modelData.ink
                            }
                            // KEYBOARD cue, and deliberately NOT a ring: a second circle is
                            // near-indistinguishable from the hairline the dot already
                            // carries, so a stale programmatic focus reads as "this swatch is
                            // special" instead of "the keyboard is here". Three short dashes
                            // UNDER the dot, inside the overhang ringInset already reserves,
                            // so they are never clipped and never mistaken for the border.
                            Rectangle {
                                objectName: "swatch-focus"
                                readonly property int dashCount: 3
                                readonly property color cueColor: dialog.neutralWhite
                                anchors.centerIn: parent; width: parent.width+8; height: parent.height+8
                                color: "transparent"; border.width: 0
                                visible: swatchDot.activeFocus && dialog.swatchKeyboardCue
                                Row {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.bottom: parent.bottom; spacing: 3
                                    Repeater { model: 3; delegate: Rectangle { width: 4; height: 2; radius: 1; color: dialog.neutralWhite } }
                                }
                            }
                            MouseArea {
                                id: dotArea
                                anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                // A pointer press is never a keyboard action, so it clears the
                                // cue: hovering or clicking can no longer latch one on.
                                onPressed: dialog.swatchKeyboardCue=false
                                onClicked: dialog.chooseSwatch(swatchDot.modelData.paper)
                            }
                        }
                    }
                    // Shown only when the current value is absent from this theme, so an
                    // unmatched assignment stays visible rather than unrepresented.
                    Rectangle {
                        id: currentDot; objectName: "swatch-current"
                        visible: swatchPop.inkMode
                            ? (dialog.inkModes[dialog.selectedId]==="explicit" && !dialog.manifest.inkInPalette[dialog.selectedId])
                            : !dialog.paperInPalette(dialog.selectedId)
                        width: swatchPop.dotSize; height: swatchPop.dotSize; radius: width/2
                        color: swatchPop.inkMode ? dialog.inkStored[dialog.selectedId] : dialog.paperColor
                        border.width: 1; border.color: dialog.neutralWhite
                        Accessible.role: Accessible.StaticText
                        Accessible.name: "Current " + (swatchPop.inkMode ? "ink " : "paper ")
                            + (swatchPop.inkMode ? dialog.inkStored[dialog.selectedId] : dialog.papers[dialog.selectedId])
                            + " · not in " + dialog.activePalette.label
                        Text { anchors.centerIn: parent; text: "●"; font.pixelSize: 8
                               color: swatchPop.inkMode ? dialog.paperColor : dialog.ink }
                    }
                }
            }
        }
        // Compact, READ-ONLY File details for the selected note.
        //
        // Every fact comes from the native filestore for a STABLE note ID — the store's own
        // anchored directory descriptor — not from the editor's buffer length, so "23 bytes on
        // disk" stays 23 while the buffer holds something longer and the mismatch is stated
        // rather than hidden. No folder action, no recovery verb, no path entry and no new
        // filesystem privilege: symlinks are refused and the path is composed from the store's
        // canonical root.
        Rectangle {
            id: infoPanel; objectName: "file-info"
            visible: dialog.showInfo && dialog.expanded; z:400
            x:58; y:surface.cardTop+50; width:Math.min(360, dialog.cardWidth-84)
            height: infoColumn.implicitHeight + 20; radius:8
            color: Qt.lighter(dialog.paperColor,1.04); border.width:1; border.color: Qt.darker(dialog.paperColor,1.3)
            Accessible.role: Accessible.Grouping
            Accessible.name: "File details for " + dialog.titles[dialog.selectedId]
            Column {
                id: infoColumn
                x:10; y:10; width: parent.width-20; spacing:3
                Text { text:"File details"; font.family:dialog.noteFont; font.pixelSize:11
                       font.weight:Font.DemiBold; color:dialog.ink }
                Repeater {
                    model: dialog.infoRows
                    delegate: Text {
                        required property var modelData
                        objectName: "file-info-row"
                        width: infoColumn.width; elide: Text.ElideMiddle
                        font.family:dialog.noteFont; font.pixelSize:10; color:dialog.ink
                        text: modelData.label + ": " + modelData.value
                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.label + ", " + modelData.value
                    }
                }
            }
        }

        /** Note icon — destination chooser, then KDE's own picker.
         *
         * An inline grid can only ever show a render-capped slice of a theme holding tens of
         * thousands of names, and `KIconThemes.IconDialog` already provides search, category
         * filters and the whole theme. This panel asks the one question the platform dialog
         * cannot — where the icon should GO. */
        Rectangle {
            id: iconPanel; objectName: "icon-panel"
            visible: dialog.iconPanelOpen && dialog.expanded; z:400
            x:58; y:surface.cardTop+50; width:Math.min(300, dialog.cardWidth-84)
            height: iconPanelColumn.implicitHeight + 20; radius:8
            color: Qt.lighter(dialog.paperColor,1.04); border.width:1; border.color: Qt.darker(dialog.paperColor,1.3)
            Accessible.role: Accessible.Grouping
            Accessible.name: "Note icon"
            Column {
                id: iconPanelColumn
                x:12; y:10; width: parent.width-24; spacing:8
                Text {
                    text: "Note icon"
                    font.family:dialog.noteFont; font.pixelSize:11
                    font.weight:Font.DemiBold; color:dialog.ink
                }
                Text {
                    width: iconPanelColumn.width
                    text: "Put the icon on:"
                    wrapMode: Text.WordWrap
                    font.family:dialog.noteFont; font.pixelSize:10
                    color: dialog.derivedTone(dialog.paperColor, 0.45)
                }
                Row {
                    spacing: 14
                    Repeater {
                        model: [{key:"tab", label:"Tab"}, {key:"note", label:"Into note"},
                                {key:"inline", label:"Wrap text"}]
                        /** An Item root, not a Row: a Row delegate cannot host the
                         *  anchors.fill MouseArea a checkbox needs (positioner children
                         *  refuse fill anchors and the whole Row stops functioning). */
                        delegate: Item {
                            required property var modelData
                            readonly property bool on: modelData.key === "tab" ? dialog.iconToTab
                                : modelData.key === "note" ? dialog.iconToNote : dialog.iconInline
                            // "Wrap text" only means something when the icon goes INTO the note.
                            opacity: modelData.key === "inline" && !dialog.iconToNote ? 0.45 : 1.0
                            width: box.width + 6 + tag.implicitWidth
                            height: 20
                            Rectangle {
                                id: box
                                width: 15; height: 15; radius: 3
                                y: 2
                                color: on ? dialog.ink : "transparent"
                                border.width: 1
                                border.color: on ? dialog.ink : dialog.derivedTone(dialog.paperColor, 0.35)
                                Text {
                                    anchors.centerIn: parent; visible: on
                                    text: "\u2713"; font.pixelSize: 11; color: dialog.paperColor
                                }
                            }
                            Text {
                                id: tag
                                x: box.width + 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                font.family: dialog.noteFont; font.pixelSize: 11; color: dialog.ink
                            }
                            Accessible.role: Accessible.CheckBox
                            Accessible.name: modelData.label + (on ? ", on" : ", off")
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: dialog.setIconDestination(modelData.key)
                            }
                        }
                    }
                }
                Item { width: 1; height: 2 }
                /** The primary action: hand off to the platform. */
                Rectangle {
                    objectName: "icon-choose"
                    width: iconPanelColumn.width; height: 30; radius: 5
                    color: chooseHover.hovered ? dialog.derivedTone(dialog.paperColor, 0.16)
                                               : dialog.derivedTone(dialog.paperColor, 0.09)
                    border.width: 1; border.color: dialog.derivedTone(dialog.paperColor, 0.3)
                    Text {
                        anchors.centerIn: parent
                        text: "Choose icon\u2026"
                        font.family: dialog.noteFont; font.pixelSize: 11
                        font.weight: Font.DemiBold; color: dialog.ink
                    }
                    HoverHandler { id: chooseHover }
                    Accessible.role: Accessible.Button
                    Accessible.name: "Choose an icon"
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: kdeIconDialog.open()
                    }
                }
                /** Clearing is a separate, quieter verb than choosing. */
                Text {
                    objectName: "icon-clear"
                    visible: dialog.iconOf(dialog.selectedId) !== ""
                    text: "Remove this note\u2019s tab icon"
                    font.family:dialog.noteFont; font.pixelSize:10
                    font.underline: clearHover.hovered
                    color: dialog.derivedTone(dialog.paperColor, 0.5)
                    HoverHandler { id: clearHover }
                    Accessible.role: Accessible.Button
                    Accessible.name: "Remove this note's tab icon"
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: dialog.setNoteIcon("") }
                }
                KIconThemes.IconDialog {
                    id: kdeIconDialog
                    onIconNameChanged: function (iconName) {
                        if (iconName === "") return   // cancelled
                        dialog.applyIcon({relative: "theme:" + iconName, name: iconName})
                        dialog.iconPanelOpen = false
                    }
                }
            }
        }
        // The compact edge target. It spans the lane the deck occupies when SPREAD, so its own
        // geometry never changes as the deck opens under the pointer: entering it cannot move
        // the target out from under the cursor, and the gaps that appear between sticks while
        // they spread are still inside it. It sits BELOW every stick and accepts Qt.NoButton,
        // so a stick keeps its own clicks and drags and this target opens the fan and nothing
        // else — never the editor, never focus.
        //
        // It also spans the reserved lift strip: a lifted face reaches inward past its own hit
        // target, so a pointer on the visible part of a stick would otherwise be over nothing
        // and the deck would fall closed under it. Expanded, the strip stops at the editor
        // rectangle's right edge, so it steals no hover from the view.
        MouseArea {
            id: fanTrigger
            objectName: "fan-trigger"
            x: surface.width-dialog.fanTabWidth-dialog.fanLiftReserve
            // From the very top when auto-hide is on: the corner reveal mark then sits INSIDE
            // this strip, so revealing rides the same containsMouse -> fanPointerInside ->
            // hysteresis chain every fan hover uses and no second hover path can disagree.
            // Otherwise the strip spans from the TOP-most stick down to the window's bottom
            // edge, "+" included.
            y: dialog.appearance.fanAutoHide === true ? 0
               : Math.max(0, dialog.fanBaseY - (dialog.order.length-1)*dialog.fanSpreadPitch - dialog.fanVisibleTop)
            width: dialog.fanTabWidth+dialog.fanLiftReserve
            height: surface.height - y
            z: 50
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            Accessible.role: Accessible.Grouping
            Accessible.name: "Note deck; hover the edge to spread the notes"
        }
        // The Library: browse the whole folder tree, including Archive, and put an archived
        // note back. Built from the same in-card panel idiom as File details above. Rows
        // come from the engine's LibraryModel, a read-only projection of the catalog.
        Rectangle {
            id: libraryPanel; objectName: "library-panel"
            // Also shown when the FAN is empty but the library is not. `dialog.expanded`
            // means "a note is open in the card", which can never be true once every note
            // is archived or pinned — and that is exactly the state with no way out.
            visible: dialog.libraryOpen && (dialog.expanded || dialog.order.length === 0); z: 401
            // The row list is the whole point of the panel, and a 300 px ceiling shows ~11
            // rows while the card below sits empty. It takes the room it needs and stops at
            // the footer.
            x: 58; y: surface.cardTop+50
            width: Math.min(460, dialog.cardWidth-84)
            /** SHRINK TO CONTENT, then cap at the footer. Sized from the MODEL, not from the
             *  ListView's contentHeight: the list's height is derived from this panel, so
             *  reading contentHeight here is circular and Qt resolves it to the fallback — a
             *  clipped final row and a scrollbar over four notes. */
            readonly property int libraryRowPitch: 32   // 30 px row + 2 px spacing
            readonly property int libraryChrome: 96     // header + subtitle + rule + margins
            // The footer cap is honoured only when the footer has a REAL position: during
            // layout its translated y can transiently make the cap negative, and a naive
            // Math.min then pins the panel to its 140 px floor permanently.
            readonly property int libraryCap:
                LayoutContract.libraryPanelCap(paper.y, footer.y, y, 140, 12)
            height: Math.max(140, Math.min(libraryModel.count * libraryRowPitch + libraryChrome,
                                           libraryCap))
            radius: 8
            color: Qt.lighter(dialog.paperColor,1.04)
            border.width: 1; border.color: Qt.darker(dialog.paperColor,1.3)
            Accessible.role: Accessible.Grouping
            Accessible.name: "Library · every note in this folder, including Archive"

            // ---- Header ------------------------------------------------------------
            // Title and subtitle, not one cramped line: the folder is the thing the user
            // recognises, so it leads at a readable size and the count is a quiet second line
            // rather than another clause fighting for the same 11 px.
            /** The library name is also the ROOT ROW: the tree draws no row for the root
             *  itself, so without this there is no gesture that returns the fan to the
             *  top-level notes. The Principal required one explicitly. */
            Text {
                id: libraryTitle
                objectName: "library-root-row"
                x: 14; y: 12; width: parent.width - 52; elide: Text.ElideMiddle
                font.family: dialog.noteFont; font.pixelSize: 14; font.weight: Font.DemiBold
                color: dialog.ink
                opacity: libraryRootArea.containsMouse ? 1.0 : 0.92
                text: libraryRoot ? String(libraryRoot).split("/").pop() : "No folder"
                Accessible.role: Accessible.Button
                Accessible.name: dialog.openFolder === ""
                    ? (libraryTitle.text + "; the fan is showing this folder")
                    : (libraryTitle.text + "; open it to return the fan to the top-level notes")
                Rectangle {
                    // Marker in the same ink language the rows use, so "you are here"
                    // reads the same whether the scope is the root or a subfolder.
                    visible: dialog.openFolder === ""
                    x: -8; anchors.verticalCenter: parent.verticalCenter
                    width: 2; height: 14; radius: 1
                    color: dialog.ink; opacity: 0.55
                }
                MouseArea {
                    id: libraryRootArea
                    anchors.fill: parent; anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { dialog.scopeToFolder(""); dialog.libraryOpen = false }
                }
            }
            Text {
                id: librarySubtitle
                x: 14; y: libraryTitle.y + libraryTitle.height + 1
                width: parent.width - 52; elide: Text.ElideRight
                font.family: dialog.noteFont; font.pixelSize: 10
                color: dialog.derivedTone(dialog.paperColor, 0.5)
                // The panel lists the WHOLE library; the fan is one folder of it. Say
                // which folder that is, because the tree alone cannot.
                text: libraryModel.count + (libraryModel.count === 1 ? " note" : " notes")
                      + " · fan: " + dialog.openFolderLabel
            }
            // A panel someone can be FORCED to use needs its own way out: with an empty
            // fan this panel is the entire interface.
            QuietButton {
                objectName: "library-close"
                x: parent.width - 34; y: 8
                size: 26; iconSize: 12
                glyph: "window-close"; ink: dialog.ink
                explanation: "Close the Library · Esc"
                onClicked: dialog.libraryOpen = false
            }
            // Hairline under the header, in the note's own spine tone — the panel idiom
            // every other surface here uses.
            Rectangle {
                id: libraryRule
                x: 14; y: librarySubtitle.y + librarySubtitle.height + 9
                width: parent.width - 28; height: 1
                color: dialog.derivedTone(dialog.paperColor, 0.18)
            }

            // ---- Rows --------------------------------------------------------------
            ListView {
                id: libraryList
                objectName: "library-list"
                x: 8; y: libraryRule.y + 8
                width: parent.width - 16; height: parent.height - y - 10
                clip: true
                model: libraryModel
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: libraryList.contentHeight > libraryList.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    width: 6
                    contentItem: Rectangle {
                        radius: 3
                        color: dialog.derivedTone(dialog.paperColor, 0.32)
                    }
                }
                delegate: Item {
                    id: libraryRow
                    required property int index
                    required property string name
                    // LibraryModel's root-relative path is the payload of the folder-scoping
                    // gesture. Qt only materialises model roles declared as required properties;
                    // omitting this made libraryRow.path undefined and every folder click resolve
                    // to the root.
                    required property string path
                    required property int depth
                    required property bool isFolder
                    required property bool archived
                    required property string documentId
                    required property bool expanded
                    required property int noteCount
                    required property bool current
                    /** True while THIS row is the one a restore is armed for. Armed state
                     *  lives on the dialog, not the delegate: a ListView recycles delegates,
                     *  so row-held state follows the wrong note once the list scrolls. */
                    readonly property bool armed: libraryRow.archived
                        && libraryRow.documentId !== ""
                        && dialog.confirmRestoreId === libraryRow.documentId
                    width: libraryList.width; height: 30
                    Rectangle {
                        anchors.fill: parent
                        anchors.rightMargin: 4
                        radius: 5
                        color: libraryRowArea.containsMouse ? dialog.derivedTone(dialog.paperColor, 0.12)
                               : libraryRow.current ? dialog.derivedTone(dialog.paperColor, 0.07)
                                                    : "transparent"
                    }
                    /** The note open in the card carries a small ink marker, the same
                     *  language the selected fan stick uses; without it the Library cannot
                     *  answer "which of these am I looking at". */
                    Rectangle {
                        objectName: "library-row-marker"
                        // The note open in the card, OR the folder the fan is currently a
                        // window onto. Without the second case the Library cannot answer
                        // "which of these folders am I looking at".
                        visible: libraryRow.isFolder
                            ? (!libraryRow.archived && libraryRow.path === dialog.openFolder)
                            : libraryRow.current
                        x: 2; anchors.verticalCenter: parent.verticalCenter
                        width: 2; height: 16; radius: 1
                        color: dialog.ink; opacity: 0.55
                    }
                    /** A real icon per row, not a "▸" in the label text. A folder states
                     *  its own openness; a note wears its assigned tab icon, so the Library
                     *  and the deck agree about what a note looks like.
                     *
                     *  On a FOLDER the icon is also the expand/collapse control. Clicking
                     *  the row now OPENS the folder (scopes the fan to it), so collapsing
                     *  the subtree needs its own target rather than sharing one press with
                     *  a gesture that changes what the whole edge shows. */
                    Kirigami.Icon {
                        id: rowIcon
                        x: 10 + libraryRow.depth*14
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16; height: 16
                        smooth: true
                        // ABOVE libraryRowArea, which is declared later and anchors.fill
                        // the whole row: without this the twisty never sees a press.
                        z: 3
                        opacity: libraryRow.archived ? 0.55 : 0.9
                        source: libraryRow.isFolder
                            ? (libraryRow.expanded ? "folder-open" : "folder")
                            : (dialog.iconSourceFor(dialog.iconOf(libraryRow.documentId)) !== ""
                                ? dialog.iconSourceFor(dialog.iconOf(libraryRow.documentId))
                                : "text-markdown")
                        MouseArea {
                            objectName: "library-row-twisty"
                            anchors.fill: parent
                            anchors.margins: -4
                            enabled: libraryRow.isFolder
                            cursorShape: Qt.PointingHandCursor
                            onClicked: libraryModel.toggle(libraryRow.index)
                        }
                    }
                    Text {
                        id: rowLabel
                        x: rowIcon.x + 24
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - (archivedTag.visible ? archivedTag.width + 18 : 14)
                        elide: Text.ElideMiddle
                        // 12 px, not 10: this is a list of the user's own documents and has
                        // to be readable. Folders carry the weight and notes the plain face,
                        // so hierarchy comes from type rather than an arrow character.
                        font.family: dialog.noteFont; font.pixelSize: 12
                        font.weight: libraryRow.isFolder ? Font.DemiBold : Font.Normal
                        color: dialog.ink
                        opacity: libraryRow.archived ? 0.7 : 1.0
                        text: libraryRow.name
                    }
                    /** Archive membership is stated on the row rather than implied by the
                     *  folder it sits under: restoring is the whole reason to open this
                     *  panel. A quiet pill, not a sentence competing with the filename. */
                    /** A folder says how much is inside rather than leaving the row empty. */
                    Text {
                        id: folderCount
                        visible: libraryRow.isFolder && libraryRow.noteCount > 0
                        anchors.right: parent.right; anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        font.family: dialog.noteFont; font.pixelSize: 10
                        color: dialog.derivedTone(dialog.paperColor, 0.5)
                        text: libraryRow.noteCount
                    }
                    Rectangle {
                        id: archivedTag
                        objectName: "library-archived-tag"
                        visible: libraryRow.archived && !libraryRow.isFolder
                        anchors.right: parent.right; anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: archivedText.implicitWidth + 12; height: 17; radius: 8
                        // Armed rows carry the ink tint the other confirm states use, so
                        // "press again and I will move your file" is visible rather than
                        // implied. Matching the footer's arm idiom deliberately.
                        color: libraryRow.armed
                            ? Qt.rgba(dialog.ink.r, dialog.ink.g, dialog.ink.b, 0.22)
                            : dialog.derivedTone(dialog.paperColor, 0.14)
                        Text {
                            id: archivedText
                            anchors.centerIn: parent
                            font.family: dialog.noteFont; font.pixelSize: 9
                            color: dialog.ink; opacity: libraryRow.armed ? 1.0 : 0.75
                            text: libraryRow.armed ? "Restore?" : "Archived"
                        }
                    }
                    LibraryRowAction {
                        id: libraryRowArea
                        anchors.fill: parent
                        folder: libraryRow.isFolder
                        archived: libraryRow.archived
                        folderPath: libraryRow.path
                        documentId: libraryRow.documentId
                        onFolderRequested: function(folderPath) {
                            // Opening a folder IS the scoping gesture: the fan becomes that
                            // folder's notes. Expanding the subtree is the folder ICON's job
                            // (rowIcon above), so one press never does both.
                            libraryModel.revealFolder(folderPath)
                            dialog.scopeToFolder(folderPath)
                            dialog.libraryOpen = false
                        }
                        onDocumentRequested: function(documentId) {
                            dialog.openFromLibrary(documentId)
                        }
                    }
                    Accessible.role: Accessible.Button
                    Accessible.name: libraryRow.isFolder
                        ? (libraryRow.archived
                            ? ("Archive folder " + libraryRow.name + "; archived notes are never on the fan")
                            : ("Folder " + libraryRow.name
                               + (libraryRow.path === dialog.openFolder
                                    ? "; open on the fan now"
                                    : "; open it to show its notes on the fan")))
                        : (libraryRow.archived
                            ? (libraryRow.armed
                                ? ("Archived note " + libraryRow.name + "; press again to restore it to its original folder and open it")
                                : ("Archived note " + libraryRow.name + "; restore and open, press twice"))
                            : ("Note " + libraryRow.name + "; open"))
                }
            }
        }
        // Upright, flat, overlapping sticks. Reorder moves identity, never content.
        Repeater {
            id: tabRepeater
            model: dialog.order
            delegate: Item {
                id: tab
                required property int index
                required property string modelData
                property string noteId: modelData
                property bool current: dialog.expanded && dialog.selectedId===noteId
                property bool hovered: dialog.hoveredNoteId===noteId
                property bool dragging: dialog.draggingNoteId===noteId
                property real offset: 0
                property real hitHeight: hit.height
                property Item hitArea: hit
                property Item faceItem: face
                // A note that has just LEFT the fan (pinned, archived, trashed) is gone from
                // the manifest while this delegate is still being torn down, so every lookup
                // below must tolerate an id the manifest no longer knows. Without the fallbacks
                // Qt logs "Unable to assign [undefined] to QString/QColor".
                property string accessibleName: dialog.titles[noteId] !== undefined
                    ? dialog.titles[noteId] : ""
                property bool labelElided: label.truncated
                /** The painted label itself, so a test can read the font that is actually
                 * RENDERED (pixelSize, weight, bold, colour) instead of the settings mirror. */
                property Item labelItem: label
                /** Outward lift of the hovered or selected stick, toward the desktop.
                 * Applied to the FACE below and never to this item, so the hit target can
                 * not slide out from under the pointer that caused the lift. */
                property var lift: LayoutContract.stickLift("right", current, hovered)
                x: surface.width-dialog.fanTabWidth
                // -fanVisibleTop: geometry is defined against the full work-area surface,
                // but collapsed the WINDOW is only the occupied strip, so every child shifts
                // up by the amount the window was trimmed from the top.
                y: dialog.fanBaseY - index*dialog.fanPitch + (dragging ? offset : 0) - dialog.fanVisibleTop
                width: dialog.fanTabWidth; height: dialog.fanTabLength
                rotation: 0
                z: dragging ? 400 : LayoutContract.stickLayer(index,current,hovered)
                // Auto-hide: sticks fade rather than unload, so the deck's identity,
                // order and geometry survive a hide/reveal untouched.
                opacity: dialog.fanHiddenIdle ? 0 : 1
                Behavior on opacity {
                    enabled: dialog.fanSpreadDuration > 0
                    NumberAnimation { duration: dialog.fanSpreadDuration }
                }
                Behavior on y {
                    enabled: !tab.dragging && dialog.fanSpreadDuration > 0
                    NumberAnimation { duration: dialog.fanSpreadDuration; easing.type: Easing.OutCubic }
                }
                Rectangle {
                    id: face
                    x: tab.lift.x; y: tab.lift.y
                    width: parent.width; height: parent.height
                    radius: Math.min(9,dialog.appearance.radius)
                    // Subtle hover highlight: the same paper, lifted a little, so the stick
                    // reads as raised without becoming a different colour.
                    color: tab.hovered ? Qt.lighter(dialog.paperOf(tab.noteId),1.07) : dialog.paperOf(tab.noteId)
                    border.width: 1
                    // Selection is the retained outward lift plus a small tonal marker,
                    // never a heavy outline around the entire tab.
                    border.color: dialog.derivedTone(dialog.paperOf(tab.noteId), 0.20)
                    Rectangle {
                        visible: tab.current
                        x: 3; anchors.verticalCenter: parent.verticalCenter
                        width: 2; height: 18; radius: 1
                        color: dialog.derivedTone(dialog.paperOf(tab.noteId),0.55)
                    }
                    // A larger label is clipped by the stick it belongs to rather than painting
                    // over its neighbours; the stick itself keeps its unchanged size.
                    clip: true
                    Behavior on x {
                        enabled: dialog.fanLiftDuration > 0
                        NumberAnimation { duration: dialog.fanLiftDuration; easing.type: Easing.OutCubic }
                    }
                    Behavior on color {
                        enabled: dialog.fanLiftDuration > 0
                        ColorAnimation { duration: dialog.fanLiftDuration }
                    }
                    /** Pure measurement, no visual presence: gives `label`'s group-sizing the
                     *  title's true unclipped width at the live font settings, without the
                     *  circularity of reading the constrained label's own contentWidth. */
                    TextMetrics {
                        id: labelMetrics
                        font.family: dialog.noteFont; font.pixelSize: dialog.fanLabelSize
                        font.weight: dialog.fanLabelBold ? Font.DemiBold : Font.Normal
                        text: tab.accessibleName.toUpperCase()
                    }
                    Text {
                        id: label
                        /**
                         * ICON AND LABEL AS ONE CENTRED GROUP.
                         *
                         * One rule for the front stick and every covered one: `exposedRun` is
                         * the only difference between them (full tab length up front, the
                         * shingle-exposed sliver behind). The group — icon + gap + label run
                         * — centres in that run. As the title grows BOTH ends travel outward
                         * from the centre together until the group fills the run's usable
                         * width (run minus a margin at each end); from there the icon has
                         * reached its floor and only the label keeps growing, into elision.
                         * The label is rotated -90°, so its "…" reads as three dots stacked
                         * vertically.
                         *
                         * EVERY TAB USES THE SAME BASIS. Clamping `exposedRun` to `fanPitch`
                         * for covered tabs measures a SPACING constant unrelated to the tab's
                         * length or the title typed, so a covered tab then shows roughly four
                         * characters whatever its title. Occlusion is handled by the z-order
                         * shingle and `face`'s `clip: true`.
                         */
                        readonly property int exposedRun: dialog.fanTabLength
                        // One margin, kept at BOTH ends of the group's travel range, so a
                        // maxed-out group still breathes at the tab's rounded caps.
                        readonly property int margin: 8
                        readonly property int gap: 4
                        readonly property int usableRange: Math.max(0, exposedRun - 2*margin)
                        // The label's own natural, UNCLIPPED width at the current font, from a
                        // separate invisible TextMetrics rather than this Text item's
                        // contentWidth: once `width` below constrains a Text with elide set,
                        // contentWidth can settle to the CONSTRAINED size, which feeds back
                        // into this same formula and settles wherever it started. TextMetrics
                        // has no width of its own, so nothing feeds back.
                        readonly property real naturalW: labelMetrics.width
                        readonly property int reserve: tabIcon.visible ? (tabIcon.width + gap) : 0
                        readonly property real groupNatural: reserve + naturalW
                        /** The group grows from the centre until it fills the usable range,
                         *  then stops — icon AND label together. Letting the label grow past
                         *  this cap and relying on `face`'s `clip: true` trades managed
                         *  truncation for a hard edge-to-edge cut and loses the end margin. */
                        readonly property real groupActual: Math.min(groupNatural, usableRange)
                        readonly property real labelActualW: Math.max(0, groupActual - reserve)
                        // Label's far edge (away from "+") in local, pre-rotation y.
                        readonly property real groupStart: exposedRun/2 - groupActual/2
                        anchors.centerIn: parent
                        width: labelActualW
                        visible: true
                        anchors.verticalCenterOffset: Math.round(groupStart + labelActualW/2
                                                                 - exposedRun/2)
                        rotation: -90
                        text: tab.accessibleName.toUpperCase(); elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter; color: dialog.labelInkOf(tab.noteId)
                        font.family: dialog.noteFont; font.pixelSize: dialog.fanLabelSize
                        // DemiBold, not Bold: it is the weight this deck already painted, and
                        // the one Qt already reports as bold. Off means Normal.
                        font.weight: dialog.fanLabelBold ? Font.DemiBold : Font.Normal
                    }
                    /** The note's own icon — part of the centred icon+label group above; see
                     *  `label`'s block for the shared geometry it reads from.
                     *
                     *  Kirigami.Icon, not Image: it takes BOTH a system icon-theme name and a
                     *  file URL (the user's own Assets/icons/ file), so one control serves both
                     *  sources and a theme change repaints the built-ins for free.
                     *
                     *  24 px, fixed regardless of title length: 16 px is too small to recognise
                     *  at a glance. The icon's POSITION moves with the title; its size does
                     *  not. */
                    Kirigami.Icon {
                        id: tabIcon
                        source: dialog.iconSourceFor(dialog.iconOf(tab.noteId))
                        /** Room-based: does the icon fit inside the tab with its own margin?
                         *  All tabs share the same basis (`exposedRun` is `fanTabLength`
                         *  everywhere), so there is no front-tab exemption to carry. */
                        visible: source != "" && label.usableRange >= tabIcon.height + label.gap
                        /** Position derived from the SAME group math as the label — the icon
                         *  sits at the group's near-"+" end: `groupStart + labelActualW + gap`
                         *  is its far edge, plus half its own size centres it. Deliberately
                         *  not `anchors.bottom: parent.bottom`, which measures from the STICK's
                         *  full-length bottom — the BURIED end whenever the exposed sliver is
                         *  shorter than the full tab. */
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: Math.round(label.groupStart + label.labelActualW
                                                                 + label.gap + height/2 - label.exposedRun/2)
                        /** Icon artwork differs in internal padding: the preferred theme leaves
                         *  a 2 px margin inside a 24 px box, while fallback artwork (a picked
                         *  name the preferred theme has no file for) draws edge-to-edge, which
                         *  is why one tab's padding looks wrong beside its neighbour's. A
                         *  full-bleed theme icon renders 4 px smaller so both PAINT the same
                         *  extent; the user's own Assets files stay full size. */
                        readonly property bool fullBleed: {
                            const src = String(source)
                            return src.length > 0 && src.indexOf("Papirus") < 0
                                   && src.indexOf("/Assets/") < 0
                        }
                        width: Math.min(24, dialog.fanTabWidth - 16) - (fullBleed ? 4 : 0)
                        height: width
                        smooth: true
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
                MouseArea {
                    id: hit
                    width: dialog.fanTabWidth
                    height: LayoutContract.stickHitLength(dialog.fanTabLength,dialog.fanPitch,tab.index,dialog.order.length)
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    property real pressY: 0
                    // Mouse coordinates are local to this (moving) MouseArea, so a naive
                    // delta chases its own motion once dragging starts. Map each sample into
                    // the stable surface frame before differencing.
                    onPressed: function(mouse) { hit.pressY=hit.mapToItem(surface,0,mouse.y).y; tab.offset=0; dialog.draggingNoteId="" }
                    onPositionChanged: function(mouse) {
                        if(!hit.pressed) return
                        var current = hit.mapToItem(surface,0,mouse.y).y
                        var delta = current - hit.pressY
                        if(Math.abs(delta)>5) dialog.draggingNoteId=tab.noteId
                        if(tab.dragging) tab.offset=delta
                    }
                    onReleased: {
                        var dragged = tab.dragging
                        var slot = Math.round(tab.index - tab.offset/dialog.fanPitch)
                        dialog.draggingNoteId=""; tab.offset=0
                        if(dragged) dialog.moveNote(tab.noteId,slot)
                        else dialog.openNoteId(tab.noteId)
                    }
                    onCanceled: { dialog.draggingNoteId=""; tab.offset=0 }
                    // Identity-scoped hover: Qt delivers enter on the stick moved ONTO before
                    // leave on the stick moved OFF, so an unconditional clear on leave would
                    // erase the highlight the new stick has already claimed.
                    onEntered: dialog.hoveredNoteId = tab.noteId
                    onExited: if(dialog.hoveredNoteId===tab.noteId) dialog.hoveredNoteId = ""
                    Accessible.role: Accessible.Button
                    Accessible.name: tab.accessibleName
                }
            }
        }
        // First-run / welcome state: fixed dark chrome (the Settings tones), the application
        // icon, instructions and theme-consistent buttons rather than default buttons on note
        // paper. Shown whenever the FAN is empty, which is now THREE distinct situations and
        // must not read as one:
        //
        //   libraryIsEmpty  — genuine first run. The full welcome: what the app is, how
        //                     each part is reached, "Create the first note".
        //   folderIsEmpty   — an empty FOLDER inside a library that has notes. A light
        //                     panel: say which folder, offer creation here and the way back
        //                     to the whole library. Reciting the first-run tour over a
        //                     library of 60 notes reads as data loss.
        //   neither         — this folder's notes are all archived or pinned.
        //
        // Creation stays reachable in all three, and it creates into the OPEN folder.
        Rectangle {
            id: emptyState
            objectName: "empty-state"
            visible: dialog.order.length === 0 && !dialog.expanded
            y: surface.cardTop
            width: dialog.cardWidth; height: dialog.cardHeight
            radius: dialog.appearance.radius
            color: dialog.neutralBg
            border.width: 1
            border.color: dialog.neutralOutline
            z: 120
            /** One themed button rule for this panel: raised charcoal, accent on hover. */
            component WelcomeButton: Rectangle {
                id: wb
                property string label: ""
                property bool prominent: false
                signal activated()
                width: Math.max(140, wbText.implicitWidth + 32); height: 34; radius: 6
                color: wbArea.containsMouse ? Qt.lighter(dialog.neutralBg, 2.0)
                                            : (wb.prominent ? Qt.lighter(dialog.neutralBg, 1.7)
                                                            : Qt.lighter(dialog.neutralBg, 1.35))
                border.width: 1
                border.color: wbArea.containsMouse ? dialog.neutralAccent : dialog.neutralOutline
                Accessible.role: Accessible.Button
                Accessible.name: wb.label
                Text {
                    id: wbText
                    anchors.centerIn: parent
                    font.family: dialog.neutralFont; font.pixelSize: 12
                    font.weight: wb.prominent ? Font.DemiBold : Font.Normal
                    color: dialog.neutralText
                    text: wb.label
                }
                MouseArea {
                    id: wbArea
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: wb.activated()
                }
            }
            Column {
                x: 28; y: 24; width: parent.width - 56; spacing: 10
                Row {
                    spacing: 10
                    Image {
                        width: 34; height: 34
                        source: appIconSource && appIconSource !== "" ? appIconSource : ""
                        visible: String(source) !== ""
                        sourceSize.width: 68; sourceSize.height: 68
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        font.family: dialog.neutralFont; font.pixelSize: 18; font.weight: Font.DemiBold
                        color: dialog.neutralText
                        text: "Fan Fold"
                    }
                }
                Text {
                    objectName: "empty-state-headline"
                    width: parent.width; wrapMode: Text.Wrap
                    font.family: dialog.neutralFont; font.pixelSize: 13
                    color: dialog.neutralText
                    text: dialog.libraryIsEmpty
                        ? "This folder has no notes yet."
                        : (dialog.folderIsEmpty
                            ? ("\u201C" + dialog.openFolderLabel + "\u201D has no notes yet.")
                            : ("Nothing from \u201C" + dialog.openFolderLabel + "\u201D is on the fan right now."))
                }
                Text {
                    objectName: "empty-state-folder"
                    width: parent.width; wrapMode: Text.Wrap; elide: Text.ElideMiddle
                    font.family: dialog.neutralFont; font.pixelSize: 10
                    color: dialog.neutralText; opacity: 0.7
                    // The library root, plus the open subfolder when the fan is scoped
                    // into one: "no notes here" is only answerable if you can see where
                    // "here" is.
                    text: shellControl.rootPath
                        ? ("Notes folder:  " + shellControl.rootPath
                           + (dialog.openFolder === "" ? "" : ("/" + dialog.openFolder)))
                        : "No folder has been chosen yet."
                }
                // What the application IS and how each part is reached, not one sentence
                // about files.
                Text {
                    width: parent.width; wrapMode: Text.Wrap
                    font.family: dialog.neutralFont; font.pixelSize: 11; color: dialog.neutralText
                    opacity: 0.85
                    lineHeight: 1.25
                    text: dialog.libraryIsEmpty
                        ? "Your notes live as ordinary Markdown files in the folder above — nothing is locked away.\n"
                          + "• Each note is a coloured tab on the fan at the screen edge; hover the edge to spread them.\n"
                          + "• The + above the fan creates a note; the footer inside a note holds colours, Library, pin, archive.\n"
                          + "• The tray icon gives New note, Show the fan and Quit at any time."
                        : (dialog.folderIsEmpty
                            ? "The fan shows one folder at a time. Your other notes are safe in the Library — open a folder there to fan it.\n"
                              + "The + creates a note in this folder."
                            : "This folder's notes are still there — archived, or open in pinned windows.\n"
                              + "Open the Library to bring one back, or create a new note here.")
                }
                Row {
                    spacing: 8
                    WelcomeButton {
                        objectName: "empty-state-create"
                        prominent: true
                        label: dialog.libraryIsEmpty ? "Create the first note" : "Create a note"
                        onActivated: dialog.createNoteOnFan(true)
                    }
                    WelcomeButton {
                        objectName: "empty-state-library"
                        visible: !dialog.libraryIsEmpty
                        label: "Open the Library"
                        onActivated: dialog.toggleLibrary()
                    }
                    WelcomeButton {
                        objectName: "empty-state-root"
                        // One press back to the top-level notes, without hunting for the
                        // Library's root row. Pointless when already at the root.
                        visible: !dialog.libraryIsEmpty && dialog.openFolder !== ""
                        label: "Back to all notes"
                        onActivated: dialog.scopeToFolder("")
                    }
                    WelcomeButton {
                        objectName: "empty-state-folder-btn"
                        label: shellControl.rootPath ? "Change folder…" : "Choose folder…"
                        onActivated: rootFolderDialog.open()
                    }
                }
                Text {
                    objectName: "empty-state-error"
                    // Startup failures AND action refusals. createNoteOnFan reports through
                    // saveStatus, which normally lives in the note footer — the footer this
                    // panel exists precisely because you cannot see. Repeating it here makes a
                    // failed "Create the first note" distinguishable from a dead button.
                    readonly property bool actionError: dialog.saveStatus !== "" && dialog.saveStatus !== "Saved" && dialog.saveStatus !== "Unsaved"
                    visible: startupError.length > 0 || actionError
                    width: parent.width; wrapMode: Text.Wrap
                    font.family: dialog.neutralFont; font.pixelSize: 10
                    color: dialog.danger !== undefined ? dialog.danger : "#dd8a7c"
                    text: startupError.length > 0 ? startupError : dialog.saveStatus
                }
            }
        }
        // New-note control on the fan lane, ABOVE the first stick. Below the last stick it
        // flees the cursor: the fan spreads DOWNWARD on hover, so approaching the button
        // spreads the deck over it. Above the top stick nothing covers it.
        //
        // The + is two strokes painted directly in ink rather than a themed icon:
        // Kirigami.Icon's isMask recolour silently fails on this control and renders the
        // theme's own grey, washing the glyph out against the note's paper.
        QuietButton {
            objectName: "fan-new-note"
            x: surface.width - dialog.fanTabWidth
            // Anchored to slot 0's VISUAL bottom edge: fanBaseY + fanTabLength, where the stick
            // actually paints. fanSlotY(0) adds fanHitOffset — a HIT-target shift, not a visual
            // one, and one that follows the live pitch (7 collapsed, up to 20 spread) — so the
            // "+" would sit a variable 19-32 px below the tab and move on hover. fanBaseY is a
            // constant, so this anchor cannot move.
            //
            // 6 px, not 10: the button's internal padding already contributes ~7 px of visual
            // air before the glyph, so a 10 px margin renders as 17.
            y: dialog.fanBaseY + dialog.fanTabLength + 6 - dialog.fanVisibleTop
            visible: dialog.order.length > 0 && !dialog.fanHiddenIdle
            // iconSize 0: with no glyph, QuietButton's contentItem otherwise paints its
            // empty-swatch circle and the + reads as ⊕.
            size: 30; iconSize: 0
            ink: dialog.libraryIsEmpty ? dialog.neutralText : dialog.ink
            explanation: "New note · Ctrl+N"
            z: 60
            Rectangle { anchors.centerIn: parent; width: 14; height: 2; radius: 1
                        color: dialog.libraryIsEmpty ? dialog.neutralText : dialog.ink }
            Rectangle { anchors.centerIn: parent; width: 2; height: 14; radius: 1
                        color: dialog.libraryIsEmpty ? dialog.neutralText : dialog.ink }
            onClicked: dialog.createNoteOnFan(true)
        }
        // No corner landmark while hidden. A reveal glyph collides with the fan's "+" and
        // duplicates what the edge already does: the trigger strip spans the lane, so
        // hovering the screen edge reveals the fan, and the tray icon toggles it.
    }

    // There is no tooltip window and no in-card tip layer. The deck communicates through the
    // sticks themselves — the hovered one lifts and lightens, the selected one keeps a smaller
    // permanent lift — and every control keeps its Accessible.name. A work-area or screen
    // change must still re-dock the fan rather than leave it under a panel.
    property Connections workAreaWatch: Connections {
        target: screenGeometry
        function onChanged() { alignment.restart() }
    }

    /** The FOLDER is the source of truth in both directions: a .md file created, renamed or
     *  deleted outside this application reaches the deck live instead of waiting for a restart.
     *
     *  The open note is preserved BY IDENTITY, never by index — a reconcile renumbers the
     *  order, and re-selecting by index is what makes a moved note adopt its neighbour's
     *  colour. A note whose file has gone away collapses the card rather than leaving a stale
     *  buffer over an absent file. */
    property Connections libraryWatch: Connections {
        target: notesStore
        function onChanged() {
            var openId = dialog.selectedId
            dialog.applyManifest(notesStore.load())
            if(dialog.expanded && openId !== "") {
                var at = dialog.order.indexOf(openId)
                if(at >= 0) dialog.selected = at
                else dialog.collapse()
            }
        }
    }

    /** System-tray verbs, each running the SAME path its in-app equivalent uses. */
    property Connections trayWatch: Connections {
        target: tray
        function onShowRequested() {
            // The tray icon is a TOGGLE of the fan, the same gesture as edge-hover, and nothing
            // more. No requestActivate on this path: a tray click carries no xdg-activation
            // token on Wayland, and asking for focus without one makes KWin flag
            // demands-attention. Auto-opening a card here also leaves the shell expanded, so
            // tab clicks visibly do nothing.
            if(dialog.expanded) {
                dialog.collapse()
            } else if(dialog.visible && dialog.fanHoverOpen) {
                dialog.fanHoverOpen = false
            } else {
                dialog.visible = true
                dialog.fanHoverOpen = true
                fanHold.restart()
                alignment.restart()
            }
        }
        function onQuitRequested() { dialog.requestClose() }
    }

    /** The one folder chooser: welcome panel and Settings both open THIS dialog, and
     *  shellControl.openFolder is the only code that changes the library root. */
    property FolderDialog rootFolderDialog: FolderDialog {
        id: rootFolderDialog
        title: "Choose the notes folder"
        currentFolder: shellControl.rootPath ? "file://" + shellControl.rootPath : ""
        onAccepted: {
            var refusal = shellControl.openFolder(selectedFolder)
            if(refusal) { dialog.saveStatus = refusal; return }
            dialog.applyManifest(notesStore.load())
            dialog.saveStatus = ""
            alignment.restart()
        }
    }
    /** Named entry point so the web Settings page can raise the chooser too. */
    function chooseRootFolder() { rootFolderDialog.open() }

    /** Explicit title apply: the web layer re-checks dirty state before any rename. */
    function applyTitle() {
        if(!loader.item) return
        loader.item.runJavaScript("fan.renameActive(" + JSON.stringify(titleField.text) + ")")
    }
    /** @return one item rectangle in surface coordinates for layout assertions. */
    function rectOf(item) {
        var p = item.mapToItem(surface,0,0)
        return {x:p.x, y:p.y, w:item.width, h:item.height, visible:item.visible}
    }
    // Never discard dirty editors; always require an explicit decision. Fixed neutral charcoal
    // and softwhite, frameless like the card, because the question is chrome: it looks the same
    // over a cream note and a near-black one. Cancel is the focused default.
    property Window closePrompt: Window {
        id: closeWindow
        visible:dialog.closeRequested; transientParent:dialog; width:510; height:176
        // A frameless child, like the card, but it keeps its title for the window list and
        // the accessibility tree.
        flags: Qt.Dialog | Qt.FramelessWindowHint
        title:"Close Fan Fold?"; color:dialog.neutralBg
        onClosing: function(close) { if(!dialog.allowDiscard){close.accepted=false;dialog.closeRequested=false;interactiveLimit.restart()} }
        /** Cancel is the safe answer, so it is what Escape and the frameless window's
         *  absent close button both mean. Nothing is discarded on this path. */
        function cancel() { dialog.closeRequested=false; interactiveLimit.restart() }
        onVisibleChanged: if(visible) continueButton.forceActiveFocus()
        Rectangle {
            anchors.fill: parent; color: dialog.neutralBg
            border.width: 1; border.color: dialog.neutralOutline
            focus: true
            Keys.onEscapePressed: function(event) { closeWindow.cancel(); event.accepted=true }
            Text { objectName:"close-prompt-title"; x:18; y:14; width:474
                   font.family:dialog.neutralFont; font.pixelSize:14; font.bold:true; color:dialog.neutralText
                   text:"Close or continue?" }
            Text { objectName:"close-prompt-body"; x:18; y:40; width:474; wrapMode:Text.Wrap
                   font.family:dialog.neutralFont; font.pixelSize:12; color:dialog.neutralText
                   text:dialog.dirty ? "Unsaved edits exist. Closing discards them; saved .md files remain. Cancel to save each edited note first." : "Close Fan Fold? Your saved .md files remain on disk." }
            // The safe choice keeps the quiet, note-derived fill of every other control here.
            Rectangle {
                id: continueButton; objectName:"close-prompt-cancel"
                x:20; y:120; width:210; height:36; radius:6
                color: cancelArea.containsMouse ? Qt.lighter(dialog.neutralBg,1.9) : Qt.lighter(dialog.neutralBg,1.5)
                border.width:1; border.color: dialog.neutralOutline
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Continue · cancel close (Escape)"
                Accessible.onPressAction: closeWindow.cancel()
                Keys.onPressed: function(event) {
                    if(event.key===Qt.Key_Space || event.key===Qt.Key_Return || event.key===Qt.Key_Enter) { closeWindow.cancel(); event.accepted=true }
                }
                Text { anchors.centerIn:parent; font.family:dialog.neutralFont; font.pixelSize:12; color:dialog.neutralText
                       text:"Continue / cancel close" }
                Rectangle { anchors.centerIn:parent; width:parent.width+8; height:parent.height+8; radius:8
                            color:"transparent"; visible:continueButton.activeFocus
                            border.width:2; border.color:dialog.neutralAccent }
                MouseArea { id: cancelArea; anchors.fill:parent; hoverEnabled:true; cursorShape:Qt.PointingHandCursor
                            onClicked: closeWindow.cancel() }
            }
            // The destructive choice is never the focused default, and never hidden either:
            // it carries the heaviest edge in the panel and says exactly what it discards.
            Rectangle {
                id: discardButton; objectName:"close-prompt-discard"
                x:250; y:120; width:240; height:36; radius:6
                color: discardArea.containsMouse ? Qt.lighter(dialog.neutralBg,2.4) : Qt.lighter(dialog.neutralBg,2.0)
                border.width:2; border.color: dialog.neutralOutline
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: "Close and discard unsaved edits"
                Accessible.onPressAction: { dialog.allowDiscard=true; Qt.quit() }
                Keys.onPressed: function(event) {
                    if(event.key===Qt.Key_Space || event.key===Qt.Key_Return || event.key===Qt.Key_Enter) { dialog.allowDiscard=true; Qt.quit(); event.accepted=true }
                }
                Text { anchors.centerIn:parent; font.family:dialog.neutralFont; font.pixelSize:12; font.bold:true
                       color:dialog.neutralText; text:"Close (discard unsaved edits)" }
                Rectangle { anchors.centerIn:parent; width:parent.width+8; height:parent.height+8; radius:8
                            color:"transparent"; visible:discardButton.activeFocus
                            border.width:2; border.color:dialog.neutralAccent }
                MouseArea { id: discardArea; anchors.fill:parent; hoverEnabled:true; cursorShape:Qt.PointingHandCursor
                            onClicked: { dialog.allowDiscard=true; Qt.quit() } }
            }
        }
    }
    property QtObject notesBridge: QtObject {
        id: bridge
        WebChannel.id: "notes"
        property int selected: dialog.selected
        property bool expanded: dialog.expanded
        property bool closePending: dialog.closeRequested
        property var ids: dialog.ids
        property var order: dialog.order
        property var titles: dialog.titles
        property var paper: dialog.papers
        property var ink: dialog.inks
        property string palette: dialog.palette
        property var palettes: dialog.palettes
        function collapse() { dialog.collapse() }
        /** Raise the native folder chooser (Settings' "Change notes folder…" button). */
        function chooseFolder() { dialog.chooseRootFolder() }
        /** The current library root, for display in Settings. */
        function libraryPath() { return shellControl.rootPath }
        /** Copy a dropped/pasted/recorded file into Assets/ and return its relative path. */
        function importAsset(name, base64, kind) { return shellControl.importAsset(name, base64, kind || "") }
        /** The user's own SVG icons from Assets/icons/, for the tab-icon picker. */
        function availableIcons() { return shellControl.availableIcons() }
        /** Installed applications by their own launcher icons. */
        function appIcons() { return shellControl.appIcons() }
        /** Search the active icon theme. */
        function searchIcons(q, limit) { return shellControl.searchIcons(q, limit) }
        /** Size of the searchable icon corpus. */
        function iconCount() { return shellControl.iconCount() }
        /** Name of the theme supplying the icons. */
        function iconThemeName() { return shellControl.iconThemeName() }
        /** Resolve a theme icon name to a concrete file. */
        function resolveThemeIcon(n) { return shellControl.resolveThemeIcon(n) }
        /** The staged application icon as a file URL, for the Settings header. */
        function appIcon() { return String(appIconSource) }
        /** The packaged revision, for Settings → About. */
        function appVersion() { return shellControl.appVersion() }
        function loadAppearance() { return appearanceStore.load() }
        function saveAppearance(value) { return appearanceStore.save(value) }
        function resetAppearance() { return appearanceStore.reset() }
        function previewAppearance(value) { dialog.appearance=appearanceStore.preview(value); alignment.restart() }
        function loadNote(id) { return store.load(id) }
        function probeNote(id) { return store.probe(id) }
        function saveNote(id,text,revision) { return store.save(id,text,revision) }
        /** Hand every keystroke to the engine without forcing a commit.
         *
         * This is the autosave seam: the engine debounces on a 250 ms quiet period and journals
         * the dirty buffer for crash recovery. A bridge with no way to report an unsaved edit
         * loses the buffer on a forced termination. */
        function noteEdited(id,text) { return store.updateContent(id,text) }
        function manifest() { return notesStore.load() }
        function renameNote(id,title,revision) {
            var result = store.rename(id,title,revision)
            if(result.ok) {
                dialog.applyManifest(notesStore.load())
                // Rename preserves the stable ID and clean flag, so neither change signal
                // fires and an open File details panel must re-read the file explicitly.
                dialog.refreshInfo()
            }
            return result
        }
        // The scoped colour API repaints the resident editor itself, whoever calls it, so a
        // caller can never leave the rendered CSS behind the manifest. Appearance only —
        // no text, history or caret.
        function setPaper(id,color) { var result=notesStore.setPaper(id,color); if(result.ok) { dialog.applyManifest(result); dialog.syncEditorColours() } return result }
        function setInk(id,value) { var result=notesStore.setInk(id,value); if(result.ok) { dialog.applyManifest(result); dialog.syncEditorColours() } return result }
        function setPalette(key) { var result=notesStore.setPalette(key); if(result.ok) dialog.applyManifest(result); return result }
        function setOrder(value) { var result=notesStore.setOrder(value); if(result.ok) dialog.applyManifest(result); return result }
        /** `dirty` is the AGGREGATE (close guard); `self` is the selected note's own
         *  buffer state, which is what the File details panel has to compare against
         *  the file. Older callers passing two arguments still work. */
        function status(text,dirty,self) { dialog.saveStatus=text; dialog.dirty=dirty; dialog.selectedDirty=(self===true) }
        /** Ask for a recording's NAME, then hand it back through the web channel's callback.
         *  Named deliberately, with no timestamp default: a voice note is worth finding by what
         *  it says. Cancelling returns an empty string and the clip is discarded. */
        /** The prompt's ANSWER, published as a property rather than returned.
         *
         *  WebChannel supplies the callback itself, so declaring one as a parameter makes the
         *  arity never match and the method is simply not found. A modal prompt cannot return
         *  synchronously, so the page starts it with `beginRecordingName()` and waits for
         *  `recordingName` to change: "" while open, the chosen name on save, "\u0000" on
         *  discard (an empty answer and a cancelled one must be distinguishable). */
        property string recordingName: ""
        function beginRecordingName() { recordingPrompt.open("Name this recording") }
        /** Read-only file facts from the native filestore, for a stable note ID only. */
        function noteInfo(id) { return store.info(id) }
        /** The families this machine's Qt/OS font stack actually reports, sorted and
         *  de-duplicated natively. The web layer never scans a directory or guesses. */
        function fontFamilies() { return fontCatalog.families() }
        function fontResolve(family) { return fontCatalog.resolveFamily(family) }
        function fontDescribe(family) { return fontCatalog.describe(family) }
        /** Boot handshake from the web layer: app.js calls `notes.ready()` at the end of
         *  its boot chain. Nothing native is done with it, but the method must stay
         *  resolvable — an unresolved WebChannel call would abort the rest of that chain. */
        function ready() {}
    }
    property WebChannel notesChannel: WebChannel { id: channel; registeredObjects: [bridge] }
    /** One ordinary window per pinned note. `pinnedIds` is the live register; the engine holds
     *  the durable `pinned` flag, so a restart reopens the same set (see restorePersistedPins).
     *
     *  Instantiator, not Repeater: a Repeater parents its delegates into a visual item and
     *  cannot create top-level Windows. */
    property Instantiator pinnedWindows: Instantiator {
        objectName: "pinned-windows"
        model: dialog.pinnedIds
        delegate: PinnedNoteWindow {
            required property string modelData
            // THIS note's own colours, by id, through the same adapter the web layer uses.
            // Reading dialog.papers/inks instead uses maps keyed by FAN membership, which a
            // pinned note has left, so the fallback paints header and footer in the SELECTED
            // note's colour over the pinned note's own body.
            readonly property var ownColours: notesStore.colourOf(modelData)
            documentId: modelData
            record: collection.documentObject(modelData)
            paper: ownColours && ownColours.paper ? ownColours.paper : "#f5f0e6"
            ink: ownColours && ownColours.ink ? ownColours.ink : "#1b1b1f"
            noteFont: dialog.noteFont
            iconSize: dialog.appearance.iconSize
            onCloseRequested: function(id) { dialog.unpinNote(id) }
        }
    }
    property Component editorFactory: Component {
        id: editorComponent
        WebEngineView {
            id: web
            backgroundColor: "transparent"
            function resume(index) { web.forceActiveFocus(); web.runJavaScript("window.fan && fan.select("+index+")") }
            function suspend() { web.runJavaScript("window.fan && fan.suspend()") }
            webChannel: channel
            profile: WebEngineProfile { offTheRecord: true; httpCacheType: WebEngineProfile.MemoryHttpCache }
            settings.localContentCanAccessRemoteUrls: false
            settings.localContentCanAccessFileUrls: true
            settings.localStorageEnabled: true
            settings.javascriptCanOpenWindows: false
            settings.pdfViewerEnabled: false
            url: Qt.resolvedUrl("index.html")
            onNavigationRequested: function(request) {
                const target = request.url.toString().split("?")[0]
                const allowed = target === Qt.resolvedUrl("index.html").toString() || (!request.isMainFrame && target === Qt.resolvedUrl("editor.html").toString())
                if(allowed) return
                // Everything else is a LINK followed inside a note. It never navigates this
                // view — the card is an editor, not a browser — but it is not silently
                // dropped either: every scheme is routed by followLink().
                request.reject()
                dialog.followLink(request.url.toString())
            }
            // Links inside a note render with target=_blank, so they arrive HERE rather than
            // at onNavigationRequested, where a note-to-note click only logs
            // BLOCK_NEW_WINDOW. Both entry points route through the same followLink().
            onNewWindowRequested: function(request) { dialog.followLink(request.requestedUrl.toString()) }
            /** The toolbar's Record button calls getUserMedia({audio}), and Chromium waits on
             *  the EMBEDDER's answer — there is no page-side prompt. Microphone capture from
             *  the app's own local editor page is granted; every other permission is denied
             *  explicitly so nothing waits forever on a question nobody will be shown. */
            onPermissionRequested: function(permission) {
                // Numeric on purpose: QWebEnginePermission::MediaAudioCapture == 1, and the
                // QML name lookup (WebEnginePermission.MediaAudioCapture) resolves to
                // undefined in this Qt, so grant() never runs and Record stays dead.
                if (permission.permissionType === 1)
                    permission.grant()
                else
                    permission.deny()
            }
            onJavaScriptConsoleMessage: function(level,message) { console.warn("Fan Fold [editor]:", message) }
        }
    }
}
