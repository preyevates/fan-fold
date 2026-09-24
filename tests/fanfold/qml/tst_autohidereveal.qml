import QtQuick
import QtTest

Item {
    id: root
    width: 80
    height: 80

    TestCase {
        name: "FanAutoHideReveal"
        when: windowShown

        function createReveal(hidden) {
            var component = Qt.createComponent("FanAutoHideReveal.qml")
            verify(component.status === Component.Ready,
                   "The auto-hidden deck must retain a visible reveal affordance: "
                   + component.errorString())
            return component.createObject(root, { hidden: hidden })
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
    }
}