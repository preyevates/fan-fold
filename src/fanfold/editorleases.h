#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

/** A bounded pool of live editor slots, and the single register of who owns each one.
 *
 * A WebEngine editor costs real memory, so a library of hundreds of notes cannot keep one
 * per note; equally, rebuilding an editor when the user switches back to a note would
 * throw away its caret, selection and undo stack. This class resolves both: at most
 * `capacity` notes hold a live editor, the least recently used *unowned* slot is the one
 * that gives way, and re-acquiring a note that is still resident returns the very same
 * slot without building anything.
 *
 * Ownership is what makes the handoff safe. Exactly one owner — the fan, or one pinned
 * window — holds a document's editor at a time. Taking a document that another surface
 * owns emits ownerDetached() so that surface can let go *before* the editor moves, which
 * is why one note can never end up with two editors bound to its buffer. A slot a live
 * window still owns is never evicted: when every slot is owned the request is refused
 * outright rather than pulled out from under a visible window.
 */
class EditorLeases final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int capacity READ capacity WRITE setCapacity NOTIFY capacityChanged)
    Q_PROPERTY(int residentCount READ residentCount NOTIFY residentChanged)

public:
    explicit EditorLeases(int capacity = 6, QObject *parent = nullptr);

    int capacity() const { return m_capacity; }
    void setCapacity(int capacity);
    /** How many notes currently hold a live editor. */
    int residentCount() const;

    /** Give `owner` the editor for `documentId`, building or reusing a slot as needed.
     * @return the slot key, or an empty string with lastError() set when every slot is
     * held by a live window.
     */
    Q_INVOKABLE QString acquire(const QString &documentId, const QString &owner);
    /** Drop `owner`'s claim. The editor stays warm so returning to it is free. */
    Q_INVOKABLE bool release(const QString &owner);
    /** Discard a note's editor entirely — it was archived, trashed or left the library. */
    Q_INVOKABLE bool forget(const QString &documentId);

    Q_INVOKABLE QString slotForDocument(const QString &documentId) const;
    Q_INVOKABLE QString documentForSlot(const QString &slot) const;
    Q_INVOKABLE QString ownerForDocument(const QString &documentId) const;
    /** Resident notes, most recently used first. */
    Q_INVOKABLE QStringList residentDocuments() const;
    Q_INVOKABLE QString lastError() const { return m_lastError; }

signals:
    /** A slot began hosting a note: the surface must build or load an editor for it. */
    void slotAssigned(const QString &slot, const QString &documentId);
    /** A note lost its slot: any editor for it must be torn down. */
    void slotEvicted(const QString &slot, const QString &documentId);
    /** `owner` must detach from `documentId` before the new owner attaches. */
    void ownerDetached(const QString &owner, const QString &documentId);
    void capacityChanged();
    void residentChanged();

private:
    struct Slot {
        QString key;
        QString documentId;
        QString owner;
        qint64 touched = 0;
    };

    QList<Slot> m_slots;
    QString m_lastError;
    int m_capacity = 6;
    qint64 m_clock = 0;

    int indexOfDocument(const QString &documentId) const;
    int indexOfOwner(const QString &owner) const;
    int leastRecentlyUsedFreeSlot() const;
    void trimToCapacity();
};
