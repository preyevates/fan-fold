#include "fontcatalog.h"

#include "appearancesettings.h"

#include <QFontDatabase>
#include <QSet>

#include <algorithm>

QStringList FontCatalog::families() const
{
    QSet<QString> unique;
    for (const QString &family : QFontDatabase::families()) {
        const QString trimmed = family.trimmed();
        // Qt/fontconfig expose private families with a leading dot (".SF NS Text").
        if (trimmed.startsWith(QLatin1Char('.')) || !AppearanceSettings::safeFamily(trimmed)) {
            continue;
        }
        unique.insert(trimmed);
    }
    QStringList out(unique.begin(), unique.end());
    std::sort(out.begin(), out.end(), [](const QString &left, const QString &right) {
        const int order = left.compare(right, Qt::CaseInsensitive);
        return order != 0 ? order < 0 : left < right;
    });
    return out;
}

bool FontCatalog::isInstalled(const QString &family) const
{
    const QString trimmed = family.trimmed();
    return !trimmed.isEmpty() && families().contains(trimmed);
}

QString FontCatalog::systemFamily() const
{
    const QString general =
        QFontDatabase::systemFont(QFontDatabase::GeneralFont).family().trimmed();
    if (AppearanceSettings::safeFamily(general) && isInstalled(general)) {
        return general;
    }
    const QStringList all = families();
    return all.isEmpty() ? QString() : all.first();
}

QString FontCatalog::resolveFamily(const QString &wanted) const
{
    const QString trimmed = wanted.trimmed();
    if (AppearanceSettings::safeFamily(trimmed) && isInstalled(trimmed)) {
        return trimmed;
    }
    return systemFamily();
}

QVariantMap FontCatalog::describe(const QString &wanted) const
{
    const QString trimmed = wanted.trimmed();
    return {{QStringLiteral("wanted"), wanted},
            {QStringLiteral("safe"), AppearanceSettings::safeFamily(trimmed)},
            {QStringLiteral("installed"), isInstalled(trimmed)},
            {QStringLiteral("resolved"), resolveFamily(wanted)},
            {QStringLiteral("system"), systemFamily()}};
}
