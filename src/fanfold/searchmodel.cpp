#include "searchmodel.h"

#include "documentcollection.h"

#include <QRegularExpression>

#include <algorithm>

namespace {
constexpr int snippetLead = 30;
constexpr int snippetTail = 70;
constexpr int snippetMaximum = 160;

/** Collapse every run of whitespace, newlines included, into one space. */
QString flatten(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return text.simplified().replace(whitespace, QStringLiteral(" "));
}

} // namespace

SearchModel::SearchModel(DocumentCollection *collection, QObject *parent)
    : QAbstractListModel(parent), m_collection(collection)
{
    if (m_collection) {
        const auto refresh = [this] { rebuild(); };
        connect(m_collection, &DocumentCollection::documentsChanged, this, refresh);
        connect(m_collection, &DocumentCollection::documentChanged, this, refresh);
        connect(m_collection, &DocumentCollection::rootChanged, this, refresh);
        connect(m_collection, &QAbstractItemModel::modelReset, this, refresh);
    }
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_hits.size());
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hits.size()) {
        return {};
    }
    const Hit &hit = m_hits.at(index.row());
    switch (role) {
    case DocumentIdRole: return hit.documentId;
    case TitleRole: return hit.title;
    case PathRole: return hit.path;
    case FolderRole: return hit.folder;
    case SnippetRole: return hit.snippet;
    case MatchKindRole: return hit.kind;
    case ArchivedRole: return hit.archived;
    case DocumentRole:
        return m_collection ? QVariant::fromValue(m_collection->document(hit.documentId)) : QVariant();
    default: return {};
    }
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {{DocumentIdRole, "documentId"}, {TitleRole, "title"}, {PathRole, "path"},
            {FolderRole, "folder"}, {SnippetRole, "snippet"}, {MatchKindRole, "matchKind"},
            {ArchivedRole, "archived"}, {DocumentRole, "document"}};
}

void SearchModel::setQuery(const QString &query)
{
    if (m_query == query) {
        return;
    }
    m_query = query;
    rebuild();
    emit queryChanged();
}

void SearchModel::setIncludeArchive(bool include)
{
    if (m_includeArchive == include) {
        return;
    }
    m_includeArchive = include;
    rebuild();
    emit includeArchiveChanged();
}

QString SearchModel::documentIdAt(int row) const
{
    return row >= 0 && row < m_hits.size() ? m_hits.at(row).documentId : QString();
}

void SearchModel::rebuild()
{
    const QList<Hit> next = buildHits();
    if (next == m_hits) {
        return;
    }
    beginResetModel();
    m_hits = next;
    endResetModel();
    emit countChanged();
}

QList<SearchModel::Hit> SearchModel::buildHits() const
{
    QList<Hit> hits;
    const QString needle = m_query.trimmed();
    if (needle.isEmpty() || !m_collection) {
        return hits;
    }

    for (const QString &id : m_collection->catalogIds()) {
        Document *document = m_collection->document(id);
        if (!document || document->trashed() || document->missing()) {
            continue;
        }
        if (document->archived() && !m_includeArchive) {
            continue;
        }

        Hit hit;
        if (document->title().contains(needle, Qt::CaseInsensitive)) {
            hit.kind = QStringLiteral("title");
            hit.rank = 0;
            hit.snippet = leadingLine(document->content());
        } else if (document->relativePath().contains(needle, Qt::CaseInsensitive)) {
            hit.kind = QStringLiteral("path");
            hit.rank = 1;
            hit.snippet = leadingLine(document->content());
        } else if (document->content().contains(needle, Qt::CaseInsensitive)) {
            hit.kind = QStringLiteral("content");
            hit.rank = 2;
            hit.snippet = snippetAround(document->content(), needle);
        } else {
            continue;
        }
        hit.documentId = id;
        hit.title = document->title();
        hit.path = document->relativePath();
        hit.folder = document->folder();
        hit.archived = document->archived();
        hits.append(hit);
    }

    std::sort(hits.begin(), hits.end(), [](const Hit &left, const Hit &right) {
        if (left.rank != right.rank) {
            return left.rank < right.rank;
        }
        const int order = left.path.compare(right.path, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : left.documentId < right.documentId;
    });
    return hits;
}

/** One single-line excerpt around the first match, with the match's own casing kept. */
QString SearchModel::snippetAround(const QString &content, const QString &needle)
{
    const qsizetype at = content.indexOf(needle, 0, Qt::CaseInsensitive);
    if (at < 0) {
        return leadingLine(content);
    }
    const qsizetype start = qMax(qsizetype(0), at - snippetLead);
    const qsizetype end = qMin(content.size(), at + needle.size() + snippetTail);
    QString window = flatten(content.mid(start, end - start));
    if (start > 0) {
        window.prepend(QStringLiteral("… "));
    }
    if (end < content.size()) {
        window.append(QStringLiteral(" …"));
    }
    return window.left(snippetMaximum);
}

QString SearchModel::leadingLine(const QString &content)
{
    return flatten(content).left(snippetMaximum);
}
