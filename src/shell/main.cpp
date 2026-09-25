/**
 * Fan Fold — an edge-docked Markdown notes shell over an ordinary folder of files.
 *
 * What the engine supplies beneath the QML presentation: markdown files on disk as the
 * source of truth with recursive discovery and atomic writes, 250 ms debounced autosave
 * with crash recovery, archive/restore and trash, library and search, pinned notes,
 * per-note paper/ink and system fonts.
 *
 * The root window is a `PlasmaCore.Dialog` of type Dock rather than a plain `Window`, and
 * that choice is load-bearing rather than cosmetic: under a Wayland compositor a plain
 * `Window` that assigns `x`/`y` is simply ignored by KWin, leaving the fan floating tens
 * of pixels clear of the screen edge. The Plasma dialog is the only construct that
 * actually docks flush.
 *
 * Usage: fanfold [--root <folder>] [--state <folder>]
 */
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QSet>
#include <QHash>
#include <QRegularExpression>
#include <QApplication>
#include <QIcon>
#include <KDBusService>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QSystemTrayIcon>
#include <QUrl>

#include "appearanceadapter.h"
#include "appearancesettings.h"
#include "documentcollection.h"
#include "fontcatalog.h"
#include "librarymodel.h"
#include "notesadapter.h"
#include "storeadapter.h"

namespace {

/**
 * Bridge between the native system-tray icon and the QML shell.
 *
 * The fan is a Dock-type window, and dock windows are deliberately absent from the task
 * manager — which left no persistent handle on the app at all once the card collapsed.
 * The tray icon is that handle: a StatusNotifierItem with the actions that make sense
 * away from the card. The signals are consumed by Connections in Main.qml, so every verb
 * runs through exactly the code path its footer/QML equivalent already exercises.
 */
class TrayBridge : public QObject
{
    Q_OBJECT
signals:
    /** Show/re-dock the fan window, as edge-hover does. */
    void showRequested();
    /** Quit through the ordinary close guard, never a raw exit. */
    void quitRequested();
    /** Reveal the fan without toggling it closed: a second launch asked to see it. */
    void revealRequested();
};

/**
 * Live shell control: the notes folder as a changeable fact.
 *
 * openFolder() is the one path that re-roots the library: it re-roots the engine,
 * refreshes the fan from the new folder's discovery, and persists the choice. This
 * interactive path is exactly the case the QSettings write exists for — an explicit
 * --root never persists.
 */
class ShellControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootChanged)
public:
    explicit ShellControl(DocumentCollection *collection, QObject *parent = nullptr)
        : QObject(parent)
        , m_collection(collection)
    {
    }

    QString rootPath() const { return m_collection ? m_collection->rootPath() : QString(); }

    /**
     * Import a dropped, pasted or recorded file into the library's own Assets tree.
     *
     * Takes the BYTES, not a path: a file dropped into a web view arrives as a DOM File
     * whose filesystem location the page is not allowed to see, and a pasted image or a
     * microphone recording never had a path at all. Base64 is the one encoding available
     * on every one of those routes.
     *
     * The library folder is the unit the user moves, syncs and backs up, so an asset
     * lives INSIDE it and is referenced relatively — an absolute path into ~/Pictures would
     * break the moment the folder travels. Files are filed by kind so the tree stays
     * legible to a human browsing it:
     *
     *     Assets/images/   Assets/audio/   Assets/video/   Assets/files/
     *
     * Only `.md` is ever scanned as a note, so nothing placed here becomes a stray tab.
     *
     * @param fileName  Original name, e.g. "diagram.png"; used for the stored name only.
     * @param base64    The file's bytes, base64-encoded.
     * @param kindHint  MIME type from the drop, e.g. "image/png"; may be empty.
     * @return A map with ok, and either `relative` (the Markdown-ready relative path) or
     *         `error` (a sentence fit to show the user).
     */
    Q_INVOKABLE QVariantMap importAsset(const QString &fileName, const QString &base64,
                                        const QString &kindHint)
    {
        const auto fail = [](const QString &message) {
            return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
        };
        if (!m_collection || m_collection->rootPath().isEmpty()) {
            return fail(QStringLiteral("No notes folder is open"));
        }
        const QByteArray bytes = QByteArray::fromBase64(base64.toLatin1());
        if (bytes.isEmpty()) {
            return fail(QStringLiteral("That file was empty or could not be read"));
        }
        // 64 MB: generous for a screenshot or a short recording, and a firm stop well
        // before a stray multi-gigabyte drop is copied into the notes folder.
        if (bytes.size() > 64 * 1024 * 1024) {
            return fail(QStringLiteral("That file is larger than 64 MB"));
        }
        // The name is used for the STORED name only, never as a path: a crafted
        // "../../.bashrc" must not escape the Assets tree.
        const QFileInfo info(QFileInfo(fileName).fileName());
        const QString suffix = info.suffix().toLower();
        const QString kind = kindHint.toLower();
        QString bucket = QStringLiteral("files");
        static const QStringList imageSuffixes{"png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "avif"};
        static const QStringList audioSuffixes{"wav", "mp3", "ogg", "opus", "flac", "m4a"};
        static const QStringList videoSuffixes{"mp4", "webm", "mkv", "mov", "avi"};
        if (kind.startsWith(QStringLiteral("image/")) || imageSuffixes.contains(suffix)) {
            bucket = QStringLiteral("images");
        } else if (kind.startsWith(QStringLiteral("audio/")) || audioSuffixes.contains(suffix)) {
            bucket = QStringLiteral("audio");
        } else if (kind.startsWith(QStringLiteral("video/")) || videoSuffixes.contains(suffix)) {
            bucket = QStringLiteral("video");
        }

        QDir root(m_collection->rootPath());
        const QString relativeDir = QStringLiteral("Assets/") + bucket;
        if (!root.mkpath(relativeDir)) {
            return fail(QStringLiteral("Could not create ") + relativeDir);
        }
        // Never overwrite an existing asset: two screenshots both called Screenshot.png
        // are two different pictures, and silently replacing one would destroy a note's
        // illustration. The second becomes "Screenshot-2.png".
        QString base = info.completeBaseName();
        if (base.isEmpty()) {
            base = QStringLiteral("asset");
        }
        const QString extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
        QString relative = relativeDir + QStringLiteral("/") + base + extension;
        int attempt = 2;
        while (QFileInfo::exists(root.filePath(relative))) {
            if (attempt > 9999) {
                // Exhausting the counter must refuse, never fall through: the write below
                // truncates whatever `relative` names, which would destroy the asset this
                // loop exists to protect.
                return fail(QStringLiteral("Could not find a free name in ") + relativeDir);
            }
            relative = relativeDir + QStringLiteral("/") + base + QStringLiteral("-")
                + QString::number(attempt) + extension;
            ++attempt;
        }
        QFile out(root.filePath(relative));
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
            return fail(QStringLiteral("Could not write into ") + relativeDir);
        }
        out.close();
        return {{QStringLiteral("ok"), true}, {QStringLiteral("relative"), relative},
                {QStringLiteral("bucket"), bucket}};
    }

    /**
     * Every application installed on this machine, by its own launcher icon.
     *
     * A note about an application should be able to wear that application's icon. On
     * Linux the material is already present — each `.desktop` entry names an icon, and
     * the theme resolves it — so this needs no download and no favicon hunting, and it
     * always matches what is actually installed.
     *
     * NoDisplay and Hidden entries are skipped: they are launchers the desktop itself
     * refuses to show (agents, MIME handlers, autostart shims), and offering them would
     * bury the real applications.
     *
     * @return Entries of `name` (the application's own Name=), `icon` (theme name) and
     *         `group` ("Apps"), sorted by display name.
     */
    Q_INVOKABLE QVariantList appIcons() const
    {
        QVariantList out;
        QSet<QString> seen;
        QStringList dirs;
        for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
            dirs << base;
        }
        for (const QString &dir : dirs) {
            QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files);
            while (it.hasNext()) {
                const QString file = it.next();
                QSettings entry(file, QSettings::IniFormat);
                entry.beginGroup(QStringLiteral("Desktop Entry"));
                const QString icon = entry.value(QStringLiteral("Icon")).toString().trimmed();
                const QString name = entry.value(QStringLiteral("Name")).toString().trimmed();
                const bool hidden = entry.value(QStringLiteral("NoDisplay")).toBool()
                                 || entry.value(QStringLiteral("Hidden")).toBool();
                entry.endGroup();
                if (icon.isEmpty() || name.isEmpty() || hidden) continue;
                if (icon.startsWith(QLatin1Char('/'))) continue;   // absolute paths are not theme names
                if (!QIcon::hasThemeIcon(icon)) continue;          // do not offer what will not paint
                if (seen.contains(icon)) continue;
                seen.insert(icon);
                out.append(QVariantMap{{QStringLiteral("name"), name},
                                       {QStringLiteral("relative"), QStringLiteral("theme:") + icon},
                                       {QStringLiteral("group"), QStringLiteral("Apps")}});
            }
        }
        std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("name")).toString()) < 0;
        });
        return out;
    }

    /**
     * Every icon NAME the active theme can paint, for the picker's search field.
     *
     * Papirus carries over 18,000 unique names, so a curated list is the wrong shape: any
     * hand-picked selection covers a fraction of a percent of the theme and reads as
     * arbitrary. The picker searches instead, and this is the corpus it searches.
     *
     * Names are harvested from the theme's own directories rather than from a hardcoded
     * table, so installing a richer theme immediately enriches the picker with no code
     * change. The scan is cached after the first call: it walks tens of thousands of files.
     *
     * @param query  Case-insensitive substring; empty returns a bounded starter set.
     * @param limit  Maximum entries returned, so the grid cannot be handed 18k items.
     * @return Entries of `name` (the icon name), `relative` ("theme:<name>") and `group`.
     */
    Q_INVOKABLE QVariantList searchIcons(const QString &query, int limit = 400) const
    {
        if (m_iconNames.isEmpty()) harvestIconNames();
        const QString needle = query.trimmed().toLower();
        QVariantList out;
        for (const QString &name : m_iconNames) {
            if (out.size() >= limit) break;
            if (!needle.isEmpty() && !name.contains(needle)) continue;
            out.append(QVariantMap{{QStringLiteral("name"), name},
                                   {QStringLiteral("relative"), QStringLiteral("theme:") + name},
                                   {QStringLiteral("group"), QStringLiteral("Symbols")}});
        }
        return out;
    }

    /** Total number of searchable theme icons, for the picker's "N icons" hint. */
    Q_INVOKABLE int iconCount() const
    {
        if (m_iconNames.isEmpty()) harvestIconNames();
        return static_cast<int>(m_iconNames.size());
    }

    /** The theme actually supplying tab icons, shown in the picker so the source is never
     *  a mystery. Not necessarily the desktop's theme — see preferredIconTheme(). */
    Q_INVOKABLE QString iconThemeName() const { return preferredIconTheme(); }

    /** The version this build actually IS: the packaged revision when built from the
     *  Debian rules (dpkg-parsechangelog), the project version for a plain source build.
     *  Settings -> About shows it so the running release is identifiable without leaving
     *  the application. */
    Q_INVOKABLE QString appVersion() const { return QCoreApplication::applicationVersion(); }

    /**
     * Resolve a theme icon NAME to a concrete file, preferring the richest theme installed.
     *
     * Papirus is preferred for tab icons on legibility grounds, but the DESKTOP theme is
     * usually Breeze, and forcing Papirus globally would restyle Fan Fold's own chrome
     * along with it. So tab icons resolve against Papirus explicitly while the
     * application keeps the desktop's look.
     *
     * Falls back per-icon, not per-theme: a name Papirus lacks still resolves from the
     * desktop theme, so a missing icon is never a blank square.
     *
     * @return A file path Kirigami can load, or an empty string to let Qt resolve it.
     */
    Q_INVOKABLE QString resolveThemeIcon(const QString &name) const
    {
        if (name.isEmpty()) return QString();
        if (m_iconNames.isEmpty()) harvestIconNames();
        // A hash hit against the index the harvest walk built. Scanning the theme tree
        // per icon here instead costs seconds of QDirIterator on the UI thread.
        return m_iconIndex.value(name.toLower());
    }

    /**
     * Land a picked icon in the library so a NOTE can reference it.
     *
     * A tab icon is a manifest string, but a note is a portable .md whose links must
     * resolve inside the library — so "put the icon in the note" means the icon becomes a
     * real file under Assets/icons/ first. Three cases:
     *   - already a library file ("Assets/icons/…"): nothing to copy, return it;
     *   - a theme name with a resolvable file: copy that file in (SVG stays SVG);
     *   - a theme name with no locatable file: rasterize via QIcon at 128 px into a PNG,
     *     so the note never ends up holding a link to nothing.
     * Never overwrites: an existing name gets a numbered sibling, same as importAsset.
     *
     * @return {ok, relative} or {ok:false, error}.
     */
    Q_INVOKABLE QVariantMap exportIconForNote(const QString &stored, const QString &baseName)
    {
        if (!m_collection || m_collection->rootPath().isEmpty()) {
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("No library open")}};
        }
        if (stored.startsWith(QStringLiteral("Assets/icons/"))) {
            return {{QStringLiteral("ok"), true}, {QStringLiteral("relative"), stored}};
        }
        if (!stored.startsWith(QStringLiteral("theme:"))) {
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Unrecognised icon reference")}};
        }
        const QString name = stored.mid(6);
        QDir icons(QDir(m_collection->rootPath()).filePath(QStringLiteral("Assets/icons")));
        if (!icons.exists() && !icons.mkpath(QStringLiteral("."))) {
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Assets/icons could not be created")}};
        }
        // A safe file stem from the display name; the icon's own name as fallback.
        QString stem = baseName.trimmed();
        stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 ._-]")), QString());
        if (stem.isEmpty()) stem = name;
        const QString themeFile = resolveThemeIcon(name);
        const QString suffix = (!themeFile.isEmpty() && themeFile.endsWith(QStringLiteral(".svg")))
            ? QStringLiteral("svg") : QStringLiteral("png");
        QString fileName = stem + QLatin1Char('.') + suffix;
        // Re-picking the same icon must REUSE the existing file, not mint "-2", "-3"…
        // siblings on every pick. Same content = same file; only a genuine name
        // collision with different content earns a numbered sibling.
        // A bound on both loops: without one a pathological directory spins forever, and
        // falling through with an existing name would overwrite the user's icon.
        const int collisionLimit = 10000;
        if (!themeFile.isEmpty()) {
            for (int n = 2; icons.exists(fileName); ++n) {
                QFile a(icons.filePath(fileName)), b(themeFile);
                if (a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly)
                    && a.readAll() == b.readAll()) {
                    return {{QStringLiteral("ok"), true},
                            {QStringLiteral("relative"), QStringLiteral("Assets/icons/") + fileName}};
                }
                if (n > collisionLimit) {
                    return {{QStringLiteral("ok"), false},
                            {QStringLiteral("error"), QStringLiteral("No free name in Assets/icons")}};
                }
                fileName = stem + QLatin1Char('-') + QString::number(n) + QLatin1Char('.') + suffix;
            }
        } else {
            for (int n = 2; icons.exists(fileName); ++n) {
                if (n > collisionLimit) {
                    return {{QStringLiteral("ok"), false},
                            {QStringLiteral("error"), QStringLiteral("No free name in Assets/icons")}};
                }
                fileName = stem + QLatin1Char('-') + QString::number(n) + QLatin1Char('.') + suffix;
            }
        }
        const QString target = icons.filePath(fileName);
        bool written = false;
        if (!themeFile.isEmpty()) {
            written = QFile::copy(themeFile, target);
        } else {
            const QIcon icon = QIcon::fromTheme(name);
            if (!icon.isNull()) written = icon.pixmap(128, 128).save(target, "PNG");
        }
        if (!written) {
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("The icon could not be written")}};
        }
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("relative"), QStringLiteral("Assets/icons/") + fileName}};
    }

    /** Papirus when installed, otherwise the desktop's own theme. */
    QString preferredIconTheme() const
    {
        for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
            if (QFileInfo::exists(d + QStringLiteral("/icons/Papirus/index.theme")))
                return QStringLiteral("Papirus");
        }
        return QIcon::themeName();
    }

private:
    /**
     * Walk the active theme's search paths once and remember every icon NAME it offers.
     *
     * Qt has no "list the theme" call, so the names come from the filesystem: every .svg
     * and .png under each theme directory, reduced to its base name. Sorted and unique, so
     * the picker's results are stable and a name that exists at several sizes appears once.
     *
     * Mutable + lazy because the QML side calls this from a const context and the cost —
     * tens of thousands of stat() calls — must be paid at most once per process.
     */
    void harvestIconNames() const
    {
        QSet<QString> names;
        const QString theme = preferredIconTheme();
        // Rank a file by its size directory so the index keeps the best copy of each
        // name. Bigger wins (tiles paint at 32 px, tabs at 24): 64 > 48 > scalable >
        // anything else. Parsed once per file during the walk, never per lookup.
        const auto rankOf = [](const QString &path) -> int {
            if (path.contains(QStringLiteral("/64"))) return 5;
            if (path.contains(QStringLiteral("/48"))) return 4;
            if (path.contains(QStringLiteral("/scalable"))) return 3;
            if (path.contains(QStringLiteral("/128"))) return 2;
            if (path.contains(QStringLiteral("/32"))) return 1;
            return 0;
        };
        QStringList roots;
        // QIcon::themeSearchPaths() is NOT sufficient: under some platform plugins it
        // returns only ":/icons", which would silently harvest nothing. The freedesktop
        // search path is <data>/icons for every standard data location, so derive it.
        QStringList bases;
        for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
            bases << d + QStringLiteral("/icons");
        }
        bases << QDir::homePath() + QStringLiteral("/.icons");   // legacy, still honoured
        for (const QString &base : QIcon::themeSearchPaths()) bases << base;
        for (const QString &base : bases) {
            const QString dir = base + QLatin1Char('/') + theme;
            if (QFileInfo::exists(dir) && !roots.contains(dir)) roots << dir;
            // hicolor is the freedesktop fallback every theme inherits; without it the
            // application icons installed by third-party packages would be invisible.
            const QString fallback = base + QStringLiteral("/hicolor");
            if (QFileInfo::exists(fallback) && !roots.contains(fallback)) roots << fallback;
        }
        // ONE walk serves both the searchable name list and the name→file index.
        // Rescanning the theme tree per icon at paint time costs seconds of QDirIterator
        // on the UI thread for a few hundred tiles, which reads as a freeze. The walk
        // itself costs roughly 150 ms once, and after it every resolve is a hash hit.
        m_iconIndex.clear();
        QHash<QString, int> bestRank;
        for (const QString &root : roots) {
            const bool fallbackRoot = root.endsWith(QStringLiteral("/hicolor"));
            QDirIterator it(root, {QStringLiteral("*.svg"), QStringLiteral("*.png")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QString name = it.fileInfo().completeBaseName().toLower();
                names.insert(name);
                // hicolor supplies names (third-party apps live there) but must not
                // shadow the chosen theme's art: its files rank below everything.
                const int rank = fallbackRoot ? -1 : rankOf(it.filePath());
                if (!m_iconIndex.contains(name) || rank > bestRank.value(name, -2)) {
                    m_iconIndex.insert(name, it.filePath());
                    bestRank.insert(name, rank);
                }
            }
        }
        m_iconNames = names.values();
        std::sort(m_iconNames.begin(), m_iconNames.end());
    }
    mutable QStringList m_iconNames;
    /** name → best on-disk file, built by harvestIconNames() in the same walk. */
    mutable QHash<QString, QString> m_iconIndex;

public:

    /**
     * The user's own icon files from `Assets/icons/` in the library.
     *
     * SVG, PNG and ICO all qualify: SVG scales best, but real-world favicons — the icons
     * people actually have on hand — are overwhelmingly PNG or ICO, so refusing them
     * would make the feature theoretical.
     *
     * @return Each entry has `name`, `relative` (path under the root) and `url` (absolute).
     */
    Q_INVOKABLE QVariantList availableIcons() const
    {
        QVariantList out;
        if (!m_collection || m_collection->rootPath().isEmpty()) {
            return out;
        }
        QDir icons(QDir(m_collection->rootPath()).filePath(QStringLiteral("Assets/icons")));
        if (!icons.exists()) {
            return out;
        }
        const QFileInfoList entries = icons.entryInfoList(
            {QStringLiteral("*.svg"), QStringLiteral("*.png"), QStringLiteral("*.ico")},
            QDir::Files, QDir::Name);
        for (const QFileInfo &entry : entries) {
            out.append(QVariantMap{{QStringLiteral("name"), entry.completeBaseName()},
                                   {QStringLiteral("relative"),
                                    QStringLiteral("Assets/icons/") + entry.fileName()},
                                   {QStringLiteral("group"), QStringLiteral("Yours")},
                                   {QStringLiteral("url"), QUrl::fromLocalFile(entry.absoluteFilePath()).toString()}});
        }
        return out;
    }

    /** Open `folderUrl` as the library root. @return an empty string, or the refusal. */
    Q_INVOKABLE QString openFolder(const QUrl &folderUrl)
    {
        if (!m_collection) {
            return QStringLiteral("No document engine");
        }
        const QString path = folderUrl.isLocalFile() ? folderUrl.toLocalFile()
                                                     : folderUrl.toString();
        if (path.isEmpty()) {
            return QStringLiteral("No folder was chosen");
        }
        if (!m_collection->openRoot(path)) {
            return m_collection->lastError();
        }
        // NOTHING joins the fan here. Fan membership is derived from the open folder, and
        // openRoot() has already set that (root for a library with no stored scope), so
        // the fan is correct the moment the library is open.
        QSettings settings;
        settings.setValue(QStringLiteral("library/root"), m_collection->rootPath());
        emit rootChanged();
        return {};
    }

signals:
    void rootChanged();

private:
    DocumentCollection *m_collection = nullptr;
};

/** First existing path from a candidate list; empty when none exists. */
QString firstExisting(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (!candidate.isEmpty() && QDir(candidate).exists()) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

/** Directory holding Main.qml and the web editor assets. */
QString shellRoot()
{
    const QString override = qEnvironmentVariable("FANFOLD_QML_ROOT");
    const QString beside = QCoreApplication::applicationDirPath();
    return firstExisting({override,
                          QDir(beside).filePath(QStringLiteral("qml")),
                          QDir(beside).filePath(QStringLiteral("../share/fanfold/qml"))});
}

/**
 * One-time move of settings and library state from the pre-1.0 vendor directory.
 *
 * Early builds wrote under an organisation name inherited from the project this
 * application grew out of, which put a stranger's directory in every user's
 * ~/.config. The name is now the application's own, so anyone upgrading would
 * otherwise silently lose their note colours, tab icons and fan order.
 *
 * Rules that keep this safe to run unconditionally on every start:
 *   - never overwrite: if the new location already exists, the legacy tree is
 *     left untouched and ignored, so a downgrade-then-upgrade cannot clobber
 *     newer state;
 *   - rename, never copy-and-delete: a failed rename leaves the original intact;
 *   - silent on failure: a migration that cannot complete must not block launch.
 */
void migrateLegacyConfiguration()
{
    const auto moveIfAbsent = [](const QString &legacy, const QString &current) {
        if (legacy.isEmpty() || current.isEmpty()) {
            return;
        }
        if (!QFileInfo::exists(legacy) || QFileInfo::exists(current)) {
            return;
        }
        QDir().mkpath(QFileInfo(current).path());
        QDir().rename(legacy, current);
    };

    const QString configHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);

    moveIfAbsent(QDir(configHome).filePath(QStringLiteral("Noty/Fan Fold")),
                 QDir(configHome).filePath(QStringLiteral("FanFold/Fan Fold")));
    moveIfAbsent(QDir(configHome).filePath(QStringLiteral("Noty/Fan Fold.conf")),
                 QDir(configHome).filePath(QStringLiteral("FanFold/Fan Fold.conf")));
    moveIfAbsent(QDir(dataHome).filePath(QStringLiteral("Noty/Fan Fold")),
                 QDir(dataHome).filePath(QStringLiteral("FanFold/Fan Fold")));

    // Remove the legacy vendor directory only when it is now empty, so a partial
    // migration never deletes state that did not move.
    for (const QString &base : {configHome, dataHome}) {
        QDir(base).rmdir(QStringLiteral("Noty"));
    }
}

} // namespace

int main(int argc, char **argv)
{
    // Qt documents this as the requirement an embedded Chromium needs from its host.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // QApplication, not QGuiApplication: QSystemTrayIcon's context menu is a widget.
    // Everything else is unaffected — the QML scene neither knows nor cares.
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FanFold"));
    QCoreApplication::setApplicationName(QStringLiteral("Fan Fold"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FANFOLD_BUILD_VERSION));
    migrateLegacyConfiguration();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Edge-fan Markdown notes over an ordinary folder."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption rootOption(
        QStringLiteral("root"),
        QCoreApplication::translate("main", "Folder of Markdown notes to open."),
        QStringLiteral("folder"));
    QCommandLineOption stateOption(
        QStringLiteral("state"),
        QCoreApplication::translate("main", "Directory for application state."),
        QStringLiteral("folder"));
    parser.addOption(rootOption);
    parser.addOption(stateOption);
    parser.process(application);

    // One dock per session. A second launch finds this name taken, asks the running
    // instance to activate, and exits 0 inside the constructor, before it can open the
    // collection or create a window. The bus name is built from the organization domain
    // and application name; "Fan Fold" contains a space, which a D-Bus name cannot, so
    // the name is borrowed only while the service registers. QSettings and
    // QStandardPaths read the application name at call time, so the real name is back
    // before anything resolves a path. The borrowed name matches the desktop file id.
    QCoreApplication::setOrganizationDomain(QStringLiteral("preyevates.github.io"));
    QCoreApplication::setApplicationName(QStringLiteral("FanFold"));
    KDBusService uniqueInstance(KDBusService::Unique);
    QCoreApplication::setApplicationName(QStringLiteral("Fan Fold"));

    DocumentCollection collection(parser.value(stateOption));

    // No default notes location is invented: the folder is the one supplied or the one
    // this machine remembers being told about.
    //
    // An EXPLICIT --root is a session override and is deliberately NOT persisted. The
    // remembered path is written only when the user chose the folder interactively
    // (shell.openFolder below) or launched plainly against the remembered library.
    // Persisting an explicit --root lets a test or automation launch against a scratch
    // directory silently overwrite the user's remembered library path; once that scratch
    // directory is deleted, the next ordinary launch fails to open a collection and every
    // note verb — including creating the first note — refuses.
    const bool explicitRoot = !parser.value(rootOption).isEmpty();
    QString wanted = parser.value(rootOption);
    if (wanted.isEmpty()) {
        QSettings settings;
        wanted = settings.value(QStringLiteral("library/root")).toString();
    }
    QString startupError;
    if (wanted.isEmpty()) {
        startupError = QCoreApplication::translate("main", "No notes folder has been chosen yet");
    } else if (!collection.openRoot(wanted)) {
        startupError = collection.lastError();
    } else if (!explicitRoot) {
        QSettings settings;
        settings.setValue(QStringLiteral("library/root"), collection.rootPath());
    }

    // No discovery-to-fan loop. Since 0.2.0 the fan is the OPEN FOLDER's notes, derived by
    // the engine: openRoot() restored the persisted folder (or fell back to the root) and
    // built the fan from it. The old loop here is precisely what put 62 tabs on the edge
    // after an agent wrote 61 files into one subfolder.
    //
    // A pinned note is still off the fan — it is already on screen in its own window, and
    // putting it in both would present the same note twice.
    // Main.qml::restorePersistedPins() opens those windows at startup.

    AppearanceSettings appearanceSettings;
    AppearanceAdapter appearance(&appearanceSettings);
    NotesAdapter notes(&collection);
    StoreAdapter store(&collection);
    LibraryModel library(&collection);
    FontCatalog fonts;
    ScreenGeometry screenGeometry;
    AnimationSettings animationSettings;

    const QString root = shellRoot();
    if (root.isEmpty()) {
        qCritical("Fan Fold shell QML not found; set FANFOLD_QML_ROOT");
        return 2;
    }

    QQmlApplicationEngine engine;
    QQmlContext *context = engine.rootContext();
    TrayBridge tray;
    ShellControl shellControl(&collection);
    // The names Main.qml binds to, supplied by adapters over the real engine.
    context->setContextProperty(QStringLiteral("notesStore"), &notes);
    context->setContextProperty(QStringLiteral("store"), &store);
    context->setContextProperty(QStringLiteral("appearanceStore"), &appearance);
    context->setContextProperty(QStringLiteral("fontCatalog"), &fonts);
    context->setContextProperty(QStringLiteral("screenGeometry"), &screenGeometry);
    context->setContextProperty(QStringLiteral("animationSettings"), &animationSettings);
    context->setContextProperty(QStringLiteral("collection"), &collection);
    context->setContextProperty(QStringLiteral("libraryModel"), &library);
    // First-run empty state: the library opened but holds no notes, or no folder has been
    // chosen at all. An empty library used to render an essentially invisible window.
    //
    // Bound to the CATALOG, not the fan. The fan is now one folder's worth of notes, so an
    // empty fan is an ordinary state (an empty folder, or every note pinned) and must not
    // be reported as a first run over a library full of notes.
    context->setContextProperty(QStringLiteral("libraryEmpty"),
                                collection.catalogIds().isEmpty());
    context->setContextProperty(QStringLiteral("libraryRoot"), collection.rootPath());
    context->setContextProperty(QStringLiteral("startupError"), startupError);
    context->setContextProperty(QStringLiteral("webAssetRoot"), QUrl::fromLocalFile(root));
    context->setContextProperty(QStringLiteral("tray"), &tray);
    context->setContextProperty(QStringLiteral("shellControl"), &shellControl);
    // The application's own icon, staged beside the QML assets, for in-app branding
    // (welcome panel and the Settings header inside the editor page).
    const QString iconFile = QDir(root).filePath(QStringLiteral("appicon.svg"));
    context->setContextProperty(QStringLiteral("appIconSource"),
                                QFile::exists(iconFile) ? QUrl::fromLocalFile(iconFile)
                                                        : QUrl());

    engine.addImportPath(root);
    engine.load(QUrl::fromLocalFile(QDir(root).filePath(QStringLiteral("Main.qml"))));
    if (engine.rootObjects().isEmpty()) {
        return 2;
    }

    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        screenGeometry.setWindow(window);
    }

    // The fan is a Dock window, invisible to the task manager by design, so this
    // StatusNotifierItem is the one place the app is always reachable. Menu actions emit
    // through TrayBridge and are handled in Main.qml, so each verb is the SAME code the
    // footer runs. If the desktop offers no tray, the app simply works as before.
    QSystemTrayIcon trayIcon;
    QMenu trayMenu;
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon.setIcon(QIcon::fromTheme(QStringLiteral("io.github.preyevates.FanFold"),
                                          QIcon::fromTheme(QStringLiteral("folder-notes"))));
        trayIcon.setToolTip(QStringLiteral("Fan Fold"));
        // Deliberately minimal: the icon exists as the one indicator that the app is
        // running. Left-click toggles the fan exactly as edge-hover shows it; the menu
        // holds only Quit. Note creation belongs to the fan's own +.
        QObject::connect(trayMenu.addAction(QCoreApplication::translate("tray", "Quit Fan Fold")),
                         &QAction::triggered, &tray, &TrayBridge::quitRequested);
        trayIcon.setContextMenu(&trayMenu);
        QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &tray,
                         [&tray](QSystemTrayIcon::ActivationReason reason) {
                             if (reason == QSystemTrayIcon::Trigger) {
                                 emit tray.showRequested();
                             }
                         });
        trayIcon.show();
    }

    // A later launch's arguments are not applied: re-rooting a running library from a
    // command line would bypass the folder chooser's persistence rules.
    QObject::connect(&uniqueInstance, &KDBusService::activateRequested, &tray,
                     &TrayBridge::revealRequested);

    // An ordinary close flushes; a forced termination still has the recovery journal.
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &collection,
                     [&collection] { collection.flushPendingSaves(); });

    return application.exec();
}

// TrayBridge is declared in this translation unit; AUTOMOC needs the generated meta
// object included explicitly for a Q_OBJECT class living in a .cpp.
#include "main.moc"
