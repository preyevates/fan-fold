#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <sys/stat.h>

#include "documentcollection.h"

namespace {

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

quint64 inodeOf(const QString &path)
{
    struct stat metadata {};
    if (::stat(QFile::encodeName(path).constData(), &metadata) != 0) {
        return 0;
    }
    return static_cast<quint64>(metadata.st_ino);
}

void atomicReplace(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
    QVERIFY2(file.commit(), qPrintable(file.errorString()));
}

struct Fixture {
    QTemporaryDir notes;
    QTemporaryDir state;
    std::unique_ptr<DocumentCollection> value;

    Fixture()
    {
        QVERIFY(notes.isValid());
        QVERIFY(state.isValid());
    }

    DocumentCollection &collection()
    {
        value = std::make_unique<DocumentCollection>(state.path());
        if (!value->openRoot(notes.path())) {
            qFatal("fixture open failed: %s", qPrintable(value->lastError()));
        }
        return *value;
    }
};

} // namespace

class DocumentCollectionTest final : public QObject
{
    Q_OBJECT

private slots:
    void discoversMarkdownRecursivelyWithoutFollowingLinks();
    void assignsStableIdsAcrossRenameAndAtomicReplacement();
    void createsUniqueUntitledNotes();
    void debouncesAtomicAutosaveFor250Milliseconds();
    void externalChangesNeverOverwriteDirtyContent();
    void externalDeleteKeepsDirtyRecoveryVisible();
    void pendingEditRecoversAfterCrashWithoutClobberingExternalChange();
    void normalCloseFlushesPendingSave();
    void archiveAndRestoreAreReversibleAndNoClobber();
    void trashAndRestoreNeverPermanentlyUnlink();
    void pinAndLiteralColoursPersistOutsideMarkdown();
    void pinnedWindowSizePersistsOutsideMarkdown();
    void watcherRearmsAfterAtomicReplacement();
    void externalReconcileEmitsDocumentAndModelChanges();
    void invalidUtf8AndHardlinksAreRefused();

    // Audit corrections: catalog/fan separation, transactional root switching,
    // collision-safe restore, file stat fields, quiet-period precision, and
    // reconciliation that stays silent when nothing actually changed.
    void catalogOrderIsIndependentOfThePersistedFanOrder();
    void fanWorkingSetPersistsAndReleasesArchivedOrTrashedNotes();
    void failedRootSwitchLeavesThePreviousLibraryIntact();
    void successfulRootSwitchFlushesPendingSavesFirst();
    void archiveRestoreFallsBackToAUniqueNameInTheOriginalFolder();
    void exposesFileStatFieldsForTheInformationPanel();
    void quietPeriodRestartsOnEveryEdit();
    void noOpReconciliationNeitherResetsTheModelNorRewritesMetadata();
    void renameMovesOneCatalogRowWithoutResettingTheModel();
    void discoversHundredsOfNotesAcrossSubfoldersWithoutChurn();
};

void DocumentCollectionTest::discoversMarkdownRecursivelyWithoutFollowingLinks()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "alpha\n");
    writeBytes(f.notes.filePath("nested/Beta.MD"), "beta\n");
    writeBytes(f.notes.filePath("ignore.txt"), "ignored\n");
    QVERIFY(QFile::link(f.notes.filePath("Alpha.md"), f.notes.filePath("link.md")));
    QVERIFY(QFile::link(QDir::tempPath(), f.notes.filePath("outside-dir")));

    auto &collection = f.collection();
    QCOMPARE(collection.documentIds().size(), 2);
    QVERIFY(collection.idForRelativePath("Alpha.md").size() > 10);
    QVERIFY(collection.idForRelativePath("nested/Beta.MD").size() > 10);
    QVERIFY(collection.idForRelativePath("link.md").isEmpty());
    QVERIFY(collection.idForRelativePath("outside-dir/anything.md").isEmpty());
}

void DocumentCollectionTest::assignsStableIdsAcrossRenameAndAtomicReplacement()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "one\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");
    QVERIFY(!id.isEmpty());

    QVERIFY(QFile::rename(f.notes.filePath("Alpha.md"), f.notes.filePath("Renamed.md")));
    collection.reconcileNow();
    QCOMPARE(collection.idForRelativePath("Renamed.md"), id);
    QVERIFY(collection.idForRelativePath("Alpha.md").isEmpty());

    atomicReplace(f.notes.filePath("Renamed.md"), "two\n");
    collection.reconcileNow();
    QCOMPARE(collection.idForRelativePath("Renamed.md"), id);
    QCOMPARE(collection.document(id)->content(), QStringLiteral("two\n"));
}

void DocumentCollectionTest::createsUniqueUntitledNotes()
{
    Fixture f;
    writeBytes(f.notes.filePath("Untitled.md"), "existing\n");
    auto &collection = f.collection();

    const QString first = collection.createNote();
    const QString second = collection.createNote("nested");
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());
    QCOMPARE(collection.document(first)->relativePath(), QStringLiteral("Untitled 2.md"));
    QCOMPARE(collection.document(second)->relativePath(), QStringLiteral("nested/Untitled.md"));
    QVERIFY(QFileInfo::exists(f.notes.filePath("Untitled 2.md")));
    QVERIFY(QFileInfo::exists(f.notes.filePath("nested/Untitled.md")));
}

void DocumentCollectionTest::debouncesAtomicAutosaveFor250Milliseconds()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "old\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");
    QFile oldHandle(f.notes.filePath("Alpha.md"));
    QVERIFY(oldHandle.open(QIODevice::ReadOnly));
    const quint64 oldInode = inodeOf(f.notes.filePath("Alpha.md"));

    QVERIFY(collection.updateContent(id, "new\n"));
    QVERIFY(collection.document(id)->dirty());
    QTest::qWait(180);
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("old\n"));
    QTRY_COMPARE_WITH_TIMEOUT(readBytes(f.notes.filePath("Alpha.md")), QByteArray("new\n"), 1200);
    QVERIFY(!collection.document(id)->dirty());
    QVERIFY(collection.document(id)->saveError().isEmpty());
    QVERIFY(inodeOf(f.notes.filePath("Alpha.md")) != oldInode);
    QCOMPARE(oldHandle.readAll(), QByteArray("old\n"));
}

void DocumentCollectionTest::externalChangesNeverOverwriteDirtyContent()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "disk\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");

    QVERIFY(collection.updateContent(id, "mine\n"));
    atomicReplace(f.notes.filePath("Alpha.md"), "external\n");
    collection.reconcileNow();
    QTest::qWait(400);

    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("external\n"));
    QCOMPARE(collection.document(id)->content(), QStringLiteral("mine\n"));
    QVERIFY(collection.document(id)->dirty());
    QVERIFY(collection.document(id)->conflict());
    QVERIFY(!collection.document(id)->saveError().isEmpty());
}

void DocumentCollectionTest::externalDeleteKeepsDirtyRecoveryVisible()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "disk\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");
    QVERIFY(collection.updateContent(id, "mine\n"));
    QVERIFY(QFile::remove(f.notes.filePath("Alpha.md")));

    collection.reconcileNow();
    QVERIFY(collection.document(id));
    QCOMPARE(collection.document(id)->content(), QStringLiteral("mine\n"));
    QVERIFY(collection.document(id)->missing());
    QVERIFY(collection.document(id)->conflict());
}

void DocumentCollectionTest::pendingEditRecoversAfterCrashWithoutClobberingExternalChange()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "base\n");
    QString id;
    {
        auto *crashed = new DocumentCollection(f.state.path());
        QVERIFY(crashed->openRoot(f.notes.path()));
        id = crashed->idForRelativePath("Alpha.md");
        QVERIFY(crashed->updateContent(id, "pending\n"));
        atomicReplace(f.notes.filePath("Alpha.md"), "external\n");
        delete crashed; // Deliberately no flush: model a process that died.
    }

    DocumentCollection recovered(f.state.path());
    QVERIFY(recovered.openRoot(f.notes.path()));
    auto *document = recovered.document(id);
    QVERIFY(document);
    QCOMPARE(document->content(), QStringLiteral("pending\n"));
    QVERIFY(document->dirty());
    QVERIFY(document->conflict());
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("external\n"));
    QVERIFY(recovered.hasRecovery(id));
}

void DocumentCollectionTest::normalCloseFlushesPendingSave()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "base\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");
    QVERIFY(collection.updateContent(id, "closing\n"));
    QVERIFY(collection.flushPendingSaves());
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("closing\n"));
    QVERIFY(!collection.hasRecovery(id));
}

void DocumentCollectionTest::archiveAndRestoreAreReversibleAndNoClobber()
{
    Fixture f;
    writeBytes(f.notes.filePath("nested/Alpha.md"), "alpha\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("nested/Alpha.md");

    QVERIFY(collection.archive(id));
    QVERIFY(collection.document(id)->archived());
    QCOMPARE(collection.document(id)->relativePath(), QStringLiteral("Archive/nested/Alpha.md"));
    QVERIFY(QFileInfo::exists(f.notes.filePath("Archive/nested/Alpha.md")));

    // A note that took the original name while this one was filed away is never
    // clobbered: the restore still happens, beside it, under a unique name.
    writeBytes(f.notes.filePath("nested/Alpha.md"), "occupant\n");
    collection.reconcileNow();
    QVERIFY2(collection.restoreArchive(id),
             qPrintable(collection.document(id)->saveError() + QStringLiteral(" | ") + collection.lastError()));
    QVERIFY(!collection.document(id)->archived());
    QCOMPARE(collection.document(id)->relativePath(), QStringLiteral("nested/Alpha (restored).md"));
    QCOMPARE(readBytes(f.notes.filePath("nested/Alpha.md")), QByteArray("occupant\n"));
    QCOMPARE(readBytes(f.notes.filePath("nested/Alpha (restored).md")), QByteArray("alpha\n"));
    QVERIFY(!QFileInfo::exists(f.notes.filePath("Archive/nested/Alpha.md")));

    // With the original name free, archive/restore is an exact round trip again.
    QVERIFY(collection.archive(id));
    QCOMPARE(collection.document(id)->relativePath(),
             QStringLiteral("Archive/nested/Alpha (restored).md"));
    QVERIFY2(collection.restoreArchive(id),
             qPrintable(collection.document(id)->saveError() + QStringLiteral(" | ") + collection.lastError()));
    QCOMPARE(collection.document(id)->relativePath(), QStringLiteral("nested/Alpha (restored).md"));
    QCOMPARE(readBytes(f.notes.filePath("nested/Alpha (restored).md")), QByteArray("alpha\n"));
}

void DocumentCollectionTest::trashAndRestoreNeverPermanentlyUnlink()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "alpha\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");

    QVERIFY(collection.moveToTrash(id));
    QVERIFY(collection.document(id)->trashed());
    QVERIFY(!QFileInfo::exists(f.notes.filePath("Alpha.md")));
    QVERIFY(!collection.document(id)->trashPath().isEmpty());
    QVERIFY(QFileInfo::exists(collection.document(id)->trashPath()));

    QVERIFY2(collection.restoreFromTrash(id),
             qPrintable(collection.document(id)->saveError() + QStringLiteral(" | ") + collection.lastError()));
    QVERIFY(!collection.document(id)->trashed());
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("alpha\n"));
}

void DocumentCollectionTest::pinAndLiteralColoursPersistOutsideMarkdown()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "alpha\n");
    QString id;
    {
        auto &collection = f.collection();
        id = collection.idForRelativePath("Alpha.md");
        QVERIFY(collection.setPinned(id, true));
        QVERIFY(collection.setPaper(id, "#AABBCC"));
        QVERIFY(collection.setInk(id, "#112233"));
    }
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("alpha\n"));

    DocumentCollection reopened(f.state.path());
    QVERIFY(reopened.openRoot(f.notes.path()));
    auto *document = reopened.document(id);
    QVERIFY(document);
    QVERIFY(document->pinned());
    QCOMPARE(document->paper(), QStringLiteral("#aabbcc"));
    QCOMPARE(document->ink(), QStringLiteral("#112233"));
}

void DocumentCollectionTest::pinnedWindowSizePersistsOutsideMarkdown()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "alpha\n");
    QString id;
    {
        auto &collection = f.collection();
        id = collection.idForRelativePath(QStringLiteral("Alpha.md"));
        QCOMPARE(collection.document(id)->pinnedWindowWidth(), 0);
        QCOMPARE(collection.document(id)->pinnedWindowHeight(), 0);
        QVERIFY(collection.setPinnedWindowSize(id, 733, 511));
        QCOMPARE(collection.document(id)->pinnedWindowWidth(), 733);
        QCOMPARE(collection.document(id)->pinnedWindowHeight(), 511);
    }
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("alpha\n"));

    DocumentCollection reopened(f.state.path());
    QVERIFY(reopened.openRoot(f.notes.path()));
    auto *document = reopened.document(id);
    QVERIFY(document);
    QCOMPARE(document->pinnedWindowWidth(), 733);
    QCOMPARE(document->pinnedWindowHeight(), 511);
}

void DocumentCollectionTest::watcherRearmsAfterAtomicReplacement()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "zero\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");

    atomicReplace(f.notes.filePath("Alpha.md"), "first\n");
    QTRY_COMPARE_WITH_TIMEOUT(collection.document(id)->content(), QStringLiteral("first\n"), 2000);
    atomicReplace(f.notes.filePath("Alpha.md"), "second\n");
    QTRY_COMPARE_WITH_TIMEOUT(collection.document(id)->content(), QStringLiteral("second\n"), 2000);
    QVERIFY(collection.watchHealthy());
}

void DocumentCollectionTest::externalReconcileEmitsDocumentAndModelChanges()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "zero\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");
    auto *document = collection.document(id);
    QSignalSpy collectionChanges(&collection, &DocumentCollection::documentChanged);
    QSignalSpy modelChanges(&collection, &QAbstractItemModel::dataChanged);
    QSignalSpy contentChanges(document, &Document::contentChanged);

    atomicReplace(f.notes.filePath("Alpha.md"), "external\n");
    collection.reconcileNow();
    QVERIFY(!collectionChanges.isEmpty());
    QVERIFY(!modelChanges.isEmpty());
    QVERIFY(!contentChanges.isEmpty());

    collectionChanges.clear();
    modelChanges.clear();
    QVERIFY(collection.updateContent(id, "mine\n"));
    collectionChanges.clear();
    modelChanges.clear();
    atomicReplace(f.notes.filePath("Alpha.md"), "conflict\n");
    collection.reconcileNow();
    QVERIFY(document->conflict());
    QVERIFY(!collectionChanges.isEmpty());
    QVERIFY(!modelChanges.isEmpty());
}

void DocumentCollectionTest::invalidUtf8AndHardlinksAreRefused()
{
    Fixture f;
    writeBytes(f.notes.filePath("bad.md"), QByteArray::fromHex("fffe"));
    writeBytes(f.notes.filePath("source.md"), "safe\n");
    QVERIFY(::link(QFile::encodeName(f.notes.filePath("source.md")).constData(),
                   QFile::encodeName(f.notes.filePath("hard.md")).constData()) == 0);

    auto &collection = f.collection();
    QVERIFY(collection.idForRelativePath("bad.md").isEmpty());
    QVERIFY(collection.idForRelativePath("source.md").isEmpty());
    QVERIFY(collection.idForRelativePath("hard.md").isEmpty());
    QVERIFY(!collection.lastError().isEmpty());
}

void DocumentCollectionTest::catalogOrderIsIndependentOfThePersistedFanOrder()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "a\n");
    writeBytes(f.notes.filePath("Beta.md"), "b\n");
    writeBytes(f.notes.filePath("nested/Gamma.md"), "g\n");

    QString alpha;
    QString beta;
    QString gamma;
    {
        auto &collection = f.collection();
        alpha = collection.idForRelativePath("Alpha.md");
        beta = collection.idForRelativePath("Beta.md");
        gamma = collection.idForRelativePath("nested/Gamma.md");

        // The catalog is every discovered note, ordered by path and never by the fan.
        QCOMPARE(collection.catalogIds(), (QStringList{alpha, beta, gamma}));
        QVERIFY(collection.fanIds().isEmpty());

        // The fan is an explicit working set with its own order.
        QVERIFY(collection.joinFan(gamma));
        QVERIFY(collection.joinFan(alpha));
        QCOMPARE(collection.fanIds(), (QStringList{gamma, alpha}));
        QVERIFY(collection.joinFan(alpha)); // idempotent
        QCOMPARE(collection.fanIds(), (QStringList{gamma, alpha}));
        QVERIFY(collection.setFanOrder({alpha, gamma}));
        QCOMPARE(collection.fanIds(), (QStringList{alpha, gamma}));
        QVERIFY(collection.isInFan(alpha));
        QVERIFY(!collection.isInFan(beta));
        QCOMPARE(collection.catalogIds(), (QStringList{alpha, beta, gamma}));
    }

    DocumentCollection reopened(f.state.path());
    QVERIFY(reopened.openRoot(f.notes.path()));
    QCOMPARE(reopened.fanIds(), (QStringList{alpha, gamma}));
    QCOMPARE(reopened.catalogIds(), (QStringList{alpha, beta, gamma}));
}

void DocumentCollectionTest::fanWorkingSetPersistsAndReleasesArchivedOrTrashedNotes()
{
    Fixture f;
    writeBytes(f.notes.filePath("Keep.md"), "k\n");
    writeBytes(f.notes.filePath("Filed.md"), "f\n");
    writeBytes(f.notes.filePath("Gone.md"), "g\n");
    auto &collection = f.collection();
    const QString keep = collection.idForRelativePath("Keep.md");
    const QString filed = collection.idForRelativePath("Filed.md");
    const QString gone = collection.idForRelativePath("Gone.md");

    QVERIFY(collection.joinFan(keep));
    QVERIFY(collection.joinFan(filed));
    QVERIFY(collection.joinFan(gone));
    QCOMPARE(collection.fanIds().size(), 3);

    QVERIFY(collection.archive(filed));
    QVERIFY(!collection.isInFan(filed));
    QVERIFY(collection.moveToTrash(gone));
    QVERIFY(!collection.isInFan(gone));
    QCOMPARE(collection.fanIds(), (QStringList{keep}));

    // Archived notes remain in the catalog; they simply left the fan.
    QVERIFY(collection.document(filed)->archived());
    QVERIFY(collection.catalogIds().contains(filed));
}

void DocumentCollectionTest::failedRootSwitchLeavesThePreviousLibraryIntact()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "alpha\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");
    QVERIFY(collection.joinFan(id));
    QVERIFY(collection.updateContent(id, "unsaved\n"));

    QSignalSpy resets(&collection, &QAbstractItemModel::modelAboutToBeReset);
    const QString missing = QDir(f.notes.path()).filePath(QStringLiteral("no-such-folder"));
    QVERIFY(!collection.openRoot(missing));
    QVERIFY(!collection.lastError().isEmpty());

    // Nothing was torn down: same root, same catalog, same fan, same unsaved buffer.
    QCOMPARE(resets.count(), 0);
    QCOMPARE(collection.rootPath(), QFileInfo(f.notes.path()).canonicalFilePath());
    QCOMPARE(collection.catalogIds(), (QStringList{id}));
    QCOMPARE(collection.fanIds(), (QStringList{id}));
    QVERIFY(collection.document(id));
    QCOMPARE(collection.document(id)->content(), QStringLiteral("unsaved\n"));
    QVERIFY(collection.document(id)->dirty());
}

void DocumentCollectionTest::successfulRootSwitchFlushesPendingSavesFirst()
{
    Fixture f;
    QTemporaryDir second;
    QVERIFY(second.isValid());
    writeBytes(f.notes.filePath("Alpha.md"), "alpha\n");
    writeBytes(second.filePath("Other.md"), "other\n");

    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");
    QVERIFY(collection.updateContent(id, "switched\n"));
    QVERIFY(collection.document(id)->dirty());

    QVERIFY2(collection.openRoot(second.path()), qPrintable(collection.lastError()));
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("switched\n"));
    QCOMPARE(collection.rootPath(), QFileInfo(second.path()).canonicalFilePath());
    QCOMPARE(collection.catalogIds().size(), 1);
    QVERIFY(!collection.idForRelativePath("Other.md").isEmpty());
    QVERIFY(collection.idForRelativePath("Alpha.md").isEmpty());
    QVERIFY(collection.fanIds().isEmpty());
}

void DocumentCollectionTest::archiveRestoreFallsBackToAUniqueNameInTheOriginalFolder()
{
    Fixture f;
    writeBytes(f.notes.filePath("nested/Alpha.md"), "archived\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("nested/Alpha.md");
    QVERIFY(collection.archive(id));

    // The original name is taken again by an unrelated note; restore must not clobber
    // it and must not refuse either.
    writeBytes(f.notes.filePath("nested/Alpha.md"), "occupant\n");
    collection.reconcileNow();
    QSignalSpy restored(&collection, &DocumentCollection::documentRestored);

    QVERIFY2(collection.restoreArchive(id), qPrintable(collection.lastError()));
    QVERIFY(!collection.document(id)->archived());
    QCOMPARE(collection.document(id)->folder(), QStringLiteral("nested"));
    QVERIFY(collection.document(id)->relativePath() != QStringLiteral("nested/Alpha.md"));
    QCOMPARE(readBytes(f.notes.filePath("nested/Alpha.md")), QByteArray("occupant\n"));
    QCOMPARE(readBytes(f.notes.filePath(collection.document(id)->relativePath())),
             QByteArray("archived\n"));
    QCOMPARE(restored.count(), 1);
    QCOMPARE(restored.first().at(0).toString(), id);
    QCOMPARE(restored.first().at(2).toBool(), true); // renamed to avoid the collision
}

void DocumentCollectionTest::exposesFileStatFieldsForTheInformationPanel()
{
    Fixture f;
    writeBytes(f.notes.filePath("nested/Alpha.md"), "twelve bytes");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("nested/Alpha.md");
    auto *document = collection.document(id);
    QVERIFY(document);

    QCOMPARE(document->fileName(), QStringLiteral("Alpha.md"));
    QCOMPARE(document->folder(), QStringLiteral("nested"));
    QCOMPARE(document->absolutePath(), f.notes.filePath("nested/Alpha.md"));
    QCOMPARE(document->diskBytes(), qint64(12));
    QVERIFY(document->diskModified().isValid());
    QVERIFY(!document->dirty());

    const int row = static_cast<int>(collection.catalogIds().indexOf(id));
    QVERIFY(row >= 0);
    const QModelIndex at = collection.index(row);
    QCOMPARE(collection.data(at, DocumentCollection::FileNameRole).toString(), QStringLiteral("Alpha.md"));
    QCOMPARE(collection.data(at, DocumentCollection::FolderRole).toString(), QStringLiteral("nested"));
    QCOMPARE(collection.data(at, DocumentCollection::AbsolutePathRole).toString(),
             f.notes.filePath("nested/Alpha.md"));
    QCOMPARE(collection.data(at, DocumentCollection::DiskBytesRole).toLongLong(), qint64(12));
    QVERIFY(collection.data(at, DocumentCollection::DiskModifiedRole).toDateTime().isValid());
    QCOMPARE(collection.data(at, DocumentCollection::DirtyRole).toBool(), false);

    QVERIFY(collection.updateContent(id, "a much longer body than before"));
    QCOMPARE(collection.data(at, DocumentCollection::DirtyRole).toBool(), true);
    QCOMPARE(document->diskBytes(), qint64(12)); // still the on-disk size while unsaved
    QVERIFY(collection.saveNow(id));
    QCOMPARE(document->diskBytes(), qint64(30));
    QVERIFY(!document->dirty());
}

void DocumentCollectionTest::quietPeriodRestartsOnEveryEdit()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "old\n");
    auto &collection = f.collection();
    const QString id = collection.idForRelativePath("Alpha.md");

    QCOMPARE(collection.autosaveQuietPeriodMs(), 250);
    QVERIFY(collection.updateContent(id, "one\n"));
    QTest::qWait(160);
    QVERIFY(collection.pendingSaveRemainingMs(id) > 0);
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("old\n"));

    // Typing again restarts the whole quiet period rather than topping up the rest of it.
    QVERIFY(collection.updateContent(id, "two\n"));
    QVERIFY(collection.pendingSaveRemainingMs(id) > 200);
    QTest::qWait(160);
    QCOMPARE(readBytes(f.notes.filePath("Alpha.md")), QByteArray("old\n"));

    QTRY_COMPARE_WITH_TIMEOUT(readBytes(f.notes.filePath("Alpha.md")), QByteArray("two\n"), 1200);
    QCOMPARE(collection.pendingSaveRemainingMs(id), -1);
}

void DocumentCollectionTest::noOpReconciliationNeitherResetsTheModelNorRewritesMetadata()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "a\n");
    writeBytes(f.notes.filePath("nested/Beta.md"), "b\n");
    auto &collection = f.collection();
    QCOMPARE(collection.catalogIds().size(), 2);

    const QString index = collection.metadataPath();
    QVERIFY(QFileInfo::exists(index));
    const QByteArray before = readBytes(index);
    const QDateTime stamp = QFileInfo(index).lastModified();

    QSignalSpy resets(&collection, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy inserts(&collection, &QAbstractItemModel::rowsAboutToBeInserted);
    QSignalSpy removes(&collection, &QAbstractItemModel::rowsAboutToBeRemoved);
    QSignalSpy rowData(&collection, &QAbstractItemModel::dataChanged);
    QSignalSpy documents(&collection, &DocumentCollection::documentsChanged);

    QTest::qWait(1100); // long enough for several periodic reconciliations
    collection.reconcileNow();
    collection.reconcileNow();

    QCOMPARE(resets.count(), 0);
    QCOMPARE(inserts.count(), 0);
    QCOMPARE(removes.count(), 0);
    QCOMPARE(rowData.count(), 0);
    QCOMPARE(documents.count(), 0);
    QCOMPARE(readBytes(index), before);
    QCOMPARE(QFileInfo(index).lastModified(), stamp);
}

void DocumentCollectionTest::renameMovesOneCatalogRowWithoutResettingTheModel()
{
    Fixture f;
    writeBytes(f.notes.filePath("Alpha.md"), "a\n");
    writeBytes(f.notes.filePath("Beta.md"), "b\n");
    writeBytes(f.notes.filePath("Delta.md"), "d\n");
    auto &collection = f.collection();
    const QString alpha = collection.idForRelativePath("Alpha.md");
    const QString beta = collection.idForRelativePath("Beta.md");
    const QString delta = collection.idForRelativePath("Delta.md");
    QCOMPARE(collection.catalogIds(), (QStringList{alpha, beta, delta}));

    QSignalSpy resets(&collection, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy moves(&collection, &QAbstractItemModel::rowsMoved);

    QVERIFY2(collection.renameDocument(alpha, QStringLiteral("Zulu")), qPrintable(collection.lastError()));
    QCOMPARE(collection.catalogIds(), (QStringList{beta, delta, alpha}));
    QCOMPARE(collection.document(alpha)->fileName(), QStringLiteral("Zulu.md"));
    QCOMPARE(resets.count(), 0);
    QCOMPARE(moves.count(), 1);
}

void DocumentCollectionTest::discoversHundredsOfNotesAcrossSubfoldersWithoutChurn()
{
    Fixture f;
    constexpr int total = 300;
    for (int n = 0; n < total; ++n) {
        writeBytes(f.notes.filePath(QStringLiteral("folder-%1/Note-%2.md")
                                        .arg(n % 12, 2, 10, QLatin1Char('0'))
                                        .arg(n, 4, 10, QLatin1Char('0'))),
                   QByteArray("body ") + QByteArray::number(n) + "\n");
    }
    auto &collection = f.collection();
    QCOMPARE(collection.catalogIds().size(), total);
    QCOMPARE(collection.rowCount(), total);

    // The catalog is path-ordered, so the model is stable between runs.
    QStringList paths;
    paths.reserve(total);
    for (const QString &id : collection.catalogIds()) {
        paths.append(collection.document(id)->relativePath());
    }
    QStringList sorted = paths;
    std::sort(sorted.begin(), sorted.end(), [](const QString &l, const QString &r) {
        return l.compare(r, Qt::CaseInsensitive) < 0;
    });
    QCOMPARE(paths, sorted);
    QCOMPARE(collection.folders().size(), 12);

    QSignalSpy resets(&collection, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy rowData(&collection, &QAbstractItemModel::dataChanged);
    collection.reconcileNow();
    QCOMPARE(resets.count(), 0);
    QCOMPARE(rowData.count(), 0);
}

int main(int argc, char **argv)
{
    const QString xdg = QDir::temp().filePath(QStringLiteral("fanfold-xdg-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(xdg);
    qputenv("XDG_DATA_HOME", xdg.toUtf8());
    qputenv("XDG_CONFIG_HOME", QDir(xdg).filePath("config").toUtf8());
    QCoreApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("FanFoldTests"));
    application.setApplicationName(QStringLiteral("document-engine"));
    DocumentCollectionTest test;
    const int result = QTest::qExec(&test, argc, argv);
    QDir(xdg).removeRecursively();
    return result;
}

#include "tst_documentcollection.moc"
