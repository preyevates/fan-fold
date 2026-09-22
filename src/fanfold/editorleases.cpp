#include "editorleases.h"

#include <algorithm>

EditorLeases::EditorLeases(int capacity, QObject *parent)
    : QObject(parent), m_capacity(qMax(1, capacity))
{
}

void EditorLeases::setCapacity(int capacity)
{
    const int bounded = qMax(1, capacity);
    if (bounded == m_capacity) {
        return;
    }
    m_capacity = bounded;
    trimToCapacity();
    emit capacityChanged();
}

int EditorLeases::residentCount() const
{
    return int(std::count_if(m_slots.cbegin(), m_slots.cend(),
                             [](const Slot &slot) { return !slot.documentId.isEmpty(); }));
}

QString EditorLeases::acquire(const QString &documentId, const QString &owner)
{
    if (documentId.isEmpty() || owner.isEmpty()) {
        m_lastError = QStringLiteral("An editor lease needs both a document and an owner");
        return {};
    }
    m_lastError.clear();

    // One owner holds one editor: taking a different note lets the previous one go warm.
    const int held = indexOfOwner(owner);
    if (held >= 0 && m_slots.at(held).documentId != documentId) {
        m_slots[held].owner.clear();
    }

    const int existing = indexOfDocument(documentId);
    if (existing >= 0) {
        Slot &slot = m_slots[existing];
        if (!slot.owner.isEmpty() && slot.owner != owner) {
            // Explicit, ordered handoff: the old surface detaches before the new attaches.
            const QString previous = slot.owner;
            slot.owner.clear();
            emit ownerDetached(previous, documentId);
        }
        slot.owner = owner;
        slot.touched = ++m_clock;
        return slot.key;
    }

    if (m_slots.size() < m_capacity) {
        Slot slot;
        slot.key = QStringLiteral("slot-%1").arg(m_slots.size());
        slot.documentId = documentId;
        slot.owner = owner;
        slot.touched = ++m_clock;
        m_slots.append(slot);
        emit slotAssigned(slot.key, documentId);
        emit residentChanged();
        return slot.key;
    }

    const int victim = leastRecentlyUsedFreeSlot();
    if (victim < 0) {
        m_lastError = QStringLiteral(
            "Every editor is held by an open window; close a pinned note to open another");
        return {};
    }
    const QString key = m_slots.at(victim).key;
    const QString evicted = m_slots.at(victim).documentId;
    m_slots[victim].documentId = documentId;
    m_slots[victim].owner = owner;
    m_slots[victim].touched = ++m_clock;
    if (!evicted.isEmpty()) {
        emit slotEvicted(key, evicted);
    }
    emit slotAssigned(key, documentId);
    emit residentChanged();
    return key;
}

bool EditorLeases::release(const QString &owner)
{
    const int held = indexOfOwner(owner);
    if (held < 0) {
        return false;
    }
    m_slots[held].owner.clear();
    return true;
}

bool EditorLeases::forget(const QString &documentId)
{
    const int at = indexOfDocument(documentId);
    if (at < 0) {
        return false;
    }
    const QString key = m_slots.at(at).key;
    m_slots.removeAt(at);
    emit slotEvicted(key, documentId);
    emit residentChanged();
    return true;
}

QString EditorLeases::slotForDocument(const QString &documentId) const
{
    const int at = indexOfDocument(documentId);
    return at < 0 ? QString() : m_slots.at(at).key;
}

QString EditorLeases::documentForSlot(const QString &slot) const
{
    for (const Slot &candidate : m_slots) {
        if (candidate.key == slot) {
            return candidate.documentId;
        }
    }
    return {};
}

QString EditorLeases::ownerForDocument(const QString &documentId) const
{
    const int at = indexOfDocument(documentId);
    return at < 0 ? QString() : m_slots.at(at).owner;
}

QStringList EditorLeases::residentDocuments() const
{
    QList<Slot> ordered = m_slots;
    std::sort(ordered.begin(), ordered.end(), [](const Slot &left, const Slot &right) {
        return left.touched > right.touched;
    });
    QStringList ids;
    for (const Slot &slot : std::as_const(ordered)) {
        if (!slot.documentId.isEmpty()) {
            ids.append(slot.documentId);
        }
    }
    return ids;
}

int EditorLeases::indexOfDocument(const QString &documentId) const
{
    if (documentId.isEmpty()) {
        return -1;
    }
    for (int at = 0; at < m_slots.size(); ++at) {
        if (m_slots.at(at).documentId == documentId) {
            return at;
        }
    }
    return -1;
}

int EditorLeases::indexOfOwner(const QString &owner) const
{
    if (owner.isEmpty()) {
        return -1;
    }
    for (int at = 0; at < m_slots.size(); ++at) {
        if (m_slots.at(at).owner == owner) {
            return at;
        }
    }
    return -1;
}

int EditorLeases::leastRecentlyUsedFreeSlot() const
{
    int victim = -1;
    for (int at = 0; at < m_slots.size(); ++at) {
        if (!m_slots.at(at).owner.isEmpty()) {
            continue;
        }
        if (victim < 0 || m_slots.at(at).touched < m_slots.at(victim).touched) {
            victim = at;
        }
    }
    return victim;
}

/** Shrink the pool after a capacity reduction, giving up unowned slots first. */
void EditorLeases::trimToCapacity()
{
    while (m_slots.size() > m_capacity) {
        const int victim = leastRecentlyUsedFreeSlot();
        if (victim < 0) {
            return; // every remaining slot is held by a live window
        }
        const QString key = m_slots.at(victim).key;
        const QString documentId = m_slots.at(victim).documentId;
        m_slots.removeAt(victim);
        if (!documentId.isEmpty()) {
            emit slotEvicted(key, documentId);
        }
        emit residentChanged();
    }
}
