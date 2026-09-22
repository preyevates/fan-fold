#include "appearancesettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {
constexpr int settingsVersion = 3;
constexpr qint64 maximumSettingsBytes = 16 * 1024;

struct Bound {
    const char *key;
    double low;
    double high;
    bool integer;
};

/** Ranges each numeric setting stays legible within. */
const QList<Bound> &numericBounds()
{
    static const QList<Bound> bounds{
        {"fontSize", 12, 28, true},       {"lineSpacing", 1.0, 2.2, false},
        {"radius", 0, 28, true},          {"padX", 8, 48, true},
        {"padY", 6, 40, true},            {"tabSpacing", 2, 8, true},
        {"fanSpacing", 28, 140, true},    {"width", 480, 800, true},
        {"fanTabWidth", 24, 72, true},    {"fanTabLength", 60, 240, true},
        {"height", 360, 650, true},
        {"iconSize", 10, 24, true},       {"fanLabelFontSize", 7, 18, true},
        {"toolbarIconSize", 10, 24, true}};
    return bounds;
}

bool isNumeric(const QVariant &value)
{
    const int id = value.metaType().id();
    return id == QMetaType::Double || id == QMetaType::Int || id == QMetaType::LongLong
        || id == QMetaType::UInt || id == QMetaType::ULongLong || id == QMetaType::Float;
}

} // namespace

AppearanceSettings::AppearanceSettings(QString configRoot, QObject *parent)
    : QObject(parent)
    , m_configRoot(configRoot.isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
          : QDir::cleanPath(std::move(configRoot)))
    , m_settings(defaults())
{
    load();
}

QVariantMap AppearanceSettings::defaults()
{
    return {{QStringLiteral("version"), settingsVersion},
            {QStringLiteral("fontFamily"), QStringLiteral("sans")},
            {QStringLiteral("fontFamilyName"), QString()},
            {QStringLiteral("themeKey"), QStringLiteral("builtin:fan-fold")},
            {QStringLiteral("fontSize"), 13},
            {QStringLiteral("lineSpacing"), 1.4},
            {QStringLiteral("radius"), 14},
            {QStringLiteral("padX"), 20},
            {QStringLiteral("padY"), 14},
            {QStringLiteral("tabSpacing"), 4},
            {QStringLiteral("fanSpacing"), 70},
            {QStringLiteral("width"), 640},
            {QStringLiteral("height"), 480},
            {QStringLiteral("iconSize"), 16},
            // Retired: the format toolbar now follows `iconSize`, the same control as
            // the footer command palette. The key and its bounds are retained so
            // existing configuration files that still carry it continue to validate on
            // load; nothing reads it any more.
            {QStringLiteral("toolbarIconSize"), 15},
            {QStringLiteral("fanLabelFontSize"), 9},
            {QStringLiteral("fanLabelBold"), true},
            {QStringLiteral("fanTabWidth"), 36},
            {QStringLiteral("fanTabLength"), 114},
            {QStringLiteral("fanAutoHide"), false}};
}

bool AppearanceSettings::safeThemeKey(const QString &key)
{
    if (key.size() > 128) {
        return false;
    }
    static const QRegularExpression expression(
        QStringLiteral("^(?:builtin:fan-fold|local:[A-Za-z0-9][A-Za-z0-9._-]{0,63}/"
                       "[A-Za-z0-9][A-Za-z0-9._-]{0,63})$"));
    return expression.match(key).hasMatch();
}

bool AppearanceSettings::safeFamily(const QString &name)
{
    if (name.isEmpty() || name.size() > 64 || name != name.trimmed()) {
        return false;
    }
    for (const QChar &character : name) {
        if (character.isLetterOrNumber()) {
            continue;
        }
        if (character == u' ' || character == u'-' || character == u'_' || character == u'.'
            || character == u'+' || character == u'&') {
            continue;
        }
        return false;
    }
    // A name made of separators alone is not a family.
    return std::any_of(name.cbegin(), name.cend(),
                       [](QChar character) { return character.isLetterOrNumber(); });
}

QVariantMap AppearanceSettings::normalize(QVariantMap input)
{
    QVariantMap out = defaults();
    if (input.value(QStringLiteral("version")).toInt() != settingsVersion) {
        return out;
    }

    const QString generic = input.value(QStringLiteral("fontFamily")).toString();
    if (QStringList{QStringLiteral("sans"), QStringLiteral("serif"), QStringLiteral("mono")}
            .contains(generic)) {
        out[QStringLiteral("fontFamily")] = generic;
    }
    const QVariant chosen = input.value(QStringLiteral("fontFamilyName"));
    if (chosen.metaType().id() == QMetaType::QString) {
        const QString name = chosen.toString();
        // Empty is not "no font": it means no family has been chosen yet.
        if (name.isEmpty() || safeFamily(name)) {
            out[QStringLiteral("fontFamilyName")] = name;
        }
    }

    const QVariant theme = input.value(QStringLiteral("themeKey"));
    if (theme.metaType().id() == QMetaType::QString && safeThemeKey(theme.toString())) {
        out[QStringLiteral("themeKey")] = theme.toString();
    }

    for (const Bound &bound : numericBounds()) {
        const QVariant candidate = input.value(QLatin1String(bound.key));
        if (!isNumeric(candidate)) {
            continue;
        }
        const double raw = candidate.toDouble();
        if (!std::isfinite(raw)) {
            continue;
        }
        const double clamped = qBound(bound.low, raw, bound.high);
        out[QLatin1String(bound.key)] =
            bound.integer ? QVariant(qRound(clamped)) : QVariant(clamped);
    }

    // Strict types, not coercion: "true" and 1 are rejected rather than guessed at.
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

QString AppearanceSettings::path() const
{
    return QDir(m_configRoot).filePath(QStringLiteral("appearance.json"));
}

void AppearanceSettings::load()
{
    m_settings = defaults();
    m_notice.clear();

    QFile file(path());
    if (!file.exists()) {
        return;
    }
    const QFileInfo info(path());
    if (!info.isFile() || info.isSymLink() || info.size() > maximumSettingsBytes) {
        m_notice = QStringLiteral("Unusable settings file; defaults in use");
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_notice = QStringLiteral("Cannot read settings; defaults in use");
        return;
    }
    QJsonParseError error {};
    const QJsonDocument parsed = QJsonDocument::fromJson(file.read(maximumSettingsBytes + 1), &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject()) {
        m_notice = QStringLiteral("Invalid settings file; defaults in use");
        return;
    }
    const QVariantMap stored = parsed.object().toVariantMap();
    if (stored.value(QStringLiteral("version")).toInt() != settingsVersion) {
        m_notice = QStringLiteral("Settings were written by another version; defaults in use");
        return;
    }
    m_settings = normalize(stored);
}

bool AppearanceSettings::setValue(const QString &key, const QVariant &value)
{
    if (key == QStringLiteral("version") || !defaults().contains(key)) {
        return false;
    }
    QVariantMap next = m_settings;
    next[key] = value;
    return apply(next);
}

bool AppearanceSettings::apply(const QVariantMap &values)
{
    QVariantMap next = values;
    next[QStringLiteral("version")] = settingsVersion;
    next = normalize(next);
    if (!save(next)) {
        return false;
    }
    m_settings = next;
    m_notice.clear();
    emit changed();
    return true;
}

bool AppearanceSettings::reset()
{
    return apply(defaults());
}

bool AppearanceSettings::save(const QVariantMap &values)
{
    if (m_configRoot.isEmpty() || !QDir().mkpath(m_configRoot)) {
        m_notice = QStringLiteral("Cannot create the settings directory");
        return false;
    }
    const QByteArray bytes =
        QJsonDocument(QJsonObject::fromVariantMap(values)).toJson(QJsonDocument::Indented);
    QSaveFile file(path());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_notice = QStringLiteral("Cannot atomically save appearance settings");
        return false;
    }
    return true;
}
