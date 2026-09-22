/** Focused suite for global appearance settings and the read-only system font catalog.
 *
 * Global appearance carries no colour: colour belongs to the note, as literal per-note
 * paper/ink stored by the document engine. Settings are never written at startup, so an
 * unavailable font family stays the user's stored choice while the platform default is
 * what gets painted.
 */
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "appearancesettings.h"
#include "fontcatalog.h"
#include "themecatalog.h"

class AppearanceTest final : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAreTheAcceptedColourlessSet();
    void normalizeClampsBoundsAndRejectsWrongTypes();
    void familyNamesAreValidatedBeforeTheyReachCss();
    void themeKeysAreValidatedWithoutRewritingUnavailableChoices();
    void changesPersistAndReloadFromXdgConfig();
    void defaultSettingsLiveInXdgConfigNotApplicationData();
    void missingOrDamagedSettingsFallBackWithoutWriting();
    void fontCatalogReportsRealInstalledFamiliesOnly();
    void unavailableFamilyResolvesWithoutRewritingThePreference();
};

void AppearanceTest::defaultsAreTheAcceptedColourlessSet()
{
    const QVariantMap defaults = AppearanceSettings::defaults();
    QCOMPARE(defaults.value(QStringLiteral("version")).toInt(), 3);
    QCOMPARE(defaults.value(QStringLiteral("fontFamily")).toString(), QStringLiteral("sans"));
    QCOMPARE(defaults.value(QStringLiteral("themeKey")).toString(), ThemeCatalog::builtInKey());
    QCOMPARE(defaults.value(QStringLiteral("fontSize")).toInt(), 13);
    QCOMPARE(defaults.value(QStringLiteral("lineSpacing")).toDouble(), 1.4);
    QCOMPARE(defaults.value(QStringLiteral("radius")).toInt(), 14);
    QCOMPARE(defaults.value(QStringLiteral("tabSpacing")).toInt(), 4);
    QCOMPARE(defaults.value(QStringLiteral("fanSpacing")).toInt(), 70);
    QCOMPARE(defaults.value(QStringLiteral("fanLabelFontSize")).toInt(), 9);
    QCOMPARE(defaults.value(QStringLiteral("fanLabelBold")).toBool(), true);

    // Appearance carries no colour at all: paper and ink are per-note literals.
    for (const QString &key : defaults.keys()) {
        QVERIFY2(!key.contains(QStringLiteral("colour"), Qt::CaseInsensitive)
                     && !key.contains(QStringLiteral("color"), Qt::CaseInsensitive)
                     && !key.contains(QStringLiteral("paper"), Qt::CaseInsensitive)
                     && !key.contains(QStringLiteral("ink"), Qt::CaseInsensitive),
                 qPrintable(key));
    }
}

void AppearanceTest::normalizeClampsBoundsAndRejectsWrongTypes()
{
    QVariantMap input = AppearanceSettings::defaults();
    input[QStringLiteral("fontSize")] = 999;
    input[QStringLiteral("lineSpacing")] = 0.1;
    input[QStringLiteral("fanSpacing")] = -5;
    input[QStringLiteral("radius")] = QStringLiteral("14");   // wrong type: falls back
    input[QStringLiteral("fanLabelBold")] = QStringLiteral("true"); // wrong type
    input[QStringLiteral("editorPaper")] = QStringLiteral("#ffffff"); // colour: dropped
    input[QStringLiteral("themeKey")] = QStringLiteral("../../not-a-theme");

    const QVariantMap out = AppearanceSettings::normalize(input);
    QCOMPARE(out.value(QStringLiteral("fontSize")).toInt(), 28);
    QCOMPARE(out.value(QStringLiteral("lineSpacing")).toDouble(), 1.0);
    QCOMPARE(out.value(QStringLiteral("fanSpacing")).toInt(), 28);
    QCOMPARE(out.value(QStringLiteral("radius")).toInt(), 14);
    QCOMPARE(out.value(QStringLiteral("fanLabelBold")).toBool(), true);
    QCOMPARE(out.value(QStringLiteral("themeKey")).toString(), ThemeCatalog::builtInKey());
    QVERIFY(!out.contains(QStringLiteral("editorPaper")));

    // A record from an older layout is not partially trusted.
    QVariantMap old = AppearanceSettings::defaults();
    old[QStringLiteral("version")] = 2;
    old[QStringLiteral("fontSize")] = 25;
    QCOMPARE(AppearanceSettings::normalize(old), AppearanceSettings::defaults());
}

void AppearanceTest::themeKeysAreValidatedWithoutRewritingUnavailableChoices()
{
    QVERIFY(AppearanceSettings::safeThemeKey(QStringLiteral("builtin:fan-fold")));
    QVERIFY(AppearanceSettings::safeThemeKey(QStringLiteral("local:catppuccin/mocha")));
    QVERIFY(!AppearanceSettings::safeThemeKey(QString()));
    QVERIFY(!AppearanceSettings::safeThemeKey(QStringLiteral("../catalog")));
    QVERIFY(!AppearanceSettings::safeThemeKey(QStringLiteral("local:a/../../catalog")));
    QVERIFY(!AppearanceSettings::safeThemeKey(QString(129, QLatin1Char('a'))));

    QTemporaryDir state;
    QVERIFY(state.isValid());
    {
        AppearanceSettings settings(state.path());
        QVERIFY(settings.setValue(QStringLiteral("themeKey"),
                                  QStringLiteral("local:catppuccin/mocha")));
    }
    AppearanceSettings reopened(state.path());
    QCOMPARE(reopened.value(QStringLiteral("themeKey")).toString(),
             QStringLiteral("local:catppuccin/mocha"));
}

void AppearanceTest::familyNamesAreValidatedBeforeTheyReachCss()
{
    QVERIFY(AppearanceSettings::safeFamily(QStringLiteral("Noto Sans")));
    QVERIFY(AppearanceSettings::safeFamily(QStringLiteral("DejaVu Sans Mono")));
    QVERIFY(AppearanceSettings::safeFamily(QStringLiteral("M+ 1c")));
    QVERIFY(!AppearanceSettings::safeFamily(QString()));
    QVERIFY(!AppearanceSettings::safeFamily(QStringLiteral(" Leading")));
    QVERIFY(!AppearanceSettings::safeFamily(QStringLiteral("a\";}body{display:none")));
    QVERIFY(!AppearanceSettings::safeFamily(QStringLiteral("---")));
    QVERIFY(!AppearanceSettings::safeFamily(QString(65, QLatin1Char('a'))));
}

void AppearanceTest::changesPersistAndReloadFromXdgConfig()
{
    QTemporaryDir state;
    QVERIFY(state.isValid());
    {
        AppearanceSettings settings(state.path());
        QSignalSpy changed(&settings, &AppearanceSettings::changed);
        QVERIFY(settings.setValue(QStringLiteral("fontSize"), 22));
        QVERIFY(settings.setValue(QStringLiteral("fanLabelBold"), false));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(settings.value(QStringLiteral("fontSize")).toInt(), 22);
        QVERIFY(QFileInfo::exists(settings.path()));

        // An out-of-range write is clamped rather than refused outright.
        QVERIFY(settings.setValue(QStringLiteral("fontSize"), 400));
        QCOMPARE(settings.value(QStringLiteral("fontSize")).toInt(), 28);
        // An unknown key is not silently invented.
        QVERIFY(!settings.setValue(QStringLiteral("nonsense"), 1));
    }

    AppearanceSettings reopened(state.path());
    QCOMPARE(reopened.value(QStringLiteral("fontSize")).toInt(), 28);
    QCOMPARE(reopened.value(QStringLiteral("fanLabelBold")).toBool(), false);
    QVERIFY(reopened.notice().isEmpty());

    QVERIFY(reopened.reset());
    QCOMPARE(reopened.settings(), AppearanceSettings::defaults());
}

void AppearanceTest::defaultSettingsLiveInXdgConfigNotApplicationData()
{
    AppearanceSettings settings;
    QCOMPARE(QFileInfo(settings.path()).absolutePath(),
             QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    QVERIFY(settings.path()
            != QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                   .filePath(QStringLiteral("appearance.json")));
}

void AppearanceTest::missingOrDamagedSettingsFallBackWithoutWriting()
{
    QTemporaryDir state;
    QVERIFY(state.isValid());
    AppearanceSettings fresh(state.path());
    QCOMPARE(fresh.settings(), AppearanceSettings::defaults());
    QVERIFY(fresh.notice().isEmpty());
    QVERIFY2(!QFileInfo::exists(fresh.path()), "reading settings must never write them");

    QDir().mkpath(QFileInfo(fresh.path()).absolutePath());
    QFile damaged(fresh.path());
    QVERIFY(damaged.open(QIODevice::WriteOnly));
    damaged.write("{ not json at all");
    damaged.close();

    AppearanceSettings recovered(state.path());
    QCOMPARE(recovered.settings(), AppearanceSettings::defaults());
    QVERIFY(!recovered.notice().isEmpty());
    // The damaged record is retained for inspection, not quietly overwritten.
    QCOMPARE(damaged.size(), qint64(17));
}

void AppearanceTest::fontCatalogReportsRealInstalledFamiliesOnly()
{
    FontCatalog fonts;
    const QStringList families = fonts.families();
    QVERIFY2(!families.isEmpty(), "the platform font database reported nothing");

    QStringList sorted = families;
    std::sort(sorted.begin(), sorted.end(), [](const QString &l, const QString &r) {
        const int order = l.compare(r, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : l < r;
    });
    QCOMPARE(families, sorted);
    QCOMPARE(families.size(), QSet<QString>(families.begin(), families.end()).size());
    for (const QString &family : families) {
        QVERIFY2(!family.startsWith(QLatin1Char('.')), qPrintable(family));
        QVERIFY2(AppearanceSettings::safeFamily(family), qPrintable(family));
        QVERIFY(fonts.isInstalled(family));
    }
    QVERIFY(!fonts.systemFamily().isEmpty());
    QVERIFY(fonts.isInstalled(fonts.systemFamily()));
}

void AppearanceTest::unavailableFamilyResolvesWithoutRewritingThePreference()
{
    QTemporaryDir state;
    QVERIFY(state.isValid());
    FontCatalog fonts;
    AppearanceSettings settings(state.path());

    const QString absent = QStringLiteral("Definitely Not Installed 12345");
    QVERIFY(!fonts.isInstalled(absent));
    QVERIFY(settings.setValue(QStringLiteral("fontFamilyName"), absent));
    QCOMPARE(fonts.resolveFamily(absent), fonts.systemFamily());

    const QVariantMap described = fonts.describe(absent);
    QCOMPARE(described.value(QStringLiteral("wanted")).toString(), absent);
    QCOMPARE(described.value(QStringLiteral("installed")).toBool(), false);
    QCOMPARE(described.value(QStringLiteral("resolved")).toString(), fonts.systemFamily());

    // Resolution is a pure read: the stored preference is still the user's choice.
    AppearanceSettings reopened(state.path());
    QCOMPARE(reopened.value(QStringLiteral("fontFamilyName")).toString(), absent);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    const QString xdg = QDir::temp().filePath(
        QStringLiteral("fanfold-appearance-xdg-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(xdg);
    qputenv("XDG_DATA_HOME", xdg.toUtf8());
    qputenv("XDG_CONFIG_HOME", QDir(xdg).filePath("config").toUtf8());
    QGuiApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("FanFoldTests"));
    application.setApplicationName(QStringLiteral("appearance"));
    AppearanceTest test;
    const int result = QTest::qExec(&test, argc, argv);
    QDir(xdg).removeRecursively();
    return result;
}

#include "tst_appearance.moc"
