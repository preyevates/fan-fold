/** Focused contract tests for optional local theme-catalog bundles.
 *
 * The production loader must be all-or-nothing: an incomplete, modified or
 * provenance-free bundle exposes none of its themes. A valid bundle retains every
 * family, variant and named role while the built-in public-package palette remains
 * available with no external files at all.
 */
#include "themecatalog.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

/** Write bytes to a test-owned path.
 * @param path Destination below a QTemporaryDir.
 * @param bytes Exact contents to write.
 * @return true when the complete file was written.
 */
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

/** Make one catalog variant with deliberately duplicated and aliased colors.
 * @param id Stable variant identifier.
 * @param name Human-readable variant name.
 * @param foreground Primary foreground color.
 * @return JSON record matching the external catalog schema.
 */
QJsonObject variant(const QString &id, const QString &name, const QString &foreground)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("background"), QStringLiteral("#101112")},
            {QStringLiteral("foreground"), foreground},
            {QStringLiteral("page"), QStringLiteral("https://example.invalid/theme/") + id},
            {QStringLiteral("source"), QStringLiteral("https://example.invalid/download/") + id},
            {QStringLiteral("sha256"), QString(64, u'a')},
            {QStringLiteral("link_verified"), true},
            {QStringLiteral("roles"),
             QJsonObject{{QStringLiteral("primary"),
                          QJsonObject{{QStringLiteral("background"), QStringLiteral("#101112")},
                                      {QStringLiteral("foreground"), foreground}}},
                         {QStringLiteral("normal"),
                          QJsonObject{{QStringLiteral("black"), QStringLiteral("#101112")},
                                      {QStringLiteral("blue"), QStringLiteral("#334455")}}}}}};
}

/** Create a complete two-variant local bundle.
 * @param root Empty temporary directory that receives catalog, manifest and notice.
 * @return exact catalog bytes used for checksum assertions.
 */
QByteArray writeValidBundle(const QString &root)
{
    const QJsonObject variants{{QStringLiteral("dark"),
                                variant(QStringLiteral("dark"), QStringLiteral("Dark"),
                                        QStringLiteral("#f0f1f2"))},
                               {QStringLiteral("light"),
                                variant(QStringLiteral("light"), QStringLiteral("Light"),
                                        QStringLiteral("#202122"))}};
    const QJsonObject family{{QStringLiteral("id"), QStringLiteral("sample")},
                             {QStringLiteral("name"), QStringLiteral("Sample")},
                             {QStringLiteral("page"), QStringLiteral("https://example.invalid/sample/")},
                             {QStringLiteral("declared_variants"), 2},
                             {QStringLiteral("variants"), variants}};
    const QByteArray catalog =
        QJsonDocument(QJsonObject{{QStringLiteral("families"),
                                   QJsonObject{{QStringLiteral("sample"), family}}}})
            .toJson(QJsonDocument::Indented);
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(catalog, QCryptographicHash::Sha256).toHex());
    const QJsonObject manifest{
        {QStringLiteral("source"), QStringLiteral("https://example.invalid")},
        {QStringLiteral("retrieved"), QStringLiteral("2026-09-16T00:00:00+00:00")},
        {QStringLiteral("catalog_sha256"), digest},
        {QStringLiteral("inventory"),
         QJsonObject{{QStringLiteral("families"), 1},
                     {QStringLiteral("variants_declared"), 2},
                     {QStringLiteral("variants_imported"), 2},
                     {QStringLiteral("unique_exported_hexes"), 4}}},
        {QStringLiteral("licensing"),
         QJsonObject{{QStringLiteral("status"), QStringLiteral("unclear")},
                     {QStringLiteral("use"), QStringLiteral("LOCAL TEST ONLY")},
                     {QStringLiteral("notice"), QStringLiteral("Synthetic test fixture")}}}};

    if (!writeFile(root + QStringLiteral("/catalog.json"), catalog)
        || !writeFile(root + QStringLiteral("/manifest.json"),
                      QJsonDocument(manifest).toJson(QJsonDocument::Indented))
        || !writeFile(root + QStringLiteral("/NOTICES.md"), QByteArray("# Synthetic provenance\n"))) {
        return {};
    }
    return catalog;
}

} // namespace

class ThemeCatalogTest final : public QObject
{
    Q_OBJECT

private slots:
    void noBundleLeavesCompleteBuiltInFallback();
    void validBundlePreservesEveryVariantAndRole();
    void checksumMismatchRejectsWholeBundle();
    void invalidVariantRejectsWholeBundle();
    void missingProvenanceRejectsWholeBundle();
    void configuredLocalBundleRetainsPrototypeInventory();
};

void ThemeCatalogTest::noBundleLeavesCompleteBuiltInFallback()
{
    ThemeCatalog catalog;

    QVERIFY(!catalog.localCatalogAvailable());
    QVERIFY(catalog.notice().isEmpty());
    QCOMPARE(catalog.familyCount(), 0);
    QCOMPARE(catalog.variantCount(), 0);
    QCOMPARE(catalog.roleCount(), 0);
    QCOMPARE(catalog.choices().size(), 1);
    QCOMPARE(catalog.choices().first().toMap().value(QStringLiteral("key")).toString(),
             ThemeCatalog::builtInKey());
    QCOMPARE(catalog.palette(QStringLiteral("local:missing/nope"))
                 .value(QStringLiteral("key")).toString(),
             ThemeCatalog::builtInKey());
}

void ThemeCatalogTest::validBundlePreservesEveryVariantAndRole()
{
    QTemporaryDir bundle;
    QVERIFY(bundle.isValid());
    QVERIFY(!writeValidBundle(bundle.path()).isEmpty());

    ThemeCatalog catalog(bundle.path());

    QVERIFY2(catalog.localCatalogAvailable(), qPrintable(catalog.notice()));
    QCOMPARE(catalog.familyCount(), 1);
    QCOMPARE(catalog.variantCount(), 2);
    QCOMPARE(catalog.roleCount(), 8); // four named aliases on each variant, duplicates retained
    QCOMPARE(catalog.uniqueColorCount(), 4);
    QCOMPARE(catalog.choices().size(), 3); // built-in plus both external variants

    const QVariantMap dark = catalog.palette(QStringLiteral("local:sample/dark"));
    QCOMPARE(dark.value(QStringLiteral("label")).toString(), QStringLiteral("Sample — Dark"));
    QCOMPARE(dark.value(QStringLiteral("tokens")).toList().size(), 4);
    QCOMPARE(dark.value(QStringLiteral("swatches")).toList().size(), 3); // duplicate hex collapsed only visually
    QCOMPARE(dark.value(QStringLiteral("background")).toString(), QStringLiteral("#101112"));
    QCOMPARE(dark.value(QStringLiteral("foreground")).toString(), QStringLiteral("#f0f1f2"));

    const QVariantMap provenance = catalog.provenance();
    QCOMPARE(provenance.value(QStringLiteral("source")).toString(),
             QStringLiteral("https://example.invalid"));
    QCOMPARE(provenance.value(QStringLiteral("retrieved")).toString(),
             QStringLiteral("2026-09-16T00:00:00+00:00"));
    QCOMPARE(provenance.value(QStringLiteral("use")).toString(),
             QStringLiteral("LOCAL TEST ONLY"));
}

void ThemeCatalogTest::checksumMismatchRejectsWholeBundle()
{
    QTemporaryDir bundle;
    QVERIFY(bundle.isValid());
    QVERIFY(!writeValidBundle(bundle.path()).isEmpty());
    QFile catalog(bundle.filePath(QStringLiteral("catalog.json")));
    QVERIFY(catalog.open(QIODevice::Append));
    QCOMPARE(catalog.write("\n"), qint64(1));
    catalog.close();

    ThemeCatalog loaded(bundle.path());
    QVERIFY(!loaded.localCatalogAvailable());
    QCOMPARE(loaded.choices().size(), 1);
    QVERIFY(loaded.notice().contains(QStringLiteral("checksum"), Qt::CaseInsensitive));
}

void ThemeCatalogTest::invalidVariantRejectsWholeBundle()
{
    QTemporaryDir bundle;
    QVERIFY(bundle.isValid());
    QVERIFY(!writeValidBundle(bundle.path()).isEmpty());

    QFile input(bundle.filePath(QStringLiteral("catalog.json")));
    QVERIFY(input.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(input.readAll()).object();
    input.close();
    QJsonObject families = root.value(QStringLiteral("families")).toObject();
    QJsonObject sample = families.value(QStringLiteral("sample")).toObject();
    QJsonObject variants = sample.value(QStringLiteral("variants")).toObject();
    QJsonObject light = variants.value(QStringLiteral("light")).toObject();
    light[QStringLiteral("background")] = QStringLiteral("not-a-color");
    variants[QStringLiteral("light")] = light;
    sample[QStringLiteral("variants")] = variants;
    families[QStringLiteral("sample")] = sample;
    root[QStringLiteral("families")] = families;
    const QByteArray changed = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QVERIFY(writeFile(bundle.filePath(QStringLiteral("catalog.json")), changed));

    QFile manifestFile(bundle.filePath(QStringLiteral("manifest.json")));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    manifest[QStringLiteral("catalog_sha256")] = QString::fromLatin1(
        QCryptographicHash::hash(changed, QCryptographicHash::Sha256).toHex());
    QVERIFY(writeFile(bundle.filePath(QStringLiteral("manifest.json")),
                      QJsonDocument(manifest).toJson(QJsonDocument::Indented)));

    ThemeCatalog loaded(bundle.path());
    QVERIFY(!loaded.localCatalogAvailable());
    QCOMPARE(loaded.choices().size(), 1);
    QVERIFY(loaded.notice().contains(QStringLiteral("color"), Qt::CaseInsensitive));
}

void ThemeCatalogTest::missingProvenanceRejectsWholeBundle()
{
    QTemporaryDir bundle;
    QVERIFY(bundle.isValid());
    QVERIFY(!writeValidBundle(bundle.path()).isEmpty());
    QVERIFY(QFile::remove(bundle.filePath(QStringLiteral("NOTICES.md"))));

    ThemeCatalog loaded(bundle.path());
    QVERIFY(!loaded.localCatalogAvailable());
    QCOMPARE(loaded.choices().size(), 1);
    QVERIFY(loaded.notice().contains(QStringLiteral("NOTICES.md")));
}

void ThemeCatalogTest::configuredLocalBundleRetainsPrototypeInventory()
{
    const QString bundle = qEnvironmentVariable("FANFOLD_TEST_THEME_BUNDLE");
    if (bundle.isEmpty()) {
        QSKIP("No opt-in local bundle configured for this build");
    }

    ThemeCatalog loaded(bundle);
    QVERIFY2(loaded.localCatalogAvailable(), qPrintable(loaded.notice()));
    QCOMPARE(loaded.familyCount(), 36);
    QCOMPARE(loaded.variantCount(), 112);
    QCOMPARE(loaded.roleCount(), 2464);
    QCOMPARE(loaded.uniqueColorCount(), 1308);
    QCOMPARE(loaded.choices().size(), 113); // built-in plus every retained variant

    const QVariantMap last = loaded.palette(QStringLiteral("local:zenbones/zenwritten-light"));
    QCOMPARE(last.value(QStringLiteral("label")).toString(),
             QStringLiteral("Zenbones — Zenwritten Light"));
    QCOMPARE(last.value(QStringLiteral("tokens")).toList().size(), 22);
}

QTEST_GUILESS_MAIN(ThemeCatalogTest)
#include "tst_themecatalog.moc"
