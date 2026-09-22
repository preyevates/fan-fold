#pragma once

#include <QObject>
#include <QVariantMap>

class DocumentCollection;

/**
 * The presentation layer's `store` contract, served from DocumentCollection.
 *
 * The QML/JS layer was written against an explicit-save store with optimistic
 * concurrency: the caller holds the SHA-256 of the bytes it loaded and hands it back on
 * save, which is refused if the file moved underneath. The engine underneath is instead
 * debounced autosave with a crash-recovery journal and its own conflict detection. This
 * adapter reconciles the two rather than faking either:
 *
 * - `load`/`probe`/`save`/`info`/`rename` keep their exact signatures and map shapes,
 *   because Main.qml, app.js and editor.js read those keys directly.
 * - The revision token is the SHA-256 of the note's current content, so the presentation
 *   layer's optimistic bookkeeping keeps working unchanged.
 * - Conflict detection is not reimplemented here. The engine already refuses a save whose
 *   on-disk revision moved and exposes that as `Document::conflict()`; this adapter
 *   reports that verdict rather than racing it with a second check.
 *
 * One behavioural difference is deliberate: a save requested here is committed through
 * the engine, and edits also reach disk on the 250 ms autosave quiet period even when
 * nothing calls save(). There is no "unsaved changes lost on forced termination" window.
 */
class StoreAdapter final : public QObject
{
    Q_OBJECT

public:
    explicit StoreAdapter(DocumentCollection *collection, QObject *parent = nullptr);

    /** Content plus a revision token. @return {ok,text,revision,filename,title}. */
    Q_INVOKABLE QVariantMap load(const QString &id);

    /** Read-only state probe, polled once a second by the editor.
     * @return {ok,revision,conflict,committed}.
     *
     * Treating ANY revision movement as an external edit is only correct for an
     * explicit-save store, where nothing but another program can move the file. Under
     * autosave it is wrong: the app's own commit moves the revision, so a note that was
     * only ever typed into reports an external conflict a second after it is created.
     *
     * The engine, not a digest race, is therefore the authority:
     *  - `conflict` is the engine's own external-change verdict;
     *  - `committed` says the engine has written exactly the buffer this adapter was last
     *    handed, so the editor can adopt that revision instead of reading it as a
     *    stranger.
     */
    Q_INVOKABLE QVariantMap probe(const QString &id);

    /** Commit `text` for `id`.
     *
     * `expected` is checked against the revision this adapter last handed out, so a stale
     * caller is refused; the authoritative on-disk check remains the engine's.
     * @return {ok,revision} or {ok:false,error}.
     */
    Q_INVOKABLE QVariantMap save(const QString &id, const QString &text, const QString &expected);

    /** Read-only file facts for the information panel.
     * @return {ok,filename,path,bytes,modified,modifiedEpoch,revision,loadedRevision,
     * matchesLoaded}. */
    Q_INVOKABLE QVariantMap info(const QString &id);

    /** Rename one note's file inside its own folder; the note id is unchanged. */
    Q_INVOKABLE QVariantMap rename(const QString &id, const QString &title, const QString &expected);

    /** Push the editor's current text into the engine without forcing a commit, so the
     *  250 ms debounced autosave and the recovery journal see every keystroke. */
    Q_INVOKABLE bool updateContent(const QString &id, const QString &text);

private:
    QVariantMap failure(const QString &message) const;
    static QString digestOf(const QString &text);

    DocumentCollection *m_collection = nullptr;
    /** Revision last handed to a caller, per note id; drives `matchesLoaded`. */
    QHash<QString, QString> m_loaded;
    /** Digest of the text the editor last pushed through updateContent(), per note id.
     *  Lets probe() distinguish the engine committing OUR buffer from a genuinely
     *  external write. */
    QHash<QString, QString> m_buffer;
};
