import QtQuick
import QtTest

Item {
    id: root
    width: 320
    height: 160

    property string requestedFolder: ""
    property string requestedDocument: ""

    LibraryRowAction {
        id: rowAction
        width: 240
        height: 40
        folder: true
        archived: false
        folderPath: "Work/Planning"
        documentId: ""
        onFolderRequested: function(folderPath) { root.requestedFolder = folderPath }
        onDocumentRequested: function(documentId) { root.requestedDocument = documentId }
    }

    TestCase {
        name: "LibraryRowAction"
        when: windowShown

        function init() {
            root.requestedFolder = ""
            root.requestedDocument = ""
            rowAction.folder = true
            rowAction.archived = false
            rowAction.folderPath = "Work/Planning"
            rowAction.documentId = ""
        }

        function test_folder_click_forwards_exact_path() {
            mouseClick(rowAction, 120, 20)
            compare(root.requestedFolder, "Work/Planning")
            compare(root.requestedDocument, "")
        }

        function test_archive_folder_is_never_a_scope() {
            rowAction.archived = true
            mouseClick(rowAction, 120, 20)
            compare(root.requestedFolder, "")
            compare(root.requestedDocument, "")
        }

        function test_note_click_forwards_exact_identity() {
            rowAction.folder = false
            rowAction.documentId = "note-123"
            mouseClick(rowAction, 120, 20)
            compare(root.requestedFolder, "")
            compare(root.requestedDocument, "note-123")
        }
    }
}
