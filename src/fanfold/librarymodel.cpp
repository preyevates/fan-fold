#include "librarymodel.h"

#include "documentcollection.h"

#include <QFileInfo>
#include <QDirIterator>
#include <QRegularExpression>

#include <algorithm>

namespace {

bool lessThanByName(const QString &left, const QString &right)
{
    const int order = left.compare(right, Qt::CaseInsensitive);
    return order != 0 ? order < 0 : left < right;
}

/** Every ancestor of `folder`, nearest last: "a/b/c" -> ["a", "a/b", "a/b/c"]. */
QStringList ancestryOf(const QString &folder)
{
    QStringList chain;
    QString current = folder;
    while (!current.isEmpty()) {
        chain.prepend(current);
        const QString parent = QFileInfo(current).path();
        current = parent == QStringLiteral(".") ? QString() : parent;
    }
    return chain;
}

} // namespace

LibraryModel::LibraryModel(DocumentCollection *collection, QObject *parent)
    : QAbstractListModel(parent), m_collection(collection)
{
    if (m_collection) {
        // Any catalog movement re-projects the whole tree; the diff in applyRows() is
        // what keeps that cheap and quiet.
        const auto refresh = [this] { rebuild(); };
        connect(m_collection, &DocumentCollection::documentsChanged, this, refresh);
        connect(m_collection, &DocumentCollection::documentChanged, this, refresh);
        connect(m_collection, &DocumentCollection::rootChanged, this, refresh);
        connect(m_collection, &QAbstractItemModel::modelReset, this, refresh);
        connect(m_collection, &QAbstractItemModel::rowsInserted, this, refresh);
        connect(m_collection, &QAbstractItemModel::rowsRemoved, this, refresh);
        connect(m_collection, &QAbstractItemModel::rowsMoved, this, refresh);
    }
    m_rows = buildRows();
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case NameRole: return row.name;
    case PathRole: return row.path;
    case DepthRole: return row.depth;
    case IsFolderRole: return row.isFolder;
    case ExpandedRole: return row.expanded;
    case HasChildrenRole: return row.isFolder && row.noteCount > 0;
    case NoteCountRole: return row.noteCount;
    case DocumentIdRole: return row.documentId;
    case ArchivedRole: return row.archived;
    case CurrentRole: return index.row() == m_currentRow;
    case SectionRole: {
        // The folder that CONTAINS the row, never the row's own path: a folder row then
        // opens its own section instead of sitting under a header repeating its name.
        const QString parent = QFileInfo(row.path).path();
        return parent == QStringLiteral(".") ? QString() : parent;
    }
    case DocumentRole:
        return row.isFolder || !m_collection
            ? QVariant()
            : QVariant::fromValue(m_collection->document(row.documentId));
    default: return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {{NameRole, "name"}, {PathRole, "path"}, {DepthRole, "depth"},
            {IsFolderRole, "isFolder"}, {ExpandedRole, "expanded"},
            {HasChildrenRole, "hasChildren"}, {NoteCountRole, "noteCount"},
            {DocumentIdRole, "documentId"}, {ArchivedRole, "archived"},
            {CurrentRole, "current"}, {DocumentRole, "document"},
            {SectionRole, "section"}};
}

void LibraryModel::setShowArchive(bool show)
{
    if (m_showArchive == show) {
        return;
    }
    m_showArchive = show;
    rebuild();
    emit showArchiveChanged();
}

void LibraryModel::setCurrentRow(int row)
{
    const int clamped = row >= 0 && row < m_rows.size() ? row : -1;
    if (clamped == m_currentRow) {
        return;
    }
    const int previous = m_currentRow;
    m_currentRow = clamped;
    m_currentKey = clamped < 0 ? QString() : m_rows.at(clamped).key;
    if (previous >= 0 && previous < m_rows.size()) {
        emit dataChanged(index(previous), index(previous), {CurrentRole});
    }
    if (clamped >= 0) {
        emit dataChanged(index(clamped), index(clamped), {CurrentRole});
    }
    emit currentChanged();
}

QString LibraryModel::currentFolder() const
{
    return folderForRow(m_currentRow);
}

QString LibraryModel::folderForRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    const Row &at = m_rows.at(row);
    if (at.isFolder) {
        return at.path;
    }
    const QString parent = QFileInfo(at.path).path();
    return parent == QStringLiteral(".") ? QString() : parent;
}

void LibraryModel::setExpanded(const QString &folder, bool expanded)
{
    if (folder.isEmpty()) {
        return;
    }
    if (expanded == m_expanded.contains(folder)) {
        return;
    }
    if (expanded) {
        m_expanded.insert(folder);
    } else {
        m_expanded.remove(folder);
    }
    rebuild();
}

void LibraryModel::collapseAll()
{
    if (m_expanded.isEmpty()) {
        return;
    }
    m_expanded.clear();
    rebuild();
}

void LibraryModel::toggle(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    const Row &at = m_rows.at(row);
    if (at.isFolder) {
        setExpanded(at.path, !m_expanded.contains(at.path));
    }
}

void LibraryModel::revealFolder(const QString &folder)
{
    const QStringList chain = ancestryOf(folder);
    bool changed = false;
    for (const QString &step : chain) {
        if (!m_expanded.contains(step)) {
            m_expanded.insert(step);
            changed = true;
        }
    }
    if (changed) {
        rebuild();
    }
}

int LibraryModel::rowForFolder(const QString &folder) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).isFolder && m_rows.at(row).path == folder) {
            return row;
        }
    }
    return -1;
}

int LibraryModel::rowForDocument(const QString &documentId) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (!m_rows.at(row).isFolder && m_rows.at(row).documentId == documentId) {
            return row;
        }
    }
    return -1;
}

void LibraryModel::rebuild()
{
    applyRows(buildRows());
}

QList<LibraryModel::Row> LibraryModel::buildRows() const
{
    QList<Row> rows;
    if (!m_collection) {
        return rows;
    }

    QHash<QString, QList<Document *>> folderNotes;
    QHash<QString, QSet<QString>> childSets;
    // Enumerate real directories rather than deriving folders from note paths: the
    // latter makes an empty folder invisible, including every folder just created and not
    // yet filed into. Assets and Archive are excluded as machinery rather than filing, as
    // are hidden directories.
    if (m_collection->isOpen()) {
        QDirIterator dirs(m_collection->rootPath(),
                          QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
        const QString root = QDir(m_collection->rootPath()).absolutePath() + QLatin1Char('/');
        while (dirs.hasNext()) {
            const QString absolute = dirs.next();
            QString relative = QDir::cleanPath(absolute);
            if (!relative.startsWith(root)) {
                continue;
            }
            relative = relative.mid(root.size());
            const QString top = relative.section(QLatin1Char('/'), 0, 0);
            if (top == QStringLiteral("Assets") || top == QStringLiteral("Archive")
                || relative.split(QLatin1Char('/')).filter(QRegularExpression(QStringLiteral("^\\."))).size() > 0) {
                continue;
            }
            QString current = relative;
            while (!current.isEmpty()) {
                const QString parent = QFileInfo(current).path();
                const QString parentFolder = parent == QStringLiteral(".") ? QString() : parent;
                childSets[parentFolder].insert(current);
                current = parentFolder;
            }
        }
    }
    for (const QString &id : m_collection->catalogIds()) {
        Document *document = m_collection->document(id);
        if (!document || document->trashed() || document->missing()) {
            continue;
        }
        if (document->archived() && !m_showArchive) {
            continue;
        }
        const QString folder = document->folder();
        folderNotes[folder].append(document);
        QString current = folder;
        while (!current.isEmpty()) {
            const QString parent = QFileInfo(current).path();
            const QString parentFolder = parent == QStringLiteral(".") ? QString() : parent;
            childSets[parentFolder].insert(current);
            current = parentFolder;
        }
    }

    QHash<QString, QStringList> childFolders;
    for (auto it = childSets.cbegin(); it != childSets.cend(); ++it) {
        QStringList children(it.value().begin(), it.value().end());
        std::sort(children.begin(), children.end(), [](const QString &left, const QString &right) {
            return lessThanByName(QFileInfo(left).fileName(), QFileInfo(right).fileName());
        });
        childFolders.insert(it.key(), children);
    }
    for (auto it = folderNotes.begin(); it != folderNotes.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(), [](Document *left, Document *right) {
            return lessThanByName(left->title(), right->title());
        });
    }

    appendFolder(rows, QString(), -1, childFolders, folderNotes);
    return rows;
}

/** Emit one folder's subtree. `depth` is the folder's own depth; the root uses -1 so its
 * direct children sit at depth 0 and the root itself is never drawn as a row. */
void LibraryModel::appendFolder(QList<Row> &rows, const QString &folder, int depth,
                                const QHash<QString, QStringList> &childFolders,
                                const QHash<QString, QList<Document *>> &folderNotes) const
{
    for (const QString &child : childFolders.value(folder)) {
        const QString leaf = QFileInfo(child).fileName();
        // Assets is machinery, never filing: it holds images, audio and icons the app
        // copied in itself, and never contains notes.
        if (leaf == QStringLiteral("Assets")) {
            continue;
        }
        // Archive is drawn as an ordinary folder row rather than hidden with archived
        // notes surfaced flat: without a folder to open, "where did my archived note go
        // and how do I get it back" has no visible answer. It stays hidden entirely while
        // showArchive is false.
        if (leaf == QStringLiteral("Archive") && !m_showArchive) {
            continue;
        }
        Row row;
        row.key = QStringLiteral("F:") + child;
        row.name = QFileInfo(child).fileName();
        row.path = child;
        row.depth = depth + 1;
        row.isFolder = true;
        row.archived = child == QStringLiteral("Archive")
            || child.startsWith(QStringLiteral("Archive/"));
        row.noteCount = notesUnder(child, childFolders, folderNotes);
        row.expanded = m_expanded.contains(child);
        rows.append(row);
        // This is the lazy boundary: descendants enter the flat view only after their
        // own folder was opened. A deep library therefore starts as a legible index rather
        // than a fully materialised wall of rows.
        if (row.expanded) {
            appendFolder(rows, child, depth + 1, childFolders, folderNotes);
        }
    }
    for (Document *document : folderNotes.value(folder)) {
        Row row;
        row.key = QStringLiteral("N:") + document->id();
        row.name = document->title();
        row.path = document->relativePath();
        row.documentId = document->id();
        row.depth = depth + 1;
        row.isFolder = false;
        row.archived = document->archived();
        rows.append(row);
    }
}

int LibraryModel::notesUnder(const QString &folder, const QHash<QString, QStringList> &childFolders,
                             const QHash<QString, QList<Document *>> &folderNotes) const
{
    int total = int(folderNotes.value(folder).size());
    for (const QString &child : childFolders.value(folder)) {
        total += notesUnder(child, childFolders, folderNotes);
    }
    return total;
}

/** Replace the row list using the smallest structural edit that explains the change.
 *
 * Expanding a folder inserts one contiguous block, collapsing removes one, and a pure
 * relabelling emits only dataChanged — so the panel never resets under the user's cursor.
 */
void LibraryModel::applyRows(const QList<Row> &next)
{
    if (m_rows == next) {
        return;
    }

    qsizetype prefix = 0;
    while (prefix < m_rows.size() && prefix < next.size()
           && m_rows.at(prefix).key == next.at(prefix).key) {
        ++prefix;
    }
    qsizetype suffix = 0;
    while (suffix < m_rows.size() - prefix && suffix < next.size() - prefix
           && m_rows.at(m_rows.size() - 1 - suffix).key == next.at(next.size() - 1 - suffix).key) {
        ++suffix;
    }

    const qsizetype removeCount = m_rows.size() - prefix - suffix;
    const qsizetype insertCount = next.size() - prefix - suffix;
    if (removeCount > 0) {
        beginRemoveRows({}, int(prefix), int(prefix + removeCount - 1));
        m_rows.remove(prefix, removeCount);
        endRemoveRows();
    }
    if (insertCount > 0) {
        beginInsertRows({}, int(prefix), int(prefix + insertCount - 1));
        for (qsizetype offset = 0; offset < insertCount; ++offset) {
            m_rows.insert(prefix + offset, next.at(prefix + offset));
        }
        endInsertRows();
    }

    // Rows that kept their identity may still have changed depth, count or label.
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row) != next.at(row)) {
            m_rows[row] = next.at(row);
            emit dataChanged(index(int(row)), index(int(row)));
        }
    }

    restoreCurrentRow();
    if (removeCount != insertCount) {
        emit countChanged();
    }
}

/** Keep the selection on the same folder or note across a re-projection. */
void LibraryModel::restoreCurrentRow()
{
    if (m_currentKey.isEmpty()) {
        m_currentRow = -1;
        return;
    }
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).key == m_currentKey) {
            if (m_currentRow != row) {
                m_currentRow = row;
                emit currentChanged();
            }
            return;
        }
    }
    m_currentRow = -1;
    m_currentKey.clear();
    emit currentChanged();
}
