pragma ComponentBehavior: Bound

import QtQuick
import QtTest
import "LayoutContract.js" as LayoutContract

// Mirror the wheel-to-offset-to-delegate path in Main.qml: fan geometry (:678,
// :685-689), scrollFanBy (:745-759), viewport/trigger (:1916-1935, :2320-2333),
// and tab/hit (:2363-2369, :2519-2552). Test Qt event delivery and actual motion,
// rather than only the pure range calculation.
Item {
    id: root
    width: 48; height: 1390
    property int count: 70
    property int fanTabWidth: 36
    property int fanTabLength: 114
    property int fanSpreadPitch: 70
    property int fanBaseY: 1194
    property int fanDeckViewportTop: 76
    property int fanDeckViewportHeight: 1232
    property real fanScrollOffset: 0
    readonly property real fanScrollMaximum: LayoutContract.fanScrollMaximum(
        count, fanSpreadPitch, fanTabLength, fanDeckViewportHeight)
    property int lastWheelTarget: -1

    function scrollFanBy(angleY, pixelY) {
        if (fanScrollMaximum <= 0) return
        var delta = LayoutContract.fanWheelDelta(angleY, pixelY, fanSpreadPitch)
        fanScrollOffset = Math.max(0, Math.min(fanScrollMaximum, fanScrollOffset + delta))
    }

    MouseArea {
        id: fanTrigger
        x: 0; y: 38; width: 48; height: root.height - y
        z: 50
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onWheel: function(wheel) {
            root.lastWheelTarget = -2
            root.scrollFanBy(wheel.angleDelta.y, wheel.pixelDelta.y)
            wheel.accepted = true
        }
    }
    Item {
        id: fanDeckViewport
        x: 0; y: 38; width: root.width; height: root.fanDeckViewportHeight
        clip: true; z: 100
        // Match Main.qml: scroll belongs to the stable viewport, not moving tabs.
        WheelHandler {
            target: null
            onWheel: function(event) {
                root.lastWheelTarget = -3
                root.scrollFanBy(event.angleDelta.y, event.pixelDelta.y)
                event.accepted = true
            }
        }
        Repeater {
            id: tabRepeater
            model: root.count
            delegate: Item {
                id: tab
                required property int index
                x: fanDeckViewport.width - root.fanTabWidth
                y: root.fanBaseY - index*root.fanSpreadPitch
                   + root.fanScrollOffset - root.fanDeckViewportTop
                width: root.fanTabWidth; height: root.fanTabLength
                z: LayoutContract.stickLayer(index, false, false, root.count)
                MouseArea {
                    width: root.fanTabWidth
                    height: LayoutContract.stickHitLength(root.fanTabLength,
                                                          root.fanSpreadPitch,
                                                          tab.index, root.count)
                    hoverEnabled: true
                    // No onWheel: the fixed viewport handles the event even if this
                    // moving stick exits the pointer between wheel notches.
                }
            }
        }
    }

    TestCase {
        name: "FanScrollWheel"
        when: windowShown
        function test_wheel_down_over_visible_stick_reveals_farther_notes() {
            root.fanScrollOffset = 0
            compare(root.fanScrollMaximum, 3712)
            compare(root.fanScrollOffset, 0)
            var stick = tabRepeater.itemAt(2)
            var before = stick.y
            // Negative angleDelta.y is an ordinary downward wheel notch.
            mouseWheel(root, 30, 1060, 0, -120)
            compare(root.lastWheelTarget, -3, "the fixed viewport must receive the wheel")
            verify(root.fanScrollOffset > 0,
                   "wheel down at the near end must reveal notes farther up the deck")
            verify(stick.y > before, "the deck must move down inside the viewport")
            mouseWheel(root, 30, 1060, 0, 120)
            compare(root.fanScrollOffset, 0,
                    "wheel up must return to the near end")
        }
        function test_repeated_wheel_notches_do_not_depend_on_a_moving_stick() {
            root.fanScrollOffset = 0
            for (var i = 0; i < 8; ++i) {
                var previous = root.fanScrollOffset
                mouseWheel(root, 30, 1060, 0, -120)
                verify(root.fanScrollOffset > previous,
                       "each notch must move the deck while tabs slide under the pointer")
            }
        }

        function test_trackpad_pixel_delta_uses_the_same_direction() {
            compare(LayoutContract.fanWheelDelta(0, -24, root.fanSpreadPitch), 24)
            compare(LayoutContract.fanWheelDelta(0, 24, root.fanSpreadPitch), -24)
        }
    }
}
