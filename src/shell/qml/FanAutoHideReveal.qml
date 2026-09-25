import QtQuick

/**
 * The discoverable resting place of an auto-hidden fan.
 *
 * The deck deliberately leaves only its occupied lower strip in the window, so a transparent
 * whole-screen edge target cannot steal clicks from the application below. This small, painted
 * handle lives in that strip and the existing non-clicking fan trigger sits behind it.
 */
Item {
    id: root

    required property bool hidden
    property color ink: "#202020"
    // The deck owns its state; this affordance merely reports the hover which wakes it.
    signal revealRequested()

    width: 20
    height: 36
    visible: hidden
    opacity: hidden ? 0.82 : 0
    Accessible.role: Accessible.StaticText
    Accessible.name: "Hidden note deck; hover the handle to reveal notes"

    Behavior on opacity {
        NumberAnimation { duration: 120 }
    }

    // Do not rely on an item behind this painted handle to receive hover. On some platforms
    // that leaves the auto-hidden deck as an inert corner after the idle timer fires.
    HoverHandler {
        onHoveredChanged: {
            if (hovered && root.hidden)
                root.revealRequested()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: 4
        height: 24
        radius: 2
        color: root.ink
        opacity: 0.56
    }
    Text {
        anchors.centerIn: parent
        text: "‹"
        color: root.ink
        font.pixelSize: 16
        font.weight: Font.DemiBold
        opacity: 0.92
    }
}