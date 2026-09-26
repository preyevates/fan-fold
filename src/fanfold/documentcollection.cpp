#include "documentcollection.h"

#include "appearancesettings.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QUuid>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1 << 1)
#endif

namespace {
constexpr qint64 maximumDocumentBytes = 8 * 1024 * 1024;

/** Quiet period before an edited buffer is written.
 *
 * The contract is "250 ms after the LAST keystroke", so the timer is restarted rather
 * than topped up on every edit, and it is a precise timer: Qt's default coarse timers may
 * fire up to 5 % early, which would write during the quiet period the UI promises.
 */
constexpr int kAutosaveQuietPeriodMs = 250;

constexpr auto archiveFolderName = "Archive";

bool ensureDirectoryChain(const QString &root, const QString &relativeDirectory, QString *error)
{
    if (relativeDirectory.isEmpty() || relativeDirectory == QStringLiteral(".")) {
        return true;
    }
    QString current = root;
    const QStringList components = QDir::cleanPath(relativeDirectory).split('/', Qt::SkipEmptyParts);
    for (const QString &component : components) {
        if (component == QStringLiteral(".") || component == QStringLiteral("..")) {
            if (error) {
                *error = QStringLiteral("Unsafe directory component");
            }
            return false;
        }
        current = QDir(current).filePath(component);
        struct stat metadata {};
        const QByteArray encoded = QFile::encodeName(current);
        if (::lstat(encoded.constData(), &metadata) == 0) {
            if (!S_ISDIR(metadata.st_mode) || S_ISLNK(metadata.st_mode)) {
                if (error) {
                    *error = QStringLiteral("Directory path contains a link or non-directory");
                }
                return false;
            }
            continue;
        }
        if (errno != ENOENT || ::mkdir(encoded.constData(), 0700) != 0) {
            if (error) {
                *error = QStringLiteral("Cannot create folder: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
            }
            return false;
        }
    }
    return true;
}

bool isRegularSingleLink(const QString &path, struct stat *output = nullptr)
{
    struct stat metadata {};
    const QByteArray encoded = QFile::encodeName(path);
    if (::lstat(encoded.constData(), &metadata) != 0 || !S_ISREG(metadata.st_mode) || metadata.st_nlink != 1) {
        return false;
    }
    if (output) {
        *output = metadata;
    }
    return true;
}

constexpr auto displacedDirectoryName = ".fanfold-displaced";

QStringList retainedInodes(const QString &root, const QString &id)
{
    const QDir directory(QDir(root).filePath(QLatin1String(displacedDirectoryName)));
    struct stat metadata {};
    const QByteArray directoryName = QFile::encodeName(directory.path());
    if (::lstat(directoryName.constData(), &metadata) != 0 || !S_ISDIR(metadata.st_mode)) return {};
    QStringList paths;
    for (const QString &name : directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        if (name.startsWith(id + QLatin1Char('-'))) paths.append(directory.filePath(name));
    }
    return paths;
}

// The expected bytes are encoded in the name, not kept only in memory or in the
// possibly cross-device XDG state directory. This works after a process restart.
bool retainedUnchanged(const QString &path, const QString &id)
{
    const QString name = QFileInfo(path).fileName();
    const QString prefix = id + QLatin1Char('-');
    if (!name.startsWith(prefix) || name.size() != prefix.size() + 64 + 1 + 36 + 4
        || name.at(prefix.size() + 64) != QLatin1Char('-') || !name.endsWith(QStringLiteral(".old"))
        || !isRegularSingleLink(path)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray bytes = file.read(maximumDocumentBytes + 1);
    return bytes.size() <= maximumDocumentBytes
        && QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
               == name.mid(prefix.size(), 64);
}

bool isArchivedPath(const QString &relativePath)
{
    return relativePath == QLatin1String(archiveFolderName)
        || relativePath.startsWith(QLatin1String(archiveFolderName) + QLatin1Char('/'));
}

/** Schema version of the XDG index this build writes.
 *
 * 1 — flat `"fan"` array: one curated working set for the whole library.
 * 2 — `"folderOrder"` (folder -> ordered ids) plus `"openFolder"`: the fan is a window
 *     onto one folder, so ORDER is per folder and membership is derived.
 */
constexpr int kIndexVersion = 2;

/** Root-relative folder of a root-relative note path; the root is an empty string. */
QString folderOfPath(const QString &relativePath)
{
    const QString parent = QFileInfo(QDir::cleanPath(relativePath)).path();
    return parent == QStringLiteral(".") ? QString() : parent;
}

/** Rewrite a version-1 index as version 2 without losing a single note.
 *
 * The old flat `fan` array is split by the folder each note actually lives in, and each
 * folder's order is the old flat order filtered to that folder — so the sequence the user
 * arranged survives the split wherever it is still meaningful. `documents` is untouched,
 * which is what guarantees no note is lost: the catalog never lived in `fan`.
 */
QJsonObject migrateIndexToVersion2(const QJsonObject &stored)
{
    QJsonObject migrated = stored;
    const QJsonObject documents = stored.value(QStringLiteral("documents")).toObject();
    QHash<QString, QJsonArray> byFolder;
    QStringList folderOrder; // stable key order, so the written file is deterministic
    for (const QJsonValue &value : stored.value(QStringLiteral("fan")).toArray()) {
        const QString id = value.toString();
        if (id.isEmpty() || !documents.contains(id)) {
            continue;
        }
        const QString folder =
            folderOfPath(documents.value(id).toObject().value(QStringLiteral("path")).toString());
        if (!byFolder.contains(folder)) {
            folderOrder.append(folder);
        }
        byFolder[folder].append(id);
    }
    QJsonObject orders;
    for (const QString &folder : std::as_const(folderOrder)) {
        orders.insert(folder, byFolder.value(folder));
    }
    migrated.remove(QStringLiteral("fan"));
    migrated.insert(QStringLiteral("folderOrder"), orders);
    // A migrated library opens at the root: the pre-0.2.0 fan spanned every folder, so no
    // stored scope exists and inventing one would hide notes the user last saw.
    migrated.insert(QStringLiteral("openFolder"), QString());
    migrated.insert(QStringLiteral("version"), kIndexVersion);
    return migrated;
}

/** Indices of one longest strictly increasing subsequence of `values`.
 *
 * Used to pick the catalog rows that may stay where they are when a rename changes one
 * note's sort key: everything outside the subsequence is what genuinely has to move, so a
 * single rename emits a single rowsMoved rather than a cascade of them.
 */
QSet<int> longestIncreasingIndices(const QList<int> &values)
{
    if (values.isEmpty()) {
        return {};
    }
    QList<int> tailIndex;             // tailIndex[k]: index of the smallest tail of length k+1
    QList<int> predecessor(values.size(), -1);
    for (int i = 0; i < values.size(); ++i) {
        int low = 0;
        int high = int(tailIndex.size());
        while (low < high) {
            const int middle = (low + high) / 2;
            if (values.at(tailIndex.at(middle)) < values.at(i)) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        if (low > 0) {
            predecessor[i] = tailIndex.at(low - 1);
        }
        if (low == tailIndex.size()) {
            tailIndex.append(i);
        } else {
            tailIndex[low] = i;
        }
    }
    QSet<int> keep;
    for (int cursor = tailIndex.isEmpty() ? -1 : tailIndex.last(); cursor >= 0;
         cursor = predecessor.at(cursor)) {
        keep.insert(cursor);
    }
    return keep;
}

} // namespace

Document::Document(QString id, QObject *parent)
    : QObject(parent), m_id(std::move(id))
{
}

QString Document::fileName() const
{
    return QFileInfo(m_relativePath).fileName();
}

QString Document::folder() const
{
    const QString parent = QFileInfo(m_relativePath).path();
    return parent == QStringLiteral(".") ? QString() : parent;
}

QString Document::title() const
{
    const QString name = fileName();
    return name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ? name.chopped(3) : name;
}

DocumentCollection::DocumentCollection(QString stateRoot, QObject *parent)
    : QAbstractListModel(parent)
    , m_stateRoot(stateRoot.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
          : QDir::cleanPath(std::move(stateRoot)))
{
    m_reconcileTimer.setSingleShot(true);
    m_reconcileTimer.setInterval(50);
    connect(&m_reconcileTimer, &QTimer::timeout, this, &DocumentCollection::reconcileNow);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleReconcile(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleReconcile(); });

    m_periodicTimer.setInterval(500);
    connect(&m_periodicTimer, &QTimer::timeout, this, &DocumentCollection::reconcileNow);
}

DocumentCollection::~DocumentCollection() = default;

int DocumentCollection::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_catalog.size());
}

QVariant DocumentCollection::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_catalog.size()) {
        return {};
    }
    Document *document = m_documents.value(m_catalog.at(index.row()));
    if (!document) {
        return {};
    }
    switch (role) {
    case IdRole: return document->id();
    case PathRole: return document->relativePath();
    case AbsolutePathRole: return document->absolutePath();
    case FileNameRole: return document->fileName();
    case FolderRole: return document->folder();
    case TitleRole: return document->title();
    case ContentRole: return document->content();
    case DirtyRole: return document->dirty();
    case ConflictRole: return document->conflict();
    case MissingRole: return document->missing();
    case SaveErrorRole: return document->saveError();
    case ArchivedRole: return document->archived();
    case TrashedRole: return document->trashed();
    case PinnedRole: return document->pinned();
    case InFanRole: return document->inFan();
    case PaperRole: return document->paper();
    case InkRole: return document->ink();
    case DiskBytesRole: return document->diskBytes();
    case DiskModifiedRole: return document->diskModified();
    case DocumentRole: return QVariant::fromValue(document);
    default: return {};
    }
}

QHash<int, QByteArray> DocumentCollection::roleNames() const
{
    return {{IdRole, "documentId"}, {PathRole, "path"}, {AbsolutePathRole, "absolutePath"},
            {FileNameRole, "fileName"}, {FolderRole, "folder"}, {TitleRole, "title"},
            {ContentRole, "content"}, {DirtyRole, "dirty"}, {ConflictRole, "conflict"},
            {MissingRole, "missing"}, {SaveErrorRole, "saveError"},
            {ArchivedRole, "archived"}, {TrashedRole, "trashed"}, {PinnedRole, "pinned"},
            {InFanRole, "inFan"}, {PaperRole, "paper"}, {InkRole, "ink"},
            {DiskBytesRole, "diskBytes"}, {DiskModifiedRole, "diskModified"},
            {DocumentRole, "document"}};
}

QString DocumentCollection::metadataPath() const
{
    return indexPath();
}

int DocumentCollection::autosaveQuietPeriodMs() const
{
    return kAutosaveQuietPeriodMs;
}

int DocumentCollection::pendingSaveRemainingMs(const QString &id) const
{
    QTimer *timer = m_saveTimers.value(id);
    return timer && timer->isActive() ? timer->remainingTime() : -1;
}

bool DocumentCollection::openRoot(const QString &folder)
{
    PendingLibrary pending;
    if (!prepareLibrary(folder, &pending)) {
        return false;
    }
    if (pending.rootPath == m_rootPath && m_open) {
        reconcileNow();
        return true;
    }
    if (m_open && !flushPendingSaves()) {
        return false;
    }
    adoptLibrary(std::move(pending));
    return true;
}

/** Validate a candidate library completely without disturbing the live one.
 *
 * Every step that can fail — the root itself, the XDG state directory and the stored
 * index — happens here, so openRoot() only tears down the previous library once the new
 * one is known to be usable.
 * @param folder Folder the host or the test supplied.
 * @param pending Filled with the canonical root, state directory and validated index.
 * @return false with lastError() set; nothing observable has changed.
 */
bool DocumentCollection::prepareLibrary(const QString &folder, PendingLibrary *pending)
{
    const QFileInfo supplied(QDir::cleanPath(QFileInfo(folder).absoluteFilePath()));
    const QString canonical = supplied.canonicalFilePath();
    if (canonical.isEmpty() || !supplied.isDir() || supplied.isSymLink()
        || canonical != supplied.absoluteFilePath()) {
        setError(QStringLiteral("The selected root must be an existing real directory with no symlink components"));
        return false;
    }

    struct stat metadata {};
    const QByteArray encoded = QFile::encodeName(canonical);
    if (::lstat(encoded.constData(), &metadata) != 0 || !S_ISDIR(metadata.st_mode) || S_ISLNK(metadata.st_mode)) {
        setError(QStringLiteral("The selected root is unsafe or unavailable"));
        return false;
    }

    const QByteArray key = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString state = QDir(m_stateRoot).filePath(QStringLiteral("libraries/%1").arg(QString::fromLatin1(key)));
    if (!QDir().mkpath(state) || !QDir().mkpath(QDir(state).filePath(QStringLiteral("recovery")))) {
        setError(QStringLiteral("Cannot create the XDG metadata directory"));
        return false;
    }

    QJsonObject stored{{QStringLiteral("version"), kIndexVersion},
                       {QStringLiteral("root"), canonical},
                       {QStringLiteral("documents"), QJsonObject{}},
                       {QStringLiteral("folderOrder"), QJsonObject{}},
                       {QStringLiteral("openFolder"), QString()}};
    QFile index(QDir(state).filePath(QStringLiteral("index.json")));
    if (index.exists()) {
        if (!index.open(QIODevice::ReadOnly) || index.size() > 4 * 1024 * 1024) {
            setError(QStringLiteral("Cannot read the XDG document index; Markdown was left untouched"));
            return false;
        }
        QJsonParseError parseError {};
        const QJsonDocument parsed = QJsonDocument::fromJson(index.readAll(), &parseError);
        const int version = parsed.isObject()
            ? parsed.object().value(QStringLiteral("version")).toInt()
            : 0;
        // The document map carries stable IDs and paths needed to attach recovery
        // journals. Other layout fields are optional for v1 and are migrated below.
        // An empty map is valid for a genuinely empty library.
        const QJsonValue documentMap = parsed.object().value(QStringLiteral("documents"));
        bool validDocuments = documentMap.isObject();
        if (validDocuments) {
            const QJsonObject documents = documentMap.toObject();
            for (auto it = documents.begin(); it != documents.end(); ++it) {
                const QJsonValue path = it.value().toObject().value(QStringLiteral("path"));
                if (it.key().isEmpty() || !it.value().isObject() || !path.isString()
                    || !confinedRelative(path.toString())) {
                    validDocuments = false;
                    break;
                }
            }
        }
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject()
            || version < 1 || version > kIndexVersion
            || parsed.object().value(QStringLiteral("root")).toString() != canonical
            || !validDocuments) {
            // Fresh IDs would orphan recovery records keyed by the old IDs, then
            // overwrite the only index that can map those edits back to their notes.
            // Keep both index and journal intact until the index is repaired.
            setError(QStringLiteral("Invalid or mismatched XDG document index; library was not opened and Markdown was left untouched"));
            return false;
        } else {
            // A pre-0.2.0 index carries a flat `fan`; migrate it here, where the previous
            // library is still untouched and a failure costs nothing.
            stored = version < kIndexVersion ? migrateIndexToVersion2(parsed.object())
                                             : parsed.object();
        }
    }

    pending->rootPath = canonical;
    pending->statePath = state;
    pending->metadata = stored;
    return true;
}

/** Commit a validated library after the old one's pending saves succeeded. */
void DocumentCollection::adoptLibrary(PendingLibrary &&pending)
{
    teardownLibrary();

    m_rootPath = pending.rootPath;
    m_libraryState = pending.statePath;
    m_metadata = pending.metadata;
    m_open = true;
    m_metadataDirty = false;

    const QJsonObject documents = m_metadata.value(QStringLiteral("documents")).toObject();
    for (auto it = documents.begin(); it != documents.end(); ++it) {
        applyMetadata(ensureDocument(it.key(), false), it.value().toObject());
    }
    const QJsonObject orders = m_metadata.value(QStringLiteral("folderOrder")).toObject();
    for (auto it = orders.begin(); it != orders.end(); ++it) {
        QStringList ids;
        for (const QJsonValue &value : it.value().toArray()) {
            const QString id = value.toString();
            // Only ids this library actually knows: a hand-edited or synced index must
            // not be able to inject phantom entries into a folder's order.
            if (!id.isEmpty() && m_documents.contains(id) && !ids.contains(id)) {
                ids.append(id);
            }
        }
        if (!ids.isEmpty()) {
            m_folderOrder.insert(normalizedFolder(it.key()), ids);
        }
    }
    // The open folder is restored, but only if it still exists on disk. A folder the user
    // has since deleted or moved must fall back to the root rather than leaving the fan
    // empty with no visible reason and no way back.
    const QString wanted = normalizedFolder(m_metadata.value(QStringLiteral("openFolder")).toString());
    m_openFolder = (!wanted.isEmpty()
                    && (!confinedRelative(wanted) || isArchivedPath(wanted)
                        || !QFileInfo(absoluteFor(wanted)).isDir()))
        ? QString()
        : wanted;

    // First discovery of a library is a genuine reset, so it is the one place that uses
    // one; every later reconciliation is incremental.
    m_bulkLoad = true;
    reconcileNow();
    m_bulkLoad = false;
    refreshWatches();
    m_periodicTimer.start();

    if (m_stateRoot == QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)) {
        QSettings settings;
        settings.setValue(QStringLiteral("library/root"), m_rootPath);
    }
    emit rootChanged();
    emit openFolderChanged();
    emit fanChanged();
}

void DocumentCollection::teardownLibrary()
{
    m_periodicTimer.stop();
    m_reconcileTimer.stop();
    const QStringList watchedFiles = m_watcher.files();
    const QStringList watchedDirectories = m_watcher.directories();
    if (!watchedFiles.isEmpty()) {
        m_watcher.removePaths(watchedFiles);
    }
    if (!watchedDirectories.isEmpty()) {
        m_watcher.removePaths(watchedDirectories);
    }

    beginResetModel();
    qDeleteAll(m_documents);
    m_documents.clear();
    m_saveTimers.clear();
    m_catalog.clear();
    m_fan.clear();
    m_folderOrder.clear();
    m_openFolder.clear();
    m_recoveryApplied.clear();
    endResetModel();

    m_open = false;
    m_rootPath.clear();
    m_libraryState.clear();
    m_metadata = QJsonObject();
    m_metadataDirty = false;
}

void DocumentCollection::closeRoot()
{
    if (!m_open) {
        return;
    }
    if (!flushPendingSaves()) {
        return;
    }
    teardownLibrary();
    emit rootChanged();
    emit openFolderChanged();
    emit documentsChanged();
    emit fanChanged();
}

QString DocumentCollection::idForRelativePath(const QString &relativePath) const
{
    const QString clean = QDir::cleanPath(relativePath);
    for (const QString &id : m_catalog) {
        Document *document = m_documents.value(id);
        if (document && !document->m_missing && !document->m_trashed && document->m_relativePath == clean) {
            return id;
        }
    }
    return {};
}

QStringList DocumentCollection::folders() const
{
    QSet<QString> found;
    for (const QString &id : m_catalog) {
        Document *document = m_documents.value(id);
        if (!document || document->m_trashed || document->m_missing) {
            continue;
        }
        QString folder = document->folder();
        while (!folder.isEmpty()) {
            found.insert(folder);
            const QString parent = QFileInfo(folder).path();
            folder = parent == QStringLiteral(".") ? QString() : parent;
        }
    }
    QStringList result(found.begin(), found.end());
    std::sort(result.begin(), result.end(), [](const QString &left, const QString &right) {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

void DocumentCollection::reconcileNow()
{
    if (!m_open || m_reconciling) {
        return;
    }
    m_reconciling = true;

    QStringList warnings;
    const QList<DiskEntry> entries = scan(&warnings);
    QSet<QString> seen;
    QHash<QString, QString> identityToId;
    QHash<QString, QString> pathToId;
    for (auto it = m_documents.cbegin(); it != m_documents.cend(); ++it) {
        Document *document = it.value();
        if (document->m_trashed) {
            continue;
        }
        if (document->m_device && document->m_inode) {
            identityToId.insert(identityKey(document->m_device, document->m_inode), it.key());
        }
        if (!document->m_relativePath.isEmpty() && !pathToId.contains(document->m_relativePath)) {
            pathToId.insert(document->m_relativePath, it.key());
        }
    }

    QStringList newlyAdded;
    for (const DiskEntry &entry : entries) {
        Document *document = m_documents.value(pathToId.value(entry.relativePath));
        if (!document) {
            const QString priorId = identityToId.value(identityKey(entry.device, entry.inode));
            if (!priorId.isEmpty() && !seen.contains(priorId)) {
                document = m_documents.value(priorId);
            }
        }
        if (!document) {
            const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            document = ensureDocument(id, false);
            newlyAdded.append(id);
        }

        seen.insert(document->m_id);
        const bool revisionChanged = !document->m_revision.isEmpty() && document->m_revision != entry.revision;
        const bool pathChanged = document->m_relativePath != entry.relativePath;
        bool stateChanged = pathChanged || document->m_missing || document->m_archived != entry.archived
            || document->m_diskBytes != entry.bytes || document->m_diskModified != entry.modified;
        bool contentChanged = false;
        document->m_relativePath = entry.relativePath;
        document->m_absolutePath = entry.absolutePath;
        document->m_device = entry.device;
        document->m_inode = entry.inode;
        document->m_diskBytes = entry.bytes;
        document->m_diskModified = entry.modified;
        document->m_missing = false;
        document->m_trashed = false;
        document->m_archived = entry.archived;

        if (document->m_dirty && revisionChanged) {
            stateChanged = true;
            document->m_conflict = true;
            document->m_saveError = QStringLiteral("CONFLICT: file changed externally; edits kept and autosave stopped");
            cancelScheduledSave(document->m_id);
        } else if (!document->m_dirty
                   && (revisionChanged || document->m_revision.isEmpty() || document->m_content.isEmpty())) {
            // `m_content.isEmpty()` is load-bearing and is NOT redundant with the two
            // conditions beside it.
            //
            // The index persists `revision` (see readDocumentObject) but never persists
            // `content` -- content is the file's, and the file is the source of truth. So
            // on the FIRST reconcile after a restart every restored document has a
            // non-empty revision that already matches the unchanged file on disk:
            // revisionChanged is false, m_revision is not empty, and the freshly scanned
            // text was therefore dropped on the floor. The document kept an empty
            // m_content until something else forced a revision change.
            //
            // The deck hid this because opening a note goes through paths that refill the
            // buffer. A PINNED note is opened directly from the restored index at startup,
            // so it hit this window exactly: loadNote() returned ok with 0 characters and
            // the pinned window painted an empty editor over a 20-byte file, while the
            // footer read "Unchanged". Measured with a live probe reporting
            // fixtures=0 / bodyText=0 against a file holding "# Alpha\n\nfirst note".
            contentChanged = document->m_content != entry.content;
            stateChanged = true;
            document->m_content = entry.content;
            document->m_revision = entry.revision;
            document->m_conflict = false;
            document->m_saveError.clear();
        }
        if (!revisionChanged || !document->m_dirty) {
            document->m_revision = entry.revision;
        }
        if (stateChanged) {
            markMetadataDirty();
            emitDocumentChanged(document, contentChanged);
        }
    }

    static const QString removedNotice =
        QStringLiteral("CONFLICT: file was removed or moved outside the selected root; buffer kept");
    for (auto it = m_documents.cbegin(); it != m_documents.cend(); ++it) {
        Document *document = it.value();
        if (document->m_trashed || seen.contains(it.key())) {
            continue;
        }
        const bool stateChanged = !document->m_missing || !document->m_conflict
            || document->m_saveError != removedNotice;
        document->m_missing = true;
        document->m_conflict = true;
        document->m_saveError = removedNotice;
        cancelScheduledSave(it.key());
        if (stateChanged) {
            markMetadataDirty();
            emitDocumentChanged(document);
        }
    }

    for (auto it = m_documents.cbegin(); it != m_documents.cend(); ++it) {
        if (!m_recoveryApplied.contains(it.key())) {
            applyRecovery(it.value());
            m_recoveryApplied.insert(it.key());
        }
    }

    const qsizetype rowsBefore = m_catalog.size();
    applyCatalogOrder(sortedCatalog());
    const bool rowsChanged = m_catalog.size() != rowsBefore;
    persistMetadata();
    refreshWatches();
    if (!warnings.isEmpty()) {
        setError(warnings.join(QStringLiteral("; ")));
    } else if (!m_lastError.startsWith(QStringLiteral("CONFLICT"))) {
        setError({});
    }
    m_reconciling = false;
    // Membership is derived, so a reconcile that discovered, lost, archived or restored
    // anything can change the fan — and must never do so by a side effect somewhere else.
    rebuildFan();
    if (rowsChanged || !newlyAdded.isEmpty()) {
        emit documentsChanged();
    }
    for (const QString &id : newlyAdded) {
        emit documentAdded(id);
    }
}

QString DocumentCollection::createNote(const QString &relativeFolder)
{
    if (!m_open) {
        setError(QStringLiteral("No notes folder is open"));
        return {};
    }
    QString folder = QDir::cleanPath(relativeFolder.trimmed());
    if (folder == QStringLiteral(".")) {
        folder.clear();
    }
    if (!folder.isEmpty() && (!confinedRelative(folder) || isArchivedPath(folder))) {
        setError(QStringLiteral("New note folder is outside the selected root or reserved for Archive"));
        return {};
    }
    QString error;
    if (!ensureDirectoryChain(m_rootPath, folder, &error)) {
        setError(error);
        return {};
    }

    for (int suffix = 1; suffix < 100000; ++suffix) {
        const QString fileName = suffix == 1 ? QStringLiteral("Untitled.md")
                                             : QStringLiteral("Untitled %1.md").arg(suffix);
        const QString relative = folder.isEmpty() ? fileName : folder + QLatin1Char('/') + fileName;
        const QString absolute = absoluteFor(relative);
        QFile file(absolute);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            if (QFileInfo::exists(absolute)) {
                continue;
            }
            setError(QStringLiteral("Cannot create note: %1").arg(file.errorString()));
            return {};
        }
        file.close();
        reconcileNow();
        return idForRelativePath(relative);
    }
    setError(QStringLiteral("Cannot find a unique Untitled filename"));
    return {};
}

bool DocumentCollection::updateContent(const QString &id, const QString &content)
{
    Document *document = m_documents.value(id);
    if (!document || document->m_trashed || document->m_missing) {
        setError(QStringLiteral("Unknown, trashed, or missing document"));
        return false;
    }
    if (document->m_content == content && !document->m_recoveryFailed) {
        return true;
    }
    document->m_content = content;
    document->m_dirty = true;
    if (!writeRecovery(document)) {
        document->m_recoveryFailed = true;
        document->m_saveError = QStringLiteral("Recovery write failed: latest edits exist only in memory; keep this window open and copy them before closing");
        emitDocumentChanged(document, true);
        return false;
    }
    document->m_recoveryFailed = false;
    document->m_saveError.clear();
    emitDocumentChanged(document, true);
    if (!document->m_conflict) {
        scheduleSave(document);
    }
    return true;
}

bool DocumentCollection::holdConflictedContent(const QString &id, const QString &content)
{
    Document *document = m_documents.value(id);
    if (!document || document->m_trashed) {
        return false;
    }
    document->m_conflict = true;
    document->m_saveError = QStringLiteral("CONFLICT: file changed externally; edits kept and autosave stopped");
    cancelScheduledSave(id);
    document->m_content = content;
    document->m_dirty = true;
    if (!writeRecovery(document)) {
        document->m_recoveryFailed = true;
        document->m_saveError = QStringLiteral("Recovery write failed: latest edits exist only in memory; keep this window open and copy them before closing");
        emitDocumentChanged(document, true);
        return false;
    }
    document->m_recoveryFailed = false;
    emitDocumentChanged(document, true);
    return true;
}

bool DocumentCollection::saveNow(const QString &id)
{
    Document *document = m_documents.value(id);
    if (!document || !document->m_dirty) {
        return document != nullptr;
    }
    cancelScheduledSave(id);
    if (document->m_recoveryFailed && !writeRecovery(document)) {
        document->m_saveError = QStringLiteral("Recovery write failed: latest edits exist only in memory; keep this window open and copy them before closing");
        emitDocumentChanged(document);
        return false;
    }
    document->m_recoveryFailed = false;
    if (document->m_conflict || document->m_missing || document->m_trashed) {
        document->m_saveError = QStringLiteral("Save refused while the document is conflicted, missing, or trashed");
        emitDocumentChanged(document);
        return false;
    }

    QFile current(document->m_absolutePath);
    if (!current.open(QIODevice::ReadOnly)) {
        document->m_saveError = QStringLiteral("Save refused: cannot read the current file");
        emitDocumentChanged(document);
        return false;
    }
    const QByteArray currentBytes = current.read(maximumDocumentBytes + 1);
    current.close();
    if (currentBytes.size() > maximumDocumentBytes || digest(currentBytes) != document->m_revision
        || !isRegularSingleLink(document->m_absolutePath)) {
        document->m_conflict = true;
        document->m_saveError = QStringLiteral("CONFLICT: disk revision changed before autosave; edits kept");
        emitDocumentChanged(document);
        return false;
    }

    // Only ONE previous generation per note is kept on the library filesystem.
    // Before another exchange, inspect that exact inode: a writable fd to the
    // previous file may have modified it after the last save (including across
    // restarts). The guarantee is bounded: an arbitrary write through an old fd
    // after its one-generation retention window cannot be detected or preserved.
    // Never prune a changed or ambiguous inode; stop and keep the journal instead.
    const QStringList previous = retainedInodes(m_rootPath, id);
    if (previous.size() > 1 || (!previous.isEmpty() && !retainedUnchanged(previous.first(), id))) {
        document->m_conflict = true;
        document->m_saveError = QStringLiteral("CONFLICT: previous displaced inode changed; edits and recovery retained");
        if (!writeRecovery(document)) {
            document->m_recoveryFailed = true;
            document->m_saveError += QStringLiteral("; Recovery write failed: local edits may exist only in memory");
        }
        emitDocumentChanged(document);
        return false;
    }
    QString directoryError;
    if (!ensureDirectoryChain(m_rootPath, QLatin1String(displacedDirectoryName), &directoryError)) {
        document->m_saveError = QStringLiteral("Cannot reserve displaced inode directory: %1").arg(directoryError);
        emitDocumentChanged(document);
        return false;
    }
    // The backup directory itself must survive a crash before we exchange a
    // live inode into it. Refuse the save while the journal still holds edits.
    const QByteArray rootName = QFile::encodeName(m_rootPath);
    const int rootFd = ::open(rootName.constData(), O_RDONLY | O_DIRECTORY);
    const bool directoryDurable = rootFd >= 0 && ::fsync(rootFd) == 0;
    if (rootFd >= 0) ::close(rootFd);
    if (!directoryDurable) {
        document->m_saveError = QStringLiteral("Cannot sync displaced inode directory; edits retained in recovery");
        emitDocumentChanged(document);
        return false;
    }
    const QByteArray output = document->m_content.toUtf8();
    if (output.size() > maximumDocumentBytes) {
        document->m_saveError = QStringLiteral("Save exceeds the 8 MiB document limit");
        emitDocumentChanged(document);
        return false;
    }
    // QSaveFile's final rename unconditionally replaces the destination. Between its
    // last read and commit an external editor can atomically publish a new revision.
    // Exchange the two names instead: the displaced inode remains available for a
    // post-swap comparison, so a racing external revision is never silently discarded.
    QTemporaryFile replacement(document->m_absolutePath + QStringLiteral(".fanfold-save-XXXXXX"));
    if (!replacement.open() || replacement.write(output) != output.size()
        || !replacement.flush() || ::fsync(replacement.handle()) != 0) {
        document->m_saveError = QStringLiteral("Atomic autosave staging failed: %1").arg(replacement.errorString());
        emitDocumentChanged(document);
        return false;
    }
    replacement.setPermissions(current.permissions());
    const QString stagedPath = replacement.fileName();
    QFile finalCheck(document->m_absolutePath);
    if (!finalCheck.open(QIODevice::ReadOnly)
        || digest(finalCheck.read(maximumDocumentBytes + 1)) != document->m_revision
        || !isRegularSingleLink(document->m_absolutePath)) {
        document->m_conflict = true;
        document->m_saveError = QStringLiteral("CONFLICT: disk revision changed during autosave; edits kept");
        emitDocumentChanged(document);
        return false;
    }
    finalCheck.close();
    const QByteArray stagedName = QFile::encodeName(stagedPath);
    const QByteArray destinationName = QFile::encodeName(document->m_absolutePath);
    if (::syscall(SYS_renameat2, AT_FDCWD, stagedName.constData(), AT_FDCWD,
                  destinationName.constData(), RENAME_EXCHANGE) != 0) {
        document->m_saveError = QStringLiteral("Atomic autosave exchange failed: %1")
                                    .arg(QString::fromLocal8Bit(std::strerror(errno)));
        emitDocumentChanged(document);
        return false;
    }
    // The old destination is now at stagedPath, even if it changed in the last
    // instruction before the exchange. Read that exact displaced inode, not a
    // fresh path lookup of the just-written file.
    QFile displaced(stagedPath);
    const bool unchanged = displaced.open(QIODevice::ReadOnly)
        && digest(displaced.read(maximumDocumentBytes + 1)) == document->m_revision
        && isRegularSingleLink(stagedPath);
    displaced.close();
    // The old inode must stay on THIS filesystem. XDG AppData may be on a
    // different device; EXDEV after the exchange would falsely flag every save.
    // Do not replace an existing backup, and never let QTemporaryFile unlink a
    // displaced inode after the exchange, even if a later operation fails.
    replacement.setAutoRemove(false);
    const QString backup = QDir(m_rootPath).filePath(
        QLatin1String(displacedDirectoryName) + QLatin1Char('/') + id + QLatin1Char('-')
        + document->m_revision + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".old"));
    const QByteArray backupName = QFile::encodeName(backup);
    const bool backedUp = isRegularSingleLink(stagedPath)
        && ::syscall(SYS_renameat2, AT_FDCWD, stagedName.constData(), AT_FDCWD,
                     backupName.constData(), RENAME_NOREPLACE) == 0;
    const QString retained = backedUp ? backup : stagedPath;
    // Persist both directory entries before retiring recovery. A failure leaves
    // the journal and both inodes available for manual repair.
    const QByteArray backupDirectory = QFile::encodeName(QFileInfo(backup).path());
    const QByteArray liveDirectory = QFile::encodeName(QFileInfo(document->m_absolutePath).path());
    const int backupFd = ::open(backupDirectory.constData(), O_RDONLY | O_DIRECTORY);
    const int liveFd = ::open(liveDirectory.constData(), O_RDONLY | O_DIRECTORY);
    const bool durable = backupFd >= 0 && liveFd >= 0
        && ::fsync(backupFd) == 0 && ::fsync(liveFd) == 0;
    if (backupFd >= 0) ::close(backupFd);
    if (liveFd >= 0) ::close(liveFd);
    if (!unchanged || !backedUp || !durable
        || (!previous.isEmpty() && !retainedUnchanged(previous.first(), id))) {
        document->m_conflict = true;
        document->m_saveError = QStringLiteral("CONFLICT: displaced inode retained at %1; inspect it for external writes")
                                    .arg(retained);
        if (!writeRecovery(document)) {
            document->m_recoveryFailed = true;
            document->m_saveError += QStringLiteral("; Recovery write failed: local edits may exist only in memory");
        }
        emitDocumentChanged(document);
        return false;
    }
    // The previous generation is now outside the promised retention window.
    // Verify again just before pruning. A concurrent write *after* that final
    // check cannot be guaranteed; users must not rely on arbitrary-late writes.
    if (!previous.isEmpty() && !QFile::remove(previous.first())) {
        document->m_conflict = true;
        document->m_saveError = QStringLiteral("CONFLICT: previous inode could not be retired; recovery retained");
        emitDocumentChanged(document);
        return false;
    }

    document->m_revision = digest(output);
    updateDiskStat(document);
    document->m_dirty = false;
    document->m_conflict = false;
    document->m_saveError.clear();
    clearRecovery(id);
    markMetadataDirty();
    persistMetadata();
    refreshWatches();
    emitDocumentChanged(document);
    return true;
}

bool DocumentCollection::flushPendingSaves()
{
    return flushPendingSavesExcept(QString());
}

bool DocumentCollection::flushPendingSavesExcept(const QString &excludedId)
{
    bool success = true;
    QString failure;
    const QStringList ids = m_catalog;
    for (const QString &id : ids) {
        if (id == excludedId) {
            continue;
        }
        Document *document = m_documents.value(id);
        if (document && document->m_dirty && !saveNow(id)) {
            success = false;
            if (failure.isEmpty()) {
                failure = document->m_saveError.isEmpty()
                    ? QStringLiteral("Unable to commit pending Markdown") : document->m_saveError;
            }
        }
    }
    setError(failure);
    return success;
}

bool DocumentCollection::discardSelectedAfterFlushingOthers(const QString &selectedId)
{
    Document *selected = m_documents.value(selectedId);
    if (!selected || selected->m_trashed || selected->m_missing) {
        setError(QStringLiteral("Discard refused: selected document unavailable"));
        return false;
    }
    if (!flushPendingSavesExcept(selectedId)) return false;
    const QString recovery = recoveryPath(selectedId);
    if (QFile::exists(recovery) && !QFile::remove(recovery)) {
        setError(QStringLiteral("Discard refused: unable to remove selected recovery"));
        return false;
    }
    cancelScheduledSave(selectedId);
    selected->m_dirty = false;
    selected->m_recoveryFailed = false;
    return true;
}

bool DocumentCollection::renameDocument(const QString &id, const QString &title)
{
    Document *document = m_documents.value(id);
    const QString fileName = safeTitleFileName(title);
    if (!document || fileName.isEmpty() || document->m_trashed || document->m_missing) {
        setError(QStringLiteral("Rename refused: invalid title or unavailable document"));
        return false;
    }
    if (document->m_dirty && !saveNow(id)) {
        return false;
    }
    const QString folder = document->folder();
    const QString target = folder.isEmpty() ? fileName : folder + QLatin1Char('/') + fileName;
    return moveInsideRoot(document, target, document->m_archived, document->m_restorePath);
}

bool DocumentCollection::archive(const QString &id)
{
    Document *document = m_documents.value(id);
    if (!document || document->m_archived || document->m_trashed || document->m_missing) {
        return false;
    }
    const QString original = document->m_relativePath;
    if (!moveInsideRoot(document, QLatin1String(archiveFolderName) + QLatin1Char('/') + original,
                        true, original)) {
        return false;
    }
    // "Archived means not on the edge" in every folder: moveInsideRoot has already changed
    // the note's folder to Archive/..., so the derivation drops it on its own.
    rebuildFan();
    return true;
}

bool DocumentCollection::restoreArchive(const QString &id)
{
    Document *document = m_documents.value(id);
    if (!document || !document->m_archived || document->m_restorePath.isEmpty()
        || document->m_trashed || document->m_missing) {
        setError(QStringLiteral("Restore refused: the note is not an archived, available note"));
        return false;
    }
    const QString desired = document->m_restorePath;
    const QString folder = QFileInfo(desired).path() == QStringLiteral(".")
        ? QString()
        : QFileInfo(desired).path();
    const QString target = uniqueTargetIn(folder, QFileInfo(desired).fileName());
    if (target.isEmpty()) {
        setError(QStringLiteral("Restore refused: no free name is available in the original folder"));
        return false;
    }
    const bool renamed = target != desired;
    if (!moveInsideRoot(document, target, false, {})) {
        return false;
    }
    emit documentRestored(id, document->m_relativePath, renamed);
    return true;
}

bool DocumentCollection::moveToTrash(const QString &id)
{
    Document *document = m_documents.value(id);
    if (!document || document->m_trashed || document->m_missing) {
        return false;
    }
    if (document->m_dirty && !saveNow(id)) {
        return false;
    }
    const QString original = document->m_relativePath;
    QString pathInTrash;
    if (!QFile::moveToTrash(document->m_absolutePath, &pathInTrash)) {
        document->m_saveError = QStringLiteral("Desktop Trash refused the file; nothing was deleted");
        emitDocumentChanged(document);
        return false;
    }
    document->m_restorePath = original;
    document->m_trashPath = pathInTrash;
    document->m_trashed = true;
    document->m_missing = false;
    document->m_absolutePath.clear();
    // A trashed note has no next autosave. Remove only a verified unchanged
    // previous generation; changed evidence is never destroyed by cleanup.
    for (const QString &path : retainedInodes(m_rootPath, id)) {
        if (retainedUnchanged(path, id)) QFile::remove(path);
    }
    document->m_saveError = pathInTrash.isEmpty()
        ? QStringLiteral("Moved to desktop Trash, but this platform did not expose a restorable path")
        : QString();
    rebuildFan();
    markMetadataDirty();
    persistMetadata();
    refreshWatches();
    emitDocumentChanged(document);
    return true;
}

bool DocumentCollection::restoreFromTrash(const QString &id)
{
    Document *document = m_documents.value(id);
    if (!document || !document->m_trashed || document->m_trashPath.isEmpty()
        || document->m_restorePath.isEmpty() || !QFileInfo::exists(document->m_trashPath)) {
        return false;
    }
    const QString desired = document->m_restorePath;
    const QString folder = QFileInfo(desired).path() == QStringLiteral(".")
        ? QString()
        : QFileInfo(desired).path();
    const QString relative = uniqueTargetIn(folder, QFileInfo(desired).fileName());
    if (relative.isEmpty()) {
        setError(QStringLiteral("Restore refused: no free name is available in the original folder"));
        return false;
    }
    const QString target = absoluteFor(relative);
    QString error;
    if (!ensureDirectoryChain(m_rootPath, folder, &error)
        || !renameNoReplace(document->m_trashPath, target, &error)) {
        setError(error);
        return false;
    }
    document->m_relativePath = relative;
    document->m_absolutePath = target;
    document->m_trashPath.clear();
    document->m_restorePath.clear();
    document->m_trashed = false;
    document->m_missing = false;
    document->m_archived = isArchivedPath(relative);
    updateDiskStat(document);
    markMetadataDirty();
    persistMetadata();
    reconcileNow();
    emitDocumentChanged(document);
    emit documentRestored(id, relative, relative != desired);
    return true;
}

QString DocumentCollection::normalizedFolder(const QString &folder)
{
    const QString clean = QDir::cleanPath(folder.trimmed());
    if (clean.isEmpty() || clean == QStringLiteral(".") || clean == QStringLiteral("/")) {
        return {};
    }
    return clean;
}

/** The whole membership rule, in one place.
 *
 * `on the fan = in the open folder AND NOT (archived | trashed | pinned)`, plus the ghost
 * rule the shell has always applied: a note whose file has gone AND which holds no
 * unsaved buffer has nothing to render, while a missing DIRTY note stays — dropping it
 * would be the one way to actually lose the user's work.
 */
bool DocumentCollection::belongsOnFan(const Document *document) const
{
    if (!document || document->m_trashed || document->m_archived || document->m_pinned) {
        return false;
    }
    if (document->m_missing && !document->m_dirty) {
        return false;
    }
    return document->folder() == m_openFolder;
}

/** Re-derive the fan from the open folder. The ONE place m_fan is ever assigned.
 *
 * Order comes from this folder's persisted arrangement; anything the arrangement does not
 * mention is appended in catalog (path) order, so a newly discovered note lands
 * predictably at the end rather than somewhere arbitrary.
 */
bool DocumentCollection::rebuildFan()
{
    QStringList next;
    const QStringList arranged = m_folderOrder.value(m_openFolder);
    for (const QString &id : arranged) {
        if (belongsOnFan(m_documents.value(id)) && !next.contains(id)) {
            next.append(id);
        }
    }
    for (const QString &id : std::as_const(m_catalog)) {
        if (belongsOnFan(m_documents.value(id)) && !next.contains(id)) {
            next.append(id);
        }
    }
    if (next == m_fan) {
        return false;
    }

    // Documents whose membership actually flipped are the only ones that need to tell the
    // UI about it; re-emitting for the whole fan on every reconcile is the churn the
    // engine's "no signals when nothing changed" contract exists to prevent.
    const QSet<QString> before(m_fan.cbegin(), m_fan.cend());
    const QSet<QString> after(next.cbegin(), next.cend());
    m_fan = next;
    QSet<QString> touched = before;
    touched.unite(after);
    for (const QString &id : std::as_const(touched)) {
        if (before.contains(id) == after.contains(id)) {
            continue;
        }
        if (Document *document = m_documents.value(id)) {
            document->m_inFan = after.contains(id);
            emitDocumentChanged(document);
        }
    }
    emit fanChanged();
    return true;
}

bool DocumentCollection::setOpenFolder(const QString &folder)
{
    if (!m_open) {
        setError(QStringLiteral("No notes folder is open"));
        return false;
    }
    const QString wanted = normalizedFolder(folder);
    if (!wanted.isEmpty() && (!confinedRelative(wanted) || isArchivedPath(wanted))) {
        setError(QStringLiteral("That folder is outside the selected root or reserved for Archive"));
        return false;
    }
    // An EMPTY folder is a legitimate scope — the fan is simply empty and the + still
    // creates there. A folder that is not on disk is not: scoping to it would show
    // nothing for a reason the user cannot see.
    if (!wanted.isEmpty() && !QFileInfo(absoluteFor(wanted)).isDir()) {
        setError(QStringLiteral("That folder no longer exists"));
        return false;
    }
    if (wanted == m_openFolder) {
        return true;
    }
    m_openFolder = wanted;
    markMetadataDirty();
    const bool ok = persistMetadata();
    emit openFolderChanged();
    rebuildFan();
    return ok;
}

bool DocumentCollection::setFanOrder(const QStringList &ids)
{
    QStringList next;
    for (const QString &id : ids) {
        if (m_fan.contains(id) && !next.contains(id)) {
            next.append(id);
        }
    }
    // The guard is scoped to the OPEN FOLDER's fan, not to the whole library: under
    // folder scoping a drag can only ever rearrange the notes currently on the edge.
    if (next.size() != m_fan.size()) {
        setError(QStringLiteral("Fan order must be a permutation of the open folder's fan"));
        return false;
    }
    // An explicit no-op drag must still establish a stored order. In particular a
    // newly discovered external note can make the path-based fan equal to the drag.
    const bool orderChanged = next != m_fan;
    m_fan = next;

    // Persisted per-folder order keeps the ids it already held that are NOT on the fan
    // right now (archived, pinned, or momentarily missing), so unpinning or restoring a
    // note returns it to the slot the user gave it rather than to the end.
    QStringList stored = next;
    for (const QString &id : m_folderOrder.value(m_openFolder)) {
        if (!stored.contains(id) && m_documents.contains(id)) {
            stored.append(id);
        }
    }
    m_folderOrder.insert(m_openFolder, stored);
    markMetadataDirty();
    const bool ok = persistMetadata();
    if (orderChanged) emit fanChanged();
    return ok;
}

bool DocumentCollection::setPinned(const QString &id, bool pinned)
{
    Document *document = m_documents.value(id);
    if (!document || document->m_pinned == pinned) {
        return document != nullptr;
    }
    document->m_pinned = pinned;
    markMetadataDirty();
    const bool ok = persistMetadata();
    emitDocumentChanged(document);
    // Pin removes the note from the fan while pinned; unpin returns it to the slot this
    // folder's persisted order still holds for it.
    rebuildFan();
    return ok;
}

bool DocumentCollection::setPinnedWindowSize(const QString &id, int width, int height)
{
    Document *document = m_documents.value(id);
    if (!document) {
        return false;
    }
    if (width < 1 || height < 1 || width > 16384 || height > 16384) {
        setError(QStringLiteral("Pinned window size is outside supported bounds"));
        return false;
    }
    if (document->m_pinnedWindowWidth == width && document->m_pinnedWindowHeight == height) {
        return true;
    }
    document->m_pinnedWindowWidth = width;
    document->m_pinnedWindowHeight = height;
    markMetadataDirty();
    const bool ok = persistMetadata();
    emitDocumentChanged(document);
    return ok;
}

bool DocumentCollection::setPaper(const QString &id, const QString &paper)
{
    Document *document = m_documents.value(id);
    const QString normalized = normalizedColor(paper);
    if (!document || normalized.isEmpty()) {
        return false;
    }
    if (document->m_paper == normalized) {
        return true;
    }
    document->m_paper = normalized;
    markMetadataDirty();
    const bool ok = persistMetadata();
    emitDocumentChanged(document);
    return ok;
}

/**
 * Set or clear a note's tab icon.
 * @param relative Path under the library root; empty clears the icon.
 * @return false if the id is unknown or the path escapes the library.
 */
bool DocumentCollection::setIcon(const QString &id, const QString &relative)
{
    Document *document = m_documents.value(id);
    if (!document) {
        return false;
    }
    // Two kinds of icon: a "theme:<name>" reference to the system icon theme (no file,
    // nothing to confine) and a path under the library. Only the latter is a filesystem
    // reference, so only the latter is confinement-checked.
    const QString trimmed = relative.trimmed();
    QString cleaned;
    if (trimmed.startsWith(QStringLiteral("theme:"))) {
        const QString name = trimmed.mid(6);
        // A theme name is a single freedesktop token; anything with a separator is not one.
        if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
            return false;
        }
        cleaned = trimmed;
    } else if (!trimmed.isEmpty()) {
        cleaned = QDir::cleanPath(trimmed);
        if (!confinedRelative(cleaned)) {
            return false;
        }
    }
    if (document->m_icon == cleaned) {
        return true;
    }
    document->m_icon = cleaned;
    markMetadataDirty();
    const bool ok = persistMetadata();
    emitDocumentChanged(document);
    return ok;
}

bool DocumentCollection::setInk(const QString &id, const QString &ink)
{
    Document *document = m_documents.value(id);
    const QString normalized = normalizedColor(ink, true);
    if (!document || normalized.isEmpty()) {
        return false;
    }
    if (document->m_ink == normalized) {
        return true;
    }
    document->m_ink = normalized;
    markMetadataDirty();
    const bool ok = persistMetadata();
    emitDocumentChanged(document);
    return ok;
}

bool DocumentCollection::setNoteFont(const QString &id, const QString &family, int size)
{
    Document *document = m_documents.value(id);
    if (!document) {
        return false;
    }
    if (!family.isEmpty() && !AppearanceSettings::safeFamily(family)) {
        return false;
    }
    if (size != 0) {
        double low = 0;
        double high = 0;
        if (!AppearanceSettings::numericRange(QStringLiteral("fontSize"), &low, &high)
            || size < low || size > high) {
            return false;
        }
    }
    if (document->m_fontFamily == family && document->m_fontSize == size) {
        return true;
    }
    document->m_fontFamily = family;
    document->m_fontSize = size;
    markMetadataDirty();
    const bool ok = persistMetadata();
    emitDocumentChanged(document);
    return ok;
}

QStringList DocumentCollection::liveIdsInFolder(const QString &folder) const
{
    const QString wanted = normalizedFolder(folder);
    QStringList out;
    for (const QString &id : m_catalog) {
        const Document *document = m_documents.value(id);
        if (!document || document->m_trashed || document->m_missing || document->m_archived) {
            continue;
        }
        if (document->folder() == wanted) {
            out.append(id);
        }
    }
    return out;
}

int DocumentCollection::setColourForFolder(const QString &folder, const QString &value, bool ink)
{
    // Validate first: a refused value must leave every note exactly as it was.
    const QString normalized = normalizedColor(value, ink);
    if (!m_open || normalized.isEmpty()) {
        return -1;
    }
    const QStringList ids = liveIdsInFolder(folder);
    QList<Document *> touched;
    for (const QString &id : ids) {
        Document *document = m_documents.value(id);
        QString &slot = ink ? document->m_ink : document->m_paper;
        if (slot != normalized) {
            slot = normalized;
            touched.append(document);
        }
    }
    bool ok = true;
    if (!touched.isEmpty()) {
        markMetadataDirty();
        ok = persistMetadata();
        for (Document *document : std::as_const(touched)) {
            emitDocumentChanged(document);
        }
    }
    return ok ? int(ids.size()) : -1;
}

int DocumentCollection::setPaperForFolder(const QString &folder, const QString &paper)
{
    return setColourForFolder(folder, paper, false);
}

int DocumentCollection::setInkForFolder(const QString &folder, const QString &ink)
{
    return setColourForFolder(folder, ink, true);
}

bool DocumentCollection::hasRecovery(const QString &id) const
{
    return QFileInfo::exists(recoveryPath(id));
}

bool DocumentCollection::watchHealthy() const
{
    if (!m_open || !m_watcher.directories().contains(m_rootPath)) {
        return false;
    }
    for (Document *document : m_documents) {
        if (!document->m_missing && !document->m_trashed && QFileInfo::exists(document->m_absolutePath)
            && !m_watcher.files().contains(document->m_absolutePath)) {
            return false;
        }
    }
    return true;
}

void DocumentCollection::setError(const QString &message)
{
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    emit errorChanged();
}

QString DocumentCollection::indexPath() const
{
    return QDir(m_libraryState).filePath(QStringLiteral("index.json"));
}

QString DocumentCollection::recoveryPath(const QString &id) const
{
    return QDir(m_libraryState).filePath(QStringLiteral("recovery/%1.json").arg(id));
}

QString DocumentCollection::absoluteFor(const QString &relativePath) const
{
    return QDir(m_rootPath).filePath(QDir::cleanPath(relativePath));
}

bool DocumentCollection::confinedRelative(const QString &relativePath) const
{
    const QString clean = QDir::cleanPath(relativePath);
    return !clean.isEmpty() && clean != QStringLiteral(".") && !QDir::isAbsolutePath(clean)
        && clean != QStringLiteral("..") && !clean.startsWith(QStringLiteral("../"));
}

/** First free `folder/fileName`, else `folder/name (restored N).md`.
 * @return a root-relative path, or an empty string when no free name exists.
 */
QString DocumentCollection::uniqueTargetIn(const QString &folder, const QString &fileName) const
{
    const auto join = [&folder](const QString &name) {
        return folder.isEmpty() ? name : folder + QLatin1Char('/') + name;
    };
    if (fileName.isEmpty()) {
        return {};
    }
    const QString direct = join(fileName);
    if (confinedRelative(direct) && !QFileInfo::exists(absoluteFor(direct))) {
        return direct;
    }
    const QString base = fileName.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)
        ? fileName.chopped(3)
        : fileName;
    for (int suffix = 1; suffix < 100000; ++suffix) {
        const QString candidate = suffix == 1
            ? QStringLiteral("%1 (restored).md").arg(base)
            : QStringLiteral("%1 (restored %2).md").arg(base).arg(suffix);
        const QString relative = join(candidate);
        if (confinedRelative(relative) && !QFileInfo::exists(absoluteFor(relative))) {
            return relative;
        }
    }
    return {};
}

QList<DocumentCollection::DiskEntry> DocumentCollection::scan(QStringList *warnings) const
{
    QList<DiskEntry> entries;
    QDirIterator iterator(m_rootPath, QDir::AllEntries | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink() || info.isDir()) {
            continue;
        }
        const QString relative = QDir(m_rootPath).relativeFilePath(absolute);
        if (relative.startsWith(QLatin1String(displacedDirectoryName) + QLatin1Char('/'))
            || !confinedRelative(relative) || !relative.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
            continue;
        }
        struct stat metadata {};
        if (!isRegularSingleLink(absolute, &metadata)) {
            if (warnings) warnings->append(QStringLiteral("Refused linked or non-regular Markdown file: %1").arg(relative));
            continue;
        }
        if (metadata.st_size > maximumDocumentBytes) {
            if (warnings) warnings->append(QStringLiteral("Refused oversized Markdown file: %1").arg(relative));
            continue;
        }
        QFile file(absolute);
        if (!file.open(QIODevice::ReadOnly)) {
            if (warnings) warnings->append(QStringLiteral("Cannot read Markdown file: %1").arg(relative));
            continue;
        }
        const QByteArray bytes = file.read(maximumDocumentBytes + 1);
        QString text;
        if (bytes.size() > maximumDocumentBytes || !decodeUtf8(bytes, &text)) {
            if (warnings) warnings->append(QStringLiteral("Refused invalid UTF-8 Markdown file: %1").arg(relative));
            continue;
        }
        DiskEntry entry;
        entry.relativePath = relative;
        entry.absolutePath = absolute;
        entry.content = text;
        entry.revision = digest(bytes);
        entry.modified = QDateTime::fromSecsSinceEpoch(qint64(metadata.st_mtime));
        entry.bytes = qint64(metadata.st_size);
        entry.device = quint64(metadata.st_dev);
        entry.inode = quint64(metadata.st_ino);
        entry.archived = isArchivedPath(relative);
        entries.append(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const DiskEntry &left, const DiskEntry &right) {
        return left.relativePath.compare(right.relativePath, Qt::CaseInsensitive) < 0;
    });
    return entries;
}

QString DocumentCollection::digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool DocumentCollection::decodeUtf8(const QByteArray &bytes, QString *text)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder(bytes);
    if (decoder.hasError()) {
        return false;
    }
    if (text) {
        *text = decoded;
    }
    return true;
}

bool DocumentCollection::fileIdentity(const QString &path, quint64 *device, quint64 *inode,
                                      QDateTime *modified, qint64 *bytes)
{
    struct stat metadata {};
    if (!isRegularSingleLink(path, &metadata)) {
        return false;
    }
    if (device) *device = quint64(metadata.st_dev);
    if (inode) *inode = quint64(metadata.st_ino);
    if (modified) *modified = QDateTime::fromSecsSinceEpoch(qint64(metadata.st_mtime));
    if (bytes) *bytes = qint64(metadata.st_size);
    return true;
}

/** Refresh the identity and stat fields the information panel reports. */
void DocumentCollection::updateDiskStat(Document *document)
{
    if (!document || document->m_absolutePath.isEmpty()) {
        return;
    }
    fileIdentity(document->m_absolutePath, &document->m_device, &document->m_inode,
                 &document->m_diskModified, &document->m_diskBytes);
}

QString DocumentCollection::identityKey(quint64 device, quint64 inode)
{
    return QStringLiteral("%1:%2").arg(device).arg(inode);
}

QString DocumentCollection::normalizedColor(const QString &value, bool allowAuto)
{
    const QString lowered = value.trimmed().toLower();
    if (allowAuto && lowered == QStringLiteral("auto")) {
        return lowered;
    }
    if (lowered.size() != 4 && lowered.size() != 7) {
        return {};
    }
    if (!lowered.startsWith(QLatin1Char('#'))) {
        return {};
    }
    for (qsizetype index = 1; index < lowered.size(); ++index) {
        const QChar character = lowered.at(index);
        if (!character.isDigit() && !(character >= QLatin1Char('a') && character <= QLatin1Char('f'))) {
            return {};
        }
    }
    if (lowered.size() == 4) {
        return QStringLiteral("#%1%1%2%2%3%3").arg(lowered.at(1)).arg(lowered.at(2)).arg(lowered.at(3));
    }
    return lowered;
}

QString DocumentCollection::safeTitleFileName(const QString &title)
{
    QString clean = title.trimmed();
    if (clean.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
        clean.chop(3);
        clean = clean.trimmed();
    }
    if (clean.isEmpty() || clean.startsWith(QLatin1Char('.'))) {
        return {};
    }
    for (const QChar &character : clean) {
        if (character == QLatin1Char('/') || character == QLatin1Char('\\')
            || character.unicode() < 0x20 || character.unicode() == 0x7f) {
            return {};
        }
    }
    const QString result = clean + QStringLiteral(".md");
    return result.toUtf8().size() <= 180 ? result : QString();
}

bool DocumentCollection::renameNoReplace(const QString &source, const QString &target, QString *error)
{
    const QByteArray from = QFile::encodeName(source);
    const QByteArray to = QFile::encodeName(target);
#ifdef SYS_renameat2
    if (::syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_NOREPLACE) == 0) {
        return true;
    }
    if (errno != ENOSYS && errno != EINVAL && errno != EPERM) {
        if (error) *error = QStringLiteral("Move refused: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
#endif
    if (QFileInfo::exists(target) || !QFile::rename(source, target)) {
        if (error) *error = QStringLiteral("Move refused because the destination exists or the filesystem rejected it");
        return false;
    }
    return true;
}

bool DocumentCollection::lessThanByPath(const QString &left, const QString &right)
{
    return left.compare(right, Qt::CaseInsensitive) < 0;
}

/** The catalog order: every known document sorted by relative path, ID as tie-break. */
QStringList DocumentCollection::sortedCatalog() const
{
    QStringList ids = m_documents.keys();
    std::sort(ids.begin(), ids.end(), [this](const QString &left, const QString &right) {
        const QString leftPath = m_documents.value(left)->m_relativePath;
        const QString rightPath = m_documents.value(right)->m_relativePath;
        const int order = leftPath.compare(rightPath, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : left < right;
    });
    return ids;
}

/** Bring the model rows to `desired` using the fewest structural signals.
 *
 * A reconciliation that discovers nothing new emits nothing at all; a rename that changes
 * one note's sort key emits one rowsMoved. The final reset is an unreachable safety net
 * that keeps the model consistent even if the incremental path ever disagreed.
 */
void DocumentCollection::applyCatalogOrder(const QStringList &desired)
{
    if (m_catalog == desired) {
        return;
    }
    if (m_bulkLoad) {
        beginResetModel();
        m_catalog = desired;
        endResetModel();
        return;
    }
    QHash<QString, int> want;
    want.reserve(int(desired.size()));
    for (int index = 0; index < desired.size(); ++index) {
        want.insert(desired.at(index), index);
    }

    for (int row = int(m_catalog.size()) - 1; row >= 0; --row) {
        if (!want.contains(m_catalog.at(row))) {
            beginRemoveRows({}, row, row);
            m_catalog.removeAt(row);
            endRemoveRows();
        }
    }

    for (const QString &id : desired) {
        if (m_catalog.contains(id)) {
            continue;
        }
        int at = 0;
        while (at < m_catalog.size() && want.value(m_catalog.at(at)) < want.value(id)) {
            ++at;
        }
        beginInsertRows({}, at, at);
        m_catalog.insert(at, id);
        endInsertRows();
    }

    QList<int> positions;
    positions.reserve(int(m_catalog.size()));
    for (const QString &id : std::as_const(m_catalog)) {
        positions.append(want.value(id));
    }
    const QSet<int> keep = longestIncreasingIndices(positions);
    QStringList movers;
    for (int row = 0; row < m_catalog.size(); ++row) {
        if (!keep.contains(row)) {
            movers.append(m_catalog.at(row));
        }
    }
    std::sort(movers.begin(), movers.end(), [&want](const QString &left, const QString &right) {
        return want.value(left) < want.value(right);
    });

    QSet<QString> settled;
    for (int row = 0; row < m_catalog.size(); ++row) {
        if (keep.contains(row)) {
            settled.insert(m_catalog.at(row));
        }
    }
    for (const QString &id : std::as_const(movers)) {
        const int from = int(m_catalog.indexOf(id));
        int anchor = -1;
        for (int row = 0; row < m_catalog.size(); ++row) {
            const QString &other = m_catalog.at(row);
            if (other == id || !settled.contains(other)) {
                continue;
            }
            if (want.value(other) < want.value(id)) {
                anchor = row;
            }
        }
        const int to = anchor < 0 ? 0 : (anchor > from ? anchor : anchor + 1);
        settled.insert(id);
        if (from == to) {
            continue;
        }
        beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
        m_catalog.move(from, to);
        endMoveRows();
    }

    if (m_catalog != desired) {
        beginResetModel();
        m_catalog = desired;
        endResetModel();
    }
}

void DocumentCollection::markMetadataDirty()
{
    m_metadataDirty = true;
}

/** Write the XDG index, but only when something in it actually changed.
 *
 * A resident application reconciles twice a second; persisting unconditionally would
 * rewrite this file forever and defeat the "no churn" contract the UI relies on.
 */
bool DocumentCollection::persistMetadata()
{
    if (m_libraryState.isEmpty() || !m_metadataDirty) {
        return !m_libraryState.isEmpty();
    }
    QJsonObject documents;
    for (auto it = m_documents.cbegin(); it != m_documents.cend(); ++it) {
        documents.insert(it.key(), metadataFor(it.value()));
    }
    m_metadata = QJsonObject{{QStringLiteral("version"), kIndexVersion},
                             {QStringLiteral("root"), m_rootPath},
                             {QStringLiteral("documents"), documents},
                             {QStringLiteral("folderOrder"), storedFolderOrder()},
                             {QStringLiteral("openFolder"), m_openFolder}};
    const QByteArray bytes = QJsonDocument(m_metadata).toJson();
    QSaveFile file(indexPath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(QStringLiteral("Cannot atomically persist XDG document metadata"));
        return false;
    }
    m_metadataDirty = false;
    return true;
}

/** The per-folder order as JSON, pruned to ids the library still holds.
 *
 * Written with folder keys sorted so two runs that changed nothing produce byte-identical
 * files — the "no churn" contract covers this index, not just the model signals.
 */
QJsonObject DocumentCollection::storedFolderOrder() const
{
    QStringList folders = m_folderOrder.keys();
    std::sort(folders.begin(), folders.end());
    QJsonObject out;
    for (const QString &folder : std::as_const(folders)) {
        QJsonArray ids;
        for (const QString &id : m_folderOrder.value(folder)) {
            if (m_documents.contains(id)) {
                ids.append(id);
            }
        }
        if (!ids.isEmpty()) {
            out.insert(folder, ids);
        }
    }
    return out;
}

QJsonObject DocumentCollection::metadataFor(const Document *document) const
{
    return {{QStringLiteral("path"), document->m_relativePath},
            {QStringLiteral("restorePath"), document->m_restorePath},
            {QStringLiteral("trashPath"), document->m_trashPath},
            {QStringLiteral("revision"), document->m_revision},
            {QStringLiteral("device"), QString::number(document->m_device)},
            {QStringLiteral("inode"), QString::number(document->m_inode)},
            {QStringLiteral("archived"), document->m_archived},
            {QStringLiteral("trashed"), document->m_trashed},
            {QStringLiteral("pinned"), document->m_pinned},
            {QStringLiteral("pinnedWindowWidth"), document->m_pinnedWindowWidth},
            {QStringLiteral("pinnedWindowHeight"), document->m_pinnedWindowHeight},
            {QStringLiteral("paper"), document->m_paper},
            {QStringLiteral("ink"), document->m_ink},
            {QStringLiteral("fontFamily"), document->m_fontFamily},
            {QStringLiteral("fontSize"), document->m_fontSize},
            {QStringLiteral("icon"), document->m_icon}};
}

void DocumentCollection::applyMetadata(Document *document, const QJsonObject &object)
{
    const QString path = QDir::cleanPath(object.value(QStringLiteral("path")).toString());
    if (confinedRelative(path)) {
        document->m_relativePath = path;
        document->m_absolutePath = absoluteFor(path);
    }
    const QString restore = QDir::cleanPath(object.value(QStringLiteral("restorePath")).toString());
    if (confinedRelative(restore)) document->m_restorePath = restore;
    document->m_trashPath = object.value(QStringLiteral("trashPath")).toString();
    document->m_revision = object.value(QStringLiteral("revision")).toString();
    document->m_device = object.value(QStringLiteral("device")).toString().toULongLong();
    document->m_inode = object.value(QStringLiteral("inode")).toString().toULongLong();
    document->m_archived = object.value(QStringLiteral("archived")).toBool();
    document->m_trashed = object.value(QStringLiteral("trashed")).toBool();
    document->m_pinned = object.value(QStringLiteral("pinned")).toBool();
    const int pinnedWindowWidth = object.value(QStringLiteral("pinnedWindowWidth")).toInt();
    const int pinnedWindowHeight = object.value(QStringLiteral("pinnedWindowHeight")).toInt();
    if (pinnedWindowWidth > 0 && pinnedWindowWidth <= 16384) {
        document->m_pinnedWindowWidth = pinnedWindowWidth;
    }
    if (pinnedWindowHeight > 0 && pinnedWindowHeight <= 16384) {
        document->m_pinnedWindowHeight = pinnedWindowHeight;
    }
    const QString paper = normalizedColor(object.value(QStringLiteral("paper")).toString());
    const QString ink = normalizedColor(object.value(QStringLiteral("ink")).toString(), true);
    if (!paper.isEmpty()) document->m_paper = paper;
    if (!ink.isEmpty()) document->m_ink = ink;
    // Typography override: validated exactly as setNoteFont() validates, so a hand-edited
    // index cannot smuggle an unsafe family into CSS or an illegible size into a note.
    // Anything refused simply follows the global setting.
    const QString family = object.value(QStringLiteral("fontFamily")).toString();
    document->m_fontFamily = AppearanceSettings::safeFamily(family) ? family : QString();
    const int size = object.value(QStringLiteral("fontSize")).toInt();
    double low = 0;
    double high = 0;
    document->m_fontSize =
        AppearanceSettings::numericRange(QStringLiteral("fontSize"), &low, &high) && size >= low
            && size <= high
        ? size
        : 0;
    // Confined to the library, like every other stored path: a metadata file that has
    // been hand-edited (or synced from elsewhere) must not be able to point the icon at
    // an arbitrary file on disk.
    const QString storedIcon = object.value(QStringLiteral("icon")).toString();
    if (storedIcon.startsWith(QStringLiteral("theme:"))) {
        document->m_icon = storedIcon;
    } else {
        const QString icon = QDir::cleanPath(storedIcon);
        if (!icon.isEmpty() && confinedRelative(icon)) document->m_icon = icon;
    }
}

/** Return the one canonical Document for `id`, creating it on first sight.
 * @param appendToCatalog Append the new row directly; reconciliation instead lets
 * applyCatalogOrder() place it so the path ordering stays authoritative.
 */
Document *DocumentCollection::ensureDocument(const QString &id, bool appendToCatalog)
{
    if (Document *existing = m_documents.value(id)) {
        return existing;
    }
    auto *document = new Document(id, this);
    auto *timer = new QTimer(document);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(kAutosaveQuietPeriodMs);
    connect(timer, &QTimer::timeout, this, [this, id] { saveNow(id); });
    m_documents.insert(id, document);
    m_saveTimers.insert(id, timer);
    if (appendToCatalog) {
        const int row = int(m_catalog.size());
        beginInsertRows({}, row, row);
        m_catalog.append(id);
        endInsertRows();
    }
    markMetadataDirty();
    return document;
}

void DocumentCollection::emitDocumentChanged(Document *document, bool contentChanged)
{
    if (!document) return;
    if (contentChanged) emit document->contentChanged();
    emit document->changed();
    const int row = rowForId(document->m_id);
    if (row >= 0) emit dataChanged(index(row), index(row));
    emit documentChanged(document->m_id);
}

int DocumentCollection::rowForId(const QString &id) const
{
    return int(m_catalog.indexOf(id));
}

bool DocumentCollection::writeRecovery(Document *document)
{
    const QJsonObject object{{QStringLiteral("version"), 1}, {QStringLiteral("id"), document->m_id},
                             {QStringLiteral("path"), document->m_relativePath},
                             {QStringLiteral("baseRevision"), document->m_revision},
                             {QStringLiteral("conflict"), document->m_conflict},
                             {QStringLiteral("content"), document->m_content},
                             {QStringLiteral("updated"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    const QByteArray bytes = QJsonDocument(object).toJson();
    QSaveFile file(recoveryPath(document->m_id));
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

void DocumentCollection::applyRecovery(Document *document)
{
    if (!document || !hasRecovery(document->m_id)) return;
    QFile file(recoveryPath(document->m_id));
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumDocumentBytes * 2) {
        document->m_saveError = QStringLiteral("Recovery record is unreadable; it was retained for inspection");
        return;
    }
    QJsonParseError error {};
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject()) {
        document->m_saveError = QStringLiteral("Recovery record is malformed; it was retained for inspection");
        return;
    }
    const QJsonObject object = parsed.object();
    if (object.value(QStringLiteral("version")).toInt() != 1
        || object.value(QStringLiteral("id")).toString() != document->m_id) {
        document->m_saveError = QStringLiteral("Recovery record identity mismatch; it was retained for inspection");
        return;
    }
    const QString recovered = object.value(QStringLiteral("content")).toString();
    const QString baseRevision = object.value(QStringLiteral("baseRevision")).toString();
    const bool recoveredConflict = object.value(QStringLiteral("conflict")).toBool();
    if (recovered == document->m_content && !recoveredConflict) {
        clearRecovery(document->m_id);
        return;
    }
    document->m_content = recovered;
    document->m_dirty = true;
    if (recoveredConflict
        || baseRevision != document->m_revision || document->m_missing || document->m_trashed) {
        document->m_conflict = true;
        document->m_saveError = QStringLiteral("CONFLICT: recovered edits are based on another disk revision; edits kept");
    } else {
        document->m_conflict = false;
        document->m_saveError = QStringLiteral("Recovered pending edits; autosave resumed");
        scheduleSave(document);
    }
    emitDocumentChanged(document, true);
}

void DocumentCollection::clearRecovery(const QString &id)
{
    QFile::remove(recoveryPath(id));
}

/** Restart the whole quiet period; typing again never shortens the remaining wait. */
void DocumentCollection::scheduleSave(Document *document)
{
    if (QTimer *timer = m_saveTimers.value(document->m_id)) {
        timer->start(kAutosaveQuietPeriodMs);
    }
}

void DocumentCollection::cancelScheduledSave(const QString &id)
{
    if (QTimer *timer = m_saveTimers.value(id)) {
        timer->stop();
    }
}

/** Keep the watcher armed using only the difference from what it already watches.
 *
 * Dropping and re-adding every path twice a second would be hundreds of syscalls per
 * reconciliation on a large library; the delta also re-arms the single path an atomic
 * replacement invalidated, which is what keeps external-edit detection working.
 */
void DocumentCollection::refreshWatches()
{
    if (!m_open) return;

    QSet<QString> wantedDirectories{m_rootPath};
    QDirIterator iterator(m_rootPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (!iterator.fileInfo().isSymLink()) wantedDirectories.insert(path);
    }
    QSet<QString> wantedFiles;
    for (Document *document : m_documents) {
        if (!document->m_missing && !document->m_trashed && !document->m_absolutePath.isEmpty()
            && QFileInfo::exists(document->m_absolutePath)) {
            wantedFiles.insert(document->m_absolutePath);
        }
    }

    const QStringList currentDirectories = m_watcher.directories();
    const QStringList currentFiles = m_watcher.files();
    const QSet<QString> haveDirectories(currentDirectories.begin(), currentDirectories.end());
    const QSet<QString> haveFiles(currentFiles.begin(), currentFiles.end());

    const QStringList drop = QStringList((haveDirectories - wantedDirectories).values())
        + QStringList((haveFiles - wantedFiles).values());
    if (!drop.isEmpty()) m_watcher.removePaths(drop);
    const QStringList add = QStringList((wantedDirectories - haveDirectories).values())
        + QStringList((wantedFiles - haveFiles).values());
    if (!add.isEmpty()) m_watcher.addPaths(add);
}

void DocumentCollection::scheduleReconcile()
{
    if (m_open) m_reconcileTimer.start();
}

bool DocumentCollection::moveInsideRoot(Document *document, QString targetRelative,
                                        bool archived, QString restorePath)
{
    if (!document || !confinedRelative(targetRelative) || document->m_trashed || document->m_missing) {
        return false;
    }
    if (document->m_dirty && !saveNow(document->m_id)) {
        return false;
    }
    QString error;
    const QString parent = QFileInfo(targetRelative).path();
    if (!ensureDirectoryChain(m_rootPath, parent, &error)) {
        setError(error);
        return false;
    }
    const QString target = absoluteFor(targetRelative);
    if (!renameNoReplace(document->m_absolutePath, target, &error)) {
        document->m_saveError = error;
        emitDocumentChanged(document);
        return false;
    }
    document->m_relativePath = QDir::cleanPath(targetRelative);
    document->m_absolutePath = target;
    document->m_archived = archived;
    document->m_restorePath = restorePath;
    updateDiskStat(document);
    document->m_saveError.clear();
    markMetadataDirty();
    applyCatalogOrder(sortedCatalog());
    persistMetadata();
    refreshWatches();
    emitDocumentChanged(document);
    // A move can change the note's FOLDER (archive, restore, rename into place), which is
    // exactly what fan membership is derived from.
    rebuildFan();
    return true;
}
