#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

/** Global appearance preferences for the Fan Fold shell.
 *
 * Appearance is deliberately colourless. Paper and ink are literal per-note values owned
 * by the document engine, so no global setting can ever repaint a note the user has
 * already coloured; what lives here is geometry and typography only, and every colour-like
 * key from an older record is dropped on load rather than honoured.
 *
 * Reading never writes. A missing file yields the defaults silently; a damaged one yields
 * the defaults plus a notice and is left on disk for inspection instead of being
 * overwritten. Only an explicit setValue()/apply()/reset() persists anything, atomically.
 */
class AppearanceSettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)

public:
    /** @param configRoot Explicit config directory for tests; empty uses AppConfigLocation. */
    explicit AppearanceSettings(QString configRoot = {}, QObject *parent = nullptr);

    /** The measured defaults the accepted visuals were tuned against. */
    static QVariantMap defaults();
    /** Normalize known keys only: wrong types fall back, finite numbers clamp, unknown
     * and colour keys are dropped, and a record from another version yields defaults. */
    static QVariantMap normalize(QVariantMap input);
    /** True when a font family name is safe to emit into CSS and is plausibly a family. */
    static bool safeFamily(const QString &name);
    /** The legible range of one numeric setting, from the SAME table normalize() clamps
     *  against, so a per-note override can never drift from the global control.
     *  @return false for an unknown key. */
    static bool numericRange(const QString &key, double *low, double *high);
    /** True for a built-in or external family/variant chooser key.
     * The key identifies offered swatches only; it is never treated as a path.
     */
    static bool safeThemeKey(const QString &key);

    QVariantMap settings() const { return m_settings; }
    /** Why the defaults are in use, when they are; empty in the ordinary case. */
    QString notice() const { return m_notice; }

    Q_INVOKABLE QVariant value(const QString &key) const { return m_settings.value(key); }
    /** Set one known key. Out-of-range values clamp; an unknown key is refused. */
    Q_INVOKABLE bool setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE bool apply(const QVariantMap &values);
    Q_INVOKABLE bool reset();
    Q_INVOKABLE QString path() const;

signals:
    void changed();

private:
    QString m_configRoot;
    QString m_notice;
    QVariantMap m_settings;

    void load();
    bool save(const QVariantMap &values);
};
