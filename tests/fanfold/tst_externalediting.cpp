#include "documentcollection.h"
#include "storeadapter.h"

#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

namespace {
void replaceFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    QVERIFY(file.commit());
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}

class ExternalEditingTest : public QObject
{
    Q_OBJECT
private slots:
    void cleanExternalRevisionCannotBeOverwrittenByStaleEditor();
    void externalRevisionBeforeReconcileCannotBeOverwritten();
    void dirtyBufferSurvivesExternalEditAndRestart();
    void ownAutosaveAllowsNextEditBeforePoll();
    void failedRecoveryJournalIsReportedAndPreventsUnsafeSave();
    void lateWriteThroughExternalDescriptorRemainsReachable();
};

void ExternalEditingTest::cleanExternalRevisionCannotBeOverwrittenByStaleEditor()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    const QString path = notes.filePath("Synthetic.md");
    replaceFile(path, "original\n");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    StoreAdapter adapter(&collection);
    const QString id = collection.idForRelativePath("Synthetic.md");
    const QVariantMap loaded = adapter.load(id);
    QVERIFY(loaded.value("ok").toBool());

    replaceFile(path, "external\n");
    collection.reconcileNow();
    QVERIFY(!adapter.updateContent(id, "local edit\n"));
    QVERIFY(collection.document(id)->conflict());
    QVERIFY(collection.document(id)->dirty());
    QCOMPARE(collection.document(id)->content(), QStringLiteral("local edit\n"));
    QVERIFY(collection.hasRecovery(id));
    QVERIFY(!adapter.save(id, "local edit\n", loaded.value("revision").toString()).value("ok").toBool());
    QTest::qWait(400);
    QCOMPARE(readFile(path), QByteArray("external\n"));
    QCOMPARE(adapter.load(id).value("text").toString(), QStringLiteral("local edit\n"));
}

void ExternalEditingTest::externalRevisionBeforeReconcileCannotBeOverwritten()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    const QString path = notes.filePath("Synthetic.md");
    replaceFile(path, "original\n");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    StoreAdapter adapter(&collection);
    const QString id = collection.idForRelativePath("Synthetic.md");
    const QVariantMap loaded = adapter.load(id);
    replaceFile(path, "external\n");
    QVERIFY(!adapter.save(id, "latest local edit\n", loaded.value("revision").toString()).value("ok").toBool());
    QCOMPARE(collection.document(id)->content(), QStringLiteral("latest local edit\n"));
    QVERIFY(collection.hasRecovery(id));
    QCOMPARE(readFile(path), QByteArray("external\n"));
    QCOMPARE(adapter.load(id).value("text").toString(), QStringLiteral("latest local edit\n"));
    collection.closeRoot();
    DocumentCollection reopened(state.path());
    QVERIFY2(reopened.openRoot(notes.path()), qPrintable(reopened.lastError()));
    const QString restoredId = reopened.idForRelativePath("Synthetic.md");
    QVERIFY(reopened.document(restoredId)->conflict());
    QCOMPARE(reopened.document(restoredId)->content(), QStringLiteral("latest local edit\n"));
    QTest::qWait(400); // no resumed autosave may replace the external version
    QCOMPARE(readFile(path), QByteArray("external\n"));
    QVERIFY(reopened.hasRecovery(restoredId));
}

void ExternalEditingTest::dirtyBufferSurvivesExternalEditAndRestart()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    const QString path = notes.filePath("Synthetic.md");
    replaceFile(path, "original\n");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    StoreAdapter adapter(&collection);
    const QString id = collection.idForRelativePath("Synthetic.md");
    QVERIFY(adapter.load(id).value("ok").toBool());
    QVERIFY(adapter.updateContent(id, "pending local\n"));
    replaceFile(path, "external\n");
    QVERIFY(!adapter.updateContent(id, "latest pending local\n"));
    QCOMPARE(collection.document(id)->content(), QStringLiteral("latest pending local\n"));
    QCOMPARE(readFile(path), QByteArray("external\n"));
    collection.closeRoot();
    DocumentCollection reopened(state.path());
    QVERIFY(reopened.openRoot(notes.path()));
    const QString restoredId = reopened.idForRelativePath("Synthetic.md");
    QVERIFY(reopened.document(restoredId)->conflict());
    QCOMPARE(reopened.document(restoredId)->content(), QStringLiteral("latest pending local\n"));
    QCOMPARE(readFile(path), QByteArray("external\n"));
}

void ExternalEditingTest::ownAutosaveAllowsNextEditBeforePoll()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    const QString path = notes.filePath("Synthetic.md");
    replaceFile(path, "original\n");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    StoreAdapter adapter(&collection);
    const QString id = collection.idForRelativePath("Synthetic.md");
    QVERIFY(adapter.load(id).value("ok").toBool());
    QVERIFY(adapter.updateContent(id, "first\n"));
    const QVariantMap beforeCommit = adapter.probe(id);
    QVERIFY(!beforeCommit.value("conflict").toBool());
    QVERIFY(!beforeCommit.value("committed").toBool());
    QCOMPARE(beforeCommit.value("revision"), adapter.info(id).value("loadedRevision"));
    QVERIFY(collection.saveNow(id));
    QVERIFY(adapter.probe(id).value("committed").toBool());
    QVERIFY(adapter.updateContent(id, "second\n"));
    QVERIFY(collection.saveNow(id));
    QCOMPARE(readFile(path), QByteArray("second\n"));
}

void ExternalEditingTest::failedRecoveryJournalIsReportedAndPreventsUnsafeSave()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    const QString path = notes.filePath("Synthetic.md");
    replaceFile(path, "original\n");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    StoreAdapter adapter(&collection);
    const QString id = collection.idForRelativePath("Synthetic.md");
    const QString revision = adapter.load(id).value("revision").toString();
    QDirIterator entries(state.path(), {QStringLiteral("recovery")}, QDir::Dirs,
                         QDirIterator::Subdirectories);
    QVERIFY(entries.hasNext());
    const QString recoveryDir = entries.next();
    QVERIFY(QDir().rmdir(recoveryDir));
    QFile blocked(recoveryDir);
    QVERIFY(blocked.open(QIODevice::WriteOnly));
    blocked.close();
    const QVariantMap saved = adapter.save(id, QStringLiteral("unsaved\n"), revision);
    QVERIFY(!saved.value("ok").toBool());
    QVERIFY(saved.value("error").toString().contains("Recovery write failed"));
    QVERIFY(adapter.probe(id).value("saveError").toString().contains("Recovery write failed"));
    QVERIFY(!collection.saveNow(id));
    QCOMPARE(readFile(path), QByteArray("original\n"));
    QCOMPARE(collection.document(id)->content(), QStringLiteral("unsaved\n"));
}

void ExternalEditingTest::lateWriteThroughExternalDescriptorRemainsReachable()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    const QString path = notes.filePath("Synthetic.md");
    replaceFile(path, "original\n");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    StoreAdapter adapter(&collection);
    const QString id = collection.idForRelativePath("Synthetic.md");
    QVERIFY(adapter.load(id).value("ok").toBool());
    QFile oldDescriptor(path);
    QVERIFY(oldDescriptor.open(QIODevice::ReadWrite));
    QVERIFY(adapter.updateContent(id, "local\n"));
    QVERIFY(collection.saveNow(id));
    QVERIFY(oldDescriptor.seek(0));
    QCOMPARE(oldDescriptor.write("late-external\n"), qint64(QByteArray("late-external\n").size()));
    QVERIFY(oldDescriptor.flush());
    QCOMPARE(readFile(path), QByteArray("local\n"));
    QDirIterator retained(notes.filePath(QStringLiteral(".fanfold-displaced")),
                          {QStringLiteral("*.old")}, QDir::Files);
    QVERIFY2(retained.hasNext(), "the previous inode must remain reachable beside the library");
    QCOMPARE(readFile(retained.next()), QByteArray("late-external\n"));
    QVERIFY(adapter.updateContent(id, "next local\n"));
    QVERIFY(!collection.saveNow(id));
    QVERIFY(collection.document(id)->conflict());
    QCOMPARE(readFile(path), QByteArray("local\n"));
}

QTEST_GUILESS_MAIN(ExternalEditingTest)
#include "tst_externalediting.moc"
