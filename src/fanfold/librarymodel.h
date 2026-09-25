#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QString>

class Document;
class DocumentCollection;

/** An ordinary folder tree of the note catalog, flattened into one list of rows.
 *
 * The Library is a read-only projection: it publishes the very same Document objects the
 * collection owns, so opening a note here can never produce a second buffer for it. A
 * flattened list (each row carrying its own `depth`) rather than a nested item model is
 * deliberate — it gives the panel one focus chain, so every folder and every note is
 * reachable with the arrow keys alone, and it keeps expansion a pure view concern.
 *
 * Rows are folders first, then notes, each group ordered by name. Archive is an ordinary
 * folder that is simply hidden until `showArchive` is set. Trashed and missing notes never
 * appear. Catalog changes are applied as the smallest possible structural edit, so
 * expanding one folder inserts exactly its own children.
 */
class LibraryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool showArchive READ showArchive WRITE setShowArchive NOTIFY showArchiveChanged)
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentChanged)
    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY currentChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        DepthRole,
        IsFolderRole,
        ExpandedRole,
        HasChildrenRole,
        NoteCountRole,
        DocumentIdRole,
        ArchivedRole,
        CurrentRole,
        DocumentRole
    };
    Q_ENUM(Role)

    explicit LibraryModel(DocumentCollection *collection, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool showArchive() const { return m_showArchive; }
    void setShowArchive(bool show);

    int currentRow() const { return m_currentRow; }
    void setCurrentRow(int row);
    /** Folder a new note belongs in: the selected folder, or the folder of the selected
     * note. Empty means the library root. */
    QString currentFolder() const;

    /** Expand or collapse one folder. The state is remembered even while the folder is
     * itself hidden inside a collapsed parent. */
    Q_INVOKABLE void setExpanded(const QString &folder, bool expanded);
    Q_INVOKABLE bool isExpanded(const QString &folder) const { return m_expanded.contains(folder); }
    /** Re-walk the library directory and re-emit rows. PUBLIC because the QML panel
     *  calls it on open — Q_INVOKABLE in a private section is silently ignored by moc,
     *  which surfaced as a TypeError only at runtime. */
    Q_INVOKABLE void rebuild();
    /** Expand/collapse the folder at `row`; a note row is ignored. */
    Q_INVOKABLE void toggle(int row);
    /** Expand every ancestor of `folder` so a note inside it becomes a visible row. */
    Q_INVOKABLE void revealFolder(const QString &folder);
    Q_INVOKABLE int rowForFolder(const QString &folder) const;
    Q_INVOKABLE int rowForDocument(const QString &documentId) const;
    /** Folder the row belongs to: a folder row's own path, a note row's parent. */
    Q_INVOKABLE QString folderForRow(int row) const;

signals:
    void showArchiveChanged();
    void currentChanged();
    void countChanged();

private:
    struct Row {
        QString key;          // "F:<path>" or "N:<documentId>"; identity for the diff
        QString name;
        QString path;
        QString documentId;
        int depth = 0;
        int noteCount = 0;
        bool isFolder = false;
        bool archived = false;

        bool operator==(const Row &other) const = default;
    };

    DocumentCollection *m_collection = nullptr;
    QList<Row> m_rows;
    /** Folders explicitly opened for this library view. Empty gives the compact index:
     *  folder names and root notes, without eagerly creating every descendant row. */
    QSet<QString> m_expanded;
    QString m_currentKey;
    int m_currentRow = -1;
    bool m_showArchive = false;

    QList<Row> buildRows() const;
    void appendFolder(QList<Row> &rows, const QString &folder, int depth,
                      const QHash<QString, QStringList> &childFolders,
                      const QHash<QString, QList<Document *>> &folderNotes) const;
    int notesUnder(const QString &folder, const QHash<QString, QStringList> &childFolders,
                   const QHash<QString, QList<Document *>> &folderNotes) const;
    void applyRows(const QList<Row> &next);
    void restoreCurrentRow();
};
