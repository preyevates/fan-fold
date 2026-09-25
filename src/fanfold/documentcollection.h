#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

/** One authoritative in-memory state for one Markdown document.
 *
 * Exactly one Document exists per stable opaque ID for the life of an open library. The
 * object is owned by DocumentCollection and is published unchanged to every fan card,
 * Library row, search hit and pinned window, so no surface can hold a second buffer for
 * the same note. Its ID is independent of the current path. Markdown on disk remains the
 * authoritative content; UI metadata lives in the collection's XDG state instead of being
 * written into the user's notes.
 *
 * The `disk*` properties describe the file as last stat'ed by the collection, not the
 * editor buffer: while `dirty` is true they still report what a different application
 * would see, which is exactly what the File information panel must show.
 */
class Document final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString relativePath READ relativePath NOTIFY changed)
    Q_PROPERTY(QString absolutePath READ absolutePath NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString content READ content NOTIFY contentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool conflict READ conflict NOTIFY changed)
    Q_PROPERTY(bool missing READ missing NOTIFY changed)
    Q_PROPERTY(QString saveError READ saveError NOTIFY changed)
    Q_PROPERTY(bool archived READ archived NOTIFY changed)
    Q_PROPERTY(bool trashed READ trashed NOTIFY changed)
    Q_PROPERTY(QString trashPath READ trashPath NOTIFY changed)
    Q_PROPERTY(bool pinned READ pinned NOTIFY changed)
    Q_PROPERTY(int pinnedWindowWidth READ pinnedWindowWidth NOTIFY changed)
    Q_PROPERTY(int pinnedWindowHeight READ pinnedWindowHeight NOTIFY changed)
    Q_PROPERTY(bool inFan READ inFan NOTIFY changed)
    Q_PROPERTY(QString paper READ paper NOTIFY changed)
    Q_PROPERTY(QString ink READ ink NOTIFY changed)
    Q_PROPERTY(qint64 diskBytes READ diskBytes NOTIFY changed)
    Q_PROPERTY(QDateTime diskModified READ diskModified NOTIFY changed)

public:
    QString id() const { return m_id; }
    QString relativePath() const { return m_relativePath; }
    QString absolutePath() const { return m_absolutePath; }
    /** Bare file name including the `.md` extension, for the information panel. */
    QString fileName() const;
    /** Root-relative containing folder, empty for a note directly in the root. */
    QString folder() const;
    /** File name without its `.md` extension; the editable note title. */
    QString title() const;
    QString content() const { return m_content; }
    bool dirty() const { return m_dirty; }
    bool conflict() const { return m_conflict; }
    bool missing() const { return m_missing; }
    QString saveError() const { return m_saveError; }
    bool archived() const { return m_archived; }
    bool trashed() const { return m_trashed; }
    QString trashPath() const { return m_trashPath; }
    bool pinned() const { return m_pinned; }
    /** Last settled pinned-window client size; zero means use the UI default. */
    int pinnedWindowWidth() const { return m_pinnedWindowWidth; }
    int pinnedWindowHeight() const { return m_pinnedWindowHeight; }
    /** True while this note is currently ON the fan.
     *
     * DERIVED, never set by hand: a note is on the fan when it lives in the collection's
     * open folder and is not archived, trashed or pinned. The flag is a cached answer to
     * that question, refreshed by DocumentCollection::rebuildFan().
     */
    bool inFan() const { return m_inFan; }
    QString paper() const { return m_paper; }
    QString ink() const { return m_ink; }
    /** Relative path of this note's tab icon under the library, or empty for none. */
    QString icon() const { return m_icon; }
    /** Size of the file on disk at the last stat, independent of the unsaved buffer. */
    qint64 diskBytes() const { return m_diskBytes; }
    /** Modification time of the file on disk at the last stat. */
    QDateTime diskModified() const { return m_diskModified; }

signals:
    void changed();
    void contentChanged();

private:
    explicit Document(QString id, QObject *parent = nullptr);

    QString m_id;
    QString m_relativePath;
    QString m_absolutePath;
    QString m_restorePath;
    QString m_trashPath;
    QString m_content;
    QString m_revision;
    QString m_saveError;
    QString m_paper = QStringLiteral("#f5f0e6");
    QString m_ink = QStringLiteral("auto");
    QString m_icon;
    QDateTime m_diskModified;
    qint64 m_diskBytes = 0;
    quint64 m_device = 0;
    quint64 m_inode = 0;
    bool m_dirty = false;
    bool m_conflict = false;
    bool m_missing = false;
    bool m_archived = false;
    bool m_trashed = false;
    bool m_pinned = false;
    int m_pinnedWindowWidth = 0;
    int m_pinnedWindowHeight = 0;
    bool m_inFan = false;

    friend class DocumentCollection;
};

/** Folder-scoped document engine for Fan Fold.
 *
 * The collection owns two separate orders and never confuses them:
 *
 * - The **catalog** is every ordinary UTF-8 Markdown file discovered below the one
 *   explicitly selected root, ordered by relative path. It is what this model's rows are,
 *   so the Library tree and search see a stable, deterministic list.
 * - The **fan** is a WINDOW ONTO ONE FOLDER: the notes of `openFolder()`, direct children
 *   only, minus anything archived, trashed or pinned. Membership is DERIVED from that
 *   rule and is not state — nothing "joins" the fan. What IS state is the per-folder
 *   ORDER the user arranges by dragging, and the open folder itself; both persist in the
 *   XDG index.
 *
 * It combines QFileSystemWatcher invalidations with authoritative rescans, writes edits
 * through QSaveFile after a precise 250 ms quiet period restarted by every keystroke,
 * journals every dirty buffer first, and refuses saves when the disk revision no longer
 * matches. Reconciliation that finds nothing new emits no model signals and rewrites no
 * metadata, so a resident application can poll indefinitely without churning the UI or
 * the disk. Metadata and crash recovery live in XDG application state rather than beside
 * the user's Markdown.
 *
 * Switching roots is transactional: the new root, its state directory and its index are
 * fully validated before anything is torn down, and any pending save in the old library
 * is flushed first. A rejected switch leaves the previous library exactly as it was.
 */
class DocumentCollection final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootChanged)
    Q_PROPERTY(bool open READ isOpen NOTIFY rootChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY documentsChanged)
    Q_PROPERTY(QStringList fanIds READ fanIds NOTIFY fanChanged)
    Q_PROPERTY(int fanCount READ fanCount NOTIFY fanChanged)
    Q_PROPERTY(QString openFolder READ openFolder NOTIFY openFolderChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        PathRole,
        AbsolutePathRole,
        FileNameRole,
        FolderRole,
        TitleRole,
        ContentRole,
        DirtyRole,
        ConflictRole,
        MissingRole,
        SaveErrorRole,
        ArchivedRole,
        TrashedRole,
        PinnedRole,
        InFanRole,
        PaperRole,
        InkRole,
        DiskBytesRole,
        DiskModifiedRole,
        DocumentRole
    };
    Q_ENUM(Role)

    /** Construct an engine.
     * @param stateRoot Explicit XDG-equivalent state root for headless tests. Empty uses
     * QStandardPaths::AppDataLocation in production.
     */
    explicit DocumentCollection(QString stateRoot = {}, QObject *parent = nullptr);
    ~DocumentCollection() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString rootPath() const { return m_rootPath; }
    bool isOpen() const { return m_open; }
    QString lastError() const { return m_lastError; }
    /** Every discovered note, in catalog (path) order; one entry per model row.
     *
     * Q_INVOKABLE because the shell reads it at startup to restore pinned windows: a
     * pinned note has LEFT the fan, so `fanIds` cannot see it and only the catalog can.
     */
    Q_INVOKABLE QStringList catalogIds() const { return m_catalog; }
    /** Backwards-compatible alias of catalogIds(). */
    QStringList documentIds() const { return m_catalog; }
    /** The fan: the open folder's notes, in this folder's own persisted order. */
    QStringList fanIds() const { return m_fan; }
    int fanCount() const { return int(m_fan.size()); }
    /** Root-relative folder the fan is currently a window onto; empty means the root. */
    QString openFolder() const { return m_openFolder; }
    Document *document(const QString &id) const { return m_documents.value(id); }
    /** Absolute path of the XDG index this library persists its metadata to. */
    Q_INVOKABLE QString metadataPath() const;

    /** Open only the folder explicitly supplied by the host or `--root` test argument.
     * No default location is invented and no Markdown file is rewritten during opening.
     * @return false with lastError() set and the previous library untouched on refusal.
     */
    Q_INVOKABLE bool openRoot(const QString &folder);
    /** Flush, close and forget the current library, leaving Markdown untouched. */
    Q_INVOKABLE void closeRoot();
    Q_INVOKABLE QString idForRelativePath(const QString &relativePath) const;
    Q_INVOKABLE QObject *documentObject(const QString &id) const { return document(id); }
    /** Every distinct root-relative folder that holds at least one note, path-ordered. */
    Q_INVOKABLE QStringList folders() const;
    Q_INVOKABLE void reconcileNow();

    /** Create a no-clobber empty Markdown file using Untitled, Untitled 2, ... names.
     * @param relativeFolder Destination folder; empty means the root. Archive is refused.
     * @return the new stable ID, or an empty string with lastError() set.
     */
    Q_INVOKABLE QString createNote(const QString &relativeFolder = {});
    /** Journal a new editor buffer immediately, then restart the full quiet period. */
    Q_INVOKABLE bool updateContent(const QString &id, const QString &content);
    /** Save one dirty document if its on-disk revision still matches. */
    Q_INVOKABLE bool saveNow(const QString &id);
    /** Flush all non-conflicting pending saves for a normal application close. */
    Q_INVOKABLE bool flushPendingSaves();
    /** The configured autosave quiet period in milliseconds. */
    Q_INVOKABLE int autosaveQuietPeriodMs() const;
    /** Milliseconds left of this note's quiet period, or -1 when no save is pending. */
    Q_INVOKABLE int pendingSaveRemainingMs(const QString &id) const;

    Q_INVOKABLE bool renameDocument(const QString &id, const QString &title);
    Q_INVOKABLE bool archive(const QString &id);
    /** Move an archived note back to its original folder, never overwriting an occupant.
     * When the original name is taken the note is restored beside it under a unique
     * `name (restored).md` style name and documentRestored() reports renamed = true.
     */
    Q_INVOKABLE bool restoreArchive(const QString &id);
    Q_INVOKABLE bool moveToTrash(const QString &id);
    Q_INVOKABLE bool restoreFromTrash(const QString &id);

    /** Point the fan at `folder` (root-relative; empty means the root).
     *
     * The scoping gesture: opening a folder in the Library calls this. The folder need
     * not contain notes — an empty folder gives an empty fan, which is a legitimate state
     * and not a refusal. A folder that does not exist on disk is refused and the previous
     * scope is kept.
     * @return false with lastError() set when the folder is outside the root, is the
     * Archive tree, or does not exist.
     */
    Q_INVOKABLE bool setOpenFolder(const QString &folder);
    /** Replace the OPEN FOLDER's tab order; must be a permutation of the current fan. */
    Q_INVOKABLE bool setFanOrder(const QStringList &ids);
    /** True while `id` is on the fan right now — i.e. in the open folder and available. */
    Q_INVOKABLE bool isInFan(const QString &id) const { return m_fan.contains(id); }

    Q_INVOKABLE bool setPinned(const QString &id, bool pinned);
    /** Persist the settled client size of a pinned window in XDG session metadata. */
    Q_INVOKABLE bool setPinnedWindowSize(const QString &id, int width, int height);
    Q_INVOKABLE bool setPaper(const QString &id, const QString &paper);

    /** Set (or clear, with an empty string) a note's tab icon.
     *  @param relative Path under the library root, e.g. "Assets/icons/book.svg". */
    Q_INVOKABLE bool setIcon(const QString &id, const QString &relative);
    Q_INVOKABLE bool setInk(const QString &id, const QString &ink);

    /** One-time bulk write of a paper colour into every live note of `folder`.
     *
     *  NOT a folder rule: each note's own stored paper is overwritten once, and every
     *  note remains individually changeable afterwards exactly as before. The set is the
     *  folder's direct children that are neither archived, trashed nor missing; pinned
     *  notes are included. The value is validated before anything is touched, metadata
     *  is persisted ONCE, and documentChanged() is emitted per note that changed.
     *  @return how many notes the set holds (changed or already equal), or -1 when the
     *  colour is invalid or the index could not be written. */
    Q_INVOKABLE int setPaperForFolder(const QString &folder, const QString &paper);
    /** Ink counterpart of setPaperForFolder(); accepts the sentinel "auto". */
    Q_INVOKABLE int setInkForFolder(const QString &folder, const QString &ink);
    /** Ids setPaperForFolder()/setInkForFolder() would write, in catalog order. */
    Q_INVOKABLE QStringList liveIdsInFolder(const QString &folder) const;
    Q_INVOKABLE bool hasRecovery(const QString &id) const;
    Q_INVOKABLE bool watchHealthy() const;

signals:
    void rootChanged();
    void errorChanged();
    void documentsChanged();
    void fanChanged();
    void openFolderChanged();
    void documentChanged(const QString &id);
    void documentAdded(const QString &id);
    void documentRemoved(const QString &id);
    /** Emitted after restoreArchive() succeeds.
     * @param id Stable document ID.
     * @param relativePath Where the note actually landed.
     * @param renamed True when a collision forced a unique fallback name.
     */
    void documentRestored(const QString &id, const QString &relativePath, bool renamed);

private:
    struct DiskEntry {
        QString relativePath;
        QString absolutePath;
        QString content;
        QString revision;
        QDateTime modified;
        qint64 bytes = 0;
        quint64 device = 0;
        quint64 inode = 0;
        bool archived = false;
    };

    /** Everything a candidate root must supply before the live library is torn down. */
    struct PendingLibrary {
        QString rootPath;
        QString statePath;
        QJsonObject metadata;
    };

    QString m_stateRoot;
    QString m_libraryState;
    QString m_rootPath;
    QString m_lastError;
    QFileSystemWatcher m_watcher;
    QTimer m_reconcileTimer;
    QTimer m_periodicTimer;
    QHash<QString, Document *> m_documents;
    QHash<QString, QTimer *> m_saveTimers;
    QSet<QString> m_recoveryApplied;
    QStringList m_catalog;
    /** The open folder's fan, derived and rebuilt; never assigned by a caller. */
    QStringList m_fan;
    /** Root-relative open folder; empty is the library root. */
    QString m_openFolder;
    /** Persisted per-folder tab order, folder -> ids. Holds ids that are not currently on
     *  the fan (archived, pinned, or simply not yet rediscovered) so a note returning to
     *  a folder resumes its old slot instead of being appended. */
    QHash<QString, QStringList> m_folderOrder;
    QJsonObject m_metadata;
    bool m_open = false;
    bool m_reconciling = false;
    bool m_metadataDirty = false;
    bool m_bulkLoad = false;

    void setError(const QString &message);
    QString indexPath() const;
    QString recoveryPath(const QString &id) const;
    QString absoluteFor(const QString &relativePath) const;
    bool confinedRelative(const QString &relativePath) const;
    QList<DiskEntry> scan(QStringList *warnings = nullptr) const;
    static QString digest(const QByteArray &bytes);
    static bool decodeUtf8(const QByteArray &bytes, QString *text);
    static bool fileIdentity(const QString &path, quint64 *device, quint64 *inode,
                             QDateTime *modified = nullptr, qint64 *bytes = nullptr);
    static QString identityKey(quint64 device, quint64 inode);
    static QString normalizedColor(const QString &value, bool allowAuto = false);
    int setColourForFolder(const QString &folder, const QString &value, bool ink);
    static QString safeTitleFileName(const QString &title);
    static bool renameNoReplace(const QString &source, const QString &target, QString *error);
    static bool lessThanByPath(const QString &left, const QString &right);

    bool prepareLibrary(const QString &folder, PendingLibrary *pending);
    void adoptLibrary(PendingLibrary &&pending);
    void teardownLibrary();
    /** True when `id` belongs on the fan right now: in the open folder, and neither
     *  archived, trashed, missing-and-clean nor pinned. */
    bool belongsOnFan(const Document *document) const;
    /** Re-derive m_fan from the open folder, honouring this folder's persisted order and
     *  appending anything new in catalog order. Emits fanChanged() only on a real change.
     *  @return true when the fan actually changed. */
    bool rebuildFan();
    /** Read the folder a persisted order entry belongs to, normalised (root = ""). */
    static QString normalizedFolder(const QString &folder);
    /** The per-folder order as JSON for the index, pruned to live ids. */
    QJsonObject storedFolderOrder() const;
    bool persistMetadata();
    void markMetadataDirty();
    QJsonObject metadataFor(const Document *document) const;
    void applyMetadata(Document *document, const QJsonObject &object);
    Document *ensureDocument(const QString &id, bool appendToCatalog);
    void emitDocumentChanged(Document *document, bool contentChanged = false);
    int rowForId(const QString &id) const;
    void applyCatalogOrder(const QStringList &desired);
    QStringList sortedCatalog() const;
    QString uniqueTargetIn(const QString &folder, const QString &fileName) const;

    bool writeRecovery(Document *document);
    void applyRecovery(Document *document);
    void clearRecovery(const QString &id);
    void scheduleSave(Document *document);
    void cancelScheduledSave(const QString &id);
    void refreshWatches();
    void scheduleReconcile();
    bool moveInsideRoot(Document *document, QString targetRelative,
                        bool archived, QString restorePath);
    void updateDiskStat(Document *document);
};
