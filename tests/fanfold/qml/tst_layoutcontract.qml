pragma ComponentBehavior: Bound

import QtQuick
import QtTest
// The contract under test, staged beside this file by CMake so the suite always runs
// against src/shell/qml/LayoutContract.js itself rather than a copy that can drift.
import "LayoutContract.js" as LayoutContract

/*
 * Behavioural tests for the fan's stick geometry contract.
 *
 * `stickHitLength` decides how much of each stick answers the pointer. Getting it wrong
 * does not crash anything — it silently routes a click to the wrong note — so the tests
 * below do not assert returned numbers alone. They rebuild the delegate exactly as
 * Main.qml binds it and fire real mouse events through Qt's own hit-testing, because the
 * only question that matters is which note opens when the user clicks a stick he can see.
 *
 * Bindings mirrored 1:1 from src/shell/qml/Main.qml. Each must be re-checked if the
 * delegate changes:
 *   tab.y       = fanBaseY - index*fanPitch                   (:2000, drag offset omitted)
 *   tab.height  = fanTabLength                                (:2001)
 *   tab.z       = LayoutContract.stickLayer(index, cur, hov, count) (:2003)
 *   face.y      = stickLift(...).y, which is always 0         (:2017, LayoutContract:24)
 *   face.height = parent.height                               (:2018)
 *   hit         = no y and no anchors, so it sits at the tab's own origin (:2153-2156)
 *   hit.height  = stickHitLength(tabLength, pitch, i, count)  (:2156)
 *
 * The sticks stack UPWARD from fanBaseY: index 0 is the bottom-most, nearest the "+",
 * and higher indices sit above it. That direction is what makes the front stick's extra
 * length harmless — it runs downward into the empty "+" zone, away from the deck.
 */
Item {
    id: root
    width: 400; height: 1500

    // The Principal's real configuration at 62 notes: fanTabLength 114 and a derived
    // fanBaseY of 1150 on a 1440 px screen with a 44 px bottom panel.
    property int fanTabLength: 114
    property int fanTabWidth: 36
    property int fanBaseY: 1150
    property int fanPitch: 14
    property int count: 62

    // Index of the stick whose MouseArea accepted the most recent press, or -1 if the
    // click fell through every stick and reached nothing.
    property int lastClicked: -1

    Item {
        id: surface
        anchors.fill: parent

        Repeater {
            id: tabRepeater
            model: root.count
            delegate: Item {
                id: tab
                required property int index
                x: surface.width - root.fanTabWidth
                y: root.fanBaseY - index*root.fanPitch
                width: root.fanTabWidth
                height: root.fanTabLength
                z: LayoutContract.stickLayer(index, false, false, root.count)

                Rectangle {
                    id: face
                    x: 0; y: 0
                    width: parent.width; height: parent.height
                    color: "steelblue"
                    border.width: 1
                }

                MouseArea {
                    id: hit
                    width: root.fanTabWidth
                    height: LayoutContract.stickHitLength(root.fanTabLength, root.fanPitch,
                                                          tab.index, root.count)
                    onPressed: root.lastClicked = tab.index
                }
            }
        }
    }

    TestCase {
        id: testCase
        name: "LayoutContractHitLength"
        when: windowShown

        function init() {
            // Every test states the deck it needs; reset to the real 62-note compact deck
            // so a test that forgets one cannot inherit its predecessor's geometry.
            root.fanTabLength = 114
            root.fanBaseY = 1150
            root.fanPitch = 14
            root.count = 62
            root.lastClicked = -1
        }

        function test_library_panel_cap_uses_scene_coordinates() {
            compare(LayoutContract.libraryPanelCap(600, 500, 650, 140, 12), 438,
                    "the footer's paper-local y must be translated by paper.y before "
                    + "it is compared with the panel's surface-local y")
        }

        function test_layering_never_drops_long_decks_below_the_trigger() {
            var triggerLayer = 50
            for (var i = 0; i < 500; ++i)
                verify(LayoutContract.stickLayer(i, false, false, 500) > triggerLayer,
                       "stick " + i + " must remain above the fan trigger")
            verify(LayoutContract.stickLayer(499, true, false, 500)
                   > LayoutContract.stickLayer(0, false, false, 500),
                   "the current stick remains above the resting deck")
            verify(LayoutContract.stickLayer(499, false, true, 500)
                   > LayoutContract.stickLayer(499, true, false, 500),
                   "hover remains the top layer")
        }

        function test_scroll_range_is_zero_when_the_deck_fits() {
            compare(LayoutContract.fanScrollMaximum(10, 70, 114, 744), 0)
        }

        function test_scroll_range_preserves_fixed_pitch_for_large_folders() {
            compare(LayoutContract.fanScrollMaximum(70, 70, 114, 1232), 3712)
        }

        function test_ensure_far_and_near_sticks_are_wholly_visible() {
            var maximum = LayoutContract.fanScrollMaximum(70, 70, 114, 1232)
            compare(LayoutContract.fanOffsetForIndex(69, 0, maximum,
                                                     1194, 70, 114, 76, 1232), maximum,
                    "the farthest stick scrolls flush to the viewport's far edge")
            compare(LayoutContract.fanOffsetForIndex(0, maximum, maximum,
                                                     1194, 70, 114, 76, 1232), 0,
                    "opening the nearest stick returns it wholly to view")
        }

        /* The y coordinate a user aims at for stick i: the middle of the part of stick i
         * he can actually SEE.
         *
         * Stick i-1 paints on top of stick i (z descends with index, so lower index wins) and
         * its face begins one pitch below stick i's top edge. Stick i is therefore
         * visible only over [top, top+pitch). The front stick is the exception: nothing
         * paints over it, so its whole length is exposed. */
        function aimY(i) {
            var top = root.fanBaseY - i*root.fanPitch
            var exposedEnd = (i === 0) ? top + root.fanTabLength : top + root.fanPitch
            return Math.round((top + exposedEnd)/2)
        }

        function clickStick(i) {
            root.lastClicked = -1
            mouseClick(root, root.width - root.fanTabWidth/2, aimY(i))
            return root.lastClicked
        }

        // ---- The returned lengths -------------------------------------------------

        /* Case 1 — an uncompressed deck. Nothing overlaps anything, so every stick,
         * front and back alike, answers over its whole length. */
        function test_uncompressed_deck_gives_every_stick_its_full_length() {
            compare(LayoutContract.stickHitLength(110, 140, 0, 6), 110,
                    "front stick of an uncompressed deck takes its full tab length")
            compare(LayoutContract.stickHitLength(110, 140, 3, 6), 110,
                    "a covered stick whose pitch exceeds the tab length is not covered "
                    + "at all, so it also takes its full length")
        }

        /* Case 3 — the boundary. At pitch exactly equal to tabLength the sticks abut
         * without overlapping, so full length is still correct and Math.min is a no-op. */
        function test_pitch_equal_to_tab_length_is_still_full_length() {
            compare(LayoutContract.stickHitLength(110, 110, 0, 6), 110,
                    "front stick at pitch == tabLength keeps its full length")
            compare(LayoutContract.stickHitLength(110, 110, 4, 6), 110,
                    "a covered stick at pitch == tabLength abuts rather than overlaps")
        }

        /* Case 4 — the top of a compressed deck.
         *
         * The last stick is the one furthest from the "+", at the top of the fan. It is
         * NOT the uncovered one: z is 100-index, so the highest index has the LOWEST z
         * and is painted over by the stick one index below it, whose face begins one
         * pitch further down. The last stick is therefore covered like every other
         * covered stick and answers over exactly one pitch.
         *
         * The stick with nothing painted over it is index 0, at the bottom of the fan. */
        function test_last_stick_of_compressed_deck_is_covered_like_the_rest() {
            compare(LayoutContract.stickHitLength(114, 12, 61, 62), 12,
                    "the last stick is overlapped by its lower-indexed neighbour, which "
                    + "paints above it, so it answers over one pitch")
            compare(LayoutContract.stickHitLength(114, 12, 60, 62), 12,
                    "its neighbour is bounded the same way")
        }

        /* Case 5 — a single-stick deck, where index 0 is also the last index. */
        function test_single_stick_deck_keeps_full_length() {
            compare(LayoutContract.stickHitLength(110, 12, 0, 1), 110,
                    "the only stick in the deck is fully exposed")
        }

        /* Case 2 — a compressed deck. The covered sticks are bounded to one pitch; the
         * front stick is not, because nothing is painted over it. */
        function test_compressed_deck_bounds_covered_sticks_to_one_pitch() {
            compare(LayoutContract.stickHitLength(114, 12, 1, 62), 12,
                    "a covered stick answers over one pitch")
            compare(LayoutContract.stickHitLength(114, 12, 30, 62), 12,
                    "every covered stick answers over one pitch, wherever it sits")
            compare(LayoutContract.stickHitLength(114, 12, 0, 62), 114,
                    "the front stick has nothing painted over it and keeps its full "
                    + "length; its extra extent runs DOWNWARD into the empty \"+\" zone, "
                    + "away from the deck, so it covers no neighbour")
        }

        // ---- What those lengths DO: real clicks through real hit-testing -----------

        /* The contract that actually matters. Aim at the visible sliver of every stick in
         * the Principal's real 62-note deck and require that each one receives its own
         * click. */
        function test_every_visible_stick_receives_its_own_click_data() {
            return [
                { tag: "62 notes, resting/compact pitch", pitch: 14, n: 62 },
                { tag: "62 notes, spread/hover pitch",    pitch: 18, n: 62 },
                { tag: "10 notes, compact",               pitch: 14, n: 10 },
                { tag: "6 notes, uncompressed",           pitch: 70, n: 6  },
                { tag: "2 notes",                         pitch: 14, n: 2  },
                { tag: "1 note",                          pitch: 14, n: 1  },
            ]
        }

        function test_every_visible_stick_receives_its_own_click(data) {
            root.fanPitch = data.pitch
            root.count = data.n
            var misrouted = []
            for (var i = 0; i < root.count; ++i) {
                var got = clickStick(i)
                if (got !== i)
                    misrouted.push("stick " + i + " (y=" + aimY(i) + ") -> " + got)
            }
            compare(misrouted.length, 0,
                    data.tag + ": every visible stick must receive its own click. "
                    + "Misrouted " + misrouted.length + " of " + root.count + ": "
                    + misrouted.join("; "))
        }

        /* The specific failure the user reported — one note opening over and over while
         * he clicked different tabs — stated as its own contract so a regression names
         * the symptom rather than a number. */
        function test_front_stick_never_steals_a_neighbours_click() {
            root.fanPitch = 14
            root.count = 62
            var stolen = []
            for (var i = 1; i <= 12; ++i)
                if (clickStick(i) === 0) stolen.push(i)
            compare(stolen.length, 0,
                    "the front stick must not receive clicks aimed at the sticks behind "
                    + "it; it stole: [" + stolen + "]")
        }

        /* The front stick's own click is a contract too, and it is the one a naive
         * "bound index 0 to one pitch as well" fix breaks: the sliver it would leave
         * behind sits at the TOP of the stick, while the face the user sees and aims at
         * extends a further tabLength-pitch pixels downward into the "+" zone. */
        function test_front_stick_answers_over_the_face_the_user_sees() {
            root.fanPitch = 14
            root.count = 62
            var top = root.fanBaseY
            var probes = [top + 4, top + 30, top + 70, top + root.fanTabLength - 4]
            for (var k = 0; k < probes.length; ++k) {
                root.lastClicked = -1
                mouseClick(root, root.width - root.fanTabWidth/2, probes[k])
                compare(root.lastClicked, 0,
                        "the front stick paints from y=" + top + " to y="
                        + (top + root.fanTabLength) + " and must answer the pointer "
                        + "everywhere it paints; y=" + probes[k] + " reached "
                        + root.lastClicked)
            }
        }
    }
}
