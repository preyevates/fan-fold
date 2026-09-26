#include "notesadapter.h"

#include "appearancesettings.h"
#include "documentcollection.h"
#include "palette.h"
#include "searchmodel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QUrl>

#include <algorithm>

namespace {
// The engine persists a drag as folderOrder in its index. An untouched folder has
// no entry; its engine fan order is path order, which is not necessarily title order.
QStringList storedDragOrder(const DocumentCollection *collection)
{
    QFile file(collection->metadataPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument index = QJsonDocument::fromJson(file.readAll());
    const QJsonArray stored = index.object().value(QStringLiteral("folderOrder"))
                                  .toObject().value(collection->openFolder()).toArray();
    QStringList ids;
    for (const QJsonValue &value : stored) {
        if (value.isString()) {
            ids.append(value.toString());
        }
    }
    return ids;
}

} // namespace

NotesAdapter::NotesAdapter(DocumentCollection *collection, QObject *parent)
    : QObject(parent)
    , m_collection(collection)
    , m_searchModel(new SearchModel(collection, this))
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
    // would reach the library and never the shell until a restart. "The files are the
    // source of truth" has to hold in both directions.
    //
    // There is deliberately NO auto-join here any more. Fan membership is DERIVED by the
    // engine from the open folder, so a discovered file joins the LIBRARY catalog always
    // and the fan only when it landed in the folder the user is looking at. The previous
    // hook joined every discovered note unconditionally, which is how 61 files written
    // into one subfolder put 62 tabs on the edge.
    if (m_collection) {
        connect(m_collection, &DocumentCollection::documentsChanged,
                this, &NotesAdapter::changed);
        // The fan can change without the catalog changing at all: opening a folder,
        // pinning, archiving. The shell must be told, or the deck keeps the membership it
        // had a moment ago.
        connect(m_collection, &DocumentCollection::fanChanged,
                this, &NotesAdapter::changed);
    }
    connect(m_searchModel, &SearchModel::queryChanged, this, &NotesAdapter::changed);
}

QAbstractItemModel *NotesAdapter::searchModel() const
{
    return m_searchModel;
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
    if (searchActive()) {
        QStringList matches;
        matches.reserve(m_searchModel->rowCount());
        for (int row = 0; row < m_searchModel->rowCount(); ++row) {
            matches.append(m_searchModel->documentIdAt(row));
        }
        return matches;
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
        // empty, suppresses the empty state on a folder with no notes left in it.
        //
        // A missing document that IS dirty stays: that buffer is the user's unsaved work
        // and dropping it from the fan would be the one way to actually lose it.
        //
        // The engine's derivation now applies the same rule, so this is a second, cheap
        // guard rather than the only one — kept because the shell must never render a
        // ghost even if the two ever disagree.
        if (document->missing() && !document->dirty()) {
            continue;
        }
        visible.append(id);
    }
    return visible;
}

bool NotesAdapter::searchActive() const
{
    return m_searchModel && !m_searchModel->query().trimmed().isEmpty();
}

/** How many live notes sit in the OPEN FOLDER.
 *
 * Direct children only. Pinned notes COUNT — a folder whose only note is open in a pinned
 * window is not empty, and telling the user it has no notes yet would be a lie. Archived
 * notes do not: they have left this folder for Archive/ and are shown there.
 */
int NotesAdapter::openFolderCount() const
{
    if (!m_collection) {
        return 0;
    }
    const QString folder = m_collection->openFolder();
    int total = 0;
    const QStringList catalog = m_collection->documentIds();
    for (const QString &id : catalog) {
        const Document *document = m_collection->document(id);
        if (!document || document->trashed() || document->missing() || document->archived()) {
            continue;
        }
        if (document->folder() == folder) {
            ++total;
        }
    }
    return total;
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
            {QStringLiteral("ink"), Palette::resolveInk(paperValue, stored)},
            // The note's own typography override ("" / 0 = follow the global setting), so
            // a pinned window honours it exactly as the deck does.
            {QStringLiteral("fontFamily"), document->fontFamily()},
            {QStringLiteral("fontSize"), document->fontSize()}};
}

QVariantMap NotesAdapter::load()
{
    return loadWithOrder(nullptr);
}

QVariantMap NotesAdapter::loadWithOrder(const QStringList *requestedOrder)
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
    QVariantMap fontFamilies;
    QVariantMap fontSizes;
    QStringList ids;

    // The per-note size control offers exactly the global control's legible range, read
    // from the one table AppearanceSettings clamps against rather than restated here.
    double fontLow = 0;
    double fontHigh = 0;
    AppearanceSettings::numericRange(QStringLiteral("fontSize"), &fontLow, &fontHigh);

    const QVariantMap active = Palette::paletteOf(m_palette);
    const QStringList offered = Palette::paletteColors(m_palette);

    if (m_collection) {
        ids = visibleFanIds();
        if (requestedOrder) {
            ids = *requestedOrder;
        } else if (!searchActive()) {
            // Index zero is the BOTTOM stick. Reverse the array so the visible
            // top-to-bottom labels read A to Z without rewriting stored drag order.
            const auto byTitle = [this](const QString &left, const QString &right) {
                const Document *a = m_collection->document(left);
                const Document *b = m_collection->document(right);
                const QString aTitle = a ? a->title() : QString();
                const QString bTitle = b ? b->title() : QString();
                const int folded = QString::compare(aTitle, bTitle, Qt::CaseInsensitive);
                if (folded != 0) {
                    return folded > 0;
                }
                const int exact = QString::compare(aTitle, bTitle, Qt::CaseSensitive);
                return exact != 0 ? exact > 0 : left > right;
            };
            const QStringList saved = storedDragOrder(m_collection);
            if (saved.isEmpty()) {
                std::sort(ids.begin(), ids.end(), byTitle);
            } else {
                // Existing dragged notes retain their stored relative order, including
                // absent/pinned ids' slots. New discoveries are not in the persisted
                // order: sort only those, and append them after the manual sequence.
                const QSet<QString> savedIds(saved.cbegin(), saved.cend());
                QStringList additions;
                QStringList manuallyOrdered;
                for (const QString &id : std::as_const(ids)) {
                    (savedIds.contains(id) ? manuallyOrdered : additions).append(id);
                }
                std::sort(additions.begin(), additions.end(), byTitle);
                ids = manuallyOrdered + additions;
            }
        }
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
            fontFamilies[id] = document->fontFamily();
            fontSizes[id] = document->fontSize();
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
            // Per-note typography overrides: "" / 0 mean follow the global Settings value.
            {QStringLiteral("fontFamily"), fontFamilies},
            {QStringLiteral("fontSize"), fontSizes},
            {QStringLiteral("fontSizeRange"),
             QVariantMap{{QStringLiteral("min"), int(fontLow)}, {QStringLiteral("max"), int(fontHigh)}}},
            {QStringLiteral("palette"), m_palette},
            {QStringLiteral("palettes"), Palette::paletteChoices()},
            {QStringLiteral("activePalette"), active},
            {QStringLiteral("paletteLabel"), active.value(QStringLiteral("label"))},
            // Keep both arrays in the same presentation order: the deck reads `order`,
            // while selection indexes into `ids`.
            {QStringLiteral("order"), QVariant(ids)},
            // How many notes the LIBRARY holds, which is NOT the size of the fan.
            //
            // Under folder scoping the fan is a window onto ONE folder, so it is smaller
            // than the library by design. This count answers "is there anything in this
            // library at all", which is the FIRST-RUN question, and must never be bound
            // to the fan: doing so announces "no notes yet" over a library full of notes
            // that simply live in another folder.
            {QStringLiteral("libraryCount"), m_collection ? int(m_collection->documentIds().size()) : 0},
            // How many live notes are in the OPEN folder. This is the question the
            // scoped empty state asks: an empty FOLDER inside a non-empty library needs
            // a lighter panel than the first-run welcome.
            {QStringLiteral("folderCount"), openFolderCount()},
            // Root-relative open folder, empty for the library root. The shell shows it
            // and offers the way back to the root.
            {QStringLiteral("openFolder"), m_collection ? m_collection->openFolder() : QString()},
            {QStringLiteral("searchActive"), searchActive()},
            {QStringLiteral("searchQuery"), m_searchModel ? m_searchModel->query() : QString()},
            {QStringLiteral("searchCount"), m_searchModel ? m_searchModel->rowCount() : 0},
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

QVariantMap NotesAdapter::applyColourToOpenFolder(const QString &mode, const QString &value)
{
    if (!m_collection) {
        return failure(QStringLiteral("No library is open"));
    }
    const bool ink = mode == QStringLiteral("ink");
    if (!ink && mode != QStringLiteral("paper")) {
        return failure(QStringLiteral("Colour mode must be \"paper\" or \"ink\""));
    }
    const QString resolved = ink ? Palette::normalizeInk(value) : Palette::normalizeColor(value);
    if (resolved.isEmpty()) {
        return failure(ink ? QStringLiteral("Note ink must be \"auto\" or a literal #rgb or #rrggbb value")
                           : QStringLiteral("Note colour must be a literal #rgb or #rrggbb value"));
    }
    const QString folder = m_collection->openFolder();
    const int applied = ink ? m_collection->setInkForFolder(folder, resolved)
                            : m_collection->setPaperForFolder(folder, resolved);
    if (applied < 0) {
        return failure(QStringLiteral("Folder colour not saved; the library index is unwritable"));
    }
    emit changed();
    QVariantMap result = load();
    result.insert(QStringLiteral("applied"), applied);
    return result;
}

QVariantMap NotesAdapter::setNoteFont(const QString &id, const QString &family, int size)
{
    if (!m_collection || !m_collection->document(id)) {
        return failure(QStringLiteral("Unknown note ID"));
    }
    if (!m_collection->setNoteFont(id, family.trimmed(), size)) {
        return failure(QStringLiteral("Note font refused: unsafe family or size out of range"));
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
    if (searchActive()) {
        return failure(QStringLiteral("Search results cannot be reordered"));
    }
    QStringList sorted = ids;
    QStringList expected = visibleFanIds();
    sorted.sort();
    expected.sort();
    if (sorted != expected) {
        return failure(QStringLiteral("Order must list every fanned note exactly once"));
    }
    // setFanOrder persists an explicit drag even if the path-based fan already
    // matches it (including after external notes join the folder).
    if (!m_collection->setFanOrder(ids)) {
        return failure(QStringLiteral("Order not saved; the library index is unwritable"));
    }
    emit changed();
    // The drag's immediate response keeps the user's chosen order.
    return loadWithOrder(&ids);
}

QVariantMap NotesAdapter::openFolder(const QString &folder)
{
    if (!m_collection) {
        return failure(QStringLiteral("No library is open"));
    }
    // Flush first. Scoping away from a folder takes the open card's note off the fan, and
    // an unsaved buffer whose card is about to be replaced is how edits go missing.
    if (!m_collection->flushPendingSaves()) {
        return failure(m_collection->lastError());
    }
    if (!m_collection->setOpenFolder(folder)) {
        const QString refusal = m_collection->lastError();
        return failure(refusal.isEmpty() ? QStringLiteral("That folder cannot be opened")
                                         : refusal);
    }
    emit changed();
    return load();
}
