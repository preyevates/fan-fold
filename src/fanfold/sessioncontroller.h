#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class DocumentCollection;
class EditorLeases;
class LibraryModel;

/** The one place every Fan Fold verb happens.
 *
 * Create, open, pin, archive and delete all route through this controller so that the
 * three pieces of state that must agree — the persisted fan working set, the bounded
 * editor cache and the pinned-window register — can never drift apart. The QML shell calls
 * these methods and reacts to the signals; it holds no note state of its own.
 *
 * Deliberate boundaries:
 * - Creating or opening a note always joins the fan and always asks for editor focus, so
 *   a new note is immediately typeable rather than merely present.
 * - Pinning moves the single canonical editor into that window; it does not clone it.
 * - Closing a pinned window returns the note to the fan. It never deletes anything.
 * - Archive and delete drop the note from the fan and release its editor, leaving the
 *   selection on a note that is still there.
 */
class SessionController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentDocumentId READ currentDocumentId NOTIFY currentChanged)
    Q_PROPERTY(QStringList pinnedIds READ pinnedIds NOTIFY pinnedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)

public:
    SessionController(DocumentCollection *collection, LibraryModel *library,
                      EditorLeases *leases, QObject *parent = nullptr);

    QString currentDocumentId() const { return m_currentDocumentId; }
    QStringList pinnedIds() const { return m_pinnedIds; }
    QString lastError() const { return m_lastError; }

    /** Owner key the fan uses when it holds an editor. */
    static QString fanOwner();
    /** Owner key one pinned window uses. */
    static QString pinnedOwner(const QString &documentId);

    /** New uniquely named note in the library root; joins the fan and takes focus. */
    Q_INVOKABLE QString createNoteInRoot();
    /** New note in the Library's currently selected folder; joins the fan and focuses. */
    Q_INVOKABLE QString createNoteInLibraryFolder();
    /** Make `id` the selected fan note and hand it the fan's editor. */
    Q_INVOKABLE bool selectDocument(const QString &id);
    /** Open a catalog note from the Library: join the fan, select it and focus it. */
    Q_INVOKABLE bool openFromLibrary(const QString &id);
    /** Ctrl+F: reveal the selected note in the Library tree, expanding its ancestors. */
    Q_INVOKABLE bool findCurrentNote();

    Q_INVOKABLE bool pin(const QString &id);
    Q_INVOKABLE bool pinCurrent();
    /** Close one pinned window; the note returns to the fan and is never deleted. */
    Q_INVOKABLE bool closePinned(const QString &id);
    Q_INVOKABLE bool isPinned(const QString &id) const { return m_pinnedIds.contains(id); }
    /** Rebuild the live pinned-window register from persisted document metadata. */
    bool restorePersistedPins();

    Q_INVOKABLE bool archiveCurrent();
    /** The footer's Trash action: the desktop Trash, never an unlink. */
    Q_INVOKABLE bool trashCurrent();

signals:
    void currentChanged();
    void pinnedChanged();
    void errorChanged();
    /** The surface holding this note must put the caret in its editor now. */
    void focusEditorRequested(const QString &documentId);
    /** The Library panel should scroll this note into view and take focus. */
    void revealInLibraryRequested(const QString &documentId);
    void pinnedWindowOpened(const QString &documentId);
    void pinnedWindowClosed(const QString &documentId);

private:
    DocumentCollection *m_collection = nullptr;
    LibraryModel *m_library = nullptr;
    EditorLeases *m_leases = nullptr;
    QStringList m_pinnedIds;
    QString m_currentDocumentId;
    QString m_lastError;

    void setError(const QString &message);
    void setCurrent(const QString &id);
    /** After a note leaves the fan, settle on one that is still in it. */
    void selectFallback();
    bool available(const QString &id) const;
};
