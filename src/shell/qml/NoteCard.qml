import QtQuick
import QtQuick.Controls

/** The note card: title spine, editable title, pin and close, the full footer, and the
 *  colour, icon and File details panels over it.
 *
 *  ONE implementation shown in two places — the fan's dock and a pinned note's own window.
 *  Everything here reads and acts through `host`, which answers for exactly one note; a
 *  pinned window's host names its own note, never the fan's selection, so a footer press
 *  in a pinned window can only ever reach the note in that window.
 *
 *  The editor itself belongs to the host (the fan's deck page or the pinned page) and is
 *  parented into `editorArea`, so it paints above the paper and below the panels. */
Item {
    id: card
    required property var host
    /** Recording prompt answers are published on this bridge's `recordingName`. */
    required property var bridge
    /** Where the panel layer lives. It must sit above everything the panels can overlap,
     *  including items that are siblings of the card rather than children. */
    property Item overlayParent: card
    property int overlayZ: 299
    /** Receives keyboard focus when a title edit is abandoned, so Escape reaches the
     *  host's own panel/collapse handling next. */
    property Item focusTarget: card
    /** The card's top edge in the coordinate space of `overlayParent`. */
    property int cardTop: 0

    property alias paperItem: paper
    property alias footerItem: footer
    property alias editorArea: editorArea
    property alias titleText: titleField.text
    property alias swatchPopup: swatchPop

    /** The one button-box rule, shared by BOTH command palettes.
     *
     * `iconSize + 12` is 6 px of padding on every side of the glyph; `actionGap` is the
     * row's spacing. The editor's format toolbar reads these same two numbers as CSS vars
     * (`--actionsize` / `--actiongap` in appearance.js), so Buttons → Size moves both
     * strips identically rather than only the glyph inside one of them. */
    readonly property real actionSize: card.host.appearance.iconSize + 12
    readonly property real actionGap: 2
    readonly property bool titleDirty: titleField.text !== titleField.committed

    function resetTitle() { titleField.reset() }

    /** Move swatch focus by `step` and say so with the keyboard cue. Order matches the
     *  painted Flow: the Auto chip first in ink mode, then every palette dot. */
    function moveSwatchFocus(step) {
        var chain = []
        if(swatchPop.autoDot.visible) chain.push(swatchPop.autoDot)
        for(var i=0;i<swatchPop.swatchRepeater.count;i++) { var d=swatchPop.swatchRepeater.itemAt(i); if(d) chain.push(d) }
        if(!chain.length) return
        var at = -1
        for(var j=0;j<chain.length;j++) if(chain[j].activeFocus) { at = j; break }
        var next = at < 0 ? 0 : (at + step + chain.length) % chain.length
        card.host.swatchKeyboardCue = true
        chain[next].forceActiveFocus(Qt.TabFocusReason)
    }
    /** The WebEngine child otherwise retains keyboard focus and consumes Escape before
     *  QML's ancestor key handler sees it. Focus lands on the swatch this note ALREADY has
     *  whenever the panel offers it, so the focus ring and selection mark start on the
     *  same dot; two lit dots read as two active choices. The scan covers the Auto chip as
     *  well as the palette dots, in the order the Flow paints them. */
    function focusCurrentSwatch() {
        var chain = []
        if(card.host.paletteMode === "ink") chain.push(swatchPop.autoDot)
        for(var i=0;i<swatchPop.swatchRepeater.count;i++) { var dot = swatchPop.swatchRepeater.itemAt(i); if(dot) chain.push(dot) }
        var target = null
        for(var j=0;j<chain.length;j++) if(chain[j].selected) { target = chain[j]; break }
        if(!target && chain.length) target = chain[0]
        if(target) target.forceActiveFocus(Qt.PopupFocusReason)
    }

    // A rename or a change of note re-seeds the field; nothing is renamed while typing.
    Connections {
        target: card.host
        function onCardTitleChanged() { titleField.reset() }
    }

    // The WebEngine rectangle stays inside the paper rounded perimeter at every bound.
    Rectangle {
        id: paper
        y: card.cardTop
        width: card.host.cardWidth; height: card.host.cardHeight
        radius: card.host.appearance.radius; color: card.host.paperColor; visible: card.host.expanded
        Item {
            id: titleSpine; width: 40; height: parent.height
            Accessible.role: Accessible.StaticText
            Accessible.name: "Note spine, left edge · " + card.host.cardTitle
            Rectangle { width: 80; height: parent.height; radius: paper.radius; color: Qt.darker(paper.color,1.14) }
        }
        Rectangle { x:40; width:40; height:parent.height; color:paper.color }
        Text { x:20-width/2; y:(parent.height-height)/2; width:parent.height-40; rotation:-90; horizontalAlignment:Text.AlignHCenter; elide:Text.ElideRight; text:card.host.cardTitle.toUpperCase(); color:card.host.ink; font.family:card.host.noteFont; font.pixelSize:10; font.weight:Font.DemiBold }
        Repeater { model:Math.max(0,Math.floor((paper.height-28)/11)); Rectangle { required property int index; x:40; y:14+index*11; width:2; height:6; radius:1; color:Qt.darker(paper.color,1.34) } }

        // Editable title with explicit apply and cancel: nothing is renamed while typing.
        TextField {
            id: titleField
            objectName: "note-title"
            property string committed: ""
            function reset() { committed = card.host.cardTitle; text = committed }
            x: 56; y: 10; height: 28
            width: Math.max(60, applyTitle.x - 64)
            color: card.host.ink
            font.family: card.host.noteFont; font.pixelSize: 15; font.weight: Font.DemiBold
            selectByMouse: true
            leftPadding: 4; rightPadding: 4; topPadding: 2; bottomPadding: 2
            Accessible.name: "Note title; apply or cancel explicitly"
            background: Rectangle { color: "transparent"; radius: 4; border.width: titleField.activeFocus ? 1 : 0; border.color: Qt.darker(card.host.paperColor,1.35) }
            onAccepted: card.host.applyTitle()
            Keys.onEscapePressed: { titleField.reset(); card.focusTarget.forceActiveFocus() }
            Component.onCompleted: reset()
        }
        QuietButton {
            id: applyTitle; objectName: "apply-title"
            x: cancelTitle.x - card.actionSize - 2; y: 10
            size: card.actionSize; iconSize: card.host.appearance.iconSize
            visible: card.titleDirty; enabled: visible
            glyph: "dialog-ok"; ink: card.host.ink
            explanation: "Rename file"
            onClicked: card.host.applyTitle()
        }
        QuietButton {
            id: cancelTitle; objectName: "cancel-title"
            x: pinButton.x - card.actionSize - 6; y: 10
            size: card.actionSize; iconSize: card.host.appearance.iconSize
            visible: card.titleDirty; enabled: visible
            glyph: "dialog-cancel"; ink: card.host.ink
            explanation: "Cancel title edit"
            onClicked: { titleField.reset(); card.focusTarget.forceActiveFocus() }
        }
        // Pin sits beside the close control: both answer "where does this note LIVE",
        // which is a different question from the footer's editing verbs.
        QuietButton {
            id: pinButton; objectName: "pin"
            x: paper.width - 22 - 12 - 30; y: 10
            size: 22; iconSize: 14
            glyph: card.host.selectedIsPinned ? "window-unpin" : "window-pin"
            ink: card.host.ink
            explanation: card.host.selectedIsPinned ? "Return this note to the fan" : "Pin this note to its own window"
            onClicked: card.host.togglePinSelected()
        }
        QuietButton {
            id: closeButton; objectName: "collapse"
            // Fixed 22 px: the retired closeSize setting drove nothing worth tuning,
            // and a stored legacy value is simply ignored.
            x: paper.width - 22 - 12; y: 10
            size: 22; iconSize: 14
            glyph: "window-close"; ink: card.host.ink
            explanation: card.host.closeExplanation
            onClicked: card.host.collapse()
        }

        // Every action and the status live in the footer; only title, pin and close are on top.
        Item {
            id: footer
            x: 50; y: paper.height - card.actionSize - 10
            width: paper.width - 62; height: card.actionSize
            Row {
                id: footerActions; spacing: card.actionGap
                // The two disclosures are mutually exclusive, so the card never stacks
                // two panels above the footer.
                QuietButton { objectName:"format"; size:card.actionSize; iconSize:card.host.appearance.iconSize; glyph:"format-text-bold"; ink:card.host.ink; explanation:"Formatting"; onClicked: card.host.toggleFormatting() }
                // Paper and Ink are SEPARATE controls onto the SAME compact panel: the
                // same button again dismisses it, the other switches its mode in place.
                QuietButton { id: colorButton; objectName:"swatch"; size:card.actionSize; iconSize:card.host.appearance.iconSize; swatch:card.host.paperColor; ink:card.host.ink; explanation:"Note paper"; onClicked: card.host.togglePalette("paper") }
                QuietButton { id: inkButton; objectName:"ink"; size:card.actionSize; iconSize:card.host.appearance.iconSize; swatch:card.host.ink; ink:card.host.ink; explanation:"Note ink"; onClicked: card.host.togglePalette("ink") }
                QuietButton { objectName:"reload"; size:card.actionSize; iconSize:card.host.appearance.iconSize; glyph:"view-refresh"; ink:card.host.ink; explanation:"Reload · saved notes only"; onClicked: card.host.reloadNote() }
                QuietButton { objectName:"library"; size:card.actionSize; iconSize:card.host.appearance.iconSize; glyph:"view-list-tree"; ink:card.host.ink; explanation:"Library · browse and restore from Archive"; onClicked: card.host.toggleLibrary() }
                QuietButton { id: iconButton; objectName:"icon"; size:card.actionSize; iconSize:card.host.appearance.iconSize; glyph:"preferences-desktop-emoticons-symbolic"; ink:card.host.ink; explanation:"Note icon · tab and/or note body"; onClicked: card.host.toggleIconPanel() }
                QuietButton { objectName:"settings"; size:card.actionSize; iconSize:card.host.appearance.iconSize; glyph:"configure"; ink:card.host.ink; explanation:"Settings"; onClicked: card.host.toggleSettings() }
                QuietButton { id: infoButton; objectName:"trial"; size:card.actionSize; iconSize:card.host.appearance.iconSize; glyph:"help-about"; ink:card.host.ink; explanation:"File details"; onClicked: card.host.toggleInfo() }
            }
            // ---- Filing group ------------------------------------------------------
            // Archive and Trash are the only two controls that move the user's file. The
            // protection is the interlock, not the layout: each ARMS on the first press
            // and acts only on the second (see confirmArchiveId / confirmTrashId).
            Row {
                id: filingActions
                objectName: "filing-actions"
                x: footerActions.width + card.actionGap
                spacing: card.actionGap
                QuietButton {
                    objectName: "archive"
                    readonly property bool armed: card.host.confirmArchiveId === card.host.selectedId && card.host.selectedId !== ""
                    size: card.actionSize; iconSize: card.host.appearance.iconSize
                    glyph: armed ? "dialog-ok" : "archive-insert"
                    ink: card.host.ink
                    explanation: armed ? "Confirm · move this note to Archive"
                                       : "Archive this note · press twice"
                    onClicked: { if(armed) card.host.archiveSelected(); else card.host.armArchive() }
                }
                QuietButton {
                    objectName: "trash"
                    readonly property bool armed: card.host.confirmTrashId === card.host.selectedId && card.host.selectedId !== ""
                    size: card.actionSize; iconSize: card.host.appearance.iconSize
                    glyph: armed ? "dialog-ok" : "user-trash"
                    ink: card.host.ink
                    explanation: armed ? "Confirm · move this note to the desktop Trash"
                                       : "Move this note to the desktop Trash · press twice"
                    onClicked: { if(armed) card.host.trashSelected(); else card.host.armTrash() }
                }
            }
            Text {
                id: statusText
                x: filingActions.x + filingActions.width + 10; width: Math.max(40, quitButton.x - x - 8)
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight; text: card.host.saveStatus; color: card.host.ink
                font.family: card.host.noteFont; font.pixelSize: 10
                // The status line can elide in a narrow card; the accessible name is
                // always the whole sentence.
                Accessible.role: Accessible.StaticText
                Accessible.name: card.host.saveStatus
            }
            QuietButton {
                id: quitButton; objectName: "quit"
                x: footer.width - card.actionSize; size: card.actionSize; iconSize: card.host.appearance.iconSize
                glyph: card.host.confirmQuit ? "dialog-ok" : "system-shutdown"; ink: card.host.ink
                explanation: card.host.confirmQuit ? "Confirm · quit Fan Fold"
                                                   : "Quit Fan Fold · press twice"
                onClicked: {
                    if(card.host.confirmQuit) card.host.requestClose()
                    else card.host.armQuit()
                }
            }
        }
    }
    Item {
        id: editorArea
        x: 42; y: card.cardTop+46
        width: card.host.cardWidth-58; height: card.host.cardHeight-46-card.actionSize-16
        visible: card.host.expanded
    }
    RecordingPrompt {
        id: recordingPrompt
        dialog: card.host
        surface: card
        bridge: card.bridge
    }
    function openRecordingPrompt(purpose) { recordingPrompt.open(purpose) }

    Item {
        id: overlay
        parent: card.overlayParent
        anchors.fill: parent
        z: card.overlayZ
        // Outside-click dismissal. It sits just under the panels and over everything
        // else, including the WebEngine view, because a press inside the web content is
        // consumed there and would never reach QML. The WHOLE footer strip is excluded:
        // excluding only the routine row silently swallows every press on Archive, Trash
        // and Quit.
        MouseArea {
            id: paletteDismiss; objectName: "palette-dismiss"
            anchors.fill: parent
            visible: (card.host.paletteOpen || card.host.showInfo || card.host.iconPanelOpen) && card.host.expanded; enabled: visible
            acceptedButtons: Qt.AllButtons
            function inside(item, mouse) {
                var p = mapToItem(item, mouse.x, mouse.y)
                return p.x>=0 && p.y>=0 && p.x<=item.width && p.y<=item.height
            }
            onPressed: function(mouse) {
                if(inside(footer, mouse)) {
                    // Every footer control keeps its own press. The three panel toggles
                    // keep their toggle semantics; everything else acts normally and ALSO
                    // dismisses, because acting while an obsolete panel hangs open reads
                    // as broken.
                    var onPanelButton = inside(colorButton, mouse) || inside(inkButton, mouse)
                                     || inside(infoButton, mouse) || inside(iconButton, mouse)
                    if(!onPanelButton) { card.host.paletteOpen=false; card.host.showInfo=false; card.host.iconPanelOpen=false }
                    mouse.accepted=false; return
                }
                card.host.paletteOpen=false; card.host.showInfo=false; card.host.iconPanelOpen=false; mouse.accepted=true
            }
        }
        // Compact circular swatches of the palette currently offering choices, immediately
        // above the footer. A swatch assigns its literal colour to THIS card's note only.
        SwatchPopup {
            id: swatchPop
            dialog: card.host
            surface: card
            paper: paper
            footer: footer
        }
        // Compact, READ-ONLY File details for this card's note.
        //
        // Every fact comes from the native filestore for a STABLE note ID — the store's own
        // anchored directory descriptor — not from the editor's buffer length, so "23 bytes
        // on disk" stays 23 while the buffer holds something longer and the mismatch is
        // stated rather than hidden. No folder action, no recovery verb, no path entry and
        // no new filesystem privilege.
        Rectangle {
            id: infoPanel; objectName: "file-info"
            visible: card.host.showInfo && card.host.expanded; z:400
            x:58; y:card.cardTop+50; width:Math.min(360, card.host.cardWidth-84)
            height: infoColumn.implicitHeight + 20; radius:8
            color: Qt.lighter(card.host.paperColor,1.04); border.width:1; border.color: Qt.darker(card.host.paperColor,1.3)
            Accessible.role: Accessible.Grouping
            Accessible.name: "File details for " + card.host.cardTitle
            Column {
                id: infoColumn
                x:10; y:10; width: parent.width-20; spacing:3
                Text { text:"File details"; font.family:card.host.noteFont; font.pixelSize:11
                       font.weight:Font.DemiBold; color:card.host.ink }
                Repeater {
                    model: card.host.infoRows
                    delegate: Text {
                        required property var modelData
                        objectName: "file-info-row"
                        width: infoColumn.width; elide: Text.ElideMiddle
                        font.family:card.host.noteFont; font.pixelSize:10; color:card.host.ink
                        text: modelData.label + ": " + modelData.value
                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.label + ", " + modelData.value
                    }
                }
            }
        }
        IconPanel {
            id: iconPanel
            dialog: card.host
            surface: card
        }
    }
}
