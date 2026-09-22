#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

class Document;
class DocumentCollection;

/** Title, path and content search across the note catalog.
 *
 * Archive is excluded by default: filing a note away is how a person says they do not want
 * to meet it again by accident, so it takes an explicit `includeArchive` to bring those
 * notes back into results. Trashed and missing notes are never searched.
 *
 * An empty or whitespace-only query yields no rows rather than the whole library, so the
 * Find surface starts silent instead of dumping every note the moment it opens. Results
 * are ranked title, then path, then content, and within a rank by path, which keeps the
 * list stable while the user keeps typing.
 */
class SearchModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool includeArchive READ includeArchive WRITE setIncludeArchive NOTIFY includeArchiveChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        DocumentIdRole = Qt::UserRole + 1,
        TitleRole,
        PathRole,
        FolderRole,
        SnippetRole,
        MatchKindRole,
        ArchivedRole,
        DocumentRole
    };
    Q_ENUM(Role)

    explicit SearchModel(DocumentCollection *collection, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool includeArchive() const { return m_includeArchive; }
    void setIncludeArchive(bool include);

    /** Stable ID of the hit at `row`, or an empty string. */
    Q_INVOKABLE QString documentIdAt(int row) const;

signals:
    void queryChanged();
    void includeArchiveChanged();
    void countChanged();

private:
    struct Hit {
        QString documentId;
        QString title;
        QString path;
        QString folder;
        QString snippet;
        QString kind;
        bool archived = false;
        int rank = 0;

        bool operator==(const Hit &other) const = default;
    };

    DocumentCollection *m_collection = nullptr;
    QList<Hit> m_hits;
    QString m_query;
    bool m_includeArchive = false;

    void rebuild();
    QList<Hit> buildHits() const;
    static QString snippetAround(const QString &content, const QString &needle);
    static QString leadingLine(const QString &content);
};
