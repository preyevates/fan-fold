#include "storeadapter.h"

#include "documentcollection.h"

#include <QCryptographicHash>
#include <QDateTime>

StoreAdapter::StoreAdapter(DocumentCollection *collection, QObject *parent)
    : QObject(parent)
    , m_collection(collection)
{
}

QVariantMap StoreAdapter::failure(const QString &message) const
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString StoreAdapter::digestOf(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantMap StoreAdapter::load(const QString &id)
{
    if (!m_collection) {
        return failure(QStringLiteral("No library is open"));
    }
    const Document *document = m_collection->document(id);
    if (!document) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    if (document->missing()) {
        return failure(QStringLiteral("Note missing from disk"));
    }
    const QString text = document->content();
    const QString revision = digestOf(text);
    m_loaded.insert(id, revision);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("text"), text},
            {QStringLiteral("revision"), revision},
            {QStringLiteral("filename"), document->fileName()},
            {QStringLiteral("title"), document->title()}};
}

QVariantMap StoreAdapter::probe(const QString &id)
{
    if (!m_collection) {
        return failure(QStringLiteral("No library is open"));
    }
    const Document *document = m_collection->document(id);
    if (!document) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    const QString revision = document->dirty() ? m_loaded.value(id, digestOf(document->content()))
                                               : digestOf(document->content());
    // Report the engine's verdict on external modification alongside whether the content
    // now on disk is the buffer the editor handed us. Comparing digests alone cannot tell
    // the two apart and misreports the app's own autosave commit as an external change.
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("revision"), revision},
            {QStringLiteral("conflict"), document->conflict()},
            {QStringLiteral("saveError"), document->saveError()},
            {QStringLiteral("committed"),
             !document->dirty() && m_buffer.contains(id) && m_buffer.value(id) == revision}};
}

bool StoreAdapter::updateContent(const QString &id, const QString &text)
{
    if (!m_collection) return false;
    m_collection->reconcileNow();
    const Document *document = m_collection->document(id);
    if (!document) return false;
    if (document->conflict() || document->missing()) {
        m_collection->holdConflictedContent(id, text);
        return false;
    }
    // The engine may already have adopted a newer clean disk revision while the editor
    // still holds an older one. Never let that editor's next keystroke turn the new
    // revision into its autosave baseline. Keep its text in recovery as a conflict.
    if (!document->dirty() && !document->conflict() && m_loaded.contains(id)
        && m_loaded.value(id) != digestOf(document->content())) {
        if (m_buffer.value(id) != digestOf(document->content())) {
            m_collection->holdConflictedContent(id, text);
            return false;
        }
        // The disk holds our own completed autosave; the editor has not necessarily
        // received its new revision from probe() yet.
    }
    if (!m_collection->updateContent(id, text)) {
        return false;
    }
    m_buffer.insert(id, digestOf(text));
    return true;
}

QVariantMap StoreAdapter::save(const QString &id, const QString &text, const QString &expected)
{
    if (!m_collection) {
        return failure(QStringLiteral("No library is open"));
    }
    m_collection->reconcileNow();
    const Document *document = m_collection->document(id);
    if (!document) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    if (document->conflict()) {
        const bool journaled = m_collection->holdConflictedContent(id, text);
        return failure(journaled ? QStringLiteral("CONFLICT: file changed externally; edits kept in recovery")
                                 : document->saveError());
    }
    if (!document->dirty() && m_loaded.contains(id)
        && m_loaded.value(id) != digestOf(document->content())
        && m_buffer.value(id) != digestOf(document->content())) {
        const bool journaled = m_collection->holdConflictedContent(id, text);
        return failure(journaled ? QStringLiteral("CONFLICT: file changed externally; edits kept in recovery")
                                 : document->saveError());
    }
    // Optimistic concurrency: a caller working from a revision this adapter never handed
    // out, or one that has since moved on, is refused rather than allowed to overwrite.
    // An empty `expected` means the caller is not participating.
    if (!expected.isEmpty() && m_loaded.contains(id) && m_loaded.value(id) != expected) {
        return failure(QStringLiteral(
            "CONFLICT: this note changed since it was loaded; keep this window open and copy your edits."));
    }
    if (!m_collection->updateContent(id, text)) {
        return failure(document->saveError().isEmpty() ? QStringLiteral("Note could not be updated")
                                                       : document->saveError());
    }
    if (!m_collection->saveNow(id)) {
        const QString reason = document->saveError();
        return failure(reason.isEmpty() ? QStringLiteral("Save failed; keep this window open and copy your edits")
                                        : reason);
    }
    if (document->conflict()) {
        return failure(QStringLiteral(
            "CONFLICT: file changed externally; keep this window open and copy edits before reload."));
    }
    const QString revision = digestOf(text);
    m_loaded.insert(id, revision);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("revision"), revision}};
}

QVariantMap StoreAdapter::info(const QString &id)
{
    if (!m_collection) {
        return failure(QStringLiteral("No library is open"));
    }
    const Document *document = m_collection->document(id);
    if (!document) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    const QString revision = digestOf(document->content());
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("filename"), document->fileName()},
            {QStringLiteral("path"), document->absolutePath()},
            {QStringLiteral("bytes"), static_cast<qlonglong>(document->diskBytes())},
            {QStringLiteral("modified"), document->diskModified().toString(Qt::ISODate)},
            {QStringLiteral("modifiedEpoch"),
             static_cast<qlonglong>(document->diskModified().toSecsSinceEpoch())},
            {QStringLiteral("revision"), revision},
            {QStringLiteral("loadedRevision"), m_loaded.value(id)},
            // The engine owns the authoritative answer to "does the buffer match disk";
            // a clean, unconflicted document does by definition.
            {QStringLiteral("matchesLoaded"), !document->dirty() && !document->conflict()}};
}

QVariantMap StoreAdapter::rename(const QString &id, const QString &title, const QString &expected)
{
    Q_UNUSED(expected)
    if (!m_collection) {
        return failure(QStringLiteral("No library is open"));
    }
    const Document *document = m_collection->document(id);
    if (!document) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    if (title.trimmed().isEmpty()) {
        return failure(QStringLiteral("A note needs a title"));
    }
    if (document->title() == title.trimmed()) {
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("unchanged"), true},
                {QStringLiteral("filename"), document->fileName()},
                {QStringLiteral("title"), document->title()}};
    }
    if (!m_collection->renameDocument(id, title)) {
        const QString reason = m_collection->lastError();
        return failure(reason.isEmpty() ? QStringLiteral("Rename refused") : reason);
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("filename"), document->fileName()},
            {QStringLiteral("title"), document->title()},
            {QStringLiteral("revision"), m_loaded.value(id)}};
}
