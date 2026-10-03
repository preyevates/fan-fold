pragma ComponentBehavior: Bound

import QtQuick
import QtTest
import "LayoutContract.js" as LayoutContract

/*
 * The bottom panel's strip belongs to the panel.
 *
 * On Wayland the work area a client reads back is the whole screen, so nothing tells the
 * dock where the task manager is. A dock window that reaches the screen's bottom edge sits on
 * top of the panel's tray and the compositor delivers the tray's clicks to the dock. These
 * tests rebuild the dock's vertical geometry exactly as Main.qml binds it, place a tray icon
 * in a bottom panel, and fire real clicks through Qt's hit-testing to see which one answers.
 *
 * Bindings mirrored from src/shell/qml/Main.qml; re-check them if the dock changes:
 *   panelClearance = LayoutContract.panelClearance(screenY, screenH, availY, availH, 60)
 *   dockWorkH      = max(0, availH - panelClearance)
 *   cardHeight     = min(appearance.height, dockWorkH - 12)
 *   fanBottomInset = LayoutContract.fanBottomInset(64, panelClearance)
 *   fanSurfaceH    = max(0, dockWorkH - 12)
 *   fanBaseY       = fanSurfaceH - fanBottomInset - fanPlusZone(56) - fanTabLength
 *   fanSearchTop   = max(42, fanBaseY - (n-1)*spreadPitch - 26 - 8)
 *   fanVisibleTop  = centred ? 0 : max(0, fanSearchTop - 4)
 *   window height  = fanSurfaceH - fanVisibleTop
 *   window y       = LayoutContract.dockWindowY(availY, dockWorkH, height, centred)
 *   "+" y          = fanBaseY + fanTabLength + 6 - fanVisibleTop   (QuietButton, size 30)
 *   card top       = max(0, round((fanSurfaceH - cardHeight)/2) - fanVisibleTop)
 * "centred" is expanded, Library open, or an empty fan; otherwise the window is the
 * collapsed strip. Hover spread and auto-hide never resize the window; with auto-hide the
 * trigger strip covers the window's full height, so the window rect is what eats clicks.
 */
Item {
    id: root
    width: 3440; height: 1440

    // The Principal's output and panel.
    property int screenW: 3440
    property int screenH: 1440
    property int panelHeight: 55
    property string lastClicked: ""

    // Today's (pre-reserve) "+" position: the window ran to the work area's bottom edge with
    // a 64 px inset. The reserve must not move the deck on screen.
    function legacyPlusScreenY(availY, availH, n, centred, tabLength, spreadPitch) {
        var surfaceH = Math.max(0, availH - 12)
        var baseY = surfaceH - 64 - 56 - tabLength
        var searchTop = Math.max(42, baseY - Math.max(0, n - 1)*spreadPitch - 26 - 8)
        var visibleTop = centred ? 0 : Math.max(0, searchTop - 4)
        var winH = surfaceH - visibleTop
        var winY = centred ? availY + Math.max(0, Math.round((availH - winH)/2))
                           : availY + Math.max(0, availH - winH)
        return winY + baseY + tabLength + 6 - visibleTop
    }

    function dock(screenY, screenH, availY, availH, n, centred, cardH, tabLength, spreadPitch) {
        var clearance = LayoutContract.panelClearance(screenY, screenH, availY, availH, 60)
        var workH = Math.max(0, availH - clearance)
        var cardHeight = Math.min(cardH, workH - 12)
        var inset = LayoutContract.fanBottomInset(64, clearance)
        var surfaceH = Math.max(0, workH - 12)
        var baseY = surfaceH - inset - 56 - tabLength
        var searchTop = Math.max(42, baseY - Math.max(0, n - 1)*spreadPitch - 26 - 8)
        var visibleTop = centred ? 0 : Math.max(0, searchTop - 4)
        var winH = surfaceH - visibleTop
        var winY = LayoutContract.dockWindowY(availY, workH, winH, centred)
        return {
            clearance: clearance,
            winY: winY, winH: winH, winBottom: winY + winH,
            plusY: winY + baseY + tabLength + 6 - visibleTop,
            cardTop: winY + Math.max(0, Math.round((surfaceH - cardHeight)/2) - visibleTop),
            cardBottom: winY + Math.max(0, Math.round((surfaceH - cardHeight)/2) - visibleTop)
                        + cardHeight
        }
    }

    // The panel along the screen's bottom edge with a tray icon in its right-hand corner,
    // under the dock's column.
    Rectangle {
        id: panel
        x: 0; y: root.screenH - root.panelHeight
        width: root.screenW; height: root.panelHeight
        color: "#333"
        MouseArea {
            id: tray; objectName: "tray-icon"
            x: parent.width - 44; y: Math.round((parent.height - height)/2)
            width: 40; height: 40
            onPressed: root.lastClicked = "tray"
        }
    }

    // The dock window. A window takes every press inside its rect, so one MouseArea over
    // the whole rect is the faithful model: the compositor routes by window, not by item.
    Rectangle {
        id: fanWindow; objectName: "fan-window"
        color: "transparent"
        z: 10
        MouseArea {
            anchors.fill: parent
            onPressed: root.lastClicked = "fan"
        }
    }

    TestCase {
        name: "PanelReserve"
        when: windowShown

        // Collapsed, spread on hover and auto-hidden share this window; expanded, Library
        // and the empty fan share the centred one.
        function test_tray_icon_receives_the_click_in_every_dock_state_data() {
            return [
                { tag: "collapsed, 62 notes", n: 62, centred: false },
                { tag: "collapsed, 3 notes", n: 3, centred: false },
                { tag: "spread on hover, 62 notes", n: 62, centred: false },
                { tag: "auto-hide, 62 notes", n: 62, centred: false },
                { tag: "expanded", n: 62, centred: true },
                { tag: "empty fan", n: 0, centred: true },
            ]
        }
        function test_tray_icon_receives_the_click_in_every_dock_state(data) {
            var d = root.dock(0, root.screenH, 0, root.screenH, data.n, data.centred,
                              900, 114, 14)
            fanWindow.x = root.screenW - (data.centred ? 932 : 52)
            fanWindow.y = d.winY
            fanWindow.width = data.centred ? 932 : 52
            fanWindow.height = d.winH
            // Sweep the panel row under the dock's column, not one hand-picked pixel.
            var misses = 0
            for (var px = fanWindow.x + 2; px < root.screenW; px += 6) {
                for (var py = panel.y + 1; py < root.screenH; py += 6) {
                    root.lastClicked = ""
                    mouseClick(root, px, py)
                    if (root.lastClicked === "fan") misses++
                }
            }
            compare(misses, 0, "the dock window must not take clicks inside the panel's strip")
            root.lastClicked = ""
            mouseClick(tray, tray.width/2, tray.height/2)
            compare(root.lastClicked, "tray", "the tray icon must receive its own click")
        }

        function test_collapsed_window_ends_sixty_px_above_the_screen_bottom() {
            var d = root.dock(0, 1440, 0, 1440, 62, false, 900, 114, 14)
            compare(d.clearance, 60)
            compare(d.winBottom, 1440 - 60)
        }

        function test_deck_and_plus_do_not_move_on_screen() {
            var heights = [900, 1080, 1440, 2160]
            var panels = [0, 30, 55, 60, 70]
            var counts = [0, 1, 3, 62, 200]
            var moved = 0
            for (var h = 0; h < heights.length; h++)
                for (var p = 0; p < panels.length; p++)
                    for (var c = 0; c < counts.length; c++)
                        for (var centred = 0; centred < 2; centred++) {
                            var availH = heights[h] - panels[p]
                            var d = root.dock(0, heights[h], 0, availH, counts[c],
                                              centred === 1, 900, 114, 14)
                            var was = root.legacyPlusScreenY(0, availH, counts[c],
                                                             centred === 1, 114, 14)
                            if (d.plusY !== was) moved++
                        }
            compare(moved, 0, "the \"+\" and the deck must keep their screen position")
            // The Principal's screen, collapsed: "+" top 1326, bottom 84 px above the edge.
            compare(root.dock(0, 1440, 0, 1440, 62, false, 900, 114, 14).plusY, 1326)
        }

        function test_expanded_card_stays_between_screen_top_and_reserve_line() {
            var heights = [720, 900, 1080, 1440, 2160]
            var cards = [400, 900, 2000]
            for (var h = 0; h < heights.length; h++)
                for (var c = 0; c < cards.length; c++) {
                    var d = root.dock(0, heights[h], 0, heights[h], 62, true,
                                      cards[c], 114, 14)
                    verify(d.winY >= 0, "window top inside the screen at " + heights[h])
                    verify(d.cardTop >= 0, "card top inside the screen at " + heights[h])
                    verify(d.winBottom <= heights[h] - 60,
                           "window clear of the panel at " + heights[h] + ": " + d.winBottom)
                    verify(d.cardBottom <= heights[h] - 60,
                           "card clear of the panel at " + heights[h] + ": " + d.cardBottom)
                }
        }

        function test_reported_panel_is_not_counted_twice() {
            // A compositor that already excludes a 70 px panel from the work area.
            var d = root.dock(0, 1440, 0, 1370, 62, false, 900, 114, 14)
            compare(d.clearance, 0)
            compare(d.winBottom, 1370)
            // One that excludes only 30 px gets the remaining 30.
            d = root.dock(0, 1440, 0, 1410, 62, false, 900, 114, 14)
            compare(d.clearance, 30)
            compare(d.winBottom, 1380)
            // A second screen below the first: measured against its own bottom edge.
            d = root.dock(1440, 1080, 1440, 1080, 62, false, 900, 114, 14)
            compare(d.winBottom, 1440 + 1080 - 60)
        }
    }
}
