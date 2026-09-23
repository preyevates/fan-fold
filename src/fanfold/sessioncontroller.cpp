#include "sessioncontroller.h"

#include "documentcollection.h"
#include "editorleases.h"
#include "librarymodel.h"

SessionController::SessionController(DocumentCollection *collection, LibraryModel *library,
                                     EditorLeases *leases, QObject *parent)
    : QObject(parent), m_collection(collection), m_library(library), m_leases(leases)
{
    if (m_collection) {
        // A note that disappears from under the session must not stay selected or pinned.
        connect(m_collection, &DocumentCollection::documentChanged, this,
                [this](const QString &id) {
                    Document *document = m_collection->document(id);
                    if (!document || !document->trashed()) {
                        return;
                    }
                    if (m_pinnedIds.removeOne(id)) {
                        emit pinnedChanged();
                        emit pinnedWindowClosed(id);
                    }
                    if (m_currentDocumentId == id) {
                        selectFallback();
                    }
                });
    }
    restorePersistedPins();
}

QString SessionController::fanOwner()
{
    return QStringLiteral("fan");
}

QString SessionController::pinnedOwner(const QString &documentId)
{
    return QStringLiteral("pin:") + documentId;
}

void SessionController::setError(const QString &message)
{
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    emit errorChanged();
}

bool SessionController::available(const QString &id) const
{
    Document *document = m_collection ? m_collection->document(id) : nullptr;
    return document && !document->trashed() && !document->missing();
}

void SessionController::setCurrent(const QString &id)
{
    if (m_currentDocumentId == id) {
        return;
    }
    m_currentDocumentId = id;
    emit currentChanged();
}

QString SessionController::createNoteInRoot()
{
    if (!m_collection) {
        return {};
    }
    // "In the root" means the fan's own scope when the fan IS the root; otherwise the
    // caller asked for the root explicitly and gets it, which also re-scopes the fan so
    // the new note is visible rather than created somewhere the user cannot see.
    const QString id = m_collection->createNote(QString());
    if (id.isEmpty()) {
        setError(m_collection->lastError());
        return {};
    }
    m_collection->setOpenFolder(QString());
    selectDocument(id);
    emit focusEditorRequested(id);
    return id;
}

QString SessionController::createNoteInLibraryFolder()
{
    if (!m_collection) {
        return {};
    }
    const QString folder = m_library ? m_library->currentFolder() : QString();
    const QString id = m_collection->createNote(folder);
    if (id.isEmpty()) {
        setError(m_collection->lastError());
        return {};
    }
    // The + creates in the open folder and the note appears on the fan naturally. When the
    // Library's selection points somewhere else, creating there MOVES the scope: a note
    // created into a folder nobody is looking at would otherwise vanish on arrival.
    m_collection->setOpenFolder(folder);
    selectDocument(id);
    emit focusEditorRequested(id);
    return id;
}

bool SessionController::selectDocument(const QString &id)
{
    if (!available(id)) {
        setError(QStringLiteral("That note is no longer available"));
        return false;
    }
    setCurrent(id);
    // A pinned note keeps its editor in its own window; the fan shows it as a card only.
    if (m_leases && !m_pinnedIds.contains(id)) {
        if (m_leases->acquire(id, fanOwner()).isEmpty()) {
            setError(m_leases->lastError());
        }
    }
    return true;
}

bool SessionController::openFromLibrary(const QString &id)
{
    if (!m_collection || !available(id)) {
        setError(QStringLiteral("That note is no longer available"));
        return false;
    }
    if (m_collection->document(id)->archived()) {
        setError(QStringLiteral("Restore this note from Archive before opening it in the fan"));
        return false;
    }
    // Opening a note from the Library scopes the fan to the folder that note lives in —
    // the same gesture as opening its folder. Membership is derived, so there is nothing
    // to "join": once the scope moves, the note is on the fan.
    if (!m_collection->setOpenFolder(m_collection->document(id)->folder())) {
        setError(m_collection->lastError());
        return false;
    }
    if (!selectDocument(id)) {
        return false;
    }
    emit focusEditorRequested(id);
    return true;
}

bool SessionController::findCurrentNote()
{
    if (m_currentDocumentId.isEmpty() || !m_library || !available(m_currentDocumentId)) {
        return false;
    }
    Document *document = m_collection->document(m_currentDocumentId);
    m_library->setShowArchive(m_library->showArchive() || document->archived());
    m_library->revealFolder(document->folder());
    const int row = m_library->rowForDocument(m_currentDocumentId);
    if (row < 0) {
        return false;
    }
    m_library->setCurrentRow(row);
    emit revealInLibraryRequested(m_currentDocumentId);
    return true;
}

bool SessionController::pin(const QString &id)
{
    if (!m_collection || !available(id)) {
        setError(QStringLiteral("That note is no longer available"));
        return false;
    }
    if (m_pinnedIds.contains(id)) {
        return true;
    }
    m_collection->setPinned(id, true);
    m_pinnedIds.append(id);
    if (m_leases && m_leases->acquire(id, pinnedOwner(id)).isEmpty()) {
        // No editor could be handed over, so no window is opened either.
        m_pinnedIds.removeOne(id);
        m_collection->setPinned(id, false);
        setError(m_leases->lastError());
        return false;
    }
    emit pinnedChanged();
    emit pinnedWindowOpened(id);
    return true;
}

bool SessionController::pinCurrent()
{
    return !m_currentDocumentId.isEmpty() && pin(m_currentDocumentId);
}

bool SessionController::restorePersistedPins()
{
    if (!m_collection || !m_collection->isOpen()) {
        return false;
    }

    bool changed = false;
    for (const QString &id : m_collection->catalogIds()) {
        Document *document = m_collection->document(id);
        if (!document || !document->pinned() || !available(id) || m_pinnedIds.contains(id)) {
            continue;
        }
        if (m_leases && m_leases->acquire(id, pinnedOwner(id)).isEmpty()) {
            setError(m_leases->lastError());
            continue;
        }
        m_pinnedIds.append(id);
        changed = true;
        emit pinnedWindowOpened(id);
    }
    if (changed) {
        emit pinnedChanged();
    }
    return true;
}

bool SessionController::closePinned(const QString &id)
{
    if (!m_pinnedIds.contains(id)) {
        return false;
    }
    m_pinnedIds.removeOne(id);
    if (m_collection) {
        // Closing a pinned window is not a delete and not a dismissal: clearing the pin
        // is enough — the derivation puts the note back on the fan whenever its folder is
        // the open one, in the slot this folder's persisted order kept for it.
        m_collection->setPinned(id, false);
    }
    if (m_leases) {
        m_leases->release(pinnedOwner(id));
    }
    emit pinnedChanged();
    emit pinnedWindowClosed(id);
    return true;
}

bool SessionController::archiveCurrent()
{
    if (!m_collection || m_currentDocumentId.isEmpty()) {
        return false;
    }
    const QString id = m_currentDocumentId;
    if (!m_collection->archive(id)) {
        setError(m_collection->lastError());
        return false;
    }
    if (m_pinnedIds.removeOne(id)) {
        emit pinnedChanged();
        emit pinnedWindowClosed(id);
    }
    if (m_leases) {
        m_leases->forget(id);
    }
    selectFallback();
    return true;
}

bool SessionController::trashCurrent()
{
    if (!m_collection || m_currentDocumentId.isEmpty()) {
        return false;
    }
    const QString id = m_currentDocumentId;
    if (!m_collection->moveToTrash(id)) {
        setError(m_collection->lastError());
        return false;
    }
    if (m_pinnedIds.removeOne(id)) {
        emit pinnedChanged();
        emit pinnedWindowClosed(id);
    }
    if (m_leases) {
        m_leases->forget(id);
    }
    selectFallback();
    return true;
}

void SessionController::selectFallback()
{
    const QStringList fan = m_collection ? m_collection->fanIds() : QStringList();
    if (fan.isEmpty()) {
        setCurrent(QString());
        return;
    }
    m_currentDocumentId.clear();
    selectDocument(fan.first());
}
