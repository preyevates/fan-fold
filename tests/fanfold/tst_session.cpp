/** Focused suite for the bounded editor cache and the Fan Fold session controller.
 *
 * These are the two pieces that keep "exactly one canonical Document buffer per stable
 * ID and no duplicate editor ownership" true: EditorLeases decides which notes may hold a
 * live WebEngine editor and who owns each one, and SessionController performs every
 * create / open / pin / archive / delete verb through that single route.
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

#include "documentcollection.h"
#include "editorleases.h"
#include "librarymodel.h"
#include "sessioncontroller.h"

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
    std::unique_ptr<DocumentCollection> collection;
    std::unique_ptr<LibraryModel> library;
    std::unique_ptr<EditorLeases> leases;
    std::unique_ptr<SessionController> session;

    SessionController &open(int capacity = 4)
    {
        collection = std::make_unique<DocumentCollection>(state.path());
        if (!collection->openRoot(notes.path())) {
            qFatal("fixture open failed: %s", qPrintable(collection->lastError()));
        }
        library = std::make_unique<LibraryModel>(collection.get());
        leases = std::make_unique<EditorLeases>(capacity);
        session = std::make_unique<SessionController>(collection.get(), library.get(), leases.get());
        return *session;
    }
};

} // namespace

class SessionTest final : public QObject
{
    Q_OBJECT

private slots:
    void warmSlotIsReusedRatherThanRecreatedPerKeystroke();
    void cacheStaysBoundedAndEvictsTheLeastRecentlyUsedFreeSlot();
    void anOwnedEditorIsNeverEvictedFromUnderItsWindow();
    void acquiringAnOwnedDocumentHandsTheOneEditorOverExplicitly();
    void createsUniqueNotesInTheRootThatFanAndTakeFocus();
    void libraryNewNoteLandsInTheCurrentFolderAndScopesTheFanThere();
    void openingFromTheLibraryScopesTheFanToThatNotesFolder();
    void findCurrentNoteRevealsItInTheLibraryTree();
    void pinnedWindowsAreOrdinaryAndClosingOneReturnsTheNoteToTheFan();
    void persistedPinsRecreateTheSessionWindowRegisterAfterRestart();
    void footerArchiveAndTrashLeaveTheFanWithoutDeletingAnything();
};

void SessionTest::warmSlotIsReusedRatherThanRecreatedPerKeystroke()
{
    EditorLeases leases(3);
    QSignalSpy assigned(&leases, &EditorLeases::slotAssigned);

    const QString first = leases.acquire(QStringLiteral("doc-a"), QStringLiteral("fan"));
    QVERIFY(!first.isEmpty());
    QCOMPARE(assigned.count(), 1);

    // Re-acquiring the same note — every selection, every keystroke-driven refresh —
    // must return the very same slot and build nothing.
    QCOMPARE(leases.acquire(QStringLiteral("doc-a"), QStringLiteral("fan")), first);
    QCOMPARE(assigned.count(), 1);
    QCOMPARE(leases.residentCount(), 1);

    // Releasing ownership keeps the editor warm, so switching back is free.
    QVERIFY(leases.release(QStringLiteral("fan")));
    QCOMPARE(leases.slotForDocument(QStringLiteral("doc-a")), first);
    QCOMPARE(leases.acquire(QStringLiteral("doc-a"), QStringLiteral("fan")), first);
    QCOMPARE(assigned.count(), 1);
}

void SessionTest::cacheStaysBoundedAndEvictsTheLeastRecentlyUsedFreeSlot()
{
    EditorLeases leases(3);
    QSignalSpy evicted(&leases, &EditorLeases::slotEvicted);
    for (const QString &id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
        QVERIFY(!leases.acquire(id, QStringLiteral("fan")).isEmpty());
        QVERIFY(leases.release(QStringLiteral("fan")));
    }
    QCOMPARE(leases.residentCount(), 3);

    // Touch "a" so "b" becomes the least recently used resident.
    QVERIFY(!leases.acquire(QStringLiteral("a"), QStringLiteral("fan")).isEmpty());
    QVERIFY(leases.release(QStringLiteral("fan")));

    const QString slot = leases.acquire(QStringLiteral("d"), QStringLiteral("fan"));
    QVERIFY(!slot.isEmpty());
    QCOMPARE(leases.residentCount(), 3);
    QCOMPARE(evicted.count(), 1);
    QCOMPARE(evicted.first().at(1).toString(), QStringLiteral("b"));
    QVERIFY(leases.slotForDocument(QStringLiteral("b")).isEmpty());
    QCOMPARE(leases.residentDocuments(), (QStringList{"d", "a", "c"}));
}

void SessionTest::anOwnedEditorIsNeverEvictedFromUnderItsWindow()
{
    EditorLeases leases(2);
    QVERIFY(!leases.acquire(QStringLiteral("pinned-1"), QStringLiteral("pin:1")).isEmpty());
    QVERIFY(!leases.acquire(QStringLiteral("pinned-2"), QStringLiteral("pin:2")).isEmpty());

    // Both slots are owned by live windows: a third note has nowhere to go and the
    // request is refused rather than pulling an editor out of a visible window.
    QSignalSpy evicted(&leases, &EditorLeases::slotEvicted);
    QVERIFY(leases.acquire(QStringLiteral("third"), QStringLiteral("fan")).isEmpty());
    QCOMPARE(evicted.count(), 0);
    QVERIFY(!leases.lastError().isEmpty());
    QCOMPARE(leases.slotForDocument(QStringLiteral("pinned-1")).isEmpty(), false);
    QCOMPARE(leases.slotForDocument(QStringLiteral("pinned-2")).isEmpty(), false);

    QVERIFY(leases.release(QStringLiteral("pin:2")));
    QVERIFY(!leases.acquire(QStringLiteral("third"), QStringLiteral("fan")).isEmpty());
    QCOMPARE(evicted.count(), 1);
    QCOMPARE(evicted.first().at(1).toString(), QStringLiteral("pinned-2"));
}

void SessionTest::acquiringAnOwnedDocumentHandsTheOneEditorOverExplicitly()
{
    EditorLeases leases(3);
    const QString slot = leases.acquire(QStringLiteral("doc"), QStringLiteral("fan"));
    QSignalSpy detached(&leases, &EditorLeases::ownerDetached);
    QSignalSpy assigned(&leases, &EditorLeases::slotAssigned);

    // Pinning takes the SAME editor over; a second editor for one note is never built.
    QCOMPARE(leases.acquire(QStringLiteral("doc"), QStringLiteral("pin:doc")), slot);
    QCOMPARE(assigned.count(), 0);
    QCOMPARE(detached.count(), 1);
    QCOMPARE(detached.first().at(0).toString(), QStringLiteral("fan"));
    QCOMPARE(detached.first().at(1).toString(), QStringLiteral("doc"));
    QCOMPARE(leases.ownerForDocument(QStringLiteral("doc")), QStringLiteral("pin:doc"));
    QCOMPARE(leases.residentCount(), 1);
}

void SessionTest::createsUniqueNotesInTheRootThatFanAndTakeFocus()
{
    Fixture f;
    auto &session = f.open();
    QSignalSpy focus(&session, &SessionController::focusEditorRequested);

    const QString first = session.createNoteInRoot();
    QVERIFY(!first.isEmpty());
    QCOMPARE(f.collection->document(first)->relativePath(), QStringLiteral("Untitled.md"));
    QVERIFY(f.collection->isInFan(first));
    QCOMPARE(session.currentDocumentId(), first);
    QCOMPARE(focus.count(), 1);
    QCOMPARE(focus.first().at(0).toString(), first);
    QCOMPARE(f.leases->ownerForDocument(first), QStringLiteral("fan"));

    const QString second = session.createNoteInRoot();
    QVERIFY(second != first);
    QCOMPARE(f.collection->document(second)->relativePath(), QStringLiteral("Untitled 2.md"));
    // A folder the user has never arranged falls back to CATALOG order, which is the same
    // order the Library lists — "Untitled 2.md" sorts before "Untitled.md" because a space
    // precedes a dot. Once a drag has given the folder an arrangement, that arrangement
    // leads and newly discovered notes append after it; perFolderOrderSurvives... covers
    // that path in the engine suite.
    QCOMPARE(f.collection->fanIds(), (QStringList{second, first}));
    QCOMPARE(focus.count(), 2);
}

void SessionTest::libraryNewNoteLandsInTheCurrentFolderAndScopesTheFanThere()
{
    Fixture f;
    writeBytes(f.notes.filePath("Work/Plan.md"), "p\n");
    auto &session = f.open();
    f.library->setExpanded(QStringLiteral("Work"), true);
    f.library->setCurrentRow(f.library->rowForFolder(QStringLiteral("Work")));

    const QString created = session.createNoteInLibraryFolder();
    QVERIFY(!created.isEmpty());
    QCOMPARE(f.collection->document(created)->relativePath(), QStringLiteral("Work/Untitled.md"));
    // "Whatever folder is open, that's where the note goes" — and creating into a folder
    // MOVES the scope, or the new note would be created somewhere nobody can see it.
    QCOMPARE(f.collection->openFolder(), QStringLiteral("Work"));
    QVERIFY(f.collection->isInFan(created));
    QCOMPARE(session.currentDocumentId(), created);
}

void SessionTest::openingFromTheLibraryScopesTheFanToThatNotesFolder()
{
    Fixture f;
    writeBytes(f.notes.filePath("Root.md"), "r\n");
    writeBytes(f.notes.filePath("Work/Plan.md"), "p\n");
    auto &session = f.open();
    const QString id = f.collection->idForRelativePath(QStringLiteral("Work/Plan.md"));
    // The root is open, so a note inside Work is NOT on the fan yet.
    QCOMPARE(f.collection->openFolder(), QString());
    QVERIFY(!f.collection->isInFan(id));

    QSignalSpy focus(&session, &SessionController::focusEditorRequested);
    QVERIFY(session.openFromLibrary(id));
    // Opening it is the same gesture as opening its folder: the scope moves, and the
    // derivation puts it on the fan. The root's note leaves with the scope.
    QCOMPARE(f.collection->openFolder(), QStringLiteral("Work"));
    QVERIFY(f.collection->isInFan(id));
    QVERIFY(!f.collection->isInFan(f.collection->idForRelativePath(QStringLiteral("Root.md"))));
    QCOMPARE(session.currentDocumentId(), id);
    QCOMPARE(focus.count(), 1);
}

void SessionTest::findCurrentNoteRevealsItInTheLibraryTree()
{
    Fixture f;
    writeBytes(f.notes.filePath("Work/Deep/Detail.md"), "d\n");
    writeBytes(f.notes.filePath("Other.md"), "o\n");
    auto &session = f.open();

    // With nothing selected there is nothing to find.
    QVERIFY(!session.findCurrentNote());

    const QString id = f.collection->idForRelativePath(QStringLiteral("Work/Deep/Detail.md"));
    QVERIFY(session.openFromLibrary(id));
    QSignalSpy revealed(&session, &SessionController::revealInLibraryRequested);

    QVERIFY(session.findCurrentNote());
    QCOMPARE(revealed.count(), 1);
    QCOMPARE(revealed.first().at(0).toString(), id);
    QVERIFY(f.library->isExpanded(QStringLiteral("Work")));
    QVERIFY(f.library->isExpanded(QStringLiteral("Work/Deep")));
    QCOMPARE(f.library->currentRow(), f.library->rowForDocument(id));
    QCOMPARE(f.library->currentFolder(), QStringLiteral("Work/Deep"));
}

void SessionTest::pinnedWindowsAreOrdinaryAndClosingOneReturnsTheNoteToTheFan()
{
    Fixture f;
    writeBytes(f.notes.filePath("One.md"), "1\n");
    writeBytes(f.notes.filePath("Two.md"), "2\n");
    auto &session = f.open();
    const QString one = f.collection->idForRelativePath(QStringLiteral("One.md"));
    const QString two = f.collection->idForRelativePath(QStringLiteral("Two.md"));
    QVERIFY(session.openFromLibrary(one));
    QVERIFY(session.openFromLibrary(two));

    QSignalSpy opened(&session, &SessionController::pinnedWindowOpened);
    QSignalSpy closed(&session, &SessionController::pinnedWindowClosed);

    // Several pinned windows may be open at once.
    QVERIFY(session.pin(one));
    QVERIFY(session.pin(two));
    QCOMPARE(session.pinnedIds(), (QStringList{one, two}));
    QCOMPARE(opened.count(), 2);
    QVERIFY(f.collection->document(one)->pinned());
    QCOMPARE(f.leases->ownerForDocument(one), QStringLiteral("pin:") + one);

    // Pinning twice is idempotent, not a second window.
    QVERIFY(session.pin(one));
    QCOMPARE(opened.count(), 2);

    // Closing a pinned window returns the note to the fan and deletes nothing.
    QVERIFY(session.closePinned(one));
    QCOMPARE(closed.count(), 1);
    QCOMPARE(session.pinnedIds(), (QStringList{two}));
    QVERIFY(!f.collection->document(one)->pinned());
    QVERIFY(f.collection->isInFan(one));
    QVERIFY(QFileInfo::exists(f.notes.filePath("One.md")));
    QCOMPARE(f.leases->ownerForDocument(one), QString());
    QCOMPARE(f.leases->slotForDocument(one).isEmpty(), false);
}

void SessionTest::persistedPinsRecreateTheSessionWindowRegisterAfterRestart()
{
    QTemporaryDir notes;
    QTemporaryDir state;
    QVERIFY(notes.isValid());
    QVERIFY(state.isValid());
    writeBytes(notes.filePath("One.md"), "1\n");
    writeBytes(notes.filePath("Two.md"), "2\n");

    QString one;
    QString two;
    {
        DocumentCollection collection(state.path());
        QVERIFY(collection.openRoot(notes.path()));
        one = collection.idForRelativePath(QStringLiteral("One.md"));
        two = collection.idForRelativePath(QStringLiteral("Two.md"));
        LibraryModel library(&collection);
        EditorLeases leases(4);
        SessionController session(&collection, &library, &leases);
        QVERIFY(session.pin(one));
        QVERIFY(session.pin(two));
        QCOMPARE(session.pinnedIds(), (QStringList{one, two}));
    }

    DocumentCollection reopenedCollection(state.path());
    QVERIFY(reopenedCollection.openRoot(notes.path()));
    LibraryModel reopenedLibrary(&reopenedCollection);
    EditorLeases reopenedLeases(4);
    SessionController reopenedSession(&reopenedCollection, &reopenedLibrary, &reopenedLeases);

    QCOMPARE(reopenedSession.pinnedIds(), (QStringList{one, two}));
    QCOMPARE(reopenedLeases.ownerForDocument(one), SessionController::pinnedOwner(one));
    QCOMPARE(reopenedLeases.ownerForDocument(two), SessionController::pinnedOwner(two));
    QVERIFY(reopenedCollection.document(one)->pinned());
    QVERIFY(reopenedCollection.document(two)->pinned());
}

void SessionTest::footerArchiveAndTrashLeaveTheFanWithoutDeletingAnything()
{
    Fixture f;
    writeBytes(f.notes.filePath("Keep.md"), "k\n");
    writeBytes(f.notes.filePath("File me.md"), "f\n");
    writeBytes(f.notes.filePath("Bin me.md"), "b\n");
    auto &session = f.open();
    const QString keep = f.collection->idForRelativePath(QStringLiteral("Keep.md"));
    const QString filed = f.collection->idForRelativePath(QStringLiteral("File me.md"));
    const QString binned = f.collection->idForRelativePath(QStringLiteral("Bin me.md"));
    QVERIFY(session.openFromLibrary(keep));
    QVERIFY(session.openFromLibrary(filed));
    QVERIFY(session.openFromLibrary(binned));

    QVERIFY(session.selectDocument(filed));
    QVERIFY(session.archiveCurrent());
    QVERIFY(f.collection->document(filed)->archived());
    QVERIFY(!f.collection->isInFan(filed));
    QVERIFY(QFileInfo::exists(f.notes.filePath("Archive/File me.md")));
    QVERIFY(f.leases->slotForDocument(filed).isEmpty()); // editor released, not leaked
    QVERIFY(f.collection->fanIds().contains(session.currentDocumentId()));

    // The footer's delete is labelled Trash and uses the desktop Trash, never unlink.
    QVERIFY(session.selectDocument(binned));
    QVERIFY(session.trashCurrent());
    QVERIFY(f.collection->document(binned)->trashed());
    QVERIFY(!f.collection->isInFan(binned));
    QVERIFY(!QFileInfo::exists(f.notes.filePath("Bin me.md")));
    QVERIFY(!f.collection->document(binned)->trashPath().isEmpty());
    QVERIFY(QFileInfo::exists(f.collection->document(binned)->trashPath()));
    QCOMPARE(f.collection->fanIds(), (QStringList{keep}));
    QCOMPARE(session.currentDocumentId(), keep);
}

int main(int argc, char **argv)
{
    const QString xdg = QDir::temp().filePath(
        QStringLiteral("fanfold-session-xdg-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(xdg);
    qputenv("XDG_DATA_HOME", xdg.toUtf8());
    qputenv("XDG_CONFIG_HOME", QDir(xdg).filePath("config").toUtf8());
    QCoreApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("FanFoldTests"));
    application.setApplicationName(QStringLiteral("session"));
    SessionTest test;
    const int result = QTest::qExec(&test, argc, argv);
    QDir(xdg).removeRecursively();
    return result;
}

#include "tst_session.moc"
