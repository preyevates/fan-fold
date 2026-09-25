/** Focused suite for the Library folder tree and the note search that feeds Find.
 *
 * Both models are read-only projections of the one canonical DocumentCollection: they
 * publish the same Document objects and never copy a buffer, so nothing here can create a
 * second authoritative state for a note.
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "documentcollection.h"
#include "librarymodel.h"
#include "searchmodel.h"

namespace {

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
}

struct Fixture {
    QTemporaryDir notes;
    QTemporaryDir state;
    std::unique_ptr<DocumentCollection> value;

    DocumentCollection &collection()
    {
        value = std::make_unique<DocumentCollection>(state.path());
        if (!value->openRoot(notes.path())) {
            qFatal("fixture open failed: %s", qPrintable(value->lastError()));
        }
        return *value;
    }
};

/** Every row of a flattened tree as "depth:name", which is what the panel draws. */
QStringList outline(const LibraryModel &model)
{
    QStringList rows;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex at = model.index(row);
        rows.append(QStringLiteral("%1:%2")
                        .arg(model.data(at, LibraryModel::DepthRole).toInt())
                        .arg(model.data(at, LibraryModel::NameRole).toString()));
    }
    return rows;
}

QStringList searchPaths(const SearchModel &model)
{
    QStringList paths;
    for (int row = 0; row < model.rowCount(); ++row) {
        paths.append(model.data(model.index(row), SearchModel::PathRole).toString());
    }
    return paths;
}

} // namespace

class LibraryModelsTest final : public QObject
{
    Q_OBJECT

private slots:
    void showsAnOrdinaryCollapsedFolderTreeOfTheCatalog();
    void expandingAFolderRevealsExactlyItsOwnChildren();
    void sectionsNameTheContainingFolderAndCollapseAllResets();
    void archiveIsHiddenUntilItIsExplicitlyRequested();
    void currentFolderTracksTheSelectedRowForNewNote();
    void followsCatalogChangesWithoutLosingExpansion();
    void searchMatchesTitlePathAndContentAndRanksThem();
    void searchExcludesArchiveUnlessAskedAndAlwaysExcludesTrash();
    void searchIsCaseInsensitiveAndOffersASnippet();
    void emptyQueryYieldsNoRowsRatherThanTheWholeLibrary();
};

void LibraryModelsTest::showsAnOrdinaryCollapsedFolderTreeOfTheCatalog()
{
    Fixture f;
    writeBytes(f.notes.filePath("Root note.md"), "r\n");
    writeBytes(f.notes.filePath("Work/Plan.md"), "p\n");
    writeBytes(f.notes.filePath("Work/Deep/Detail.md"), "d\n");
    writeBytes(f.notes.filePath("Admin/Bills.md"), "b\n");
    auto &collection = f.collection();

    LibraryModel model(&collection);
    // Folders are the library's compact, initial index. Their contents enter the flat
    // projection only when the user asks for that folder, while root notes remain useful
    // immediately.
    QCOMPARE(outline(model), (QStringList{"0:Admin", "0:Work", "0:Root note"}));

    const QModelIndex admin = model.index(model.rowForFolder(QStringLiteral("Admin")));
    QCOMPARE(model.data(admin, LibraryModel::IsFolderRole).toBool(), true);
    QCOMPARE(model.data(admin, LibraryModel::HasChildrenRole).toBool(), true);
    QCOMPARE(model.data(admin, LibraryModel::ExpandedRole).toBool(), false);
    QCOMPARE(model.data(admin, LibraryModel::NoteCountRole).toInt(), 1);
    QCOMPARE(model.data(model.index(model.rowForFolder(QStringLiteral("Work"))),
                        LibraryModel::NoteCountRole)
                 .toInt(),
             2); // counted recursively

    // Locate the root-level note by PATH rather than by a hand-counted row index: the
    // outline above already pins the ordering, and an index literal here would silently
    // point at the wrong row the moment the fixture gains a file.
    int noteRow = -1;
    for (int r = 0; r < model.rowCount(); ++r) {
        if (model.data(model.index(r), LibraryModel::PathRole).toString()
            == QStringLiteral("Root note.md")) {
            noteRow = r;
            break;
        }
    }
    QVERIFY(noteRow >= 0);
    const QModelIndex note = model.index(noteRow);
    QCOMPARE(model.data(note, LibraryModel::IsFolderRole).toBool(), false);
    QCOMPARE(model.data(note, LibraryModel::PathRole).toString(), QStringLiteral("Root note.md"));
    QVERIFY(!model.data(note, LibraryModel::DocumentIdRole).toString().isEmpty());
    QVERIFY(model.data(note, LibraryModel::DocumentRole).value<Document *>() != nullptr);
}

void LibraryModelsTest::expandingAFolderRevealsExactlyItsOwnChildren()
{
    Fixture f;
    writeBytes(f.notes.filePath("Work/Plan.md"), "p\n");
    writeBytes(f.notes.filePath("Work/Deep/Detail.md"), "d\n");
    writeBytes(f.notes.filePath("Admin/Bills.md"), "b\n");
    auto &collection = f.collection();
    LibraryModel model(&collection);

    QCOMPARE(outline(model), (QStringList{"0:Admin", "0:Work"}));

    QSignalSpy resets(&model, &QAbstractItemModel::modelAboutToBeReset);

    model.setExpanded(QStringLiteral("Work"), true);
    QCOMPARE(outline(model), (QStringList{"0:Admin", "0:Work", "1:Deep", "1:Plan"}));
    QCOMPARE(resets.count(), 0);

    // A single folder switch removes only its own projected children. Its nested folder
    // remains collapsed until separately opened, so a large tree is never materialised
    // merely by opening one level.
    model.toggle(model.rowForFolder(QStringLiteral("Work")));
    QCOMPARE(outline(model), (QStringList{"0:Admin", "0:Work"}));
    QVERIFY(!model.isExpanded(QStringLiteral("Work/Deep")));
}

void LibraryModelsTest::sectionsNameTheContainingFolderAndCollapseAllResets()
{
    Fixture f;
    writeBytes(f.notes.filePath("Work/Plan.md"), "p\n");
    writeBytes(f.notes.filePath("Work/Deep/Detail.md"), "d\n");
    writeBytes(f.notes.filePath("Root note.md"), "r\n");
    auto &collection = f.collection();
    LibraryModel model(&collection);

    // The folder row itself must report the change: a delegate that toggles on its
    // expanded role would otherwise keep re-expanding a folder it already opened.
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setExpanded(QStringLiteral("Work"), true);
    QVERIFY(model.data(model.index(0), LibraryModel::ExpandedRole).toBool());
    bool folderRowSignalled = false;
    for (const auto &args : changed) {
        folderRowSignalled |= args.at(0).value<QModelIndex>().row() == 0;
    }
    QVERIFY(folderRowSignalled);
    model.setExpanded(QStringLiteral("Work/Deep"), true);
    QStringList sections;
    for (int r = 0; r < model.rowCount(); ++r) {
        sections.append(model.data(model.index(r), LibraryModel::NameRole).toString()
                        + QLatin1Char('@')
                        + model.data(model.index(r), LibraryModel::SectionRole).toString());
    }
    // A folder row belongs to its parent's section, so it never sits beneath a header
    // repeating its own name; root rows carry no section at all.
    QCOMPARE(sections, (QStringList{"Work@", "Deep@Work", "Detail@Work/Deep", "Plan@Work",
                                    "Root note@"}));

    model.collapseAll();
    QVERIFY(!model.isExpanded(QStringLiteral("Work")));
    QVERIFY(!model.isExpanded(QStringLiteral("Work/Deep")));
    QCOMPARE(outline(model), (QStringList{"0:Work", "0:Root note"}));
}

void LibraryModelsTest::archiveIsHiddenUntilItIsExplicitlyRequested()
{
    Fixture f;
    writeBytes(f.notes.filePath("Live.md"), "l\n");
    writeBytes(f.notes.filePath("Archive/Filed.md"), "f\n");
    auto &collection = f.collection();
    LibraryModel model(&collection);

    QCOMPARE(model.showArchive(), false);
    QCOMPARE(outline(model), (QStringList{"0:Live"}));
    QCOMPARE(model.rowForFolder(QStringLiteral("Archive")), -1);

    // Archive is hidden entirely until asked for, and is then drawn as an ORDINARY
    // folder row so archived notes remain discoverable. Hiding the container without
    // surfacing its contents leaves no route back from an archived note.
    model.setShowArchive(true);
    QVERIFY(model.rowForFolder(QStringLiteral("Archive")) >= 0);
    const QStringList shown = outline(model);
    QVERIFY(shown.contains(QStringLiteral("0:Live")));
    QVERIFY(shown.contains(QStringLiteral("0:Archive")));

    // It begins as a compact filing index just like every other folder; opening it makes
    // its archived note reachable, rather than a one-way destination.
    QVERIFY(!shown.contains(QStringLiteral("1:Filed")));
    model.setExpanded(QStringLiteral("Archive"), true);
    const QStringList opened = outline(model);
    QVERIFY2(opened.contains(QStringLiteral("1:Filed")),
             qPrintable(QStringLiteral("archived note unreachable after opening Archive; rows were: ")
                        + opened.join(QStringLiteral(", "))));
    // It is still flagged, so the panel draws its "Archived" pill and offers Restore.
    int filedRow = -1;
    for (int r = 0; r < model.rowCount(); ++r) {
        if (model.data(model.index(r), LibraryModel::NameRole).toString()
            == QStringLiteral("Filed")) {
            filedRow = r;
            break;
        }
    }
    QVERIFY(filedRow >= 0);
    QCOMPARE(model.data(model.index(filedRow), LibraryModel::ArchivedRole).toBool(), true);
    QVERIFY(!model.data(model.index(filedRow), LibraryModel::DocumentIdRole)
                 .toString().isEmpty());

    // And hiding it again must hide the folder AND its contents.
    model.setShowArchive(false);
    const QStringList hidden = outline(model);
    QVERIFY(!hidden.contains(QStringLiteral("0:Archive")));
    QVERIFY(!hidden.contains(QStringLiteral("1:Filed")));
}

void LibraryModelsTest::currentFolderTracksTheSelectedRowForNewNote()
{
    Fixture f;
    writeBytes(f.notes.filePath("Root.md"), "r\n");
    writeBytes(f.notes.filePath("Work/Plan.md"), "p\n");
    auto &collection = f.collection();
    LibraryModel model(&collection);
    model.setExpanded(QStringLiteral("Work"), true);

    // Nothing selected yet: a new note belongs in the root.
    QCOMPARE(model.currentFolder(), QString());

    model.setCurrentRow(model.rowForFolder(QStringLiteral("Work")));
    QCOMPARE(model.currentFolder(), QStringLiteral("Work"));

    // Selecting a NOTE targets the folder that note lives in, not the note itself.
    model.setCurrentRow(model.rowForDocument(collection.idForRelativePath("Work/Plan.md")));
    QCOMPARE(model.currentFolder(), QStringLiteral("Work"));
    model.setCurrentRow(model.rowForDocument(collection.idForRelativePath("Root.md")));
    QCOMPARE(model.currentFolder(), QString());
}

void LibraryModelsTest::followsCatalogChangesWithoutLosingExpansion()
{
    Fixture f;
    writeBytes(f.notes.filePath("Work/Plan.md"), "p\n");
    auto &collection = f.collection();
    LibraryModel model(&collection);
    model.setExpanded(QStringLiteral("Work"), true);
    QCOMPARE(outline(model), (QStringList{"0:Work", "1:Plan"}));

    const QString created = collection.createNote(QStringLiteral("Work"));
    QVERIFY(!created.isEmpty());
    QCOMPARE(outline(model), (QStringList{"0:Work", "1:Plan", "1:Untitled"}));
    QVERIFY(model.isExpanded(QStringLiteral("Work")));

    QVERIFY(collection.renameDocument(created, QStringLiteral("Agenda")));
    QCOMPARE(outline(model), (QStringList{"0:Work", "1:Agenda", "1:Plan"}));

    QVERIFY(collection.archive(created));
    QCOMPARE(outline(model), (QStringList{"0:Work", "1:Plan"}));
}

void LibraryModelsTest::searchMatchesTitlePathAndContentAndRanksThem()
{
    Fixture f;
    writeBytes(f.notes.filePath("Budget.md"), "nothing relevant here\n");
    writeBytes(f.notes.filePath("budget-folder/Notes.md"), "also nothing\n");
    writeBytes(f.notes.filePath("Groceries.md"), "remember the budget for milk\n");
    writeBytes(f.notes.filePath("Unrelated.md"), "nothing at all\n");
    auto &collection = f.collection();

    SearchModel model(&collection);
    model.setQuery(QStringLiteral("budget"));
    QCOMPARE(searchPaths(model),
             (QStringList{"Budget.md", "budget-folder/Notes.md", "Groceries.md"}));
    QCOMPARE(model.data(model.index(0), SearchModel::MatchKindRole).toString(),
             QStringLiteral("title"));
    QCOMPARE(model.data(model.index(1), SearchModel::MatchKindRole).toString(),
             QStringLiteral("path"));
    QCOMPARE(model.data(model.index(2), SearchModel::MatchKindRole).toString(),
             QStringLiteral("content"));
    QCOMPARE(model.rowCount(), 3);

    model.setQuery(QStringLiteral("milk"));
    QCOMPARE(searchPaths(model), (QStringList{"Groceries.md"}));
}

void LibraryModelsTest::searchExcludesArchiveUnlessAskedAndAlwaysExcludesTrash()
{
    Fixture f;
    writeBytes(f.notes.filePath("Live budget.md"), "x\n");
    writeBytes(f.notes.filePath("Archive/Old budget.md"), "x\n");
    writeBytes(f.notes.filePath("Doomed budget.md"), "x\n");
    auto &collection = f.collection();
    QVERIFY(collection.moveToTrash(collection.idForRelativePath("Doomed budget.md")));

    SearchModel model(&collection);
    QCOMPARE(model.includeArchive(), false);
    model.setQuery(QStringLiteral("budget"));
    QCOMPARE(searchPaths(model), (QStringList{"Live budget.md"}));

    model.setIncludeArchive(true);
    QCOMPARE(searchPaths(model), (QStringList{"Archive/Old budget.md", "Live budget.md"}));
    QCOMPARE(model.data(model.index(0), SearchModel::ArchivedRole).toBool(), true);
}

void LibraryModelsTest::searchIsCaseInsensitiveAndOffersASnippet()
{
    Fixture f;
    writeBytes(f.notes.filePath("Trip.md"),
               "Packing list for the trip.\nRemember the PASSPORT before leaving home.\n");
    auto &collection = f.collection();
    SearchModel model(&collection);

    model.setQuery(QStringLiteral("passport"));
    QCOMPARE(model.rowCount(), 1);
    const QString snippet = model.data(model.index(0), SearchModel::SnippetRole).toString();
    QVERIFY2(snippet.contains(QStringLiteral("PASSPORT")), qPrintable(snippet));
    QVERIFY(!snippet.contains(QLatin1Char('\n')));
    QCOMPARE(model.data(model.index(0), SearchModel::TitleRole).toString(), QStringLiteral("Trip"));

    model.setQuery(QStringLiteral("TRIP"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), SearchModel::MatchKindRole).toString(),
             QStringLiteral("title"));
}

void LibraryModelsTest::emptyQueryYieldsNoRowsRatherThanTheWholeLibrary()
{
    Fixture f;
    writeBytes(f.notes.filePath("One.md"), "a\n");
    writeBytes(f.notes.filePath("Two.md"), "b\n");
    auto &collection = f.collection();
    SearchModel model(&collection);

    QCOMPARE(model.rowCount(), 0);
    model.setQuery(QStringLiteral("   "));
    QCOMPARE(model.rowCount(), 0);
    model.setQuery(QStringLiteral("o"));
    QCOMPARE(model.rowCount(), 2);
    model.setQuery(QString());
    QCOMPARE(model.rowCount(), 0);
}

int main(int argc, char **argv)
{
    const QString xdg = QDir::temp().filePath(
        QStringLiteral("fanfold-library-xdg-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(xdg);
    qputenv("XDG_DATA_HOME", xdg.toUtf8());
    qputenv("XDG_CONFIG_HOME", QDir(xdg).filePath("config").toUtf8());
    QCoreApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("FanFoldTests"));
    application.setApplicationName(QStringLiteral("library-models"));
    LibraryModelsTest test;
    const int result = QTest::qExec(&test, argc, argv);
    QDir(xdg).removeRecursively();
    return result;
}

#include "tst_librarymodels.moc"
