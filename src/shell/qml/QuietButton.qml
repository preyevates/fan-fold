import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
/** Compact native-themed icon or colour swatch with keyboard focus and an accessible
 * name. `explanation` serves as both the Accessible.name a screen reader announces and
 * the tooltip text; hover is additionally expressed by the background tint.
 *
 * size is the hit box; iconSize is the global formatting icon size, except for the
 * collapse control which uses its own global size. Setting swatch and leaving glyph
 * empty renders the per-note paper colour instead of a themed icon.
 */
AbstractButton {
    id: control
    property string glyph: ""
    property string explanation: ""
    property color ink: "#3b352c"
    property color swatch: "transparent"
    property real size: 28
    property real iconSize: 16
    width: size; height: size
    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: explanation
    // An icon-only rail is not self-describing, so every control carries a tooltip.
    // Styled small and charcoal deliberately: the platform default renders oversized
    // against a dark background.
    ToolTip {
        visible: control.hovered && control.explanation !== ""
        delay: 500
        contentItem: Text {
            text: control.explanation
            color: "#d8dfe6"
            font.pixelSize: 10
        }
        background: Rectangle {
            color: "#2c2f34"
            border.width: 1
            border.color: "#3a3d43"
            radius: 4
        }
    }
    background: Rectangle {
        radius: 5
        color: control.hovered || control.activeFocus ? Qt.rgba(control.ink.r,control.ink.g,control.ink.b,0.12) : "transparent"
    }
    contentItem: Item {
        Rectangle {
            visible: control.glyph === ""
            anchors.centerIn: parent
            width: control.iconSize; height: control.iconSize; radius: width/2
            color: control.swatch; border.width: 1; border.color: control.ink
        }
        Kirigami.Icon {
            visible: control.glyph !== ""
            anchors.centerIn: parent
            width: control.iconSize; height: control.iconSize
            source: control.glyph; color: control.ink; isMask: true
        }
    }
}
