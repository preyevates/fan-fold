#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/** Read-only view of the font families this machine's own Qt/OS stack reports.
 *
 * No font is downloaded, copied, installed or cached here, and fontconfig's existing cache
 * is used as-is. Private families (a leading dot) and names that are not safe to emit into
 * CSS are filtered out, because they are implementation detail rather than a choice a
 * person should be offered.
 *
 * Resolution is a pure read. A stored preference naming a family this machine does not
 * have resolves to the platform default for painting, and the stored preference is left
 * exactly as the user wrote it — so carrying a configuration between machines degrades
 * safely instead of silently losing the choice.
 */
class FontCatalog final : public QObject
{
    Q_OBJECT

public:
    explicit FontCatalog(QObject *parent = nullptr) : QObject(parent) { }

    /** Sorted, de-duplicated, CSS-safe families as the platform reports them now. */
    Q_INVOKABLE QStringList families() const;
    /** The family to actually paint with for a stored preference; never writes. */
    Q_INVOKABLE QString resolveFamily(const QString &wanted) const;
    Q_INVOKABLE bool isInstalled(const QString &family) const;
    /** The platform's own general-purpose family, not a name chosen here. */
    Q_INVOKABLE QString systemFamily() const;
    /** What a stored preference resolves to and why, for the settings panel and tests. */
    Q_INVOKABLE QVariantMap describe(const QString &wanted) const;
};
