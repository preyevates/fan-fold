pragma ComponentBehavior: Bound

import QtQuick
import QtTest
import "LayoutContract.js" as LayoutContract

/*
 * The dock must never be presented without a real screen, and never at a zero or negative
 * size.
 *
 * When the last output goes away Qt Wayland keeps the application alive on a placeholder
 * screen that reports a 0x0 geometry. Every dock extent in Main.qml is derived from the
 * screen, so the window collapses to a negative height, and the compositor answers a
 * non-positive window geometry with a protocol error that kills the connection. These tests
 * fix the two rules that keep the process alive: do not show on a screen with no area, and
 * clamp what is shown to at least one pixel.
 *
 * Bindings mirrored from src/shell/qml/Main.qml; re-check them if the dock changes:
 *   visible        = LayoutContract.dockCanShow(Qt.application.screens, Screen.width, Screen.height)
 *   surface.width  = LayoutContract.windowExtent(collapsed ? fanCollapsedWidth : cardWidth + 32)
 *   surface.height = LayoutContract.windowExtent(fanSurfaceH - fanVisibleTop)
 *   fanSurfaceH    = max(0, dockWorkH - 12), dockWorkH = max(0, availH - panelClearance)
 */
TestCase {
    id: test
    name: "NoScreen"

    // What Qt reports for the placeholder it creates when every output is gone.
    readonly property var placeholder: ({ width: 0, height: 0 })
    readonly property var monitor: ({ width: 3440, height: 1440 })

    function test_no_screens_at_all_cannot_show() {
        compare(LayoutContract.dockCanShow([], 3440, 1440), false)
        compare(LayoutContract.dockCanShow(undefined, 3440, 1440), false)
        compare(LayoutContract.dockCanShow(null, 3440, 1440), false)
    }

    function test_placeholder_screen_cannot_show() {
        compare(LayoutContract.dockCanShow([placeholder], 0, 0), false)
        // A window whose own screen is the placeholder stays hidden even when another
        // screen has already appeared in the list; its geometry would still be 0x0.
        compare(LayoutContract.dockCanShow([placeholder, monitor], 0, 0), false)
    }

    function test_real_screen_can_show() {
        compare(LayoutContract.dockCanShow([monitor], 3440, 1440), true)
    }

    function test_screen_list_alone_is_not_enough() {
        // The list can already hold the real screen while the window is still being
        // moved onto it; the window's own screen decides.
        compare(LayoutContract.dockCanShow([monitor], 0, 0), false)
        compare(LayoutContract.dockCanShow([monitor], 3440, 0), false)
    }

    function test_every_listed_screen_empty_cannot_show() {
        compare(LayoutContract.dockCanShow([placeholder, placeholder], 3440, 1440), false)
    }

    function test_malformed_extents_are_not_a_screen() {
        compare(LayoutContract.dockCanShow([{ width: -1, height: 1440 }], -1, 1440), false)
        compare(LayoutContract.dockCanShow([{ width: NaN, height: NaN }], NaN, NaN), false)
        compare(LayoutContract.dockCanShow([monitor], undefined, undefined), false)
    }

    function test_extent_is_never_below_one_pixel() {
        compare(LayoutContract.windowExtent(-1), 1)
        compare(LayoutContract.windowExtent(0), 1)
        compare(LayoutContract.windowExtent(-840), 1)
        compare(LayoutContract.windowExtent(NaN), 1)
        compare(LayoutContract.windowExtent(undefined), 1)
        compare(LayoutContract.windowExtent(Infinity), 1)
    }

    function test_extent_keeps_a_valid_size() {
        compare(LayoutContract.windowExtent(1), 1)
        compare(LayoutContract.windowExtent(52), 52)
        compare(LayoutContract.windowExtent(1368), 1368)
        compare(LayoutContract.windowExtent(52.4), 52)
    }

    // The failing case from the field: the collapsed strip on a 0x0 screen is 52 wide and
    // -1 high, exactly "52x-1". Rebuilt here with the same arithmetic Main.qml uses.
    function collapsedSurface(availH, panelClearance, fanVisibleTop) {
        var dockWorkH = Math.max(0, availH - panelClearance)
        var fanSurfaceH = Math.max(0, dockWorkH - 12)
        return {
            w: LayoutContract.windowExtent(52),
            h: LayoutContract.windowExtent(fanSurfaceH - fanVisibleTop)
        }
    }

    function test_collapsed_dock_on_a_zero_screen_is_positive() {
        // With availH 0 the unclamped height is 0 - fanVisibleTop; the mask keeps
        // fanVisibleTop at a positive value for a populated fan.
        var s = collapsedSurface(0, 60, 1)
        compare(s.w, 52)
        verify(s.h >= 1)
        var t = collapsedSurface(0, 0, 0)
        verify(t.h >= 1)
    }

    function test_collapsed_dock_on_the_real_screen_is_unchanged() {
        var s = collapsedSurface(1440, 60, 700)
        compare(s.h, 1440 - 60 - 12 - 700)
    }
}
