import QtQuick
import QtTest

Item {
    id: root
    width: 80
    height: 80
    property int revealRequestCount: 0

    TestCase {
        name: "FanAutoHideReveal"
        when: windowShown

        function createReveal(hidden) {
            var component = Qt.createComponent("FanAutoHideReveal.qml")
            verify(component.status === Component.Ready,
                   "The auto-hidden deck must retain a visible reveal affordance: "
                   + component.errorString())
            var reveal = component.createObject(root, { hidden: hidden })
            reveal.revealRequested.connect(function() { root.revealRequestCount += 1 })
            return reveal
        }

        function test_reveal_is_visible_only_while_the_deck_is_hidden() {
            var reveal = createReveal(true)
            verify(reveal.visible,
                   "An auto-hidden deck must leave a visible hover target; an invisible MouseArea is not discoverable.")
            reveal.destroy()

            reveal = createReveal(false)
            verify(!reveal.visible,
                   "The reveal affordance must disappear while the note deck is already visible.")
            reveal.destroy()
        }

        function test_hovering_the_visible_handle_requests_reveal() {
            root.revealRequestCount = 0
            var reveal = createReveal(true)
            mouseMove(reveal, reveal.width / 2, reveal.height / 2)
            tryCompare(root, "revealRequestCount", 1)
            reveal.destroy()
        }
    }
}