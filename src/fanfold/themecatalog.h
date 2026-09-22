#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

/** Validated, read-only palette choices for Fan Fold.
 *
 * The built-in palette is always present and is the entire default/public-package
 * catalog. An external local bundle is optional. When supplied, its catalog.json,
 * manifest.json and NOTICES.md are validated as one unit before any external choice is
 * exposed; one malformed family, variant, role, inventory field or checksum rejects the
 * whole bundle rather than silently reducing it.
 *
 * Theme selection changes only the swatches offered to the user. Notes continue to own
 * literal paper and ink values, so changing or removing a catalog cannot repaint them.
 */
class ThemeCatalog final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList choices READ choices CONSTANT)
    Q_PROPERTY(bool localCatalogAvailable READ localCatalogAvailable CONSTANT)
    Q_PROPERTY(QString notice READ notice CONSTANT)
    Q_PROPERTY(int familyCount READ familyCount CONSTANT)
    Q_PROPERTY(int variantCount READ variantCount CONSTANT)
    Q_PROPERTY(int roleCount READ roleCount CONSTANT)
    Q_PROPERTY(int uniqueColorCount READ uniqueColorCount CONSTANT)
    Q_PROPERTY(QVariantMap provenance READ provenance CONSTANT)

public:
    /** Load an optional local-only theme bundle.
     * @param bundleRoot Directory containing catalog.json, manifest.json and NOTICES.md;
     * empty means the public-package built-in palette only.
     * @param parent QObject owner.
     * @sideeffect Reads only the explicitly supplied bundle; never writes or fetches.
     */
    explicit ThemeCatalog(QString bundleRoot = {}, QObject *parent = nullptr);

    /** Stable key of the palette shipped in every package. */
    static QString builtInKey();

    /** Labels and stable keys for the built-in and every validated local variant. */
    QVariantList choices() const { return m_choices; }
    /** True only after the complete external bundle passes validation. */
    bool localCatalogAvailable() const { return m_localCatalogAvailable; }
    /** Empty for no bundle or a valid bundle; otherwise the all-or-nothing refusal. */
    QString notice() const { return m_notice; }
    /** Number of external families; excludes the built-in fallback. */
    int familyCount() const { return m_familyCount; }
    /** Number of external variants; excludes the built-in fallback. */
    int variantCount() const { return m_variantCount; }
    /** Number of named external role aliases, including aliases sharing one color. */
    int roleCount() const { return m_roleCount; }
    /** Number of distinct external role colors across the complete catalog. */
    int uniqueColorCount() const { return m_uniqueColorCount; }
    /** Source, retrieval, license-use notice and verified catalog digest. */
    QVariantMap provenance() const { return m_provenance; }

    /** Resolve a palette key.
     * @param key Built-in or local family/variant key.
     * @return Full palette record. Unknown/unavailable keys fall back to built-in without
     * mutating the stored preference or any note.
     */
    Q_INVOKABLE QVariantMap palette(const QString &key) const;

private:
    QVariantList m_choices;
    QVariantMap m_palettes;
    QVariantMap m_provenance;
    QString m_notice;
    int m_familyCount = 0;
    int m_variantCount = 0;
    int m_roleCount = 0;
    int m_uniqueColorCount = 0;
    bool m_localCatalogAvailable = false;

    /** Validate and atomically adopt one external bundle. */
    void loadBundle(const QString &bundleRoot);
    /** Complete built-in palette record used by public packages and error fallback. */
    static QVariantMap builtInPalette();
};
