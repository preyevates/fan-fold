#include "documentcollection.h"
#include "storeadapter.h"

#include <QDirIterator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

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
    void loadReportsEncodedNoteUrl();
    void legacyAssetsPreferNoteFolder();
    void cleanExternalRevisionCannotBeOverwrittenByStaleEditor();
    void externalRevisionBeforeReconcileCannotBeOverwritten();
    void dirtyBufferSurvivesExternalEditAndRestart();
    void ownAutosaveAllowsNextEditBeforePoll();
    void failedRecoveryJournalIsReportedAndPreventsUnsafeSave();
    void lateWriteThroughExternalDescriptorRemainsReachable();
};

void ExternalEditingTest::legacyAssetsPreferNoteFolder()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    QVERIFY(QDir(notes.path()).mkpath("Nested/Assets"));
    QVERIFY(QDir(notes.path()).mkpath("Assets"));
    replaceFile(notes.filePath("Nested/Source.md"), "![Legacy](Assets/picture%20%231.svg)\n");
    replaceFile(notes.filePath("Assets/picture #1.svg"), "root asset");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    StoreAdapter adapter(&collection);
    const QString id = collection.idForRelativePath("Nested/Source.md");
    auto resolve = [&](const QString &href) {
        QString result;
        const bool invoked = QMetaObject::invokeMethod(&adapter, "resolveAsset", Qt::DirectConnection,
            Q_RETURN_ARG(QString, result), Q_ARG(QString, id), Q_ARG(QString, href));
        return invoked ? result : QStringLiteral("MISSING RESOLVER");
    };
    const QString rootUrl = QUrl::fromLocalFile(notes.filePath("Assets/picture #1.svg")).toString(QUrl::FullyEncoded);
    QCOMPARE(resolve("Assets/picture%20%231.svg"), rootUrl);
    const QString localPath = notes.filePath("Nested/Assets/picture #1.svg");
    replaceFile(localPath, "note-local asset");
    QCOMPARE(resolve("Assets/picture%20%231.svg"), QUrl::fromLocalFile(localPath).toString(QUrl::FullyEncoded));
    const QString absolute = QUrl::fromLocalFile(notes.filePath("Nested/missing.pdf")).toString(QUrl::FullyEncoded);
    QCOMPARE(resolve(absolute), absolute);
    QCOMPARE(resolve("https://example.com/Assets/a.svg"), QStringLiteral("https://example.com/Assets/a.svg"));
    QCOMPARE(resolve("../Other/x.md"), QUrl::fromLocalFile(notes.filePath("Other/x.md")).toString(QUrl::FullyEncoded));
}

void ExternalEditingTest::loadReportsEncodedNoteUrl()
{
    QTemporaryDir notes, state;
    QVERIFY(notes.isValid() && state.isValid());
    QVERIFY(QDir(notes.path()).mkpath("Remediation #%"));
    const QString path = notes.filePath("Remediation #%/Source.md");
    replaceFile(path, "[Sibling](../Other/x.md)\n");
    DocumentCollection collection(state.path());
    QVERIFY(collection.openRoot(notes.path()));
    StoreAdapter adapter(&collection);
    const QVariantMap loaded = adapter.load(collection.idForRelativePath("Remediation #%/Source.md"));
    QVERIFY(loaded.value("ok").toBool());
    const QString url = loaded.value("url").toString();
    QCOMPARE(loaded.value("assetPrefix").toString(), QStringLiteral("../"));
    QVERIFY(url.endsWith("/Remediation%20%23%25/Source.md"));
    QCOMPARE(QUrl(url).toLocalFile(), path);
    QCOMPARE(QUrl(url).resolved(QUrl("../Other/x.md")).toLocalFile(), notes.filePath("Other/x.md"));
}

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
    const QString recoveryDir = QFileInfo(collection.metadataPath()).dir().filePath(QStringLiteral("recovery"));
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
    QDirIterator retained(notes.filePath(QStringLiteral(".fanfold/displaced")),
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
