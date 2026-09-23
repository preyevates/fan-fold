import QtQuick

/** The Library row's interaction contract.
 *
 * Folder rows request their exact root-relative path; note rows request their document id.
 * Archive is visible filing, never a fan scope, so its folder row deliberately does nothing.
 * Keeping this in a real component lets QtTest drive the same MouseArea the shipped shell uses.
 */
MouseArea {
    id: control

    required property bool folder
    required property bool archived
    required property string folderPath
    required property string documentId

    signal folderRequested(string folderPath)
    signal documentRequested(string documentId)

    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor

    onClicked: {
        if (control.folder) {
            if (!control.archived)
                control.folderRequested(control.folderPath)
        } else if (control.documentId !== "") {
            control.documentRequested(control.documentId)
        }
    }
}
