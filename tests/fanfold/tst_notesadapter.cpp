#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "documentcollection.h"
#include "notesadapter.h"
#include "searchmodel.h"

namespace {

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(bytes), bytes.size());
}

} // namespace

class NotesAdapterTest final : public QObject
{
    Q_OBJECT

private slots:
    void searchTemporarilyReplacesTheFanProjection();
    void folderLoadsSortTabsByTitleEvenAfterRefresh();
    void draggedOrderSurvivesRenameAndRefresh();
    void externalChangesMergeAlphabeticallyWithoutLosingDragOrder();
    void manualOrderSurvivesFolderSwitchAndReopen();
    void dragMatchingMergedProjectionPersistsAfterReopen();
    void failedFlushRefusesFolderScopeAndPreservesProjection();
};

void NotesAdapterTest::searchTemporarilyReplacesTheFanProjection()
{
    QTemporaryDir notes;
    QTemporaryDir state;
    QVERIFY(notes.isValid());
    QVERIFY(state.isValid());
    writeBytes(notes.filePath(QStringLiteral("Work/Local.md")), "ordinary text\n");
    writeBytes(notes.filePath(QStringLiteral("Elsewhere/Needle.md")), "a match\n");

    DocumentCollection collection(state.path());
    QVERIFY2(collection.openRoot(notes.path()), qPrintable(collection.lastError()));
    QVERIFY(collection.setOpenFolder(QStringLiteral("Work")));
    NotesAdapter adapter(&collection);
    auto *search = qobject_cast<SearchModel *>(adapter.searchModel());
    QVERIFY(search);

    const QString localId = collection.idForRelativePath(QStringLiteral("Work/Local.md"));
    const QString matchId = collection.idForRelativePath(QStringLiteral("Elsewhere/Needle.md"));
    const QVariantMap folder = adapter.load();
    QCOMPARE(folder.value(QStringLiteral("openFolder")).toString(), QStringLiteral("Work"));
    QCOMPARE(folder.value(QStringLiteral("order")).toStringList(), QStringList{localId});

    QSignalSpy changed(&adapter, &NotesAdapter::changed);
    search->setQuery(QStringLiteral("needle"));
    QCOMPARE(changed.count(), 1);
    const QVariantMap matches = adapter.load();
    QCOMPARE(matches.value(QStringLiteral("searchActive")).toBool(), true);
    QCOMPARE(matches.value(QStringLiteral("searchCount")).toInt(), 1);
    QCOMPARE(matches.value(QStringLiteral("openFolder")).toString(), QStringLiteral("Work"));
    QCOMPARE(matches.value(QStringLiteral("order")).toStringList(), QStringList{matchId});
    QCOMPARE(adapter.setOrder(QStringList{matchId}).value(QStringLiteral("ok")).toBool(), false);

    search->setQuery(QString());
    const QVariantMap restored = adapter.load();
    QCOMPARE(restored.value(QStringLiteral("searchActive")).toBool(), false);
    QCOMPARE(restored.value(QStringLiteral("openFolder")).toString(), QStringLiteral("Work"));
    QCOMPARE(restored.value(QStringLiteral("order")).toStringList(), QStringList{localId});
}

void NotesAdapterTest::folderLoadsSortTabsByTitleEvenAfterRefresh()
{
    QTemporaryDir notes;
    QTemporaryDir state;
    QVERIFY(notes.isValid());
    QVERIFY(state.isValid());
    writeBytes(notes.filePath(QStringLiteral("Work/zulu.md")), "z\n");
    writeBytes(notes.filePath(QStringLiteral("Work/Bravo.md")), "b\n");
    writeBytes(notes.filePath(QStringLiteral("Work/alpha.md")), "a\n");

    DocumentCollection collection(state.path());
    QVERIFY2(collection.openRoot(notes.path()), qPrintable(collection.lastError()));
    const QString zulu = collection.idForRelativePath(QStringLiteral("Work/zulu.md"));
    const QString bravo = collection.idForRelativePath(QStringLiteral("Work/Bravo.md"));
    const QString alpha = collection.idForRelativePath(QStringLiteral("Work/alpha.md"));
    QVERIFY(collection.setOpenFolder(QStringLiteral("Work")));
    NotesAdapter adapter(&collection);
    // The array runs bottom-to-top; the visible order runs A-to-Z from top.
    const QStringList expected{zulu, bravo, alpha};

    const QVariantMap opened = adapter.openFolder(QStringLiteral("Work"));
    QCOMPARE(opened.value(QStringLiteral("ids")).toStringList(), expected);
    QCOMPARE(opened.value(QStringLiteral("order")).toStringList(), expected);
    // A manifest refresh must keep the alphabetical order of an untouched folder.
    collection.reconcileNow();
    const QVariantMap refreshed = adapter.load();
    QCOMPARE(refreshed.value(QStringLiteral("ids")).toStringList(), expected);
    QCOMPARE(refreshed.value(QStringLiteral("order")).toStringList(), expected);
}

void NotesAdapterTest::draggedOrderSurvivesRenameAndRefresh()
{
    QTemporaryDir notes;
    QTemporaryDir state;
    QVERIFY(notes.isValid());
    QVERIFY(state.isValid());
    writeBytes(notes.filePath(QStringLiteral("Work/Bravo.md")), "b\n");
    writeBytes(notes.filePath(QStringLiteral("Work/Charlie.md")), "c\n");

    DocumentCollection collection(state.path());
    QVERIFY2(collection.openRoot(notes.path()), qPrintable(collection.lastError()));
    QVERIFY(collection.setOpenFolder(QStringLiteral("Work")));
    NotesAdapter adapter(&collection);
    const QString bravo = collection.idForRelativePath(QStringLiteral("Work/Bravo.md"));
    const QString charlie = collection.idForRelativePath(QStringLiteral("Work/Charlie.md"));
    QCOMPARE(adapter.load().value(QStringLiteral("order")).toStringList(),
             (QStringList{charlie, bravo}));

    const QVariantMap dragged = adapter.setOrder(QStringList{bravo, charlie});
    QCOMPARE(dragged.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(dragged.value(QStringLiteral("ids")).toStringList(),
             (QStringList{bravo, charlie}));
    QCOMPARE(dragged.value(QStringLiteral("order")).toStringList(),
             (QStringList{bravo, charlie}));
    collection.reconcileNow();
    QCOMPARE(adapter.load().value(QStringLiteral("order")).toStringList(),
             (QStringList{bravo, charlie}));

    QVERIFY2(collection.renameDocument(charlie, QStringLiteral("Aardvark")),
             qPrintable(collection.lastError()));
    const QVariantMap renamed = adapter.load();
    QCOMPARE(renamed.value(QStringLiteral("ids")).toStringList(),
             (QStringList{bravo, charlie}));
    QCOMPARE(renamed.value(QStringLiteral("order")).toStringList(),
             (QStringList{bravo, charlie}));
    QCOMPARE(renamed.value(QStringLiteral("titles")).toMap().value(charlie).toString(),
             QStringLiteral("Aardvark"));
}

void NotesAdapterTest::externalChangesMergeAlphabeticallyWithoutLosingDragOrder()
{
    QTemporaryDir notes;
    QTemporaryDir state;
    QVERIFY(notes.isValid());
    QVERIFY(state.isValid());
    writeBytes(notes.filePath(QStringLiteral("Work/Alpha.md")), "a\n");
    writeBytes(notes.filePath(QStringLiteral("Work/Charlie.md")), "c\n");
    writeBytes(notes.filePath(QStringLiteral("Work/Echo.md")), "e\n");
    DocumentCollection collection(state.path());
    QVERIFY2(collection.openRoot(notes.path()), qPrintable(collection.lastError()));
    NotesAdapter adapter(&collection);
    const QString alpha = collection.idForRelativePath(QStringLiteral("Work/Alpha.md"));
    const QString charlie = collection.idForRelativePath(QStringLiteral("Work/Charlie.md"));
    const QString echo = collection.idForRelativePath(QStringLiteral("Work/Echo.md"));
    QCOMPARE(adapter.openFolder(QStringLiteral("Work")).value(QStringLiteral("order")).toStringList(),
             (QStringList{echo, charlie, alpha}));
    const QStringList dragged{alpha, echo, charlie};
    QVERIFY(adapter.setOrder(dragged).value(QStringLiteral("ok")).toBool());
    collection.reconcileNow();
    QCOMPARE(adapter.load().value(QStringLiteral("order")).toStringList(), dragged);

    // New external notes join in title order, without rearranging the existing drag.
    writeBytes(notes.filePath(QStringLiteral("Work/Delta.md")), "d\n");
    writeBytes(notes.filePath(QStringLiteral("Work/Bravo.md")), "b\n");
    collection.reconcileNow();
    const QString delta = collection.idForRelativePath(QStringLiteral("Work/Delta.md"));
    const QString bravo = collection.idForRelativePath(QStringLiteral("Work/Bravo.md"));
    QCOMPARE(adapter.load().value(QStringLiteral("order")).toStringList(),
             (QStringList{alpha, echo, charlie, delta, bravo}));

    QVERIFY(QFile::remove(notes.filePath(QStringLiteral("Work/Echo.md"))));
    collection.reconcileNow();
    QCOMPARE(adapter.load().value(QStringLiteral("order")).toStringList(),
             (QStringList{alpha, charlie, delta, bravo}));

    QVERIFY(QFile::rename(notes.filePath(QStringLiteral("Work/Delta.md")),
                          notes.filePath(QStringLiteral("Work/Aardvark.md"))));
    collection.reconcileNow();
    QCOMPARE(collection.idForRelativePath(QStringLiteral("Work/Aardvark.md")), delta);
    QCOMPARE(adapter.load().value(QStringLiteral("order")).toStringList(),
             (QStringList{alpha, charlie, bravo, delta}));
    QCOMPARE(adapter.load().value(QStringLiteral("titles")).toMap().value(delta).toString(),
             QStringLiteral("Aardvark"));

    QVERIFY(QFile::rename(notes.filePath(QStringLiteral("Work/Alpha.md")),
                          notes.filePath(QStringLiteral("Work/Yarrow.md"))));
    collection.reconcileNow();
    QCOMPARE(collection.idForRelativePath(QStringLiteral("Work/Yarrow.md")), alpha);
    QCOMPARE(adapter.load().value(QStringLiteral("order")).toStringList(),
             (QStringList{alpha, charlie, bravo, delta}));

    DocumentCollection reopened(state.path());
    QVERIFY2(reopened.openRoot(notes.path()), qPrintable(reopened.lastError()));
    NotesAdapter restored(&reopened);
    QCOMPARE(restored.openFolder(QStringLiteral("Work")).value(QStringLiteral("order")).toStringList(),
             (QStringList{alpha, charlie, bravo, delta}));
}

void NotesAdapterTest::manualOrderSurvivesFolderSwitchAndReopen()
{
    QTemporaryDir notes;
    QTemporaryDir state;
    QVERIFY(notes.isValid());
    QVERIFY(state.isValid());
    writeBytes(notes.filePath(QStringLiteral("Work/Alpha.md")), "a\n");
    writeBytes(notes.filePath(QStringLiteral("Work/Zulu.md")), "z\n");
    writeBytes(notes.filePath(QStringLiteral("Other/Delta.md")), "d\n");
    writeBytes(notes.filePath(QStringLiteral("Other/Bravo.md")), "b\n");
    QStringList manual;
    {
        DocumentCollection collection(state.path());
        QVERIFY2(collection.openRoot(notes.path()), qPrintable(collection.lastError()));
        NotesAdapter adapter(&collection);
        QVERIFY(adapter.openFolder(QStringLiteral("Work")).value(QStringLiteral("ok")).toBool());
        manual = {collection.idForRelativePath(QStringLiteral("Work/Alpha.md")),
                  collection.idForRelativePath(QStringLiteral("Work/Zulu.md"))};
        QSignalSpy fanChanges(&collection, &DocumentCollection::fanChanged);
        QVERIFY(adapter.setOrder(manual).value(QStringLiteral("ok")).toBool());
        QCOMPARE(fanChanges.count(), 0); // metadata-only drag must not fake a visual change
        QCOMPARE(adapter.openFolder(QStringLiteral("Other")).value(QStringLiteral("order")).toStringList(),
                 (QStringList{collection.idForRelativePath(QStringLiteral("Other/Delta.md")),
                              collection.idForRelativePath(QStringLiteral("Other/Bravo.md"))}));
        QCOMPARE(adapter.openFolder(QStringLiteral("Work")).value(QStringLiteral("order")).toStringList(), manual);
    }
    DocumentCollection reopened(state.path());
    QVERIFY2(reopened.openRoot(notes.path()), qPrintable(reopened.lastError()));
    NotesAdapter adapter(&reopened);
    QCOMPARE(adapter.openFolder(QStringLiteral("Work")).value(QStringLiteral("order")).toStringList(), manual);
}

void NotesAdapterTest::dragMatchingMergedProjectionPersistsAfterReopen()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    writeBytes(notes.filePath(QStringLiteral("Work/Alpha.md")), "a\n");
    writeBytes(notes.filePath(QStringLiteral("Work/Zulu.md")), "z\n");
    QStringList merged;
    {
        DocumentCollection collection(state.path());
        QVERIFY(collection.openRoot(notes.path()));
        NotesAdapter adapter(&collection);
        QVERIFY(adapter.openFolder(QStringLiteral("Work")).value(QStringLiteral("ok")).toBool());
        const QString alpha = collection.idForRelativePath(QStringLiteral("Work/Alpha.md"));
        const QString zulu = collection.idForRelativePath(QStringLiteral("Work/Zulu.md"));
        QVERIFY(adapter.setOrder(QStringList{alpha, zulu}).value(QStringLiteral("ok")).toBool());
        writeBytes(notes.filePath(QStringLiteral("Work/Bravo.md")), "b\n");
        collection.reconcileNow();
        merged = adapter.load().value(QStringLiteral("order")).toStringList();
        QCOMPARE(merged.size(), 3);
        // This explicit drag matches the projection but must mark the merged order manual.
        QVERIFY(adapter.setOrder(merged).value(QStringLiteral("ok")).toBool());
        writeBytes(notes.filePath(QStringLiteral("Work/Aardvark.md")), "new\n");
        collection.reconcileNow();
        merged.push_back(collection.idForRelativePath(QStringLiteral("Work/Aardvark.md")));
        QCOMPARE(adapter.load().value(QStringLiteral("order")).toStringList(), merged);
    }
    DocumentCollection reopened(state.path());
    QVERIFY(reopened.openRoot(notes.path()));
    NotesAdapter adapter(&reopened);
    QCOMPARE(adapter.openFolder(QStringLiteral("Work")).value(QStringLiteral("order")).toStringList(), merged);
}

void NotesAdapterTest::failedFlushRefusesFolderScopeAndPreservesProjection()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    writeBytes(notes.filePath("Work/Alpha.md"), "on disk\n");
    writeBytes(notes.filePath("Other/Beta.md"), "other\n");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    NotesAdapter adapter(&collection);
    QVERIFY(adapter.openFolder(QStringLiteral("Work")).value(QStringLiteral("ok")).toBool());
    const QString id = collection.idForRelativePath(QStringLiteral("Work/Alpha.md"));
    const QVariantMap before = adapter.load();
    const QString recoveryDir = QFileInfo(collection.metadataPath()).dir().filePath("recovery");
    const QString blockedRecord = QDir(recoveryDir).filePath(id + ".json");
    QVERIFY(QDir().mkpath(blockedRecord));
    QVERIFY(!collection.updateContent(id, QStringLiteral("memory only\n")));
    QSignalSpy changes(&adapter, &NotesAdapter::changed);
    const QVariantMap result = adapter.openFolder(QStringLiteral("Other"));
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("Recovery write failed")));
    QCOMPARE(changes.size(), 0);
    QCOMPARE(collection.openFolder(), QStringLiteral("Work"));
    QCOMPARE(adapter.load().value(QStringLiteral("order")), before.value(QStringLiteral("order")));
    QCOMPARE(collection.document(id)->content(), QStringLiteral("memory only\n"));
    QVERIFY(collection.document(id)->dirty());
    QVERIFY(QDir().rmdir(blockedRecord));
    QVERIFY(adapter.openFolder(QStringLiteral("Other")).value(QStringLiteral("ok")).toBool());
}

QTEST_GUILESS_MAIN(NotesAdapterTest)
#include "tst_notesadapter.moc"
