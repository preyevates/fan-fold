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
    const QString revision = digestOf(document->content());
    // Report the engine's verdict on external modification alongside whether the content
    // now on disk is the buffer the editor handed us. Comparing digests alone cannot tell
    // the two apart and misreports the app's own autosave commit as an external change.
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("revision"), revision},
            {QStringLiteral("conflict"), document->conflict()},
            {QStringLiteral("committed"),
             !document->dirty() && m_buffer.contains(id) && m_buffer.value(id) == revision}};
}

bool StoreAdapter::updateContent(const QString &id, const QString &text)
{
    if (!m_collection || !m_collection->updateContent(id, text)) {
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
    const Document *document = m_collection->document(id);
    if (!document) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    // Optimistic concurrency: a caller working from a revision this adapter never handed
    // out, or one that has since moved on, is refused rather than allowed to overwrite.
    // An empty `expected` means the caller is not participating.
    if (!expected.isEmpty() && m_loaded.contains(id) && m_loaded.value(id) != expected) {
        return failure(QStringLiteral(
            "CONFLICT: this note changed since it was loaded; your edits are kept."));
    }
    if (!m_collection->updateContent(id, text)) {
        return failure(QStringLiteral("Note could not be updated"));
    }
    if (!m_collection->saveNow(id)) {
        const QString reason = document->saveError();
        return failure(reason.isEmpty() ? QStringLiteral("Save failed; your edits are kept")
                                        : reason);
    }
    if (document->conflict()) {
        return failure(QStringLiteral(
            "CONFLICT: file changed externally; your edits are kept. Copy edits before reload."));
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
