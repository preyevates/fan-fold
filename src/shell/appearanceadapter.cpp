#include "appearanceadapter.h"

#include "appearancesettings.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QMetaType>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QWindow>

#include <algorithm>
#include <cmath>

// ----------------------------------------------------------------- appearance ----

AppearanceAdapter::AppearanceAdapter(AppearanceSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

bool AppearanceAdapter::safeFamily(const QString &name)
{
    // Letters, digits, spaces and a short list of punctuation actually used by real font
    // names. Everything else — quotes, backslashes, semicolons, braces, brackets,
    // parentheses, colons, control characters — is REFUSED, not stripped: a sanitizer
    // that quietly rewrites a name is a place for a bypass to hide, whereas a refusal
    // leaves the previous value in force. Because no accepted name can contain a quote, a
    // backslash or a semicolon, the quoted CSS string carrying it cannot be closed, the
    // declaration cannot be terminated early, and no url() can be introduced.
    if (name.isEmpty() || name.size() > 64) {
        return false;
    }
    if (name != name.trimmed()) {
        return false;
    }
    for (const QChar &c : name) {
        if (c.isLetterOrNumber()) {
            continue;
        }
        if (c == u' ' || c == u'-' || c == u'_' || c == u'.' || c == u'+' || c == u'&') {
            continue;
        }
        return false;
    }
    return std::any_of(name.cbegin(), name.cend(), [](QChar c) { return c.isLetterOrNumber(); });
}

QVariantMap AppearanceAdapter::defaults()
{
    return {{QStringLiteral("version"), 3},
            {QStringLiteral("fontFamily"), QStringLiteral("sans")},
            {QStringLiteral("fontFamilyName"), QString()},
            {QStringLiteral("fontSize"), 13},
            {QStringLiteral("lineSpacing"), 1.4},
            {QStringLiteral("radius"), 14},
            {QStringLiteral("padX"), 20},
            {QStringLiteral("padY"), 14},
            {QStringLiteral("tabSpacing"), 4},
            {QStringLiteral("fanSpacing"), 70},
            {QStringLiteral("fanTabWidth"), 36},
            {QStringLiteral("fanTabLength"), 114},
            {QStringLiteral("fanAutoHide"), false},
            {QStringLiteral("width"), 640},
            {QStringLiteral("height"), 480},
            {QStringLiteral("iconSize"), 16},
            {QStringLiteral("toolbarIconSize"), 15},
            {QStringLiteral("fanLabelFontSize"), 9},
            {QStringLiteral("fanLabelBold"), true}};
}

QVariantMap AppearanceAdapter::normalize(QVariantMap input)
{
    QVariantMap out = defaults();
    if (input.value(QStringLiteral("version")).toInt() != 3) {
        return out;
    }
    const QString font = input.value(QStringLiteral("fontFamily")).toString();
    if (QStringList{QStringLiteral("sans"), QStringLiteral("serif"), QStringLiteral("mono")}
            .contains(font)) {
        out[QStringLiteral("fontFamily")] = font;
    }
    // The chosen INSTALLED family, stored by name. Empty is not "no font": it means this
    // configuration predates the control, and the legacy sans/serif/mono enum resolves the
    // family in memory instead. That migration happens at render time, so an older record
    // is never rewritten at startup and keeps its current look.
    const QVariant chosen = input.value(QStringLiteral("fontFamilyName"));
    if (chosen.metaType().id() == QMetaType::QString && safeFamily(chosen.toString())) {
        out[QStringLiteral("fontFamilyName")] = chosen.toString();
    }
    struct Bound
    {
        const char *key;
        double low;
        double high;
        bool integer;
    };
    for (const Bound &b : {Bound{"fontSize", 12, 28, true},
                           Bound{"lineSpacing", 1.0, 2.2, false},
                           Bound{"radius", 0, 28, true},
                           Bound{"padX", 8, 48, true},
                           Bound{"padY", 6, 40, true},
                           Bound{"tabSpacing", 2, 8, true},
                           Bound{"fanSpacing", 28, 140, true},
                           // Tab geometry. The width floor keeps the vertical label and
                           // the hover hit target usable; the length floor keeps at least
                           // one word of the title readable; the ceilings stop a tab from
                           // swallowing the card.
                           Bound{"fanTabWidth", 24, 72, true},
                           Bound{"fanTabLength", 60, 240, true},
                           Bound{"width", 480, 800, true},
                           Bound{"height", 360, 650, true},
                           Bound{"iconSize", 10, 24, true},
                           Bound{"toolbarIconSize", 10, 24, true},
                           Bound{"fanLabelFontSize", 7, 18, true}}) {
        const QVariant v = input.value(QLatin1String(b.key));
        const int type = v.metaType().id();
        if (type != QMetaType::Double && type != QMetaType::Int && type != QMetaType::LongLong) {
            continue;
        }
        double n = v.toDouble();
        if (!std::isfinite(n)) {
            continue;
        }
        n = qBound(b.low, n, b.high);
        out[QLatin1String(b.key)] = b.integer ? QVariant(qRound(n)) : QVariant(n);
    }
    // Strict types, not coercion: "true", 1 and "#nope" are rejected and fall back to the
    // default, so a hand-edited file can never paint an unreadable label.
    const QVariant bold = input.value(QStringLiteral("fanLabelBold"));
    if (bold.metaType().id() == QMetaType::Bool) {
        out[QStringLiteral("fanLabelBold")] = bold.toBool();
    }
    const QVariant autoHide = input.value(QStringLiteral("fanAutoHide"));
    if (autoHide.metaType().id() == QMetaType::Bool) {
        out[QStringLiteral("fanAutoHide")] = autoHide.toBool();
    }
    return out;
}

QVariantMap AppearanceAdapter::load()
{
    QVariantMap stored = m_settings ? m_settings->settings() : QVariantMap();
    // Records written before the schema version was introduced carry the keys but no
    // version field; treat those as version 3 rather than discarding their values.
    if (!stored.isEmpty() && !stored.contains(QStringLiteral("version"))) {
        stored[QStringLiteral("version")] = 3;
    }
    const QVariantMap settings = stored.isEmpty() ? defaults() : normalize(stored);
    const QString notice = m_settings ? m_settings->notice() : QString();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("settings"), settings},
            {QStringLiteral("warning"), notice}};
}

QVariantMap AppearanceAdapter::save(const QVariantMap &values)
{
    const QVariantMap settings = normalize(values);
    if (!m_settings || !m_settings->apply(settings)) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QStringLiteral("Appearance save failed; preview is not persisted")}};
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("settings"), settings},
            {QStringLiteral("warning"), QString()}};
}

// -------------------------------------------------------------- screen geometry ----

ScreenGeometry::ScreenGeometry(QObject *parent)
    : QObject(parent)
{
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this] { retarget(); });
}

QRect ScreenGeometry::full() const
{
    const QScreen *screen = m_window && m_window->screen() ? m_window->screen()
                                                           : QGuiApplication::primaryScreen();
    return screen ? screen->geometry() : QRect();
}

QRect ScreenGeometry::available() const
{
    const QScreen *screen = m_window && m_window->screen() ? m_window->screen()
                                                           : QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect();
}

void ScreenGeometry::setWindow(QWindow *window)
{
    m_window = window;
    if (m_window) {
        connect(m_window, &QWindow::screenChanged, this, [this] { retarget(); });
    }
    retarget();
}

void ScreenGeometry::retarget()
{
    for (const QMetaObject::Connection &c : std::as_const(m_screenLinks)) {
        disconnect(c);
    }
    m_screenLinks.clear();
    QScreen *screen = m_window && m_window->screen() ? m_window->screen()
                                                     : QGuiApplication::primaryScreen();
    if (screen) {
        m_screenLinks << connect(screen, &QScreen::availableGeometryChanged, this,
                                 &ScreenGeometry::changed);
        m_screenLinks << connect(screen, &QScreen::geometryChanged, this, &ScreenGeometry::changed);
    }
    emit changed();
}

// ------------------------------------------------------------ animation settings ----

AnimationSettings::AnimationSettings(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    m_path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kdeglobals");
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &AnimationSettings::reload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &AnimationSettings::reload);
    const QDir folder = QFileInfo(m_path).dir();
    if (folder.exists()) {
        m_watcher->addPath(folder.absolutePath());
    }
    reload();
}

void AnimationSettings::reload()
{
    const bool present = QFileInfo::exists(m_path);
    qreal value = 1.0;
    if (present) {
        QSettings settings(m_path, QSettings::IniFormat);
        bool ok = false;
        const qreal read
            = settings.value(QStringLiteral("KDE/AnimationDurationFactor"), 1.0).toDouble(&ok);
        if (ok && read >= 0.0) {
            value = read;
        }
        if (!m_watcher->files().contains(m_path)) {
            m_watcher->addPath(m_path);
        }
    }
    if (present == m_present && qFuzzyCompare(value + 1.0, m_factor + 1.0)) {
        return;
    }
    m_present = present;
    m_factor = value;
    emit changed();
}
