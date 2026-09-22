#include "notesadapter.h"

#include "documentcollection.h"
#include "palette.h"

#include <QDir>
#include <QSettings>
#include <QUrl>

#include <algorithm>

NotesAdapter::NotesAdapter(DocumentCollection *collection, QObject *parent)
    : QObject(parent)
    , m_collection(collection)
    // Pastels stays the DEFAULT palette even though the ColorBrewer group is listed
    // first: a new library should open with the gentle originals, not Bold red.
    // The last pick is restored from settings — with 25 palettes the choice is real
    // state, not a session whim; an unknown stored key falls back safely.
    , m_palette(QStringLiteral("pastels"))
{
    const QString stored =
        QSettings().value(QStringLiteral("appearance/palette")).toString();
    if (Palette::paletteKeys().contains(stored)) {
        m_palette = stored;
    }
    // Relay the ENGINE's own reconcile into the shell. The engine watches the folder and
    // reconciles on its own, but this adapter emits changed() only from its own write
    // paths, so a .md file created OUTSIDE the app (another editor, a sync, a script)
    // would reach the library and never the fan until a restart. "The files are the
    // source of truth" has to hold in both directions.
    //
    // The engine deliberately does NOT put a discovered note in the fan — that is the
    // shell's policy, applied once at startup in main.cpp. documentAdded fires per newly
    // discovered id, so the SAME rule runs here for a note that appears while the app is
    // running: not archived, not trashed, not pinned (a pinned note already has its own
    // window and must not be shown twice).
    if (m_collection) {
        connect(m_collection, &DocumentCollection::documentAdded, this,
                [this](const QString &id) {
                    const Document *document = m_collection->document(id);
                    if (document && !document->archived() && !document->trashed()
                        && !document->pinned() && !document->inFan()) {
                        m_collection->joinFan(id);
                    }
                    // joinFan() changes the fan AFTER documentsChanged has already been
                    // delivered for this reconcile (the engine emits documentsChanged
                    // first, then documentAdded per new id), so the shell must be told
                    // again or the deck keeps the membership it had a moment ago.
                    emit changed();
                });
        connect(m_collection, &DocumentCollection::documentsChanged,
                this, &NotesAdapter::changed);
    }
}

QVariantMap NotesAdapter::failure(const QString &message) const
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QStringList NotesAdapter::visibleFanIds() const
{
    if (!m_collection) {
        return {};
    }
    QStringList visible;
    const QStringList fan = m_collection->fanIds();
    for (const QString &id : fan) {
        const Document *document = m_collection->document(id);
        if (!document) {
            continue;
        }
        // A note whose file has gone from disk and which holds no unsaved buffer is a
        // GHOST: the engine keeps its record so a reappearing file is recognised, but
        // there is nothing to render and nothing the user can do with it. Drawing it
        // anyway produces a blank, unlabelled stick and, because the deck is then not
        // empty, suppresses the first-run empty state on a library with no notes left
        // in it.
        //
        // A missing document that IS dirty stays: that buffer is the user's unsaved work
        // and dropping it from the fan would be the one way to actually lose it.
        if (document->missing() && !document->dirty()) {
            continue;
        }
        visible.append(id);
    }
    return visible;
}

QVariantMap NotesAdapter::colourOf(const QString &id) const
{
    // Paper and ink for ANY document, whether or not it is currently on the fan.
    //
    // load()'s paper/ink maps are keyed by visibleFanIds(), which is correct for the deck
    // but wrong for a pinned note: pinning REMOVES a note from the fan, so a pinned
    // window asking the manifest for its own colours gets `undefined` and writes
    // "--ink: undefined" into the editor's stylesheet — text present and selectable but
    // painted invisibly, with the footer still reading "Unchanged".
    if (!m_collection) {
        return {};
    }
    const Document *document = m_collection->document(id);
    if (!document) {
        return {};
    }
    const QString paperValue = Palette::normalizeColor(document->paper()).isEmpty()
        ? Palette::defaultPaper()
        : Palette::normalizeColor(document->paper());
    const QString stored = Palette::normalizeInk(document->ink()).isEmpty()
        ? Palette::autoInk()
        : Palette::normalizeInk(document->ink());
    return {{QStringLiteral("paper"), paperValue},
            {QStringLiteral("ink"), Palette::resolveInk(paperValue, stored)}};
}

QVariantMap NotesAdapter::load()
{
    QVariantMap files;
    QVariantMap titles;
    QVariantMap icons;
    QVariantMap paper;
    QVariantMap inks;
    QVariantMap inkStored;
    QVariantMap inkModes;
    QVariantMap inPalette;
    QVariantMap inkInPalette;
    QStringList ids;

    const QVariantMap active = Palette::paletteOf(m_palette);
    const QStringList offered = Palette::paletteColors(m_palette);

    if (m_collection) {
        ids = visibleFanIds();
        for (const QString &id : std::as_const(ids)) {
            const Document *document = m_collection->document(id);
            if (!document) {
                continue;
            }
            // The engine stores a literal paper and either "auto" or a literal ink,
            // which is the shell's own model, so both pass through without translation.
            const QString paperValue = Palette::normalizeColor(document->paper()).isEmpty()
                ? Palette::defaultPaper()
                : Palette::normalizeColor(document->paper());
            const QString stored = Palette::normalizeInk(document->ink()).isEmpty()
                ? Palette::autoInk()
                : Palette::normalizeInk(document->ink());

            files[id] = document->fileName();
            titles[id] = document->title();
            // Either a THEME NAME (Kirigami.Icon resolves it against the system icon
            // theme, so it follows whichever theme is set) or an absolute file URL for a
            // user-supplied file in Assets/icons/. Empty when no icon is set.
            const QString storedIcon = document->icon();
            if (storedIcon.isEmpty()) {
                icons[id] = QString();
            } else if (storedIcon.startsWith(QStringLiteral("theme:"))) {
                icons[id] = storedIcon.mid(6);
            } else {
                icons[id] =
                    QUrl::fromLocalFile(QDir(m_collection->rootPath()).filePath(storedIcon))
                        .toString();
            }
            paper[id] = paperValue;
            inks[id] = Palette::resolveInk(paperValue, stored);
            inkStored[id] = stored;
            inkModes[id] = stored == Palette::autoInk() ? QStringLiteral("auto")
                                                        : QStringLiteral("explicit");
            inPalette[id] = offered.contains(paperValue);
            inkInPalette[id] = stored != Palette::autoInk() && offered.contains(stored);
        }
    }

    return {{QStringLiteral("ok"), true},
            {QStringLiteral("ids"), QVariant(ids)},
            {QStringLiteral("files"), files},
            {QStringLiteral("titles"), titles},
            {QStringLiteral("icons"), icons},
            {QStringLiteral("paper"), paper},
            {QStringLiteral("ink"), inks},
            {QStringLiteral("inkStored"), inkStored},
            {QStringLiteral("inkMode"), inkModes},
            {QStringLiteral("inPalette"), inPalette},
            {QStringLiteral("inkInPalette"), inkInPalette},
            {QStringLiteral("palette"), m_palette},
            {QStringLiteral("palettes"), Palette::paletteChoices()},
            {QStringLiteral("activePalette"), active},
            {QStringLiteral("paletteLabel"), active.value(QStringLiteral("label"))},
            // The fan IS the order, so `order` and `ids` are the same list.
            // Presentation reads `order` for the deck and `ids` for selection, and they
            // must stay consistent.
            {QStringLiteral("order"), QVariant(ids)},
            // How many notes the LIBRARY holds, which is NOT the size of the fan.
            //
            // The fan is a curated subset: archiving a note removes it from the fan, and
            // so does pinning it to its own window. Binding the first-run empty state to
            // the fan would therefore claim "This folder has no notes yet" while notes
            // sit in Archive/ or open in a pinned window. The empty state must mean
            // "there is nothing here", not "nothing is on the fan right now".
            {QStringLiteral("libraryCount"), m_collection ? int(m_collection->documentIds().size()) : 0},
            {QStringLiteral("migrated"), false},
            {QStringLiteral("warning"), QString()}};
}

QVariantMap NotesAdapter::setPaper(const QString &id, const QString &color)
{
    if (!m_collection || !m_collection->document(id)) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    const QString resolved = Palette::normalizeColor(color);
    if (resolved.isEmpty()) {
        return failure(QStringLiteral("Note colour must be a literal #rgb or #rrggbb value"));
    }
    if (!m_collection->setPaper(id, resolved)) {
        return failure(QStringLiteral("Note colour not saved; the library index is unwritable"));
    }
    emit changed();
    return load();
}

QVariantMap NotesAdapter::setInk(const QString &id, const QString &value)
{
    if (!m_collection || !m_collection->document(id)) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    const QString resolved = Palette::normalizeInk(value);
    if (resolved.isEmpty()) {
        return failure(
            QStringLiteral("Note ink must be \"auto\" or a literal #rgb or #rrggbb value"));
    }
    if (!m_collection->setInk(id, resolved)) {
        return failure(QStringLiteral("Note ink not saved; the library index is unwritable"));
    }
    emit changed();
    return load();
}

QVariantMap NotesAdapter::setPalette(const QString &key)
{
    if (!Palette::paletteKeys().contains(key)) {
        return failure(QStringLiteral("Unknown palette"));
    }
    // A palette change is a CHOOSER change: it must never repaint a note. Nothing below
    // touches any document, which is what makes that true by construction.
    m_palette = key;
    QSettings().setValue(QStringLiteral("appearance/palette"), key);
    emit changed();
    return load();
}

QVariantMap NotesAdapter::setOrder(const QStringList &ids)
{
    if (!m_collection) {
        return failure(QStringLiteral("No library is open"));
    }
    QStringList sorted = ids;
    QStringList expected = visibleFanIds();
    sorted.sort();
    expected.sort();
    if (sorted != expected) {
        return failure(QStringLiteral("Order must list every fanned note exactly once"));
    }
    if (!m_collection->setFanOrder(ids)) {
        return failure(QStringLiteral("Order not saved; the library index is unwritable"));
    }
    emit changed();
    return load();
}
