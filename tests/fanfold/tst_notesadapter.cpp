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

QTEST_GUILESS_MAIN(NotesAdapterTest)
#include "tst_notesadapter.moc"
