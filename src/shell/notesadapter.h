#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class DocumentCollection;
class SearchModel;

/**
 * Serves the shell's `notesStore` contract from Fan Fold's DocumentCollection.
 *
 * The presentation layer does not depend on library size: the fan is a Repeater over
 * `order`, and every pitch calculation derives from `order.length`, so an order of any
 * length drives it unchanged. This class supplies the store beneath it.
 *
 * The method signatures and returned map shapes are fixed by Main.qml::applyManifest(),
 * which reads those keys directly. The data behind them is the real on-disk library:
 * markdown files discovered recursively, atomic writes, archive/trash, and per-note
 * paper/ink persisted in the engine's own metadata index.
 *
 * Note identity: document ids are opaque on both sides and pass through unchanged.
 *
 * Scope: this adapter exposes the FAN (`DocumentCollection::fanIds()`), which since 0.2.0
 * is the notes of the currently OPEN FOLDER — not the whole catalog. That is what the fan
 * renders, and it is what keeps a large library from turning into an unusable
 * hundred-stick deck.
 */
class NotesAdapter final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *searchModel READ searchModel CONSTANT)

public:
    explicit NotesAdapter(DocumentCollection *collection, QObject *parent = nullptr);

    /** The engine's whole-library search projection. Its query is transient shell state. */
    QAbstractItemModel *searchModel() const;

    /** One complete manifest snapshot.
     *
     * Every key Main.qml::applyManifest() reads is present:
     * ok, ids, files, titles, paper, ink, inkStored, inkMode, inPalette, inkInPalette,
     * palette, palettes, activePalette, paletteLabel, order, migrated, warning.
     */
    Q_INVOKABLE QVariantMap load();

    /** Paper and ink for one document by id, independent of fan membership.
     *
     *  A pinned note is NOT on the fan, so load()'s per-note colour maps do not contain
     *  it. Pinned windows must read their colours through here or they render with
     *  undefined CSS variables. */
    Q_INVOKABLE QVariantMap colourOf(const QString &id) const;

    /** Assign one literal paper colour to one note. Other notes are never touched and no
     *  Markdown is written. @return a fresh load() on success. */
    Q_INVOKABLE QVariantMap setPaper(const QString &id, const QString &color);

    /** Assign one note's ink: the sentinel "auto", or a literal colour. An explicit
     *  low-contrast choice is accepted as given. @return a fresh load() on success. */
    Q_INVOKABLE QVariantMap setInk(const QString &id, const QString &value);
    /** One-time bulk colour: write `value` into the OWN stored paper (mode "paper") or
     *  ink (mode "ink", "auto" allowed) of every note openFolderCount() counts. Each note
     *  stays individually changeable afterwards; this is not a folder-default rule.
     *  Returns the refreshed manifest plus `applied` (how many notes), like setPaper(). */
    Q_INVOKABLE QVariantMap applyColourToOpenFolder(const QString &mode, const QString &value);
    /** Per-note typography override: `family` empty and `size` 0 each mean "follow the
     *  global Settings value". Stored in library metadata only; the .md is never written.
     *  An unsafe family or an out-of-range size is refused. @return a fresh load(). */
    Q_INVOKABLE QVariantMap setNoteFont(const QString &id, const QString &family, int size);

    /** Select the palette offering the CHOICES. Repaints nothing; persisted so the panel
     *  reopens on the same palette. */
    Q_INVOKABLE QVariantMap setPalette(const QString &key);

    /** Persist the OPEN FOLDER's tab order. Must be a permutation of the current fan. */
    Q_INVOKABLE QVariantMap setOrder(const QStringList &ids);

    /** Scope the fan to `folder` (root-relative; empty is the library root).
     *  @return a fresh load() on success, or a failure map carrying the engine's refusal. */
    Q_INVOKABLE QVariantMap openFolder(const QString &folder);

    /** The selected palette key; used by the appearance adapter's warning text. */
    QString paletteKey() const { return m_palette; }

signals:
    void changed();

private:
    QVariantMap failure(const QString &message) const;
    QVariantMap loadWithOrder(const QStringList *requestedOrder);

    /** The fan ids that actually have something to render.
     *
     * The engine keeps a record for a note whose file has disappeared, so a reappearing
     * file is recognised rather than duplicated. Those ghosts must not reach the
     * presentation layer: the fan draws one stick per id, so a ghost paints a blank,
     * unlabelled stick and -- worse -- makes an otherwise empty library look non-empty,
     * suppressing the first-run empty state. A missing document with unsaved edits is
     * kept, because that buffer is the user's work.
     */
    QStringList visibleFanIds() const;

    /** Live notes in the open folder, direct children only; pinned ones count. */
    int openFolderCount() const;
    bool searchActive() const;

    DocumentCollection *m_collection = nullptr;
    SearchModel *m_searchModel = nullptr;
    QString m_palette;
};
